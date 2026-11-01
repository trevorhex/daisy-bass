#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "Svf.h"

namespace bass {

/**
 * Bass tuner using the YIN pitch detector (de Cheveigné & Kawahara, 2002).
 *
 * Split across two contexts:
 *   Push()    — audio callback. Low-passes, downsamples to ~12 kHz,
 *               writes into a ring buffer. A few operations per sample.
 *   Analyze() — main loop, 10–20× per second. Runs YIN over the newest
 *               ~125 ms of audio. Never call this from the audio callback.
 *
 * Range is 25–500 Hz: below a 5-string's low B (30.9 Hz) up to well past
 * the highest fretted fundamental on a bass.
 */
class Tuner {
public:
    struct Reading {
        bool valid = false;    // false: silence or no clear pitch
        float frequency = 0.f; // Hz
        int midiNote = 0;      // nearest note, A4 = 69
        float cents = 0.f;     // -50 .. +50 from that note
        float clarity = 0.f;   // 0..1, how periodic the signal is

        const char* NoteName() const {
            // Function-local so it needs no out-of-class definition (firmware is C++14).
            static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F",
                                                  "F#", "G", "G#", "A", "A#", "B"};
            return names[((midiNote % 12) + 12) % 12];
        }
        int Octave() const { return midiNote / 12 - 1; }
    };

    static constexpr float kAnalysisRate = 12000.f; // decimate to about this
    static constexpr float kMinHz = 25.f;
    static constexpr float kMaxHz = 500.f;
    static constexpr float kThreshold = 0.15f; // YIN's dip threshold
    static constexpr float kGateRms = 0.004f;  // about -48 dBFS

    void Init(float sampleRate, float referenceA4 = 440.f) {
        // 48 kHz → ÷4, 96 kHz → ÷8, ... so the lag range covers 25 Hz at any rate.
        decimation_ = int(sampleRate / kAnalysisRate + 0.5f);
        if (decimation_ < 1) decimation_ = 1;
        rate_ = sampleRate / float(decimation_);
        reference_ = referenceA4;
        aa1_.Init(sampleRate);
        aa2_.Init(sampleRate);
        aa1_.SetParams(1000.f, 0.70710678f); // 4th-order anti-alias before downsampling
        aa2_.SetParams(1000.f, 0.70710678f);
        minLag_ = int(rate_ / kMaxHz);
        maxLag_ = int(rate_ / kMinHz);
        if (maxLag_ > kMaxLag) maxLag_ = kMaxLag;
        write_ = 0;
        phase_ = 0;
        for (float& s : ring_) s = 0.f;
    }

    void SetReference(float a4) { reference_ = a4; }
    float Reference() const { return reference_; }

    /** Audio-callback side. Real-time safe. */
    void Push(float sample) {
        const float filtered = aa2_.Process(aa1_.Process(sample).low).low;
        if (++phase_ < decimation_) return;
        phase_ = 0;
        ring_[write_ & (kRingSize - 1)] = filtered;
        write_ = write_ + 1;
    }

    void Push(const float* buffer, size_t size) {
        for (size_t i = 0; i < size; i++) Push(buffer[i]);
    }

    /** Main-loop side. Takes a fraction of a millisecond on the Daisy. */
    Reading Analyze() {
        Reading r;
        const int needed = kWindow + maxLag_ + 2;

        // Snapshot the newest samples. The audio callback keeps writing
        // ahead of us, but only ~12 samples per millisecond, far from the
        // oldest sample we read, so no lock is needed.
        const uint32_t end = write_;
        if (end < uint32_t(needed)) return r;
        double energy = 0;
        for (int i = 0; i < needed; i++) {
            work_[i] = ring_[(end - needed + i) & (kRingSize - 1)];
            if (i < kWindow) energy += double(work_[i] * work_[i]);
        }
        if (std::sqrt(energy / kWindow) < double(kGateRms)) return r;

        // Steps 1–3 of YIN: difference function → cumulative mean normalized.
        cmnd_[0] = 1.f;
        double running = 0;
        for (int tau = 1; tau <= maxLag_ + 1; tau++) {
            float d = 0.f;
            for (int j = 0; j < kWindow; j++) {
                const float delta = work_[j] - work_[j + tau];
                d += delta * delta;
            }
            running += double(d);
            cmnd_[tau] = running > 0 ? float(double(d) * tau / running) : 1.f;
        }

        // Step 4: first dip under the threshold, then slide to its bottom.
        int best = -1;
        for (int tau = minLag_; tau <= maxLag_; tau++) {
            if (cmnd_[tau] < kThreshold) {
                while (tau + 1 <= maxLag_ && cmnd_[tau + 1] < cmnd_[tau]) tau++;
                best = tau;
                break;
            }
        }
        if (best < 0) return r;

        // Step 5: parabolic interpolation for sub-sample (sub-cent) accuracy.
        float lag = float(best);
        if (best > 1 && best < maxLag_ + 1) {
            const float a = cmnd_[best - 1], b = cmnd_[best], c = cmnd_[best + 1];
            const float denom = a - 2.f * b + c;
            if (std::fabs(denom) > 1e-9f) lag += 0.5f * (a - c) / denom;
        }

        r.valid = true;
        r.frequency = rate_ / lag;
        r.clarity = 1.f - cmnd_[best];
        const float semis = 12.f * std::log2(r.frequency / reference_) + 69.f;
        r.midiNote = int(std::lround(semis));
        r.cents = 100.f * (semis - float(r.midiNote));
        return r;
    }

private:
    static constexpr int kRingSize = 2048; // power of two; 170 ms at 12 kHz
    static constexpr int kWindow = 1024;   // YIN integration window, 85 ms
    static constexpr int kMaxLag = 500;    // 25 Hz at up to 12.5 kHz

    float rate_ = 12000.f;
    float reference_ = 440.f;
    int minLag_ = 24, maxLag_ = kMaxLag;

    Svf aa1_, aa2_;
    int decimation_ = 4;
    int phase_ = 0;
    float ring_[kRingSize] = {};
    volatile uint32_t write_ = 0;

    float work_[kWindow + kMaxLag + 2] = {};
    float cmnd_[kMaxLag + 2] = {};
};

} // namespace bass
