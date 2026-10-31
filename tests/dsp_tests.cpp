// Plain C++ tests for the DSP. No framework: each CHECK prints on failure,
// and the exit code is the number of failures.

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

#include "../desktop/TestSignal.h"
#include "../desktop/Wav.h"
#include "../dsp/Crossover.h"
#include "../dsp/EnvelopeFilter.h"
#include "../dsp/EnvelopeFollower.h"
#include "../dsp/Svf.h"

using namespace bass;

namespace {

int failures = 0;
const char* current = "";

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::printf("  FAIL [%s] %s:%d  %s\n", current, __FILE__, __LINE__, #cond); \
            failures++;                                                                \
        }                                                                              \
    } while (0)

constexpr float kRate = 48000.f;
constexpr float kPi = 3.14159265f;

/** RMS of a process run on a sine, skipping the first half to let things settle. */
float SineRms(float freq, const std::function<float(float)>& process, float seconds = 0.5f) {
    const size_t n = size_t(seconds * kRate);
    double sum = 0;
    for (size_t i = 0; i < n; i++) {
        const float y = process(std::sin(2.f * kPi * freq * i / kRate));
        if (i >= n / 2) sum += double(y * y);
    }
    return float(std::sqrt(sum / (n - n / 2)));
}

float Db(float ratio) { return 20.f * std::log10(ratio); }

void Test(const char* name, const std::function<void()>& body) {
    current = name;
    const int before = failures;
    body();
    std::printf("%s %s\n", failures == before ? "  ok  " : "  FAIL", name);
}

} // namespace

int main() {
    const float sineRms = 1.f / std::sqrt(2.f);

    Test("svf low-pass passes lows and cuts highs", [&] {
        Svf f;
        f.Init(kRate);
        f.SetParams(500.f, 0.707f);
        CHECK(std::fabs(Db(SineRms(50.f, [&](float x) { return f.Process(x).low; }) / sineRms)) < 0.5f);
        f.Reset();
        // 2nd order: about -24 dB two octaves up... at 3 octaves expect well under -30 dB.
        CHECK(Db(SineRms(4000.f, [&](float x) { return f.Process(x).low; }) / sineRms) < -30.f);
    });

    Test("svf stays stable when the cutoff jumps every sample", [&] {
        Svf f;
        f.Init(kRate);
        float peak = 0.f;
        for (int i = 0; i < 48000; i++) {
            f.SetParams((i % 2) ? 40.f : 15000.f, 12.f);
            peak = std::fmax(peak, std::fabs(f.Process(std::sin(i * 0.05f)).low));
        }
        CHECK(std::isfinite(peak));
        CHECK(peak < 50.f);
    });

    Test("crossover splits bands and sums back flat", [&] {
        for (float freq : {40.f, 150.f, 1000.f}) {
            Crossover x;
            x.Init(kRate, 150.f);
            const float summed = SineRms(freq, [&](float s) { auto b = x.Process(s); return b.low + b.high; });
            CHECK(std::fabs(Db(summed / sineRms)) < 0.3f); // Linkwitz-Riley: flat sum
        }
        Crossover x;
        x.Init(kRate, 150.f);
        CHECK(Db(SineRms(1500.f, [&](float s) { return x.Process(s).low; }) / sineRms) < -40.f);
    });

    Test("envelope follower attacks fast and releases slowly", [&] {
        EnvelopeFollower e;
        e.Init(kRate);
        e.SetTimes(0.005f, 0.2f);
        for (int i = 0; i < int(0.02f * kRate); i++) e.Process(1.f); // 20 ms = 4 attack constants
        CHECK(e.Value() > 0.95f);
        for (int i = 0; i < int(0.02f * kRate); i++) e.Process(0.f); // 20 ms of 200 ms release
        CHECK(e.Value() > 0.85f);
    });

    Test("bypass and dry mix pass the input through untouched", [&] {
        for (int which = 0; which < 2; which++) {
            EnvelopeFilter fx;
            EnvelopeFilter::Params p;
            (which ? p.mix : p.level) = which ? 0.f : 0.5f;
            p.bypass = which == 0;
            fx.Init(kRate);
            fx.SetParams(p, true);
            float maxDiff = 0.f;
            for (int i = 0; i < 4800; i++) {
                const float x = 0.5f * std::sin(i * 0.01f);
                maxDiff = std::fmax(maxDiff, std::fabs(fx.Process(x) - x));
            }
            CHECK(maxDiff < 1e-4f);
        }
    });

    Test("clean-lows keeps the fundamental while the filter works the highs", [&] {
        EnvelopeFilter::Params p;
        p.range = 0.f;        // filter parked low...
        p.sensitivity = 0.f;  // ...and barely moving
        p.resonance = 0.f;
        p.cleanLows = true;

        EnvelopeFilter fx;
        fx.Init(kRate);
        fx.SetParams(p, true);
        const float lowRms = SineRms(41.2f, [&](float x) { return fx.Process(0.3f * x); }); // low E
        CHECK(std::fabs(Db(lowRms / (0.3f * sineRms))) < 1.5f);
    });

    Test("playing harder opens the filter (more highs)", [&] {
        auto highsFor = [](float amplitude) {
            EnvelopeFilter::Params p;
            p.cleanLows = false;
            p.resonance = 0.f;
            EnvelopeFilter fx;
            fx.Init(kRate);
            fx.SetParams(p, true);
            // 2 kHz content riding on the note: how much survives tells us where the cutoff is.
            return SineRms(2000.f, [&](float x) { return fx.Process(amplitude * x); }) / amplitude;
        };
        CHECK(highsFor(0.6f) > 4.f * highsFor(0.02f));
    });

    Test("output stays finite and sane on the demo riff at extreme settings", [&] {
        const auto riff = testsignal::BassRiff(kRate, 1);
        for (float res : {0.f, 1.f}) {
            for (bool bp : {false, true}) {
                EnvelopeFilter::Params p;
                p.sensitivity = 1.f;
                p.resonance = res;
                p.mode = bp ? EnvelopeFilter::Mode::BandPass : EnvelopeFilter::Mode::LowPass;
                EnvelopeFilter fx;
                fx.Init(kRate);
                fx.SetParams(p, true);
                float peak = 0.f;
                for (float s : riff) peak = std::fmax(peak, std::fabs(fx.Process(s)));
                CHECK(std::isfinite(peak));
                CHECK(peak < 2.5f);
            }
        }
    });

    Test("wav round-trips through 24-bit", [&] {
        wav::Audio a{44100.f, {0.f, 0.5f, -0.5f, 0.999f, -1.f}};
        const char* path = "build/roundtrip-test.wav";
        wav::Write(path, a);
        const wav::Audio b = wav::Read(path);
        CHECK(b.sampleRate == 44100.f);
        CHECK(b.samples.size() == a.samples.size());
        for (size_t i = 0; i < a.samples.size() && i < b.samples.size(); i++)
            CHECK(std::fabs(a.samples[i] - b.samples[i]) < 1e-6f);
        std::remove(path);
    });

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures;
}
