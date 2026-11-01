#include "../control/FootswitchGesture.h"
#include "../control/KnobPickup.h"
#include "../control/PageLayout.h"
#include "../control/PotSwitch.h"
#include "../dsp/EnvelopeFilter.h"
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
        CHECK(fs.Update(true, 0) == E::None);
        CHECK(fs.Update(true, 200) == E::None);
        CHECK(fs.Update(false, 250) == E::Tap);

        CHECK(fs.Update(true, 1000) == E::None);
        CHECK(fs.Update(true, 1499) == E::None);
        CHECK(fs.Update(true, 1500) == E::Hold); // fires while still held
        CHECK(fs.Update(true, 2500) == E::None); // only once
        CHECK(fs.Update(false, 2600) == E::None); // no tap after a hold
    });

    Test("footswitch hold still works across the millisecond counter wrapping", [&] {
        using E = FootswitchGesture::Event;
        FootswitchGesture fs;
        const uint32_t nearWrap = 0xFFFFFF00u; // 256 ms before the 32-bit counter wraps
        CHECK(fs.Update(true, nearWrap) == E::None);
        CHECK(fs.Update(true, nearWrap + 499u) == E::None); // wrapped around to 243
        CHECK(fs.Update(true, nearWrap + 500u) == E::Hold);
    });

    Test("pot switch only flips when the pot crosses the middle", [&] {
        PotSwitch s;
        s.Arm(0.8f); // armed on the "on" side: nothing happens yet
        bool out = false;
        CHECK(!s.Update(0.6f, out));
        CHECK(!s.Update(0.47f, out)); // inside the hysteresis band
        CHECK(s.Update(0.3f, out));
        CHECK(!out);
        CHECK(!s.Update(0.53f, out));
        CHECK(s.Update(0.7f, out));
        CHECK(out);
    });

    Test("page layout fits the env filter on one page with four toggles", [&] {
        EnvelopeFilter fx;
        PageLayout layout;
        layout.Build(fx, 6, 4);
        CHECK(layout.numPages == 1);
        CHECK(layout.pages[0].pot[0] == EnvelopeFilter::kSens);
        CHECK(layout.pages[0].pot[5] == EnvelopeFilter::kLevel);
        CHECK(layout.pages[0].toggle[3] == EnvelopeFilter::kXover);
    });

    Test("page layout moves extra switches to page 2 with two toggles", [&] {
        EnvelopeFilter fx;
        PageLayout layout;
        layout.Build(fx, 6, 2);
        CHECK(layout.numPages == 2);
        CHECK(layout.pages[0].toggle[0] == EnvelopeFilter::kMode);
        CHECK(layout.pages[0].toggle[1] == EnvelopeFilter::kSweep);
        CHECK(layout.pages[0].toggle[2] == -1);
        CHECK(layout.pages[1].pot[0] == EnvelopeFilter::kLows);
        CHECK(layout.pages[1].pot[1] == EnvelopeFilter::kXover);
        CHECK(layout.pages[1].pot[2] == -1);
        CHECK(layout.pages[1].toggle[0] == -1);
    });
}
