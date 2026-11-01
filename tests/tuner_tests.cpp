#include <cmath>
#include <cstring>
#include <initializer_list>

#include "../dsp/Tuner.h"
#include "Harness.h"

using namespace bass;
using namespace harness;

namespace {

float NoteHz(int midi, float cents = 0.f, float a4 = 440.f) {
    return a4 * std::pow(2.f, (float(midi - 69) + cents / 100.f) / 12.f);
}

/**
 * Feed a plucked, harmonic-rich tone and analyze after `seconds`.
 * `harmonics` gives each partial's amplitude, starting at the fundamental.
 */
Tuner::Reading Listen(float freq, std::initializer_list<float> harmonics, float seconds = 0.4f,
                      float amplitude = 0.3f, float a4 = 440.f, float rate = kRate) {
    Tuner tuner;
    tuner.Init(rate, a4);
    const size_t n = size_t(seconds * rate);
    for (size_t i = 0; i < n; i++) {
        const float t = float(i) / rate;
        float v = 0.f;
        int h = 1;
        for (float amp : harmonics) v += amp * std::sin(2.f * kPi * freq * float(h++) * t);
        tuner.Push(amplitude * v * std::exp(-t * 1.5f)); // a slowly decaying pluck
    }
    return tuner.Analyze();
}

} // namespace

void RunTunerTests() {
    Test("tuner names open strings on 4- and 5-string bass", [&] {
        struct { int midi; const char* name; int octave; } strings[] = {
            {23, "B", 0}, {28, "E", 1}, {33, "A", 1}, {38, "D", 2}, {43, "G", 2},
        };
        for (const auto& s : strings) {
            const Tuner::Reading r = Listen(NoteHz(s.midi), {1.f, 0.5f, 0.33f, 0.25f});
            CHECK(r.valid);
            CHECK(r.midiNote == s.midi);
            CHECK(std::strcmp(r.NoteName(), s.name) == 0);
            CHECK(r.Octave() == s.octave);
            CHECK_NEAR(r.cents, 0.f, 1.f);
        }
    });

    Test("tuner reads detuning to within a cent", [&] {
        for (float cents : {-23.f, -4.f, 7.f, 31.f}) {
            const Tuner::Reading r = Listen(NoteHz(28, cents), {1.f, 0.5f, 0.33f});
            CHECK(r.valid);
            CHECK(r.midiNote == 28);
            CHECK_NEAR(r.cents, cents, 1.f);
        }
    });

    Test("tuner handles high fretted notes", [&] {
        const Tuner::Reading r = Listen(NoteHz(67, 5.f), {1.f, 0.4f}); // G4, 24th fret G string
        CHECK(r.valid);
        CHECK(r.midiNote == 67);
        CHECK_NEAR(r.cents, 5.f, 3.f);
    });

    Test("tuner avoids octave errors when the 2nd harmonic is louder", [&] {
        const Tuner::Reading r = Listen(NoteHz(28), {0.5f, 1.f, 0.4f, 0.3f});
        CHECK(r.valid);
        CHECK(r.midiNote == 28); // not 40 (an octave up)
    });

    Test("tuner respects a custom A4 reference", [&] {
        const Tuner::Reading r = Listen(NoteHz(33, 0.f, 442.f), {1.f, 0.5f}, 0.4f, 0.3f, 442.f);
        CHECK(r.valid);
        CHECK(r.midiNote == 33);
        CHECK_NEAR(r.cents, 0.f, 1.f);
    });

    Test("tuner reaches low B at 44.1 and 96 kHz too", [&] {
        for (float rate : {44100.f, 96000.f}) {
            const Tuner::Reading r = Listen(NoteHz(23, 6.f), {1.f, 0.5f, 0.33f}, 0.4f, 0.3f, 440.f, rate);
            CHECK(r.valid);
            CHECK(r.midiNote == 23);
            CHECK_NEAR(r.cents, 6.f, 1.f);
        }
    });

    Test("tuner stays quiet on silence and noise-floor hiss", [&] {
        Tuner tuner;
        tuner.Init(kRate);
        CHECK(!tuner.Analyze().valid); // nothing pushed yet
        unsigned seed = 1;
        for (int i = 0; i < 24000; i++) {
            seed = seed * 1664525u + 1013904223u;
            tuner.Push(0.001f * (float(seed >> 8) / 8388608.f - 1.f)); // about -66 dBFS
        }
        CHECK(!tuner.Analyze().valid);
    });
}
