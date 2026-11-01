#pragma once

// A copy of everything the screen and LEDs show, taken from PedalUi in one
// go (with interrupts blocked on the pedal, under a mutex in the simulator),
// so drawing never sees a half-switched page. Values only: no indices or
// pointers into live state (names and labels are static string literals).

#include "../dsp/Effect.h"
#include "../dsp/Chain.h"

namespace bass {

struct PageView {
    static constexpr int kPots = 6;
    static constexpr int kToggles = 4;
    static constexpr int kMaxEffects = Chain::kMaxEffects;

    enum class Screen { Preset, Page, Tuner };

    struct Control {
        bool used = false;
        ParamInfo info{"", 0.f};
        float value = 0.f;    // the parameter, 0..1
        float position = 0.f; // pots: where the pot physically is, 0..1
        bool caught = true;   // pots: false = pickup pending, turn to `value`
    };
    struct EffectSummary {
        const char* name = "";
        bool enabled = false;
    };

    Screen screen = Screen::Preset;
    bool editMode = false;

    // Preset
    int preset = 0;            // 0-based
    bool modified = false;     // unsaved changes since the preset was loaded/saved
    uint32_t presetAgeMs = 0;  // since the preset was selected (stock-panel LED blink count)
    EffectSummary effects[kMaxEffects];
    int numEffects = 0;

    // Current page
    const char* name = "";
    bool enabled = false;
    int page = 0, pages = 1;
    float activity = -1.f; // Effect::Activity() of this effect: 0..1, < 0 = none
    Control pots[kPots];
    Control toggles[kToggles];

    // Strongest activity of any enabled effect (meter in preset view).
    float chainActivity = -1.f;

    // Short-lived notice, e.g. "Saved P3". Empty = none.
    char message[20] = "";
};

} // namespace bass
