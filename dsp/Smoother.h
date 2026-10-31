#pragma once

#include <cmath>

namespace bass {

/**
 * One-pole glide toward a target, so knob moves (which arrive in steps
 * about once per millisecond) don't cause zipper noise.
 */
class Smoother {
public:
    void Init(float sampleRate, float seconds = 0.02f, float initial = 0.f) {
        coef_ = std::exp(-1.f / (seconds * sampleRate));
        value_ = target_ = initial;
    }

    void SetTarget(float target) { target_ = target; }
    void Jump(float value) { value_ = target_ = value; }

    float Process() {
        value_ = target_ + coef_ * (value_ - target_);
        return value_;
    }

    float Value() const { return value_; }

private:
    float coef_ = 0.f, value_ = 0.f, target_ = 0.f;
};

} // namespace bass
