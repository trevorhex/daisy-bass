#pragma once

#include <cstdint>

#include "FootswitchGesture.h"

namespace bass {

/**
 * Both footswitches together: per-switch taps and holds, plus "both held".
 *
 * Pressing the second switch while the first is still down (before its hold
 * fires) starts a chord: neither switch reports a tap or hold for that
 * press, and BothHold fires once when both have been down for kBothHoldMs.
 */
class FootswitchPair {
public:
    enum class Event { None, Tap1, Tap2, Hold1, Hold2, BothHold };

    static constexpr uint32_t kBothHoldMs = 1000;

    Event Update(bool fs1, bool fs2, uint32_t nowMs) {
        const FootswitchGesture::Event e1 = a_.Update(fs1, nowMs);
        const FootswitchGesture::Event e2 = b_.Update(fs2, nowMs);

        if (fs1 && fs2 && !chord_) {
            chord_ = true;
            chordFired_ = false;
            chordAt_ = nowMs;
        }
        if (chord_) {
            a_.Suppress();
            b_.Suppress();
            Event e = Event::None;
            if (fs1 && fs2 && !chordFired_ && nowMs - chordAt_ >= kBothHoldMs) {
                chordFired_ = true;
                e = Event::BothHold;
            }
            if (!fs1 && !fs2) chord_ = false;
            return e;
        }

        using G = FootswitchGesture::Event;
        if (e1 == G::Tap) return Event::Tap1;
        if (e1 == G::Hold) return Event::Hold1;
        if (e2 == G::Tap) return Event::Tap2;
        if (e2 == G::Hold) return Event::Hold2;
        return Event::None;
    }

private:
    FootswitchGesture a_, b_;
    bool chord_ = false, chordFired_ = false;
    uint32_t chordAt_ = 0;
};

} // namespace bass
