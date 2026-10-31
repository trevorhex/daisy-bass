#include <cmath>
#include <cstdio>
#include <vector>

#include "../desktop/TestSignal.h"
#include "../desktop/Wav.h"
#include "../dsp/Chain.h"
#include "../dsp/Crossover.h"
#include "../dsp/EnvelopeFilter.h"
#include "../dsp/EnvelopeFollower.h"
#include "../dsp/Svf.h"
#include "Harness.h"

using namespace bass;
using namespace harness;

void RunDspTests() {
    const float sineRms = 1.f / std::sqrt(2.f);

    Test("svf low-pass passes lows and cuts highs", [&] {
        Svf f;
        f.Init(kRate);
        f.SetParams(500.f, 0.707f);
        CHECK(std::fabs(Db(SineRms(50.f, [&](float x) { return f.Process(x).low; }) / sineRms)) < 0.5f);
        f.Reset();
        // 2nd order, 3 octaves above cutoff: expect well under -30 dB.
        CHECK(Db(SineRms(4000.f, [&](float x) { return f.Process(x).low; }) / sineRms) < -30.f);
    });

    Test("svf stays stable when the cutoff jumps every sample", [&] {
        Svf f;
        f.Init(kRate);
        float peak = 0.f;
        for (int i = 0; i < 48000; i++) {
            f.SetParams((i % 2) ? 40.f : 15000.f, 12.f);
            peak = std::fmax(peak, std::fabs(f.Process(std::sin(float(i) * 0.05f)).low));
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

    Test("dry mix at unity level passes the input through", [&] {
        EnvelopeFilter fx;
        EnvelopeFilter::Params p;
        p.mix = 0.f;
        fx.Init(kRate);
        fx.SetParams(p, true);
        float maxDiff = 0.f;
        for (int i = 0; i < 4800; i++) {
            const float x = 0.5f * std::sin(float(i) * 0.01f);
            maxDiff = std::fmax(maxDiff, std::fabs(fx.Process(x) - x));
        }
        CHECK(maxDiff < 1e-4f);
    });

    Test("clean-lows keeps the fundamental while the filter works the highs", [&] {
        EnvelopeFilter::Params p;
        p.range = 0.f;       // filter parked low...
        p.sensitivity = 0.f; // ...and barely moving
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
            // How much 2 kHz survives tells us where the cutoff is.
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

    Test("effect interface round-trips every parameter", [&] {
        EnvelopeFilter fx;
        fx.Init(kRate);
        Effect& e = fx;
        CHECK(e.NumParams() == 10);
        for (int i = 0; i < e.NumParams(); i++) {
            const ParamInfo info = e.Param(i);
            const float v = info.isSwitch ? 1.f : 0.73f;
            e.SetParam(i, v);
            CHECK_NEAR(e.GetParam(i), v, 1e-6f);
        }
        CHECK(fx.GetParams().mode == EnvelopeFilter::Mode::BandPass);
        CHECK(fx.GetParams().crossoverHz == 250.f);
        e.ResetParams();
        for (int i = 0; i < e.NumParams(); i++) CHECK_NEAR(e.GetParam(i), e.Param(i).defaultValue, 1e-6f);
    });

    Test("chain bypass is transparent and switches without clicks", [&] {
        EnvelopeFilter fx;
        Chain chain;
        chain.Add(&fx, /*enabled=*/false);
        chain.Init(kRate);

        // Off: output == input exactly.
        float buf[48], in[48];
        float maxDiff = 0.f;
        for (int block = 0; block < 100; block++) {
            for (int i = 0; i < 48; i++) in[i] = buf[i] = 0.4f * std::sin(float(block * 48 + i) * 0.03f);
            chain.Process(buf, 48);
            for (int i = 0; i < 48; i++) maxDiff = std::fmax(maxDiff, std::fabs(buf[i] - in[i]));
        }
        CHECK(maxDiff < 1e-6f);

        // Switch on: no sample-to-sample jump bigger than the signal's own slope allows.
        chain.SetEnabled(0, true);
        float prev = buf[47], maxStep = 0.f;
        for (int block = 0; block < 20; block++) {
            for (int i = 0; i < 48; i++) buf[i] = 0.4f * std::sin(float((100 + block) * 48 + i) * 0.03f);
            chain.Process(buf, 48);
            for (int i = 0; i < 48; i++) {
                maxStep = std::fmax(maxStep, std::fabs(buf[i] - prev));
                prev = buf[i];
            }
        }
        CHECK(maxStep < 0.05f);
        CHECK(chain.IsEnabled(0));
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
}
