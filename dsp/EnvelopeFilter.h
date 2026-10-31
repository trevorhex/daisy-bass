#pragma once

#include <cmath>

#include "Crossover.h"
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
 * All knob values are 0..1 so the same struct can come from pots on the
 * pedal or command-line flags on the desktop.
 */
class EnvelopeFilter {
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
        bool bypass = false;
    };

    void Init(float sampleRate) {
        sampleRate_ = sampleRate;
        filter_.Init(sampleRate);
        follower_.Init(sampleRate);
        crossover_.Init(sampleRate, params_.crossoverHz);
        baseHz_.Init(sampleRate, 0.03f);
        q_.Init(sampleRate, 0.03f);
        mix_.Init(sampleRate, 0.02f);
        gain_.Init(sampleRate, 0.02f);
        wetAmount_.Init(sampleRate, 0.01f); // bypass crossfade, avoids clicks
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
        Target(wetAmount_, p.bypass ? 0.f : 1.f, jump);
    }

    const Params& GetParams() const { return params_; }

    float Process(float in) {
        const float bypassMix = wetAmount_.Process();
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

        const float effect = (in * (1.f - mix) + wet * mix) * gain;
        return in + bypassMix * (effect - in);
    }

    /** Current envelope 0..1, e.g. to drive an LED's brightness. */
    float Envelope() const { return 1.f - std::exp(-follower_.Value() * envGain_); }

private:
    static constexpr float kSweepOctaves = 4.f;

    static float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

    static void Target(Smoother& s, float value, bool jump) {
        jump ? s.Jump(value) : s.SetTarget(value);
    }

    float sampleRate_ = 48000.f;
    Params params_;
    float envGain_ = 1.f;

    Svf filter_;
    EnvelopeFollower follower_;
    Crossover crossover_;
    Smoother baseHz_, q_, mix_, gain_, wetAmount_;
};

} // namespace bass
