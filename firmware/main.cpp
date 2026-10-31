// daisy-bass firmware: multi-effect bass pedal on a PedalPCB Terrarium.
//
// Effects: Env Filter (more to come). Pots and toggles edit the effect on
// the current page; the tuner listens to the dry input.
//
//   FS 1 tap    Current effect on/off (exits the tuner if it's open)
//   FS 2 hold   Tuner on/off. Output is muted while tuning
//   FS 2 tap    Next effect page (exits the tuner if it's open)
//
//   LED 1/2     Normal: effect on / envelope activity
//               Tuner:  LED 1 blinks = flat, LED 2 blinks = sharp,
//                       both solid = in tune (±3 cents), slow blink = no note
//
// Env Filter page:
//   Pot 1 Sensitivity   Pot 2 Range       Pot 3 Resonance
//   Pot 4 Response      Pot 5 Mix         Pot 6 Level
//   SW 1  LP / BP       SW 2  Up / Down   SW 3 Lows filtered / clean   SW 4 Xover 150 / 250
//
// Build flags (see README): OLED=1, LOG=1, KNOB_CHECK=1, BOOT_SRAM=1.

#include <cmath>

#include "control/FootswitchGesture.h"
#include "control/KnobPickup.h"
#include "daisy_seed.h"
#include "dsp/Chain.h"
#include "dsp/EnvelopeFilter.h"
#include "dsp/Smoother.h"
#include "dsp/Tuner.h"
#include "terrarium.h"

#ifdef OLED
#include "Display.h"
#endif

using namespace daisy;

namespace {

Terrarium hw;
bass::Chain chain;
bass::EnvelopeFilter envFilter;
bass::Tuner tuner;
bass::Smoother outputGain; // fades to silence in tuner mode
CpuLoadMeter cpu;
#ifdef OLED
Display display;
#endif

// --- UI state. Written in the audio callback, read in the main loop. ----

volatile int page = 0;
volatile bool tunerMode = false;

bass::FootswitchGesture fs1, fs2;
bass::KnobPickup pickups[Terrarium::kNumPots];
int potParam[Terrarium::kNumPots];       // effect param under each pot, -1 = none
int toggleParam[Terrarium::kNumToggles]; // effect param under each toggle
bool lastToggle[Terrarium::kNumToggles];
bool potCaught[Terrarium::kNumPots];     // copy for the display

/** Lay the current effect's params onto the pots (continuous) and toggles (switches). */
void MapPage(bool syncToggles) {
    bass::Effect* fx = chain.At(page);
    int pot = 0, toggle = 0;
    for (int& p : potParam) p = -1;
    for (int& t : toggleParam) t = -1;

    for (int i = 0; i < fx->NumParams(); i++) {
        if (fx->Param(i).isSwitch) {
            if (toggle < Terrarium::kNumToggles) toggleParam[toggle++] = i;
        } else if (pot < Terrarium::kNumPots) {
            pickups[pot].Arm(fx->GetParam(i), hw.Pot(pot));
            potParam[pot++] = i;
        }
    }

    for (int t = 0; t < Terrarium::kNumToggles; t++) {
        lastToggle[t] = hw.Toggle(t);
        // At power-up the physical toggles are the truth. On later page
        // changes, toggles only act when flipped, like the pickup pots.
        if (syncToggles && toggleParam[t] >= 0) fx->SetParam(toggleParam[t], lastToggle[t] ? 1.f : 0.f);
    }
}

void HandleFootswitches() {
    using E = bass::FootswitchGesture::Event;
    const float now = float(System::GetNow());

    switch (fs1.Update(hw.Footswitch(0), now)) {
        case E::Tap:
            if (tunerMode) tunerMode = false;
            else chain.Toggle(page);
            break;
        default: break;
    }

    switch (fs2.Update(hw.Footswitch(1), now)) {
        case E::Hold: tunerMode = !tunerMode; break;
        case E::Tap:
            if (tunerMode) {
                tunerMode = false;
            } else if (chain.Size() > 1) {
                page = (page + 1) % chain.Size();
                MapPage(false);
            }
            break;
        default: break;
    }
}

void HandleKnobsAndToggles() {
    bass::Effect* fx = chain.At(page);
    for (int pot = 0; pot < Terrarium::kNumPots; pot++) {
        float v;
        if (potParam[pot] >= 0 && pickups[pot].Update(hw.Pot(pot), v)) fx->SetParam(potParam[pot], v);
        potCaught[pot] = pickups[pot].Caught();
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

    float buffer[Terrarium::kBlockSize];
    for (size_t i = 0; i < size; i++) buffer[i] = in[0][i];

    tuner.Push(buffer, size); // tuner hears the dry bass
    outputGain.SetTarget(tunerMode ? 0.f : 1.f);
    if (outputGain.Value() > 1e-4f || !tunerMode) chain.Process(buffer, size);

    for (size_t i = 0; i < size; i++) {
        const float y = buffer[i] * outputGain.Process();
        out[0][i] = y;
        out[1][i] = y; // Terrarium is mono
    }
    cpu.OnBlockEnd();
}

// --- Main loop helpers (non-real-time) ------------------------------------

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
        hw.seed.PrintLine("pots %3d %3d %3d / %3d %3d %3d | sw %d%d%d%d | fs %d%d",
            int(hw.Pot(0) * 100), int(hw.Pot(1) * 100), int(hw.Pot(2) * 100),
            int(hw.Pot(3) * 100), int(hw.Pot(4) * 100), int(hw.Pot(5) * 100),
            hw.Toggle(0), hw.Toggle(1), hw.Toggle(2), hw.Toggle(3),
            hw.Footswitch(0), hw.Footswitch(1));
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
    MapPage(true);
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

        if (tunerMode) {
            UpdateTunerLeds(reading, cents, now);
        } else {
            hw.SetLed(0, chain.IsEnabled(page));
            hw.SetLed(1, chain.IsEnabled(0) && envFilter.Envelope() > 0.5f);
        }

#ifdef OLED
        if (now >= nextDraw) {
            nextDraw = now + 33; // ~30 fps
            if (tunerMode) {
                display.DrawTuner(reading, cents, tuner.Reference());
            } else {
                display.DrawEffect({chain.At(page), chain.IsEnabled(page), page, chain.Size(),
                                    int(cpu.GetAvgCpuLoad() * 100.f), potParam, toggleParam, potCaught});
            }
        }
#endif

#ifdef LOG
        if (now >= nextLog) {
            nextLog = now + 1000;
            hw.seed.PrintLine("cpu avg %d%% max %d%% | page %d tuner %d",
                int(cpu.GetAvgCpuLoad() * 100.f), int(cpu.GetMaxCpuLoad() * 100.f), page, tunerMode);
        }
#endif
        (void)nextDraw;
        (void)nextLog;
        System::Delay(1);
    }
}
