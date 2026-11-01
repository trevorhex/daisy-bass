// daisy-bass firmware: multi-effect bass pedal on a PedalPCB Terrarium.
//
// The pedal itself (effects, tuner, footswitch/page/preset logic) lives in
// control/Pedal.h and is shared with the desktop simulator; the screens are
// in ui/Screens.h. This file only connects them to the hardware:
//
//   audio callback   read the controls → Pedal::Process()
//   main loop        snapshot → OLED / LEDs, tuner analysis, preset saving
//
// Controls (see control/PedalUi.h for the full table):
//   Preset mode   FS 1 / FS 2 tap: previous / next preset, FS 1 hold: edit mode
//   Edit mode     FS 1 tap: effect on/off, FS 2 tap: next page, FS 1 hold: back
//   Either        FS 2 hold: tuner, both held 1 s: save the current preset
//
// Build flags (see README): SCREEN_PANEL=1, OLED=1, LOG=1, KNOB_CHECK=1, BOOT_SRAM=1.

#include "control/Pedal.h"
#include "daisy_seed.h"
#include "terrarium.h"
#include "ui/Screens.h"
#include "util/PersistentStorage.h"
#include "util/scopedirqblocker.h"

#ifdef OLED
#include "Display.h"
#endif

using namespace daisy;

namespace {

static_assert(Terrarium::kNumPots <= bass::PedalUi::kMaxPots, "more pots than PedalUi supports");
static_assert(Terrarium::kNumToggles <= bass::PedalUi::kMaxToggles, "more toggles than PedalUi supports");

// Presets live near the end of the 8 MB QSPI flash, far past the bootloader's
// program area (0x90040000 + up to ~480 KB).
constexpr uint32_t kPresetFlashOffset = 0x7F0000;

Terrarium hw;
bass::Pedal pedal;
CpuLoadMeter cpu;
PersistentStorage<bass::PresetBank> presetStorage(hw.seed.qspi);
#ifdef OLED
Display display;
#endif

bass::PedalUi::Inputs ReadInputs() {
    bass::PedalUi::Inputs in;
    for (int i = 0; i < Terrarium::kNumPots; i++) in.pots[i] = hw.Pot(i);
    for (int i = 0; i < Terrarium::kNumToggles; i++) in.toggles[i] = hw.Toggle(i);
    for (int i = 0; i < Terrarium::kNumFootswitches; i++) in.footswitches[i] = hw.Footswitch(i);
    return in;
}

// Runs every 48 samples (1 ms). Must never block, allocate, or touch the OLED or flash.
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    cpu.OnBlockStart();
    hw.ProcessControls();
    pedal.Process(ReadInputs(), System::GetNow(), in[0], out[0], size);
    for (size_t i = 0; i < size; i++) out[1][i] = out[0][i]; // Terrarium is mono
    cpu.OnBlockEnd();
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
    cpu.Init(sr, Terrarium::kBlockSize);

    hw.StartAdc();
    System::Delay(10); // let the pots read once before mapping them
    for (int i = 0; i < 20; i++) hw.ProcessControls();

    // Blank flash (first boot) or an older preset format: start from the factory bank.
    presetStorage.Init(bass::PresetBank(), kPresetFlashOffset);
    const bass::PresetBank saved = presetStorage.GetSettings();
    pedal.Init(sr, Terrarium::kNumPots, Terrarium::kNumToggles, &saved, ReadInputs(), System::GetNow());
    hw.seed.StartAudio(AudioCallback);

    ui::TunerNeedle needle;
    uint32_t nextDraw = 0, nextLog = 0;
    bass::PresetBank toSave;

    while (true) {
        const uint32_t now = System::GetNow();

        bass::PageView view;
        bool save;
        {
            ScopedIrqBlocker block; // a few microseconds: copy state the audio callback writes
            view = pedal.ui.Snapshot();
            save = pedal.ui.TakeSaveRequest(toSave);
        }
        if (save) {
            // Erase + write takes tens of ms. Audio keeps running: the program runs
            // from SRAM / internal flash, and the callback doesn't touch QSPI.
            presetStorage.GetSettings() = toSave;
            presetStorage.Save();
        }

        if (view.screen == bass::PageView::Screen::Tuner) needle.Update(pedal.tuner, now);

        bool leds[2];
        ui::ComputeLeds(view, needle, now, leds);
        hw.SetLed(0, leds[0]);
        hw.SetLed(1, leds[1]);

#ifdef OLED
        if (int32_t(now - nextDraw) >= 0) {
            nextDraw = now + 33; // ~30 fps
            display.Draw(view, needle, pedal.tuner.Reference(), int(cpu.GetAvgCpuLoad() * 100.f));
        }
#endif

#ifdef LOG
        if (int32_t(now - nextLog) >= 0) {
            nextLog = now + 1000;
            hw.seed.PrintLine("cpu avg %d%% max %d%% | preset %d%s %s", int(cpu.GetAvgCpuLoad() * 100.f),
                              int(cpu.GetMaxCpuLoad() * 100.f), view.preset + 1, view.modified ? "*" : "",
                              view.editMode ? "edit" : "play");
        }
#endif
        (void)nextDraw;
        (void)nextLog;
        System::Delay(1);
    }
}
