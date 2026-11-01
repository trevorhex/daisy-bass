// daisy-bass firmware: multi-effect bass pedal on a PedalPCB Terrarium.
//
// Effects: Env Filter (more to come). Pots and toggles edit the effect on
// the current page; the tuner listens to the dry input.
//
//   FS 1 tap    Current effect on/off (exits the tuner if it's open)
//   FS 2 hold   Tuner on/off. Output is muted while tuning
//   FS 2 tap    Next page (exits the tuner if it's open)
//
//   LED 1/2     Normal: current effect on / its activity (Effect::Activity)
//               Tuner:  LED 1 blinks = flat, LED 2 blinks = sharp,
//                       both solid = in tune (±3 cents), slow blink = no note
//               (SCREEN_PANEL has no LEDs; the OLED shows all of this.)
//
// Env Filter, standard panel (one page):
//   Pot 1 Sensitivity   Pot 2 Range       Pot 3 Resonance
//   Pot 4 Response      Pot 5 Mix         Pot 6 Level
//   SW 1  LP / BP       SW 2  Up / Down   SW 3 Lows filtered / clean   SW 4 Xover 150 / 250
//
// Env Filter, SCREEN_PANEL (SW 1 and SW 4 only): the pots are the same,
// SW 1 = LP / BP, SW 4 = Up / Down, and page 2 puts Lows on Pot 1 and Xover
// on Pot 2 (left half / right half). Effects get as many pages as
// PageLayout needs; FS 2 tap steps through them all. Toggles always act on
// the current effect, whichever of its pages is showing.
//
// Adding an effect: give it a static instance below, chain.Add() it in
// main(), and that's it. Pages, pickup, toggles, OLED and the activity LED
// all work from the Effect interface.
//
// Build flags (see README): SCREEN_PANEL=1, OLED=1, LOG=1, KNOB_CHECK=1, BOOT_SRAM=1.

#include <cmath>

#include "PageView.h"
#include "control/FootswitchGesture.h"
#include "control/KnobPickup.h"
#include "control/PageLayout.h"
#include "control/PotSwitch.h"
#include "daisy_seed.h"
#include "dsp/Chain.h"
#include "dsp/EnvelopeFilter.h"
#include "dsp/Smoother.h"
#include "dsp/Tuner.h"
#include "terrarium.h"
#include "util/scopedirqblocker.h"

#ifdef OLED
#include "Display.h"
#endif

using namespace daisy;

namespace {

static_assert(Terrarium::kNumPots <= bass::PageLayout::kMaxPots, "more pots than PageLayout supports");
static_assert(Terrarium::kNumToggles <= bass::PageLayout::kMaxToggles, "more toggles than PageLayout supports");
static_assert(PageView::kPots == bass::PageLayout::kMaxPots && PageView::kToggles == bass::PageLayout::kMaxToggles,
              "PageView and PageLayout disagree");

Terrarium hw;
bass::Chain chain;
bass::EnvelopeFilter envFilter;
bass::Tuner tuner;
bass::Smoother outputGain; // fades to silence in tuner mode
CpuLoadMeter cpu;
#ifdef OLED
Display display;
#endif

// --- UI state. Owned by the audio callback; the main loop only reads it
// --- through Snapshot(), with interrupts blocked.

// A page is one effect and one of its PageLayout pages. FS 2 steps
// through every page of every effect.
struct View {
    int effect;
    int sub;
};
bass::PageLayout layouts[bass::Chain::kMaxEffects];
View views[bass::Chain::kMaxEffects * bass::PageLayout::kMaxPages];
int numViews = 0;

int page = 0; // index into views
volatile bool tunerMode = false;

bass::FootswitchGesture fs1, fs2;
bass::KnobPickup pickups[Terrarium::kNumPots];
bass::PotSwitch potSwitches[Terrarium::kNumPots];
int potParam[Terrarium::kNumPots];            // effect param under each pot, -1 = none
bool potIsSwitch[Terrarium::kNumPots];        // that param is a switch: flips at the midpoint
int toggleParam[bass::PageLayout::kMaxToggles]; // effect param under each toggle, -1 = none
bool lastToggle[Terrarium::kNumToggles];

void BuildViews() {
    numViews = 0;
    for (int e = 0; e < chain.Size(); e++) {
        layouts[e].Build(*chain.At(e), Terrarium::kNumPots, Terrarium::kNumToggles);
        for (int sub = 0; sub < layouts[e].numPages; sub++) views[numViews++] = {e, sub};
    }
}

int CurrentEffect() { return views[page].effect; }

/**
 * At power-up the physical toggles are the truth, for every effect: each
 * effect's toggle params take the toggles' positions. After that, toggles
 * only act when flipped, like the pickup pots.
 */
void SyncTogglesToAllEffects() {
    for (int e = 0; e < chain.Size(); e++) {
        for (int t = 0; t < Terrarium::kNumToggles; t++) {
            const int i = layouts[e].pages[0].toggle[t];
            if (i >= 0) chain.At(e)->SetParam(i, hw.Toggle(t) ? 1.f : 0.f);
        }
    }
}

/** Lay the current page's params onto the pots, and its effect's switches onto the toggles. */
void MapPage() {
    const View& v = views[page];
    bass::Effect* fx = chain.At(v.effect);
    const bass::PageLayout::Page& p = layouts[v.effect].pages[v.sub];

    for (int pot = 0; pot < Terrarium::kNumPots; pot++) {
        const int i = p.pot[pot];
        potParam[pot] = i;
        potIsSwitch[pot] = i >= 0 && fx->Param(i).isSwitch;
        if (i < 0) continue;
        if (potIsSwitch[pot]) potSwitches[pot].Arm(hw.Pot(pot));
        else pickups[pot].Arm(fx->GetParam(i), hw.Pot(pot));
    }

    // Toggles belong to the effect's first page, so they keep working on its other pages.
    for (int t = 0; t < bass::PageLayout::kMaxToggles; t++) toggleParam[t] = layouts[v.effect].pages[0].toggle[t];
    for (int t = 0; t < Terrarium::kNumToggles; t++) lastToggle[t] = hw.Toggle(t);
}

void HandleFootswitches() {
    using E = bass::FootswitchGesture::Event;
    const uint32_t now = System::GetNow();

    switch (fs1.Update(hw.Footswitch(0), now)) {
        case E::Tap:
            if (tunerMode) tunerMode = false;
            else chain.Toggle(CurrentEffect());
            break;
        default: break;
    }

    switch (fs2.Update(hw.Footswitch(1), now)) {
        case E::Hold: tunerMode = !tunerMode; break;
        case E::Tap:
            if (tunerMode) {
                tunerMode = false;
            } else if (numViews > 1) {
                page = (page + 1) % numViews;
                MapPage();
            }
            break;
        default: break;
    }
}

void HandleKnobsAndToggles() {
    bass::Effect* fx = chain.At(CurrentEffect());
    for (int pot = 0; pot < Terrarium::kNumPots; pot++) {
        const int i = potParam[pot];
        if (i < 0) continue;
        if (potIsSwitch[pot]) {
            bool on;
            if (potSwitches[pot].Update(hw.Pot(pot), on)) fx->SetParam(i, on ? 1.f : 0.f);
        } else {
            float v;
            if (pickups[pot].Update(hw.Pot(pot), v)) fx->SetParam(i, v);
        }
    }
    for (int t = 0; t < Terrarium::kNumToggles; t++) {
        const bool on = hw.Toggle(t);
        if (on != lastToggle[t] && toggleParam[t] >= 0) fx->SetParam(toggleParam[t], on ? 1.f : 0.f);
        lastToggle[t] = on;
    }
}

// Runs every 48 samples (1 ms). Must never block, allocate, or touch the OLED.
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    cpu.OnBlockStart();
    hw.ProcessControls();
    HandleFootswitches();
    HandleKnobsAndToggles();
    outputGain.SetTarget(tunerMode ? 0.f : 1.f);

    // Normally one pass of kBlockSize; chunked so a different block size can't overrun `buffer`.
    float buffer[Terrarium::kBlockSize];
    for (size_t start = 0; start < size; start += Terrarium::kBlockSize) {
        const size_t n = size - start < Terrarium::kBlockSize ? size - start : Terrarium::kBlockSize;
        for (size_t i = 0; i < n; i++) buffer[i] = in[0][start + i];

        tuner.Push(buffer, n); // tuner hears the dry bass
        if (outputGain.Value() > 1e-4f || !tunerMode) chain.Process(buffer, n);

        for (size_t i = 0; i < n; i++) {
            const float y = buffer[i] * outputGain.Process();
            out[0][start + i] = y;
            out[1][start + i] = y; // Terrarium is mono
        }
    }
    cpu.OnBlockEnd();
}

// --- Main loop helpers (non-real-time) ------------------------------------

/** Copy the current page's state. Interrupts are blocked for a few microseconds. */
PageView Snapshot() {
    PageView v;
    ScopedIrqBlocker block;
    const int e = CurrentEffect();
    const bass::Effect* fx = chain.At(e);
    v.name = fx->Name();
    v.enabled = chain.IsEnabled(e);
    v.page = page;
    v.pages = numViews;
    v.activity = fx->Activity();
    for (int pot = 0; pot < Terrarium::kNumPots; pot++) {
        PageView::Control& c = v.pots[pot];
        const int i = potParam[pot];
        if (i < 0) continue;
        c.used = true;
        c.info = fx->Param(i);
        c.value = fx->GetParam(i);
        c.position = hw.Pot(pot);
        c.caught = potIsSwitch[pot] || pickups[pot].Caught();
    }
    for (int t = 0; t < PageView::kToggles; t++) {
        PageView::Control& c = v.toggles[t];
        const int i = toggleParam[t];
        if (i < 0) continue;
        c.used = true;
        c.info = fx->Param(i);
        c.value = fx->GetParam(i);
    }
    return v;
}

/** Blink period: 80 ms when way off, 600 ms when nearly there. */
bool Blink(uint32_t now, float centsOff) {
    const float t = std::fmin(1.f, (std::fabs(centsOff) - 3.f) / 47.f);
    const uint32_t period = uint32_t(600.f - 520.f * t);
    return (now % period) < period / 2;
}

void UpdateTunerLeds(const bass::Tuner::Reading& r, float cents, uint32_t now) {
    if (!r.valid) {
        const bool on = (now % 1000) < 100; // slow heartbeat: "tuner is listening"
        hw.SetLed(0, on);
        hw.SetLed(1, on);
    } else if (std::fabs(cents) <= 3.f) {
        hw.SetLed(0, true);
        hw.SetLed(1, true);
    } else {
        hw.SetLed(0, cents < 0 && Blink(now, cents));
        hw.SetLed(1, cents > 0 && Blink(now, cents));
    }
}

#ifdef KNOB_CHECK
void RunKnobCheck() {
    hw.seed.StartLog(true); // waits for a serial monitor
    hw.StartAdc();
    while (true) {
        hw.ProcessControls();
        char sw[Terrarium::kNumToggles + 1];
        for (int t = 0; t < Terrarium::kNumToggles; t++) sw[t] = hw.Toggle(t) ? '1' : '0';
        sw[Terrarium::kNumToggles] = '\0';
        hw.seed.PrintLine("pots %3d %3d %3d / %3d %3d %3d | sw %s | fs %d%d",
            int(hw.Pot(0) * 100), int(hw.Pot(1) * 100), int(hw.Pot(2) * 100),
            int(hw.Pot(3) * 100), int(hw.Pot(4) * 100), int(hw.Pot(5) * 100),
            sw, hw.Footswitch(0), hw.Footswitch(1));
        hw.SetLed(0, hw.Footswitch(0));
        hw.SetLed(1, hw.Footswitch(1));
        System::Delay(100);
    }
}
#endif

} // namespace

int main() {
    hw.Init();

#ifdef KNOB_CHECK
    RunKnobCheck();
#endif
#ifdef LOG
    hw.seed.StartLog(false);
#endif
#ifdef OLED
    display.Init();
#endif

    const float sr = hw.seed.AudioSampleRate();
    chain.Add(&envFilter);
    chain.Init(sr);
    tuner.Init(sr);
    outputGain.Init(sr, 0.01f, 1.f);
    cpu.Init(sr, Terrarium::kBlockSize);

    hw.StartAdc();
    System::Delay(10); // let the pots read once before mapping them
    for (int i = 0; i < 20; i++) hw.ProcessControls();
    BuildViews();
    SyncTogglesToAllEffects();
    MapPage();
    hw.seed.StartAudio(AudioCallback);

    bass::Tuner::Reading reading;
    float cents = 0.f;
    uint32_t nextAnalyze = 0, nextDraw = 0, nextLog = 0;

    while (true) {
        const uint32_t now = System::GetNow();

        if (tunerMode && now >= nextAnalyze) {
            nextAnalyze = now + 50;
            const bass::Tuner::Reading r = tuner.Analyze();
            // Glide the needle on the same note; snap when the note changes.
            cents = (r.valid && reading.valid && r.midiNote == reading.midiNote)
                        ? cents + 0.35f * (r.cents - cents)
                        : r.cents;
            reading = r;
        }

        const PageView view = Snapshot();
        if (tunerMode) {
            UpdateTunerLeds(reading, cents, now);
        } else {
            hw.SetLed(0, view.enabled);
            hw.SetLed(1, view.enabled && view.activity > 0.5f);
        }

#ifdef OLED
        if (now >= nextDraw) {
            nextDraw = now + 33; // ~30 fps
            if (tunerMode) display.DrawTuner(reading, cents, tuner.Reference());
            else display.DrawEffect(view, {int(cpu.GetAvgCpuLoad() * 100.f)});
        }
#endif

#ifdef LOG
        if (now >= nextLog) {
            nextLog = now + 1000;
            hw.seed.PrintLine("cpu avg %d%% max %d%% | page %d tuner %d",
                int(cpu.GetAvgCpuLoad() * 100.f), int(cpu.GetMaxCpuLoad() * 100.f), view.page, tunerMode);
        }
#endif
        (void)nextDraw;
        (void)nextLog;
        System::Delay(1);
    }
}
