#pragma once

#include <cstdint>

namespace bass {

/**
 * Turns a momentary footswitch into tap / hold events.
 *
 *   Tap  — released before kHoldMs. Fires on release.
 *   Hold — still down at kHoldMs. Fires once, while still held, so the
 *          player gets feedback without lifting their foot.
 *
 * Hardware-independent: feed it "is it pressed?" and a millisecond clock.
 * Integer milliseconds, so it stays exact for days of uptime and survives
 * the 32-bit counter wrapping (unsigned subtraction).
 */
class FootswitchGesture {
public:
    enum class Event { None, Tap, Hold };

    static constexpr uint32_t kHoldMs = 500;

    Event Update(bool pressed, uint32_t nowMs) {
        Event e = Event::None;
        if (pressed && !wasPressed_) {
            downAt_ = nowMs;
            holdFired_ = false;
        } else if (pressed && !holdFired_ && nowMs - downAt_ >= kHoldMs) {
            holdFired_ = true;
            e = Event::Hold;
        } else if (!pressed && wasPressed_ && !holdFired_) {
            e = Event::Tap;
        }
        wasPressed_ = pressed;
        return e;
    }

    /** Forget the current press: no tap or hold until the switch is pressed again. */
    void Suppress() { holdFired_ = true; }

private:
    bool wasPressed_ = false;
    bool holdFired_ = false;
    uint32_t downAt_ = 0;
};

} // namespace bass
