#pragma once

#include <cstddef>

namespace bass {

/** Describes one parameter so UIs (pots, toggles, OLED, CLI) can be generic. */
struct ParamInfo {
    const char* name;       // at most 6 characters (OLED row): "Sens", "Reso"
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
 *
 * Param(), SetParam() and GetParam() must tolerate any index, including
 * out-of-range ones (return a placeholder / do nothing / return 0): UI code
 * is generic and a stale index must never read past a table.
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

    /**
     * Something 0..1 worth showing on a meter or LED (an envelope, a
     * compressor's gain reduction, ...), or < 0 when the effect has none.
     * Called from the main loop, so it should just read a float.
     */
    virtual float Activity() const { return -1.f; }

    /** Restore every parameter to its default. */
    void ResetParams() {
        for (int i = 0; i < NumParams(); i++) SetParam(i, Param(i).defaultValue);
    }
};

} // namespace bass
