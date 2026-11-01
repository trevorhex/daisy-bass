#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "../dsp/Chain.h"
#include "FootswitchPair.h"
#include "KnobPickup.h"
#include "PageLayout.h"
#include "PageView.h"
#include "PotSwitch.h"
#include "Presets.h"

namespace bass {

/**
 * Everything the pedal's controls do, independent of hardware: the firmware
 * and the desktop simulator both feed it pot/toggle/footswitch readings and
 * a millisecond clock, once per audio block, so they behave identically.
 *
 *                Preset mode (power-up)      Edit mode
 *   FS 1 tap     previous preset             current effect on/off
 *   FS 2 tap     next preset                 next page
 *   FS 1 hold    → Edit mode                 → Preset mode
 *   FS 2 hold    tuner                       tuner
 *   both 1 s     save to the current preset  save to the current preset
 *   pots/toggles edit the current page       edit the current page
 *
 * Any tap leaves the tuner. Edits are never saved on their own: changing
 * preset (or powering off) brings back the saved version. In Preset mode,
 * touching a control pops its page up on the screen for kPopupMs.
 *
 * Threading: Update() runs in the audio callback. Snapshot() and
 * TakeSaveRequest() run in the main loop with the audio callback held off
 * (interrupts blocked / mutex), since they read state Update() writes.
 */
class PedalUi {
public:
    static constexpr int kMaxPots = PageLayout::kMaxPots;
    static constexpr int kMaxToggles = PageLayout::kMaxToggles;
    static constexpr uint32_t kPopupMs = 2000;
    static constexpr uint32_t kMessageMs = 1500;

    struct Inputs {
        float pots[kMaxPots] = {};
        bool toggles[kMaxToggles] = {};
        bool footswitches[2] = {};
    };

    /** Call before audio starts. Loads the bank's current preset immediately. */
    void Init(Chain* chain, int numPots, int numToggles, const PresetBank& bank, const Inputs& in, uint32_t nowMs) {
        chain_ = chain;
        numPots_ = numPots < kMaxPots ? numPots : kMaxPots;
        numToggles_ = numToggles < kMaxToggles ? numToggles : kMaxToggles;
        bank_ = bank;
        in_ = in;
        numViews_ = 0;
        for (int e = 0; e < chain_->Size(); e++) {
            layouts_[e].Build(*chain_->At(e), numPots_, numToggles_);
            for (int sub = 0; sub < layouts_[e].numPages; sub++) views_[numViews_++] = {e, sub};
        }
        for (int t = 0; t < kMaxToggles; t++) lastToggle_[t] = in.toggles[t];
        LoadPreset(int(bank_.current), nowMs, /*immediate=*/true);
    }

    void Update(const Inputs& in, uint32_t nowMs) {
        in_ = in;
        now_ = nowMs;
        using E = FootswitchPair::Event;
        switch (footswitches_.Update(in.footswitches[0], in.footswitches[1], nowMs)) {
            case E::Tap1:
                if (tuner_) tuner_ = false;
                else if (editMode_) ToggleCurrentEffect();
                else LoadPreset(preset_ - 1, nowMs);
                break;
            case E::Tap2:
                if (tuner_) tuner_ = false;
                else if (editMode_) NextPage();
                else LoadPreset(preset_ + 1, nowMs);
                break;
            case E::Hold1:
                if (tuner_) tuner_ = false;
                else editMode_ = !editMode_;
                popupUntil_ = nowMs; // close any popup
                break;
            case E::Hold2: tuner_ = !tuner_; break;
            case E::BothHold:
                if (!tuner_) Save(nowMs);
                break;
            case E::None: break;
        }
        if (!tuner_) HandleKnobsAndToggles(nowMs);
        else for (int t = 0; t < numToggles_; t++) lastToggle_[t] = in.toggles[t];
    }

    bool TunerMode() const { return tuner_; }
    bool EditMode() const { return editMode_; }
    int Preset() const { return preset_; }
    bool Modified() const { return modified_; }
    const PresetBank& Bank() const { return bank_; }

    /** Replace the whole bank (e.g. factory reset) and load its current preset. */
    void SetBank(const PresetBank& bank, uint32_t nowMs) {
        bank_ = bank;
        LoadPreset(int(bank_.current), nowMs);
    }

    /** If a save happened since the last call, copy the bank to persist and return true. */
    bool TakeSaveRequest(PresetBank& out) {
        if (!saveRequested_) return false;
        saveRequested_ = false;
        out = bank_;
        return true;
    }

    PageView Snapshot() const {
        PageView v;
        v.editMode = editMode_;
        v.screen = tuner_                                         ? PageView::Screen::Tuner
                   : (editMode_ || int32_t(popupUntil_ - now_) > 0) ? PageView::Screen::Page
                                                                    : PageView::Screen::Preset;
        v.preset = preset_;
        v.modified = modified_;
        v.presetAgeMs = now_ - presetAt_;
        v.numEffects = chain_->Size();
        for (int e = 0; e < chain_->Size(); e++) {
            v.effects[e] = {chain_->At(e)->Name(), chain_->IsEnabled(e)};
            const float a = chain_->At(e)->Activity();
            if (chain_->IsEnabled(e) && a > v.chainActivity) v.chainActivity = a;
        }

        const int e = CurrentEffect();
        const Effect* fx = chain_->At(e);
        v.name = fx->Name();
        v.enabled = chain_->IsEnabled(e);
        v.page = page_;
        v.pages = numViews_;
        v.activity = fx->Activity();
        for (int pot = 0; pot < numPots_; pot++) {
            PageView::Control& c = v.pots[pot];
            const int i = potParam_[pot];
            if (i < 0) continue;
            c.used = true;
            c.info = fx->Param(i);
            c.value = fx->GetParam(i);
            c.position = in_.pots[pot];
            c.caught = potIsSwitch_[pot] || pickups_[pot].Caught();
        }
        for (int t = 0; t < numToggles_; t++) {
            PageView::Control& c = v.toggles[t];
            const int i = toggleParam_[t];
            if (i < 0) continue;
            c.used = true;
            c.info = fx->Param(i);
            c.value = fx->GetParam(i);
        }
        if (int32_t(messageUntil_ - now_) > 0) std::snprintf(v.message, sizeof v.message, "%s", message_);
        return v;
    }

private:
    struct View {
        int effect;
        int sub;
    };

    int CurrentEffect() const { return views_[page_].effect; }

    void LoadPreset(int index, uint32_t nowMs, bool immediate = false) {
        const int n = PresetBank::kNumPresets;
        preset_ = ((index % n) + n) % n;
        ApplyPreset(bank_.presets[preset_], *chain_, immediate);
        modified_ = false;
        presetAt_ = nowMs;
        popupUntil_ = nowMs;
        // Knobs start on the first effect that's on in this preset.
        page_ = 0;
        for (int i = 0; i < numViews_; i++) {
            if (views_[i].sub == 0 && chain_->IsEnabled(views_[i].effect)) {
                page_ = i;
                break;
            }
        }
        MapPage(); // stored values changed under the pots: re-arm pickup
    }

    void Save(uint32_t nowMs) {
        CapturePreset(*chain_, bank_.presets[preset_]);
        bank_.current = uint32_t(preset_);
        modified_ = false;
        saveRequested_ = true;
        std::snprintf(message_, sizeof message_, "Saved P%d", preset_ + 1);
        messageUntil_ = nowMs + kMessageMs;
    }

    void ToggleCurrentEffect() {
        chain_->Toggle(CurrentEffect());
        modified_ = true;
    }

    void NextPage() {
        if (numViews_ < 2) return;
        page_ = (page_ + 1) % numViews_;
        MapPage();
    }

    /** Lay the current page's params onto the pots, and its effect's switches onto the toggles. */
    void MapPage() {
        const View& v = views_[page_];
        const Effect* fx = chain_->At(v.effect);
        const PageLayout::Page& p = layouts_[v.effect].pages[v.sub];
        for (int pot = 0; pot < numPots_; pot++) {
            const int i = p.pot[pot];
            potParam_[pot] = i;
            potIsSwitch_[pot] = i >= 0 && fx->Param(i).isSwitch;
            lastPot_[pot] = in_.pots[pot];
            if (i < 0) continue;
            if (potIsSwitch_[pot]) potSwitches_[pot].Arm(in_.pots[pot]);
            else pickups_[pot].Arm(fx->GetParam(i), in_.pots[pot]);
        }
        // Toggles belong to the effect's first page, so they keep working on its other pages.
        for (int t = 0; t < kMaxToggles; t++) toggleParam_[t] = layouts_[v.effect].pages[0].toggle[t];
    }

    void HandleKnobsAndToggles(uint32_t nowMs) {
        Effect* fx = chain_->At(CurrentEffect());
        bool touched = false;
        for (int pot = 0; pot < numPots_; pot++) {
            const int i = potParam_[pot];
            if (i < 0) continue;
            if (potIsSwitch_[pot]) {
                bool on;
                if (potSwitches_[pot].Update(in_.pots[pot], on)) {
                    fx->SetParam(i, on ? 1.f : 0.f);
                    touched = true;
                }
            } else {
                float value;
                if (pickups_[pot].Update(in_.pots[pot], value)) {
                    fx->SetParam(i, value);
                    touched = true;
                }
                // Moving an un-caught pot changes nothing yet, but still show its page.
                if (!pickups_[pot].Caught() && std::fabs(in_.pots[pot] - lastPot_[pot]) > 0.01f) {
                    if (!editMode_) popupUntil_ = nowMs + kPopupMs;
                    lastPot_[pot] = in_.pots[pot];
                }
            }
        }
        for (int t = 0; t < numToggles_; t++) {
            const bool on = in_.toggles[t];
            if (on != lastToggle_[t] && toggleParam_[t] >= 0) {
                fx->SetParam(toggleParam_[t], on ? 1.f : 0.f);
                touched = true;
            }
            lastToggle_[t] = on;
        }
        if (touched) {
            modified_ = true;
            if (!editMode_) popupUntil_ = nowMs + kPopupMs;
        }
    }

    Chain* chain_ = nullptr;
    int numPots_ = 0, numToggles_ = 0;
    PageLayout layouts_[Chain::kMaxEffects];
    View views_[Chain::kMaxEffects * PageLayout::kMaxPages] = {};
    int numViews_ = 0;
    int page_ = 0;

    PresetBank bank_;
    int preset_ = 0;
    bool modified_ = false;
    bool saveRequested_ = false;
    bool editMode_ = false;
    bool tuner_ = false;

    Inputs in_;
    uint32_t now_ = 0, presetAt_ = 0, popupUntil_ = 0, messageUntil_ = 0;
    char message_[20] = "";

    FootswitchPair footswitches_;
    KnobPickup pickups_[kMaxPots];
    PotSwitch potSwitches_[kMaxPots];
    float lastPot_[kMaxPots] = {};
    int potParam_[kMaxPots] = {-1, -1, -1, -1, -1, -1};
    bool potIsSwitch_[kMaxPots] = {};
    int toggleParam_[kMaxToggles] = {-1, -1, -1, -1};
    bool lastToggle_[kMaxToggles] = {};
};

} // namespace bass
