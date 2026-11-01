#pragma once

#include <initializer_list>

#include "Svf.h"

namespace bass {

/**
 * 4th-order Linkwitz-Riley crossover: two cascaded Butterworth (Q = 0.707)
 * stages per band. low + high sums back to a flat magnitude response, so
 * "keep the lows clean, process the highs" doesn't leave a hole at the split.
 */
class Crossover {
public:
    struct Bands { float low, high; };

    void Init(float sampleRate, float frequency = 150.f) {
        for (Svf* f : {&lp1_, &lp2_, &hp1_, &hp2_}) f->Init(sampleRate);
        SetFrequency(frequency);
    }

    void SetFrequency(float hz) {
        for (Svf* f : {&lp1_, &lp2_, &hp1_, &hp2_}) f->SetParams(hz, kButterworthQ);
    }

    Bands Process(float x) {
        const float low = lp2_.Process(lp1_.Process(x).low).low;
        const float high = hp2_.Process(hp1_.Process(x).high).high;
        return {low, high};
    }

private:
    static constexpr float kButterworthQ = 0.70710678f;
    Svf lp1_, lp2_, hp1_, hp2_;
};

} // namespace bass
