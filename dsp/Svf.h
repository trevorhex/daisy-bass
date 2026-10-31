#pragma once

#include <cmath>

namespace bass {

/**
 * State-variable filter, "topology-preserving transform" form (Zavalishin).
 * Stays stable while the cutoff is swept every sample, which is exactly
 * what an envelope filter does.
 */
class Svf {
public:
    struct Out { float low, band, high; };

    void Init(float sampleRate) {
        sampleRate_ = sampleRate;
        Reset();
        SetParams(1000.f, 0.707f);
    }

    void Reset() { ic1_ = ic2_ = 0.f; }

    /** cutoff in Hz (clamped below Nyquist), q of 0.5 (soft) up to ~20 (whistly). */
    void SetParams(float cutoff, float q) {
        const float maxCutoff = 0.45f * sampleRate_;
        cutoff = cutoff < 10.f ? 10.f : (cutoff > maxCutoff ? maxCutoff : cutoff);
        g_ = std::tan(kPi * cutoff / sampleRate_);
        k_ = 1.f / (q < 0.5f ? 0.5f : q);
        a1_ = 1.f / (1.f + g_ * (g_ + k_));
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;
    }

    Out Process(float x) {
        const float v3 = x - ic2_;
        const float v1 = a1_ * ic1_ + a2_ * v3;
        const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2.f * v1 - ic1_;
        ic2_ = 2.f * v2 - ic2_;
        return {v2, v1, x - k_ * v1 - v2};
    }

private:
    static constexpr float kPi = 3.14159265358979f;
    float sampleRate_ = 48000.f;
    float g_ = 0, k_ = 1, a1_ = 0, a2_ = 0, a3_ = 0;
    float ic1_ = 0, ic2_ = 0;
};

} // namespace bass
