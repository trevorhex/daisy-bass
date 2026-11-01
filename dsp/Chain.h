#pragma once

#include <cstddef>

#include "Effect.h"
#include "Smoother.h"

namespace bass {

/**
 * Runs effects in series. Each slot can be switched on/off with a short
 * crossfade so footswitch presses never click. Effects are owned elsewhere
 * (statically allocated); the chain just holds pointers.
 */
class Chain {
public:
    static constexpr int kMaxEffects = 8;
    static constexpr size_t kMaxBlock = 256;

    void Init(float sampleRate) {
        sampleRate_ = sampleRate;
        for (int i = 0; i < count_; i++) InitSlot(i);
    }

    /** Add before Init(). Returns the slot index, or -1 when full. */
    int Add(Effect* effect, bool enabled = true) {
        if (count_ >= kMaxEffects) return -1;
        slots_[count_] = {effect, enabled, {}};
        return count_++;
    }

    int Size() const { return count_; }
    Effect* At(int slot) const { return slots_[slot].effect; }

    bool IsEnabled(int slot) const { return slots_[slot].enabled; }
    void SetEnabled(int slot, bool on) {
        slots_[slot].enabled = on;
        slots_[slot].fade.SetTarget(on ? 1.f : 0.f);
    }
    void Toggle(int slot) { SetEnabled(slot, !slots_[slot].enabled); }

    /** Process in place. Blocks larger than kMaxBlock are split. */
    void Process(float* buffer, size_t size) {
        while (size > kMaxBlock) {
            Process(buffer, kMaxBlock);
            buffer += kMaxBlock;
            size -= kMaxBlock;
        }
        for (int i = 0; i < count_; i++) {
            Slot& s = slots_[i];
            // Fully off and settled: skip the effect entirely (saves CPU).
            if (!s.enabled && s.fade.Value() < 1e-4f) continue;
            // Fully on and settled: no dry copy or crossfade needed.
            if (s.enabled && s.fade.Value() > 1.f - 1e-4f) {
                s.effect->Process(buffer, size);
                continue;
            }

            for (size_t n = 0; n < size; n++) dry_[n] = buffer[n];
            s.effect->Process(buffer, size);
            for (size_t n = 0; n < size; n++) {
                const float wet = s.fade.Process();
                buffer[n] = dry_[n] + wet * (buffer[n] - dry_[n]);
            }
        }
    }

private:
    struct Slot {
        Effect* effect;
        bool enabled;
        Smoother fade;
    };

    void InitSlot(int i) {
        slots_[i].effect->Init(sampleRate_);
        slots_[i].fade.Init(sampleRate_, 0.01f, slots_[i].enabled ? 1.f : 0.f);
    }

    float sampleRate_ = 48000.f;
    Slot slots_[kMaxEffects] = {};
    int count_ = 0;
    float dry_[kMaxBlock] = {};
};

} // namespace bass
