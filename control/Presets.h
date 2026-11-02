#pragma once

#include <cstdint>
#include <cstring>

#include "../dsp/Chain.h"
#include "../dsp/Effect.h"

namespace bass {

/** Stable id for an effect, from its name, so presets survive effects being reordered. */
inline uint32_t EffectId(const Effect& fx) {
    uint32_t h = 2166136261u; // FNV-1a
    for (const char* c = fx.Name(); *c; c++) h = (h ^ uint8_t(*c)) * 16777619u;
    return h;
}

/**
 * Every preset, as one plain block of memory: the pedal writes it to QSPI
 * flash as-is and the simulator writes it to a file. Only 32-bit fields, so
 * there's no padding and two banks compare equal byte for byte.
 */
struct PresetBank {
    static constexpr uint32_t kMagic = 0x42415353; // "BASS"
    static constexpr uint32_t kVersion = 2;
    static constexpr int kNumPresets = 16;
    // Version 1 had 8 presets and was otherwise identical, so a version 1
    // bank is the start of a version 2 one (see UpgradeBank).
    static constexpr int kV1NumPresets = 8;
    static constexpr int kMaxEffects = Chain::kMaxEffects;
    static constexpr int kMaxParams = 16;

    struct EffectState {
        uint32_t id;
        uint32_t enabled;
        uint32_t numParams;
        float values[kMaxParams];
    };
    struct Preset {
        uint32_t numEffects;
        EffectState fx[kMaxEffects];
    };

    uint32_t magic;
    uint32_t version;
    uint32_t current; // the preset loaded at power-up: the last one saved
    uint32_t reserved;
    Preset presets[kNumPresets];

    PresetBank() { std::memset(this, 0, sizeof *this); }

    bool Valid() const { return magic == kMagic && version == kVersion && current < uint32_t(kNumPresets); }

    /** A version 1 bank: readable by UpgradeBank(). */
    bool IsV1() const { return magic == kMagic && version == 1 && current < uint32_t(kV1NumPresets); }

    bool operator==(const PresetBank& o) const { return std::memcmp(this, &o, sizeof *this) == 0; }
    bool operator!=(const PresetBank& o) const { return !(*this == o); }
};

/** Store every effect's on/off state and parameters. */
inline void CapturePreset(const Chain& chain, PresetBank::Preset& p) {
    std::memset(&p, 0, sizeof p);
    p.numEffects = uint32_t(chain.Size());
    for (int e = 0; e < chain.Size() && e < PresetBank::kMaxEffects; e++) {
        const Effect& fx = *chain.At(e);
        PresetBank::EffectState& s = p.fx[e];
        s.id = EffectId(fx);
        s.enabled = chain.IsEnabled(e) ? 1 : 0;
        const int n = fx.NumParams() < PresetBank::kMaxParams ? fx.NumParams() : PresetBank::kMaxParams;
        s.numParams = uint32_t(n);
        for (int i = 0; i < n; i++) s.values[i] = fx.GetParam(i);
    }
}

/**
 * Restore a preset. Effects are matched by id, not position. An effect the
 * preset doesn't know (added in a later firmware) gets its defaults and is
 * switched off. `immediate` skips the on/off crossfade (power-up).
 */
inline void ApplyPreset(const PresetBank::Preset& p, Chain& chain, bool immediate = false) {
    for (int e = 0; e < chain.Size(); e++) {
        Effect& fx = *chain.At(e);
        const uint32_t id = EffectId(fx);
        const PresetBank::EffectState* s = nullptr;
        for (uint32_t k = 0; k < p.numEffects && k < uint32_t(PresetBank::kMaxEffects); k++) {
            if (p.fx[k].id == id) {
                s = &p.fx[k];
                break;
            }
        }
        if (!s) {
            fx.ResetParams();
            chain.SetEnabled(e, false, immediate);
            continue;
        }
        fx.ResetParams(); // params the preset doesn't cover start from defaults
        for (int i = 0; i < int(s->numParams) && i < fx.NumParams(); i++) fx.SetParam(i, s->values[i]);
        chain.SetEnabled(e, s->enabled != 0, immediate);
    }
}

/** A fresh bank: every slot holds the chain as it is now (normally: defaults, all off). */
inline PresetBank MakeEmptyBank(const Chain& chain) {
    PresetBank bank;
    bank.magic = PresetBank::kMagic;
    bank.version = PresetBank::kVersion;
    for (auto& p : bank.presets) CapturePreset(chain, p);
    return bank;
}

/**
 * Bring an older bank up to date: its presets keep their slots and the new
 * slots get the chain as it is now (normally: defaults, all off), like
 * MakeEmptyBank(). Returns false if `old` isn't a bank it understands.
 */
inline bool UpgradeBank(const PresetBank& old, const Chain& chain, PresetBank& out) {
    if (!old.IsV1()) return false;
    out = MakeEmptyBank(chain);
    out.current = old.current;
    for (int i = 0; i < PresetBank::kV1NumPresets; i++) out.presets[i] = old.presets[i];
    return true;
}

} // namespace bass
