#pragma once

// PedalPCB Terrarium control map.
//
// The Terrarium is wired like Electro-Smith's Daisy Petal, so libDaisy's
// DaisyPetal class drives it; this file just names the controls the way
// they're labelled on the Terrarium PCB / 125B drill template:
//
//     POT 1   POT 2   POT 3
//     POT 4   POT 5   POT 6
//   SW 1  SW 2  SW 3  SW 4       (SPDT ON/ON toggles)
//     LED 1          LED 2
//     FS 1           FS 2        (momentary SPST footswitches)
//
// Switch indices come from PedalPCB's published terrarium.h. Builders have
// reported the pot order not always matching the silkscreen, so flash with
// KNOB_CHECK defined (see main.cpp) once and confirm before trusting POTS.

#include "daisy_petal.h"

namespace terrarium {

// Index into DaisyPetal::switches[].
enum Switch {
    FOOTSWITCH_1 = 4,
    FOOTSWITCH_2 = 5,
    SWITCH_1 = 2,
    SWITCH_2 = 1,
    SWITCH_3 = 0,
    SWITCH_4 = 6,
};

// POTS[n] is the DaisyPetal knob behind Terrarium "Pot n+1".
constexpr daisy::DaisyPetal::Knob POTS[6] = {
    daisy::DaisyPetal::KNOB_1, daisy::DaisyPetal::KNOB_2, daisy::DaisyPetal::KNOB_3,
    daisy::DaisyPetal::KNOB_4, daisy::DaisyPetal::KNOB_5, daisy::DaisyPetal::KNOB_6,
};

// LEDs sit on Seed pins D22 / D23.
inline daisy::Pin Led1Pin() { return daisy::seed::D22; }
inline daisy::Pin Led2Pin() { return daisy::seed::D23; }

} // namespace terrarium
