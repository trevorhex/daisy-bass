#include "../control/FootswitchGesture.h"
#include "../control/KnobPickup.h"
#include "Harness.h"

using namespace bass;
using namespace harness;

void RunControlTests() {
    Test("knob pickup ignores the pot until it reaches the stored value", [&] {
        KnobPickup k;
        k.Arm(/*stored=*/0.7f, /*pot=*/0.2f);
        float out = -1.f;
        CHECK(!k.Caught());
        CHECK(!k.Update(0.4f, out)); // still below 0.7: no change
        CHECK(!k.Update(0.6f, out));
        CHECK(k.Update(0.72f, out)); // crossed 0.7: now it tracks
        CHECK_NEAR(out, 0.72f, 1e-6f);
        CHECK(k.Update(0.3f, out));
        CHECK_NEAR(out, 0.3f, 1e-6f);
    });

    Test("knob pickup catches when crossing from above", [&] {
        KnobPickup k;
        k.Arm(0.3f, 0.9f);
        float out = 0.f;
        CHECK(!k.Update(0.5f, out));
        CHECK(k.Update(0.1f, out)); // jumped past 0.3 in one step: still caught
    });

    Test("knob pickup is live immediately when already in place", [&] {
        KnobPickup k;
        k.Arm(0.5f, 0.51f);
        float out = 0.f;
        CHECK(k.Caught());
        CHECK(k.Update(0.6f, out));
        CHECK(!k.Update(0.6005f, out)); // ADC jitter is ignored
    });

    Test("footswitch tells taps from holds", [&] {
        using E = FootswitchGesture::Event;
        FootswitchGesture fs;
        CHECK(fs.Update(true, 0.f) == E::None);
        CHECK(fs.Update(true, 200.f) == E::None);
        CHECK(fs.Update(false, 250.f) == E::Tap);

        CHECK(fs.Update(true, 1000.f) == E::None);
        CHECK(fs.Update(true, 1499.f) == E::None);
        CHECK(fs.Update(true, 1500.f) == E::Hold); // fires while still held
        CHECK(fs.Update(true, 2500.f) == E::None); // only once
        CHECK(fs.Update(false, 2600.f) == E::None); // no tap after a hold
    });
}
