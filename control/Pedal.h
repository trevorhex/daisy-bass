#pragma once

#include <cstddef>
#include <cstdint>

#include "../dsp/Chain.h"
#include "../dsp/Compressor.h"
#include "../dsp/EnvelopeFilter.h"
#include "../dsp/Smoother.h"
#include "../dsp/Tuner.h"
#include "PedalUi.h"
#include "Presets.h"

namespace bass {

/**
 * The whole pedal minus hardware: the effects, their order, the tuner, the
 * output mute and the controls. The firmware's audio callback and the
 * simulator's both just call Process().
 *
 * Adding an effect: add a member below, Add() it in Init() in chain order,
 * and give it a factory preset if you like. Pages, presets, pickup, the
 * screen and the LEDs all work from the Effect interface.
 */
class Pedal {
public:
    static constexpr size_t kBlockSize = 48; // 1 ms at 48 kHz: one control update per block

    // Chain order: compressor first (it evens out what the filter's envelope reacts to).
    Compressor compressor;
    EnvelopeFilter envFilter;
    enum Slot { kCompSlot, kEnvSlot };

    Chain chain;
    Tuner tuner;
    PedalUi ui;

    /**
     * `saved` is the bank read from storage, or nullptr / an invalid bank to
     * start from the factory presets.
     */
    void Init(float sampleRate, int numPots, int numToggles, const PresetBank* saved,
              const PedalUi::Inputs& inputs, uint32_t nowMs) {
        chain.Add(&compressor, false); // kCompSlot
        chain.Add(&envFilter, false);  // kEnvSlot
        chain.Init(sampleRate);
        tuner.Init(sampleRate);
        outputGain_.Init(sampleRate, 0.01f, 1.f);
        const PresetBank bank = (saved && saved->Valid()) ? *saved : FactoryBank();
        ui.Init(&chain, numPots, numToggles, bank, inputs, nowMs);
    }

    /** Factory presets. Built from the effects' defaults, so call after Init() has added them. */
    PresetBank FactoryBank() {
        // Each preset starts from defaults with everything off.
        auto start = [&]() {
            for (int e = 0; e < chain.Size(); e++) {
                chain.At(e)->ResetParams();
                chain.SetEnabled(e, false, true);
            }
        };
        start();
        PresetBank bank = MakeEmptyBank(chain); // P7, P8: clean (all off)

        start(); // P1: compressor, defaults (Fast)
        chain.SetEnabled(kCompSlot, true, true);
        CapturePreset(chain, bank.presets[0]);

        start(); // P2: envelope filter, defaults
        chain.SetEnabled(kEnvSlot, true, true);
        CapturePreset(chain, bank.presets[1]);

        start(); // P3: compressor into envelope filter
        chain.SetEnabled(kCompSlot, true, true);
        chain.SetEnabled(kEnvSlot, true, true);
        CapturePreset(chain, bank.presets[2]);

        start(); // P4: smooth opto squeeze, blended
        compressor.SetParam(Compressor::kMode, 1.f);
        compressor.SetParam(Compressor::kThresh, 0.35f);
        compressor.SetParam(Compressor::kRatio, 0.6f);
        compressor.SetParam(Compressor::kBlend, 0.7f);
        compressor.SetParam(Compressor::kLevel, 0.62f);
        chain.SetEnabled(kCompSlot, true, true);
        CapturePreset(chain, bank.presets[3]);

        start(); // P5: quacky band-pass, compressed first
        envFilter.SetParam(EnvelopeFilter::kMode, 1.f);
        envFilter.SetParam(EnvelopeFilter::kReso, 0.75f);
        envFilter.SetParam(EnvelopeFilter::kSens, 0.65f);
        chain.SetEnabled(kCompSlot, true, true);
        chain.SetEnabled(kEnvSlot, true, true);
        CapturePreset(chain, bank.presets[4]);

        start(); // P6: sweep down, darker
        envFilter.SetParam(EnvelopeFilter::kSweep, 1.f);
        envFilter.SetParam(EnvelopeFilter::kRange, 0.5f);
        envFilter.SetParam(EnvelopeFilter::kReso, 0.6f);
        chain.SetEnabled(kEnvSlot, true, true);
        CapturePreset(chain, bank.presets[5]);

        start();
        return bank;
    }

    /**
     * Audio callback: one control update, then `size` samples (normally
     * kBlockSize) from `input` to `output`. Mono.
     */
    void Process(const PedalUi::Inputs& inputs, uint32_t nowMs, const float* input, float* output, size_t size) {
        ui.Update(inputs, nowMs);
        outputGain_.SetTarget(ui.TunerMode() ? 0.f : 1.f);

        float buffer[kBlockSize];
        for (size_t start = 0; start < size; start += kBlockSize) {
            const size_t n = size - start < kBlockSize ? size - start : kBlockSize;
            for (size_t i = 0; i < n; i++) buffer[i] = input[start + i];

            tuner.Push(buffer, n); // the tuner hears the dry bass
            if (outputGain_.Value() > 1e-4f || !ui.TunerMode()) chain.Process(buffer, n);

            for (size_t i = 0; i < n; i++) output[start + i] = buffer[i] * outputGain_.Process();
        }
    }

private:
    Smoother outputGain_; // fades to silence in tuner mode
};

} // namespace bass
