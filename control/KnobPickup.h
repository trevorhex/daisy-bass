#pragma once

#include <cmath>

namespace bass {

/**
 * "Pickup" (a.k.a. soft takeover) for a pot that controls different
 * parameters on different pages.
 *
 * After a page change the pot's physical position rarely matches the stored
 * value. Instead of jumping, the pot does nothing until it crosses (or gets
 * within a hair of) the stored value; from then on it tracks normally.
 */
class KnobPickup {
public:
    /** Call when the pot starts controlling a parameter with this value. */
    void Arm(float storedValue, float potPosition) {
        stored_ = storedValue;
        last_ = potPosition;
        caught_ = std::fabs(potPosition - storedValue) < kCatchWindow;
    }

    /**
     * Feed the current pot position. Returns true and writes `out` when the
     * parameter should change.
     */
    bool Update(float pot, float& out) {
        if (!caught_) {
            const bool crossed = (last_ - stored_) * (pot - stored_) <= 0.f;
            if (crossed || std::fabs(pot - stored_) < kCatchWindow) caught_ = true;
        }
        last_ = pot;
        if (!caught_ || std::fabs(pot - stored_) < kJitter) return false;
        stored_ = out = pot;
        return true;
    }

    bool Caught() const { return caught_; }

    /** Where the stored value sits, for drawing a "turn to here" marker. */
    float Stored() const { return stored_; }

private:
    static constexpr float kCatchWindow = 0.02f;
    static constexpr float kJitter = 0.002f; // ignore ADC noise

    float stored_ = 0.f;
    float last_ = 0.f;
    bool caught_ = true;
};

} // namespace bass
