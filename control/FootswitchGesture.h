#pragma once

namespace bass {

/**
 * Turns a momentary footswitch into tap / hold events.
 *
 *   Tap  — released before kHoldMs. Fires on release.
 *   Hold — still down at kHoldMs. Fires once, while still held, so the
 *          player gets feedback without lifting their foot.
 *
 * Hardware-independent: feed it "is it pressed?" and a millisecond clock.
 */
class FootswitchGesture {
public:
    enum class Event { None, Tap, Hold };

    static constexpr float kHoldMs = 500.f;

    Event Update(bool pressed, float nowMs) {
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

private:
    bool wasPressed_ = false;
    bool holdFired_ = false;
    float downAt_ = 0.f;
};

} // namespace bass
