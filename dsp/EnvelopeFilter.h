#pragma once

#include <cmath>

#include "Crossover.h"
#include "Effect.h"
#include "EnvelopeFollower.h"
#include "Smoother.h"
#include "Svf.h"

namespace bass {

/**
 * Envelope filter ("auto-wah") voiced for bass.
 *
 * The harder you pluck, the further a resonant filter sweeps. Optionally the
 * lows below a crossover stay dry so the low end never thins out.
 *
 * Usable two ways: directly via the Params struct (tests, bassfx), or via the
 * generic Effect interface (pedal UI, OLED). Bypass lives in Chain.
 */
class EnvelopeFilter : public Effect {
public:
    enum class Mode { LowPass, BandPass };

    struct Params {
        float sensitivity = 0.5f; // how hard the envelope pushes the filter
        float range = 0.3f;       // where the sweep starts (60 Hz .. 600 Hz)
        float resonance = 0.5f;   // Q 0.7 .. 12
        float response = 0.4f;    // envelope release 40 ms .. 600 ms
        float mix = 1.0f;         // dry .. wet
        float level = 0.5f;       // output -18 dB .. +18 dB (0.5 = unity)
        Mode mode = Mode::LowPass;
        bool sweepDown = false;   // start high and close down instead of opening up
        bool cleanLows = true;    // keep everything below the crossover dry
        float crossoverHz = 150.f;
    };

    // Effect parameter indices: pots first, then toggles.
    enum ParamIndex { kSens, kRange, kReso, kResp, kMix, kLevel, kMode, kSweep, kLows, kXover, kNumParams };

    const char* Name() const override { return "Env Filter"; }

    void Init(float sampleRate) override {
        filter_.Init(sampleRate);
        follower_.Init(sampleRate);
        crossover_.Init(sampleRate, params_.crossoverHz);
        baseHz_.Init(sampleRate, 0.03f);
        q_.Init(sampleRate, 0.03f);
        mix_.Init(sampleRate, 0.02f);
        gain_.Init(sampleRate, 0.02f);
        SetParams(params_, /*jump=*/true);
    }

    /** Cheap: only stores targets. Safe to call once per audio block. */
    void SetParams(const Params& p, bool jump = false) {
        if (p.crossoverHz != params_.crossoverHz || jump) crossover_.SetFrequency(p.crossoverHz);
        params_ = p;

        envGain_ = 0.5f * std::exp2(Clamp01(p.sensitivity) * 6.f); // 0.5x .. 32x
        const float release = 0.04f * std::pow(15.f, Clamp01(p.response)); // 40 ms .. 600 ms
        follower_.SetTimes(0.002f + 0.01f * Clamp01(p.response), release);

        Target(baseHz_, 60.f * std::pow(10.f, Clamp01(p.range)), jump); // 60 .. 600 Hz
        Target(q_, 0.7f * std::pow(12.f / 0.7f, Clamp01(p.resonance)), jump);
        Target(mix_, Clamp01(p.mix), jump);
        Target(gain_, std::pow(10.f, (Clamp01(p.level) - 0.5f) * 36.f / 20.f), jump);
    }

    const Params& GetParams() const { return params_; }

    // --- Effect interface ---------------------------------------------------

    int NumParams() const override { return kNumParams; }

    ParamInfo Param(int i) const override {
        static const ParamInfo info[kNumParams] = {
            {"Sens", 0.5f},
            {"Range", 0.3f},
            {"Reso", 0.5f},
            {"Resp", 0.4f},
            {"Mix", 1.0f},
            {"Level", 0.5f},
            {"Mode", 0.f, true, "LP", "BP"},
            {"Sweep", 0.f, true, "Up", "Down"},
            {"Lows", 1.f, true, "Filt", "Clean"},
            {"Xover", 0.f, true, "150", "250"},
        };
        return info[i];
    }

    void SetParam(int i, float v) override {
        Params p = params_;
        switch (i) {
            case kSens: p.sensitivity = v; break;
            case kRange: p.range = v; break;
            case kReso: p.resonance = v; break;
            case kResp: p.response = v; break;
            case kMix: p.mix = v; break;
            case kLevel: p.level = v; break;
            case kMode: p.mode = v >= 0.5f ? Mode::BandPass : Mode::LowPass; break;
            case kSweep: p.sweepDown = v >= 0.5f; break;
            case kLows: p.cleanLows = v >= 0.5f; break;
            case kXover: p.crossoverHz = v >= 0.5f ? 250.f : 150.f; break;
            default: return;
        }
        SetParams(p);
    }

    float GetParam(int i) const override {
        const Params& p = params_;
        switch (i) {
            case kSens: return p.sensitivity;
            case kRange: return p.range;
            case kReso: return p.resonance;
            case kResp: return p.response;
            case kMix: return p.mix;
            case kLevel: return p.level;
            case kMode: return p.mode == Mode::BandPass ? 1.f : 0.f;
            case kSweep: return p.sweepDown ? 1.f : 0.f;
            case kLows: return p.cleanLows ? 1.f : 0.f;
            case kXover: return p.crossoverHz >= 200.f ? 1.f : 0.f;
            default: return 0.f;
        }
    }

    void Process(float* buffer, size_t size) override {
        for (size_t i = 0; i < size; i++) buffer[i] = Process(buffer[i]);
    }

    // --- Per-sample processing ---------------------------------------------

    float Process(float in) {
        const float base = baseHz_.Process();
        const float q = q_.Process();
        const float mix = mix_.Process();
        const float gain = gain_.Process();

        // 0..1 "how hard am I playing", with a soft knee instead of a hard clamp.
        const float env = 1.f - std::exp(-follower_.Process(in) * envGain_);
        const float sweep = params_.sweepDown ? 1.f - env : env;
        filter_.SetParams(base * std::exp2(kSweepOctaves * sweep), q);

        float low = 0.f, toFilter = in;
        if (params_.cleanLows) {
            const Crossover::Bands bands = crossover_.Process(in);
            low = bands.low;
            toFilter = bands.high;
        }

        const Svf::Out f = filter_.Process(toFilter);
        // Band output scaled by 1/Q so its peak sits at unity like the low-pass.
        const float filtered = params_.mode == Mode::LowPass ? f.low : f.band / q;
        const float wet = low + std::tanh(filtered); // tanh tames resonant peaks

        return (in * (1.f - mix) + wet * mix) * gain;
    }

    /** Current envelope 0..1, e.g. to drive an LED's brightness. */
    float Envelope() const { return 1.f - std::exp(-follower_.Value() * envGain_); }

private:
    static constexpr float kSweepOctaves = 4.f;

    static float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

    static void Target(Smoother& s, float value, bool jump) {
        jump ? s.Jump(value) : s.SetTarget(value);
    }

    Params params_;
    float envGain_ = 1.f;

    Svf filter_;
    EnvelopeFollower follower_;
    Crossover crossover_;
    Smoother baseHz_, q_, mix_, gain_;
};

} // namespace bass
