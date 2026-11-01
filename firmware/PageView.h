#pragma once

// A copy of everything the main loop shows (OLED, LEDs) for the current
// page. main.cpp fills it in one go with interrupts blocked, so the main
// loop never sees a half-switched page: e.g. one effect's name with another
// effect's parameter indices. It holds values only, no indices or pointers
// into live state (Name() and ParamInfo strings are static literals).

#include "dsp/Effect.h"

struct PageView {
    static constexpr int kPots = 6;
    static constexpr int kToggles = 4;

    struct Control {
        bool used = false;
        bass::ParamInfo info{"", 0.f};
        float value = 0.f;    // the parameter, 0..1
        float position = 0.f; // pots: where the pot physically is, 0..1
        bool caught = true;   // pots: false = pickup pending, turn to `value`
    };

    const char* name = "";
    bool enabled = false;
    int page = 0, pages = 1;
    float activity = -1.f; // Effect::Activity(): 0..1, < 0 = none
    Control pots[kPots];
    Control toggles[kToggles];
};
