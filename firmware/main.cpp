// daisy-bass firmware: envelope filter for bass on a PedalPCB Terrarium.
//
// Controls
//   Pot 1 Sensitivity   Pot 2 Range       Pot 3 Resonance
//   Pot 4 Response      Pot 5 Mix         Pot 6 Level
//   SW 1  Low-pass / Band-pass
//   SW 2  Sweep up / Sweep down
//   SW 3  Clean lows on / off (crossover)
//   SW 4  Crossover 150 Hz / 250 Hz
//   FS 1  Bypass (LED 1 = effect on)
//   FS 2  Reserved for the next effect. LED 2 shows the envelope for now.
//
// Build: `make` here, flash: `make program-dfu` (see README).

#include "daisy_petal.h"
#include "dsp/EnvelopeFilter.h"
#include "terrarium.h"

// Uncomment to stream pot/switch readings over USB serial instead of
// processing audio, to verify the control mapping on your board.
// #define KNOB_CHECK

using namespace daisy;
using namespace terrarium;

namespace {

DaisyPetal hw;
bass::EnvelopeFilter envFilter;
bass::EnvelopeFilter::Params params;

GPIO led1, led2;
volatile bool bypassed = false;

float Pot(int n) { return hw.GetKnobValue(POTS[n]); }
bool Toggle(Switch s) { return hw.switches[s].Pressed(); }

// Runs every block (48 samples = 1 ms). Must never block or allocate.
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    hw.ProcessAllControls();

    if (hw.switches[FOOTSWITCH_1].RisingEdge()) bypassed = !bypassed;

    params.sensitivity = Pot(0);
    params.range = Pot(1);
    params.resonance = Pot(2);
    params.response = Pot(3);
    params.mix = Pot(4);
    params.level = Pot(5);
    params.mode = Toggle(SWITCH_1) ? bass::EnvelopeFilter::Mode::BandPass : bass::EnvelopeFilter::Mode::LowPass;
    params.sweepDown = Toggle(SWITCH_2);
    params.cleanLows = !Toggle(SWITCH_3);
    params.crossoverHz = Toggle(SWITCH_4) ? 250.f : 150.f;
    params.bypass = bypassed; // crossfaded inside the effect, so no click
    envFilter.SetParams(params);

    for (size_t i = 0; i < size; i++) {
        const float y = envFilter.Process(in[0][i]);
        out[0][i] = y;
        out[1][i] = y; // Terrarium is mono; keep both codec channels identical
    }
}

#ifdef KNOB_CHECK
void RunKnobCheck() {
    hw.seed.StartLog(true); // waits for a serial monitor to connect
    hw.StartAdc();
    while (true) {
        hw.ProcessAllControls();
        hw.seed.PrintLine("pots %3d %3d %3d %3d %3d %3d | sw %d%d%d%d | fs %d%d",
            int(Pot(0) * 100), int(Pot(1) * 100), int(Pot(2) * 100),
            int(Pot(3) * 100), int(Pot(4) * 100), int(Pot(5) * 100),
            Toggle(SWITCH_1), Toggle(SWITCH_2), Toggle(SWITCH_3), Toggle(SWITCH_4),
            Toggle(FOOTSWITCH_1), Toggle(FOOTSWITCH_2));
        System::Delay(200);
    }
}
#endif

} // namespace

int main() {
    hw.Init();
    hw.SetAudioBlockSize(48);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    led1.Init(Led1Pin(), GPIO::Mode::OUTPUT);
    led2.Init(Led2Pin(), GPIO::Mode::OUTPUT);

#ifdef KNOB_CHECK
    RunKnobCheck();
#endif

    envFilter.Init(hw.AudioSampleRate());

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    // Main loop: only non-real-time work lives here.
    while (true) {
        led1.Write(!bypassed);
        led2.Write(!bypassed && envFilter.Envelope() > 0.5f);
        System::Delay(5);
    }
}
