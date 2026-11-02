#pragma once

#include <cmath>

#include "Crossover.h"
#include "Effect.h"
#include "Smoother.h"
#include "Svf.h"

namespace bass {

/**
 * Overdrive / fuzz voiced for bass.
 *
 *   Grit  Asymmetric soft clipping: warm, amp-like, even harmonics.
 *   Fuzz  12 dB more gain into a harder, flatter clipper: square-ish and buzzy.
 *
 * With Lows = Clean, a crossover splits off everything below Xover and keeps
 * it dry, so only the upper band is distorted: the low end stays tight however
 * hard you push it, and Blend mixes only that upper band. With Lows = Dirty
 * the whole signal is distorted and Blend mixes against the full dry signal.
 *
 * Tone (a low-pass) and Mid (a bell at 800 Hz) shape only the distorted
 * signal. The clipping runs at twice the sample rate to keep aliasing down,
 * and its output level is compensated so Drive changes grit, not volume.
 */
class Drive : public Effect {
public:
    enum ParamIndex { kDrive, kTone, kMid, kXover, kBlend, kLevel, kChar, kLows, kNumParams };

    const char* Name() const override { return "Drive"; }

    void Init(float sampleRate) override {
        sampleRate_ = sampleRate;
        const float osRate = 2.f * sampleRate;
        // 4th-order Butterworth image / anti-alias filters at the 2x rate.
        const float aaHz = 0.42f * sampleRate;
        const float butterworth4Q[2] = {0.54119610f, 1.30656296f};
        for (int i = 0; i < 2; i++) {
            up_[i].Init(osRate);
            down_[i].Init(osRate);
            up_[i].SetParams(aaHz, butterworth4Q[i]);
            down_[i].SetParams(aaHz, butterworth4Q[i]);
        }
        tone_.Init(osRate);
        mid_.Init(osRate);
        crossover_.Init(sampleRate);
        dcCoef_ = std::exp(-2.f * kPi * 10.f / osRate);
        dcIn_ = dcOut_ = 0.f;
        peakDecay_ = std::exp(-1.f / (0.15f * osRate));
        peak_ = 0.f;

        drive_.Init(sampleRate, 0.03f);
        toneHz_.Init(sampleRate, 0.03f);
        midGain_.Init(sampleRate, 0.03f);
        xoverHz_.Init(sampleRate, 0.05f);
        blend_.Init(sampleRate, 0.02f);
        level_.Init(sampleRate, 0.02f);
        lows_.Init(sampleRate, 0.01f);
        fuzz_.Init(sampleRate, 0.01f);
        Update(/*jump=*/true);
        UpdateFilters();
        countdown_ = kFilterUpdateSamples;
    }

    // --- Effect interface ---------------------------------------------------

    int NumParams() const override { return kNumParams; }

    ParamInfo Param(int i) const override {
        static const ParamInfo info[kNumParams] = {
            {"Drive", 0.4f}, // +0 .. +40 dB into the clipper
            {"Tone", 0.6f},  // low-pass 500 Hz .. 10 kHz
            {"Mid", 0.5f},   // 800 Hz bell, -12 .. +12 dB, noon is flat
            {"Xover", 0.5f}, // 50 .. 400 Hz (Lows = Clean)
            {"Blend", 1.0f}, // clean .. driven
            {"Level", 0.5f}, // -18 .. +18 dB, 0.5 = unity
            {"Char", 0.f, true, "Grit", "Fuzz"},
            {"Lows", 1.f, true, "Dirty", "Clean"},
        };
        if (i < 0 || i >= kNumParams) return {"?", 0.f};
        return info[i];
    }

    void SetParam(int i, float v) override {
        if (i < 0 || i >= kNumParams) return;
        v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
        if (Param(i).isSwitch) v = v >= 0.5f ? 1.f : 0.f;
        values_[i] = v;
        Update(/*jump=*/false);
    }

    float GetParam(int i) const override { return (i < 0 || i >= kNumParams) ? 0.f : values_[i]; }

    void Process(float* buffer, size_t size) override {
        for (size_t i = 0; i < size; i++) buffer[i] = Process(buffer[i]);
    }

    /** How deep into clipping: 0 at the onset, 1 at 30 dB past it. */
    float Activity() const override {
        if (peak_ <= 1.f) return 0.f;
        const float db = 20.f * std::log10(peak_);
        return db >= 30.f ? 1.f : db / 30.f;
    }

    // --- Per-sample processing ---------------------------------------------

    float Process(float x) {
        if (--countdown_ <= 0) {
            UpdateFilters();
            countdown_ = kFilterUpdateSamples;
        }
        const float lows = lows_.Process(); // 1 = clean lows, 0 = dirty; glides when switched
        const float drive = drive_.Process();
        const float fuzz = fuzz_.Process();
        const float blend = blend_.Process();
        const float level = level_.Process();
        midGain_.Process();
        toneHz_.Process();
        xoverHz_.Process();

        // Always runs so it's settled when Lows is switched. The distorted band
        // glides between the upper band (clean lows) and the full signal.
        const Crossover::Bands bands = crossover_.Process(x);
        const float in = lows * bands.high + (1.f - lows) * x;

        // Makeup: a signal at the reference level comes out at that peak level,
        // less up to 3 dB as it squares off (a square is 3 dB louder than a sine
        // with the same peak).
        const float driven = drive * kRef * (1.f + (kFuzzGain - 1.f) * fuzz);
        const float clipped = Mix(Grit(drive * kRef), Fuzz(kFuzzGain * drive * kRef), fuzz);
        const float squareness = 1.f - clipped / driven; // 0 = linear .. 1 = square
        const float makeup = kRef / clipped * (1.f - 0.3f * squareness);

        // 2x oversampling: zero-stuff, filter, clip, blend, filter, keep every other sample.
        float out = 0.f;
        for (int phase = 0; phase < 2; phase++) {
            float u = phase == 0 ? 2.f * in : 0.f;
            u = up_[1].Process(up_[0].Process(u).low).low;

            const float v = drive * u;
            const float av = std::fabs(v) * (1.f + (kFuzzGain - 1.f) * fuzz);
            peak_ = av > peak_ ? av : peak_ * peakDecay_;

            float wet = Mix(Grit(v), Fuzz(kFuzzGain * v), fuzz) * makeup;
            // DC blocker: the asymmetric curves shift the average.
            const float dc = wet - dcIn_ + dcCoef_ * dcOut_;
            dcIn_ = wet;
            dcOut_ = dc;
            wet = tone_.Process(dc).low;
            const Svf::Out m = mid_.Process(wet);
            wet += (midGain_.Value() - 1.f) * kMidK * m.band; // bell: gain at the center frequency

            const float mixed = u + blend * (wet - u);
            const float y = down_[1].Process(down_[0].Process(mixed).low).low;
            if (phase == 0) out = y;
        }

        return (lows * bands.low + out) * level;
    }

private:
    static constexpr float kPi = 3.14159265358979f;
    static constexpr float kRef = 0.3f;      // typical bass level, for the makeup gain
    static constexpr float kFuzzGain = 4.f;  // +12 dB in Fuzz
    static constexpr float kGritBias = 0.25f;
    static constexpr float kFuzzBias = 0.1f;
    static constexpr float kMidHz = 800.f;
    static constexpr float kMidQ = 0.7f;
    static constexpr float kMidK = 1.f / kMidQ;
    static constexpr int kFilterUpdateSamples = 16;

    /** Cheap tanh: exact enough for audio, 1 at |x| ≥ 3 with zero slope there. */
    static float SoftClip(float x) {
        if (x >= 3.f) return 1.f;
        if (x <= -3.f) return -1.f;
        const float x2 = x * x;
        return x * (27.f + x2) / (27.f + 9.f * x2);
    }

    /** Hard-kneed clipper: linear-ish, then flat at ±1. */
    static float HardClip(float x) {
        x = x < -1.f ? -1.f : (x > 1.f ? 1.f : x);
        return 1.5f * x - 0.5f * x * x * x;
    }

    static float Grit(float x) { return SoftClip(x + kGritBias) - SoftClip(kGritBias); }
    static float Fuzz(float x) { return HardClip(x + kFuzzBias) - HardClip(kFuzzBias); }
    static float Mix(float a, float b, float t) { return t <= 0.f ? a : (t >= 1.f ? b : a + t * (b - a)); }

    /** Coefficient updates (a few tan() calls), done every kFilterUpdateSamples. */
    void UpdateFilters() {
        crossover_.SetFrequency(xoverHz_.Value());
        tone_.SetParams(toneHz_.Value(), 0.70710678f);
        mid_.SetParams(kMidHz, kMidQ);
    }

    void Update(bool jump) {
        const float* v = values_;
        Target(drive_, std::pow(10.f, v[kDrive] * 40.f / 20.f), jump);           // 1 .. 100
        Target(toneHz_, 500.f * std::pow(20.f, v[kTone]), jump);                 // 500 Hz .. 10 kHz
        Target(midGain_, std::pow(10.f, (v[kMid] - 0.5f) * 24.f / 20.f), jump);  // ±12 dB
        Target(xoverHz_, 50.f * std::pow(8.f, v[kXover]), jump);                 // 50 .. 400 Hz
        Target(blend_, v[kBlend], jump);
        Target(level_, std::pow(10.f, (v[kLevel] - 0.5f) * 36.f / 20.f), jump);  // ±18 dB
        Target(lows_, v[kLows], jump);
        Target(fuzz_, v[kChar], jump);
    }

    static void Target(Smoother& s, float value, bool jump) { jump ? s.Jump(value) : s.SetTarget(value); }

    float sampleRate_ = 48000.f;
    float values_[kNumParams] = {0.4f, 0.6f, 0.5f, 0.5f, 1.0f, 0.5f, 0.f, 1.f};

    Svf up_[2], down_[2], tone_, mid_;
    Crossover crossover_;
    float dcCoef_ = 0.f, dcIn_ = 0.f, dcOut_ = 0.f;
    float peak_ = 0.f, peakDecay_ = 0.f;
    int countdown_ = 0;
    Smoother drive_, toneHz_, midGain_, xoverHz_, blend_, level_, lows_, fuzz_;
};

} // namespace bass
