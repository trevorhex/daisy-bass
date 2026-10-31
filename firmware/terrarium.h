#pragma once

// PedalPCB Terrarium hardware, wired from the schematic in PedalPCB's
// build doc (rev. 10/23/25), page 3:
//
//     POT 1   POT 2   POT 3        Seed D16  D17  D18
//     POT 4   POT 5   POT 6             D19  D20  D21
//   SW 1  SW 2  SW 3  SW 4         D10 D9 D8 D7   (SPDT ON/ON toggles)
//     LED 1          LED 2         D22 / D23
//     FS 1           FS 2          D25 / D26      (momentary SPST)
//
// Free on the Terrarium: D11/D12 (I2C1 SCL/SDA), used here for the
// optional OLED.
//
// We drive the Seed directly instead of reusing libDaisy's DaisyPetal class:
// the Petal's knob numbering is not in pot order (its KNOB_2 is D19, i.e.
// Terrarium Pot 4), and it also starts an encoder and an I2C LED driver
// that the Terrarium doesn't have.

#include "daisy_seed.h"

class Terrarium {
public:
    static constexpr int kNumPots = 6;
    static constexpr int kNumToggles = 4;
    static constexpr int kNumFootswitches = 2;
    static constexpr size_t kBlockSize = 48; // 1 ms at 48 kHz

    daisy::DaisySeed seed;

    void Init() {
        using namespace daisy;
        seed.Init(true); // 480 MHz
        seed.SetAudioBlockSize(kBlockSize);
        seed.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

        const Pin potPins[kNumPots] = {seed::D16, seed::D17, seed::D18, seed::D19, seed::D20, seed::D21};
        AdcChannelConfig adc[kNumPots];
        for (int i = 0; i < kNumPots; i++) adc[i].InitSingle(potPins[i]);
        seed.adc.Init(adc, kNumPots);
        for (int i = 0; i < kNumPots; i++) pots_[i].Init(seed.adc.GetPtr(i), seed.AudioCallbackRate());

        // Lug 3 (clockwise end) goes to 3V3, so values rise clockwise.
        const Pin togglePins[kNumToggles] = {seed::D10, seed::D9, seed::D8, seed::D7};
        for (int i = 0; i < kNumToggles; i++) toggles_[i].Init(togglePins[i]);

        footswitches_[0].Init(seed::D25);
        footswitches_[1].Init(seed::D26);

        leds_[0].Init(seed::D22, GPIO::Mode::OUTPUT);
        leds_[1].Init(seed::D23, GPIO::Mode::OUTPUT);
    }

    void StartAdc() { seed.adc.Start(); }

    /** Call once per audio block: smooths pots and debounces switches. */
    void ProcessControls() {
        for (auto& p : pots_) p.Process();
        for (auto& t : toggles_) t.Debounce();
        for (auto& f : footswitches_) f.Debounce();
    }

    /** Pot 1..6 as index 0..5, value 0..1. */
    float Pot(int i) const { return pots_[i].Value(); }

    /** Toggle 1..4 as index 0..3. Which physical position reads "on" depends on how it's mounted. */
    bool Toggle(int i) const { return toggles_[i].Pressed(); }

    /** Footswitch 1..2 as index 0..1. */
    bool Footswitch(int i) const { return footswitches_[i].Pressed(); }

    /** LED 1..2 as index 0..1. */
    void SetLed(int i, bool on) { leds_[i].Write(on); }

private:
    daisy::AnalogControl pots_[kNumPots];
    daisy::Switch toggles_[kNumToggles];
    daisy::Switch footswitches_[kNumFootswitches];
    daisy::GPIO leds_[2];
};
