#pragma once

#include <cmath>

namespace bass {

/**
 * Tracks how loud the signal is right now: jumps up quickly when you pluck
 * (attack) and falls back slowly as the note decays (release).
 */
class EnvelopeFollower {
public:
    void Init(float sampleRate) {
        sampleRate_ = sampleRate;
        env_ = 0.f;
        SetTimes(0.005f, 0.15f);
    }

    /** Attack and release in seconds. */
    void SetTimes(float attack, float release) {
        attackCoef_ = Coef(attack);
        releaseCoef_ = Coef(release);
    }

    float Process(float x) {
        const float level = std::fabs(x);
        const float coef = level > env_ ? attackCoef_ : releaseCoef_;
        env_ = level + coef * (env_ - level);
        return env_;
    }

    float Value() const { return env_; }

private:
    float Coef(float seconds) const {
        return seconds <= 0.f ? 0.f : std::exp(-1.f / (seconds * sampleRate_));
    }

    float sampleRate_ = 48000.f;
    float env_ = 0.f;
    float attackCoef_ = 0.f, releaseCoef_ = 0.f;
};

} // namespace bass
