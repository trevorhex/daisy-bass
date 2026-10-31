#pragma once

#include <cstddef>

namespace bass {

/** Describes one parameter so UIs (pots, toggles, OLED, CLI) can be generic. */
struct ParamInfo {
    const char* name;       // short, fits an OLED row: "Sens", "Reso"
    float defaultValue;     // 0..1
    bool isSwitch = false;  // true: only 0 or 1 make sense (a toggle)
    const char* offLabel = nullptr; // for switches: label at 0 / 1, e.g. "LP" / "BP"
    const char* onLabel = nullptr;
};

/**
 * Common shape for every effect in the chain.
 *
 * Parameters are normalized 0..1 and indexed. By convention the first six
 * are continuous (they land on the six pots) and switch parameters come
 * after (they land on the toggles), but nothing depends on that.
 *
 * Real-time rules for implementations: SetParam() and Process() must not
 * allocate, lock, or block. SetParam() should only store targets.
 */
class Effect {
public:
    virtual ~Effect() = default;

    virtual const char* Name() const = 0;
    virtual void Init(float sampleRate) = 0;

    virtual int NumParams() const = 0;
    virtual ParamInfo Param(int index) const = 0;
    virtual void SetParam(int index, float value) = 0;
    virtual float GetParam(int index) const = 0;

    /** Process a block in place. */
    virtual void Process(float* buffer, size_t size) = 0;

    /** Restore every parameter to its default. */
    void ResetParams() {
        for (int i = 0; i < NumParams(); i++) SetParam(i, Param(i).defaultValue);
    }
};

} // namespace bass
