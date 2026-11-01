#pragma once

#include <cmath>

#include "Effect.h"
#include "Smoother.h"
#include "Svf.h"

namespace bass {

/**
 * Compressor voiced for bass. Feed-forward, gain computed in dB with a soft knee.
 *
 *   Fast  Peak detector, 6 dB knee, Attack/Release exactly as set. Grabs
 *         every pluck: punchy, good for slap and evening out a pick.
 *   Opto  RMS detector, 12 dB knee and a two-stage release (quick at first,
 *         then slow), like an optical compressor: smooth, even sustain.
 *
 * Blend mixes the dry signal back in ("parallel" compression): the dry
 * attack stays while the compressed signal adds sustain underneath.
 * Detect = HPF makes the detector ignore the lows (below ~100 Hz), so open
 * low strings don't push the whole signal down.
 */
class Compressor : public Effect {
public:
    enum ParamIndex { kThresh, kRatio, kAttack, kRelease, kBlend, kLevel, kMode, kDetect, kNumParams };

    const char* Name() const override { return "Compressor"; }

    void Init(float sampleRate) override {
        sampleRate_ = sampleRate;
        detectHpf_.Init(sampleRate);
        detectHpf_.SetParams(100.f, 0.70710678f);
        blend_.Init(sampleRate, 0.02f);
        level_.Init(sampleRate, 0.02f);
        rmsCoef_ = Coef(0.005f);
        meanSquare_ = grFast_ = grSlow_ = gr_ = 0.f;
        Update(/*jump=*/true);
    }

    // --- Effect interface ---------------------------------------------------

    int NumParams() const override { return kNumParams; }

    ParamInfo Param(int i) const override {
        static const ParamInfo info[kNumParams] = {
            {"Thresh", 0.5f}, // -40 .. 0 dB
            {"Ratio", 0.35f}, // 1.5:1 .. 20:1
            {"Atk", 0.6f},    // 0.3 .. 50 ms
            {"Rel", 0.4f},    // 40 ms .. 1 s
            {"Blend", 1.0f},  // dry .. compressed
            {"Level", 0.5f},  // -18 .. +18 dB, 0.5 = unity
            {"Mode", 0.f, true, "Fast", "Opto"},
            {"Detect", 0.f, true, "Full", "HPF"},
        };
        if (i < 0 || i >= kNumParams) return {"?", 0.f};
        return info[i];
    }

    void SetParam(int i, float v) override {
        if (i < 0 || i >= kNumParams) return;
        v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
        if (Param(i).isSwitch) v = v >= 0.5f ? 1.f : 0.f;
        values_[i] = v;
        Update(/*jump=*/false);
    }

    float GetParam(int i) const override { return (i < 0 || i >= kNumParams) ? 0.f : values_[i]; }

    void Process(float* buffer, size_t size) override {
        for (size_t i = 0; i < size; i++) buffer[i] = Process(buffer[i]);
    }

    /** Gain reduction on a 0..20 dB scale, for the meter / LED. */
    float Activity() const override { return gr_ >= 20.f ? 1.f : gr_ / 20.f; }

    // --- Per-sample processing ---------------------------------------------

    float Process(float x) {
        // Both detectors always run so switching Mode / Detect doesn't jump.
        const float hp = detectHpf_.Process(x).high;
        const float sc = hpfDetect_ ? hp : x;
        meanSquare_ = sc * sc + rmsCoef_ * (meanSquare_ - sc * sc);
        // RMS scaled by √2 reads a sine's peak, so both modes share a threshold.
        const float level = opto_ ? std::sqrt(2.f * meanSquare_) : std::fabs(sc);
        const float target = GainReductionDb(20.f * std::log10(level + 1e-6f));

        // Ballistics on the gain reduction itself (in dB): attack while it has
        // to grow, release while it may shrink.
        grFast_ = target + (target > grFast_ ? attackCoef_ : releaseCoef_) * (grFast_ - target);
        if (opto_) {
            grSlow_ = target + (target > grSlow_ ? attackCoef_ : slowReleaseCoef_) * (grSlow_ - target);
            gr_ = 0.6f * grFast_ + 0.4f * grSlow_; // 60% lets go quickly, 40% lingers
        } else {
            grSlow_ = grFast_;
            gr_ = grFast_;
        }

        const float compressed = x * std::pow(10.f, -gr_ / 20.f);
        const float blend = blend_.Process();
        return (x + blend * (compressed - x)) * level_.Process();
    }

    /** Current gain reduction in dB (≥ 0). */
    float GainReductionDb() const { return gr_; }

private:
    float Coef(float seconds) const { return std::exp(-1.f / (seconds * sampleRate_)); }

    /** Static curve: how many dB to turn a signal at `levelDb` down, soft knee. */
    float GainReductionDb(float levelDb) const {
        const float over = levelDb - thresholdDb_;
        const float slope = 1.f - 1.f / ratio_;
        if (2.f * over <= -kneeDb_) return 0.f;
        if (2.f * over >= kneeDb_) return over * slope;
        const float k = over + 0.5f * kneeDb_;
        return slope * k * k / (2.f * kneeDb_);
    }

    void Update(bool jump) {
        const float* v = values_;
        thresholdDb_ = -40.f + 40.f * v[kThresh];
        ratio_ = 1.5f * std::pow(20.f / 1.5f, v[kRatio]);
        opto_ = v[kMode] >= 0.5f;
        hpfDetect_ = v[kDetect] >= 0.5f;
        kneeDb_ = opto_ ? 12.f : 6.f;

        const float attack = 0.0003f * std::pow(50.f / 0.3f, v[kAttack]);
        const float release = 0.04f * std::pow(25.f, v[kRelease]);
        attackCoef_ = Coef(attack);
        releaseCoef_ = Coef(opto_ ? 0.3f * release : release);
        slowReleaseCoef_ = Coef(3.f * release);

        const float level = std::pow(10.f, (v[kLevel] - 0.5f) * 36.f / 20.f);
        if (jump) {
            blend_.Jump(v[kBlend]);
            level_.Jump(level);
        } else {
            blend_.SetTarget(v[kBlend]);
            level_.SetTarget(level);
        }
    }

    float sampleRate_ = 48000.f;
    float values_[kNumParams] = {0.5f, 0.35f, 0.6f, 0.4f, 1.0f, 0.5f, 0.f, 0.f};

    float thresholdDb_ = -20.f, ratio_ = 4.f, kneeDb_ = 6.f;
    bool opto_ = false, hpfDetect_ = false;
    float attackCoef_ = 0.f, releaseCoef_ = 0.f, slowReleaseCoef_ = 0.f, rmsCoef_ = 0.f;

    Svf detectHpf_;
    float meanSquare_ = 0.f;
    float grFast_ = 0.f, grSlow_ = 0.f, gr_ = 0.f;
    Smoother blend_, level_;
};

} // namespace bass
