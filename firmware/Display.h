#pragma once

// Optional 128×64 SSD1306 OLED on I2C1 (Seed D11 = SCL, D12 = SDA, 3V3, GND),
// address 0x3C. Built only with `make OLED=1`.
//
// Everything here runs in the main loop: an Update() pushes 1 KB over I2C
// (~10 ms) and must never happen inside the audio callback.

#include <cmath>
#include <cstdio>
#include <cstring>

#include "daisy_seed.h"
#include "dev/oled_ssd130x.h"
#include "dsp/Effect.h"
#include "dsp/Tuner.h"

class Display {
public:
    /** What to draw for an effect page, gathered by main.cpp. */
    struct EffectView {
        const bass::Effect* effect;
        bool enabled;
        int page, pages;
        int cpuPercent;
        const int* potParam;         // [6] effect param index per pot, -1 = unused
        const int* toggleParam;      // [4]
        const bool* potCaught;       // [6] pickup state; false = turn pot to the marker
    };

    void Init() {
        Oled::Config cfg; // defaults: I2C1, PB8/PB9 (D11/D12), 1 MHz, 0x3C
        oled_.Init(cfg);
        oled_.Fill(false);
        oled_.Update();
    }

    void DrawEffect(const EffectView& v) {
        using namespace daisy;
        char line[32];
        oled_.Fill(false);

        // Header: name, on/off, page, CPU.
        oled_.SetCursor(0, 0);
        oled_.WriteString(v.effect->Name(), Font_6x8, true);
        std::snprintf(line, sizeof line, "%s %d/%d", v.enabled ? "ON " : "OFF", v.page + 1, v.pages);
        oled_.SetCursor(128 - 6 * int(std::strlen(line)), 0);
        oled_.WriteString(line, Font_6x8, true);
        oled_.DrawLine(0, 9, 127, 9, true);

        // One row per pot: name, bar, value. A hollow tick marks where an
        // un-caught pot needs to go before it takes over.
        for (int pot = 0; pot < 6; pot++) {
            const int i = v.potParam[pot];
            if (i < 0) continue;
            const int y = 11 + pot * 8;
            const float value = v.effect->GetParam(i);

            oled_.SetCursor(0, y);
            oled_.WriteString(v.effect->Param(i).name, Font_6x8, true);

            const int x0 = 36, x1 = 104;
            oled_.DrawRect(x0, y + 1, x1, y + 6, true, false);
            const int fill = x0 + int(value * float(x1 - x0));
            if (fill > x0) oled_.DrawRect(x0, y + 1, fill, y + 6, true, true);
            if (!v.potCaught[pot]) oled_.DrawLine(fill, y - 1, fill, y + 7, true);

            std::snprintf(line, sizeof line, "%3d", int(std::lround(value * 100.f)));
            oled_.SetCursor(108, y);
            oled_.WriteString(line, Font_6x8, v.potCaught[pot]);
        }

        // Footer: toggle states, e.g. "LP Up Clean 150", plus CPU.
        int x = 0;
        for (int t = 0; t < 4; t++) {
            const int i = v.toggleParam[t];
            if (i < 0) continue;
            const bass::ParamInfo info = v.effect->Param(i);
            const char* label = v.effect->GetParam(i) >= 0.5f ? info.onLabel : info.offLabel;
            oled_.SetCursor(x, 59);
            oled_.WriteString(label ? label : "?", Font_4x6, true);
            x += 4 * int(std::strlen(label ? label : "?")) + 6;
        }
        std::snprintf(line, sizeof line, "%d%%", v.cpuPercent);
        oled_.SetCursor(128 - 4 * int(std::strlen(line)), 59);
        oled_.WriteString(line, Font_4x6, true);

        oled_.Update();
    }

    void DrawTuner(const bass::Tuner::Reading& r, float cents, float referenceA4) {
        using namespace daisy;
        char line[24];
        oled_.Fill(false);

        oled_.SetCursor(0, 0);
        oled_.WriteString("TUNER", Font_6x8, true);
        std::snprintf(line, sizeof line, "A=%d", int(std::lround(referenceA4)));
        oled_.SetCursor(128 - 6 * int(std::strlen(line)), 0);
        oled_.WriteString(line, Font_6x8, true);

        // Needle track: center line plus ticks every 10 cents.
        const int cx = 64, y = 52;
        oled_.DrawLine(4, y, 123, y, true);
        for (int c = -50; c <= 50; c += 10) {
            const int tx = cx + c * 6 / 5;
            oled_.DrawLine(tx, y - (c == 0 ? 6 : 2), tx, y + (c == 0 ? 6 : 2), true);
        }

        if (!r.valid) {
            oled_.SetCursor(46, 22);
            oled_.WriteString("- -", Font_11x18, true);
            oled_.Update();
            return;
        }

        const bool inTune = std::fabs(cents) <= 3.f;
        std::snprintf(line, sizeof line, "%s%d", r.NoteName(), r.Octave());
        const int w = 16 * int(std::strlen(line));
        if (inTune) oled_.DrawRect(cx - w / 2 - 3, 12, cx + w / 2 + 2, 40, true, true);
        oled_.SetCursor(cx - w / 2, 13);
        oled_.WriteString(line, Font_16x26, !inTune);

        const int nx = cx + int(std::lround(std::fmax(-50.f, std::fmin(50.f, cents)) * 1.2f));
        oled_.DrawRect(nx - 1, y - 8, nx + 1, y + 8, true, true);

        std::snprintf(line, sizeof line, "%+d", int(std::lround(cents)));
        oled_.SetCursor(cents < 0 ? 104 : 4, 13);
        oled_.WriteString(line, Font_6x8, true);

        oled_.Update();
    }

private:
    using Oled = daisy::OledDisplay<daisy::SSD130xI2c128x64Driver>;
    Oled oled_;
};
