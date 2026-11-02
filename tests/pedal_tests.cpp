#include <cstring>
#include <string>

#include "../control/FootswitchPair.h"
#include "../control/Pedal.h"
#include "../control/Presets.h"
#include "../ui/Framebuffer.h"
#include "../ui/Screens.h"
#include "Harness.h"

using namespace bass;
using namespace harness;

namespace {

/** Minimal effect for preset tests: three params, one of them a switch. */
class Dummy : public Effect {
public:
    explicit Dummy(const char* name) : name_(name) {}
    const char* Name() const override { return name_; }
    void Init(float) override {}
    int NumParams() const override { return 3; }
    ParamInfo Param(int i) const override {
        static const ParamInfo info[3] = {{"A", 0.2f}, {"B", 0.4f}, {"Sw", 0.f, true, "Off", "On"}};
        return (i < 0 || i >= 3) ? ParamInfo{"?", 0.f} : info[i];
    }
    void SetParam(int i, float v) override {
        if (i >= 0 && i < 3) v_[i] = v;
    }
    float GetParam(int i) const override { return (i < 0 || i >= 3) ? 0.f : v_[i]; }
    void Process(float*, size_t) override {}

private:
    const char* name_;
    float v_[3] = {0.2f, 0.4f, 0.f};
};

/** Drives a Pedal's controls one millisecond at a time, like the audio callback. */
struct Rig {
    Pedal pedal;
    PedalUi::Inputs in;
    uint32_t now = 0;

    explicit Rig(const PresetBank* saved = nullptr) {
        for (float& p : in.pots) p = 0.5f;
        pedal.Init(48000.f, 6, 2, saved, in, now);
    }
    void Run(uint32_t ms) {
        for (uint32_t i = 0; i < ms; i++) pedal.ui.Update(in, ++now);
    }
    void Tap(int fs) {
        in.footswitches[fs] = true;
        Run(60);
        in.footswitches[fs] = false;
        Run(20);
    }
    void Hold(int fs, uint32_t ms = 600) {
        in.footswitches[fs] = true;
        Run(ms);
        in.footswitches[fs] = false;
        Run(20);
    }
    void HoldBoth() {
        in.footswitches[0] = in.footswitches[1] = true;
        Run(1100);
        in.footswitches[0] = in.footswitches[1] = false;
        Run(20);
    }
    float Env(int param) const { return pedal.envFilter.GetParam(param); }
    float Comp(int param) const { return pedal.compressor.GetParam(param); }
    bool CompOn() const { return pedal.chain.IsEnabled(Pedal::kCompSlot); }
    bool EnvOn() const { return pedal.chain.IsEnabled(Pedal::kEnvSlot); }
    bool DriveOn() const { return pedal.chain.IsEnabled(Pedal::kDriveSlot); }
};

int LitPixels(const ui::Framebuffer& fb, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) n += fb.Pixel(x, y) ? 1 : 0;
    return n;
}

} // namespace

void RunPedalTests() {
    Test("footswitch pair: taps, holds, and both held", [&] {
        using E = FootswitchPair::Event;
        FootswitchPair fs;
        uint32_t t = 0;
        auto step = [&](bool a, bool b, uint32_t ms) {
            E last = E::None;
            for (uint32_t i = 0; i < ms; i++) {
                const E e = fs.Update(a, b, ++t);
                if (e != E::None) last = e;
            }
            return last;
        };
        CHECK(step(true, false, 50) == E::None);
        CHECK(step(false, false, 10) == E::Tap1);
        CHECK(step(false, true, 600) == E::Hold2);
        CHECK(step(false, false, 10) == E::None); // no tap after a hold

        // Both: FS1 first, FS2 joins 100 ms later. Neither taps nor holds on its own.
        CHECK(step(true, false, 100) == E::None);
        CHECK(step(true, true, 900) == E::None);
        CHECK(step(true, true, 200) == E::BothHold);
        CHECK(step(true, false, 50) == E::None); // FS2 lifted first
        CHECK(step(false, false, 50) == E::None); // no stray Tap1 either
        CHECK(step(true, false, 50) == E::None);
        CHECK(step(false, false, 10) == E::Tap1); // back to normal
    });

    Test("presets restore effects by name, whatever the chain order", [&] {
        Dummy a("Alpha"), b("Beta"), a2("Alpha"), b2("Beta");
        Chain one, two;
        one.Add(&a);
        one.Add(&b, false);
        two.Add(&b2);
        two.Add(&a2);
        a.SetParam(0, 0.9f);
        b.SetParam(2, 1.f);
        PresetBank::Preset p;
        CapturePreset(one, p);
        ApplyPreset(p, two, true);
        CHECK_NEAR(a2.GetParam(0), 0.9f, 1e-6f);
        CHECK_NEAR(b2.GetParam(2), 1.f, 1e-6f);
        CHECK(two.IsEnabled(1));  // Alpha on
        CHECK(!two.IsEnabled(0)); // Beta off

        // An effect the preset doesn't know: defaults, off.
        Dummy c("Gamma");
        Chain three;
        three.Add(&c);
        c.SetParam(0, 0.7f);
        ApplyPreset(p, three, true);
        CHECK_NEAR(c.GetParam(0), 0.2f, 1e-6f);
        CHECK(!three.IsEnabled(0));
    });

    Test("preset bank is plain data with a validity check", [&] {
        PresetBank a, b;
        CHECK(!a.Valid()); // zeroed memory, like blank flash
        CHECK(a == b);
        Dummy d("Alpha");
        Chain chain;
        chain.Add(&d);
        PresetBank c = MakeEmptyBank(chain);
        CHECK(c.Valid());
        CHECK(c != a);
        c.version++;
        CHECK(!c.Valid()); // a future format isn't misread
    });

    Test("power-up loads the saved current preset in preset mode", [&] {
        Rig fresh;
        CHECK(fresh.pedal.ui.Preset() == 0);
        CHECK(!fresh.pedal.ui.EditMode());
        CHECK(fresh.CompOn()); // factory P1: compressor only
        CHECK(!fresh.EnvOn());
        CHECK(fresh.pedal.ui.Snapshot().screen == PageView::Screen::Preset);

        PresetBank saved = fresh.pedal.ui.Bank();
        saved.current = 2;
        Rig again(&saved);
        CHECK(again.pedal.ui.Preset() == 2);
        CHECK(again.CompOn() && again.EnvOn()); // factory P3: both
    });

    Test("a saved 8-preset bank is upgraded, keeping its presets", [&] {
        Rig fresh;
        PresetBank old = fresh.pedal.ui.Bank();
        old.version = 1;
        old.current = 5;
        old.presets[5].fx[Pedal::kCompSlot].values[Compressor::kThresh] = 0.9f;
        // Past the old bank's end: whatever the flash held (erased flash reads 0xFF).
        std::memset(&old.presets[PresetBank::kV1NumPresets], 0xFF,
                    sizeof(PresetBank::Preset) * (PresetBank::kNumPresets - PresetBank::kV1NumPresets));
        Rig r(&old);
        CHECK(r.pedal.ui.Bank().Valid());
        CHECK(r.pedal.ui.Preset() == 5);
        CHECK_NEAR(r.Comp(Compressor::kThresh), 0.9f, 1e-6f);
        for (int i = PresetBank::kV1NumPresets; i < PresetBank::kNumPresets; i++) {
            const PresetBank::Preset& p = r.pedal.ui.Bank().presets[i];
            CHECK(p.numEffects == uint32_t(r.pedal.chain.Size()));
            for (uint32_t e = 0; e < p.numEffects; e++) CHECK(p.fx[e].enabled == 0);
        }

        PresetBank junk = old;
        junk.version = 7; // unknown: factory presets
        Rig f(&junk);
        CHECK(f.pedal.ui.Preset() == 0);
        CHECK(f.CompOn());
    });

    Test("preset mode: FS 1/FS 2 step through presets, wrapping", [&] {
        Rig r;
        r.Tap(1);
        CHECK(r.pedal.ui.Preset() == 1);
        CHECK(!r.CompOn() && r.EnvOn()); // factory P2: envelope filter only
        CHECK(std::strcmp(r.pedal.ui.Snapshot().name, "Env Filter") == 0); // knobs follow the effect that's on
        r.Tap(0);
        r.Tap(0);
        CHECK(r.pedal.ui.Preset() == PresetBank::kNumPresets - 1);
        CHECK(!r.CompOn() && !r.DriveOn() && !r.EnvOn()); // P16: empty
        for (int i = PresetBank::kNumPresets - 1; i > 7; i--) r.Tap(0);
        CHECK(r.pedal.ui.Preset() == 7);
        CHECK(!r.CompOn() && r.DriveOn() && r.EnvOn()); // P8: fuzz into the envelope filter
        CHECK(r.pedal.drive.GetParam(Drive::kChar) == 1.f);
    });

    Test("knobs edit in preset mode; changing preset discards the edit", [&] {
        Rig r;
        r.in.pots[Compressor::kThresh] = 0.8f; // Thresh defaults to 0.5, where the pot is: caught
        r.Run(5);
        CHECK_NEAR(r.Comp(Compressor::kThresh), 0.8f, 1e-6f);
        CHECK(r.pedal.ui.Modified());
        CHECK(r.pedal.ui.Snapshot().screen == PageView::Screen::Page); // popup shows what's changing
        r.Run(PedalUi::kPopupMs + 10);
        CHECK(r.pedal.ui.Snapshot().screen == PageView::Screen::Preset);
        CHECK(r.pedal.ui.Snapshot().modified);

        r.Tap(1);
        r.Tap(0); // away and back
        CHECK(r.pedal.ui.Preset() == 0);
        CHECK_NEAR(r.Comp(Compressor::kThresh), 0.5f, 1e-6f);
        CHECK(!r.pedal.ui.Modified());
    });

    Test("holding both footswitches saves the current preset", [&] {
        Rig r;
        r.in.pots[Compressor::kThresh] = 0.8f;
        r.Run(5);
        PresetBank out;
        CHECK(!r.pedal.ui.TakeSaveRequest(out));
        r.HoldBoth();
        CHECK(r.pedal.ui.Preset() == 0); // the taps of a chord don't change preset
        CHECK(!r.pedal.ui.Modified());
        CHECK(std::strcmp(r.pedal.ui.Snapshot().message, "Saved P1") == 0);
        CHECK(r.pedal.ui.TakeSaveRequest(out));
        CHECK(!r.pedal.ui.TakeSaveRequest(out)); // once
        CHECK(out.current == 0);
        CHECK_NEAR(out.presets[0].fx[Pedal::kCompSlot].values[Compressor::kThresh], 0.8f, 1e-6f);

        r.Tap(1);
        r.Tap(0);
        CHECK_NEAR(r.Comp(Compressor::kThresh), 0.8f, 1e-6f); // the saved version now

        Rig rebooted(&out);
        CHECK_NEAR(rebooted.Comp(Compressor::kThresh), 0.8f, 1e-6f);
    });

    Test("edit mode: FS 1 toggles the effect, FS 2 pages, hold FS 1 to leave", [&] {
        Rig r;
        r.Hold(0);
        CHECK(r.pedal.ui.EditMode());
        CHECK(r.pedal.ui.Preset() == 0);
        CHECK(r.pedal.ui.Snapshot().screen == PageView::Screen::Page);
        CHECK(std::strcmp(r.pedal.ui.Snapshot().name, "Compressor") == 0);
        r.Tap(0);
        CHECK(!r.CompOn());
        CHECK(r.pedal.ui.Modified());
        // Screen panel pages: Compressor 1/4, Drive 2/4, Env Filter 3/4 and 4/4 (Lows + Xover on pots).
        CHECK(r.pedal.ui.Snapshot().pages == 4);
        r.Tap(1);
        CHECK(std::strcmp(r.pedal.ui.Snapshot().name, "Drive") == 0);
        CHECK(r.pedal.ui.Snapshot().toggles[1].info.name == std::string("Lows")); // both switches on toggles
        r.Tap(1);
        CHECK(std::strcmp(r.pedal.ui.Snapshot().name, "Env Filter") == 0);
        r.Tap(1);
        CHECK(r.pedal.ui.Snapshot().page == 3);
        CHECK(r.pedal.ui.Snapshot().pots[0].info.isSwitch); // Lows on pot 1
        r.Tap(1);
        CHECK(r.pedal.ui.Snapshot().page == 0); // wraps to the compressor
        r.Hold(0);
        CHECK(!r.pedal.ui.EditMode());
        CHECK(r.pedal.ui.Modified()); // leaving edit mode keeps the (unsaved) edit
    });

    Test("tuner: hold FS 2 to open, any tap closes without other effects", [&] {
        Rig r;
        r.Hold(1);
        CHECK(r.pedal.ui.TunerMode());
        CHECK(r.pedal.ui.Snapshot().screen == PageView::Screen::Tuner);
        r.Tap(0);
        CHECK(!r.pedal.ui.TunerMode());
        CHECK(r.pedal.ui.Preset() == 0);
    });

    Test("pedal mutes its output in tuner mode", [&] {
        Rig r;
        float in[48], out[48];
        for (int i = 0; i < 48; i++) in[i] = 0.3f;
        r.in.footswitches[1] = true;
        for (int b = 0; b < 700; b++) r.pedal.Process(r.in, ++r.now, in, out, 48); // hold 700 ms
        CHECK(r.pedal.ui.TunerMode());
        CHECK(std::fabs(out[47]) < 1e-3f);
    });

    Test("screens draw: preset, page, tuner and a message", [&] {
        Rig r;
        ui::Framebuffer fb;
        ui::TunerNeedle needle;
        PageView v = r.pedal.ui.Snapshot();
        ui::DrawScreen(fb, v, needle, 440.f, 12);
        CHECK(LitPixels(fb, 50, 13, 80, 38) > 20); // the big preset number

        r.Hold(0);
        v = r.pedal.ui.Snapshot();
        ui::DrawScreen(fb, v, needle, 440.f, 12);
        CHECK(LitPixels(fb, 0, 0, 59, 7) > 20); // "Compressor" header
        CHECK(LitPixels(fb, 36, 12, 104, 17) > 10); // first pot's bar

        r.HoldBoth();
        v = r.pedal.ui.Snapshot();
        ui::DrawScreen(fb, v, needle, 440.f, 12);
        CHECK(!fb.Pixel(64 - 28 - 6, 30)); // the message box is cleared around its text

        v.screen = PageView::Screen::Tuner;
        v.message[0] = '\0';
        ui::DrawScreen(fb, v, needle, 440.f, 12);
        CHECK(LitPixels(fb, 4, 52, 123, 52) > 100); // needle track
    });
}
