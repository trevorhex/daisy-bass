#pragma once

namespace bass {

/**
 * A pot standing in for a toggle: left half = off, right half = on.
 *
 * Like a toggle, it only acts when moved. Arming it on a page change just
 * records which side the pot is on; the parameter changes when the pot
 * crosses the midpoint (with a little hysteresis so it can't chatter).
 */
class PotSwitch {
public:
    void Arm(float pot) { high_ = pot >= 0.5f; }

    /** Returns true and writes `out` when the pot crosses to the other side. */
    bool Update(float pot, bool& out) {
        const bool high = high_ ? pot > 0.5f - kHysteresis : pot >= 0.5f + kHysteresis;
        if (high == high_) return false;
        high_ = out = high;
        return true;
    }

private:
    static constexpr float kHysteresis = 0.05f;
    bool high_ = false;
};

} // namespace bass
