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
#include "dsp/Tuner.h"
#include "PageView.h"

class Display {
public:
    /** Extra numbers for the effect screen that aren't part of the page. */
    struct Status {
        int cpuPercent;
    };

    void Init() {
        Oled::Config cfg; // defaults: I2C1, PB8/PB9 (D11/D12), 1 MHz, 0x3C
        oled_.Init(cfg);
        oled_.Fill(false);
        oled_.Update();
    }

    void DrawEffect(const PageView& v, const Status& status) {
        using namespace daisy;
        char line[32];
        oled_.Fill(false);

        // Header: name, on/off, page. "ON" is inverted so it reads at a
        // glance from standing height (the screen panel has no LEDs).
        oled_.SetCursor(0, 0);
        oled_.WriteString(v.name, Font_6x8, true);
        std::snprintf(line, sizeof line, "%d/%d", v.page + 1, v.pages);
        int x = 128 - 6 * int(std::strlen(line));
        oled_.SetCursor(x, 0);
        oled_.WriteString(line, Font_6x8, true);
        DrawLabel(x - 6 - 6 * (v.enabled ? 2 : 3), 0, v.enabled ? "ON" : "OFF", v.enabled);

        // Divider doubles as the activity meter (what LED 2 shows): solid up
        // to the current level, dotted beyond it.
        const int lit = v.activity < 0.f ? 127 : int(std::fmin(v.activity, 1.f) * 127.f);
        for (int px = 0; px < 128; px++) oled_.DrawPixel(px, 9, px <= lit || px % 4 == 0);

        // One row per pot: name, bar, value. Before an un-caught pot takes
        // over, a tick shows where the pot physically is: turn it to the end
        // of the bar.
        for (int pot = 0; pot < PageView::kPots; pot++) {
            const PageView::Control& c = v.pots[pot];
            if (!c.used) continue;
            const int y = 11 + pot * 8;

            oled_.SetCursor(0, y);
            oled_.WriteString(c.info.name, Font_6x8, true);

            if (c.info.isSwitch) {
                // A switch on a pot: both labels, the active one inverted.
                // Left half of the pot's travel = the first label.
                const bool on = c.value >= 0.5f;
                DrawLabel(40, y, c.info.offLabel ? c.info.offLabel : "Off", !on);
                DrawLabel(80, y, c.info.onLabel ? c.info.onLabel : "On", on);
                continue;
            }

            const int x0 = 36, x1 = 104;
            oled_.DrawRect(x0, y + 1, x1, y + 6, true, false);
            const int fill = x0 + int(c.value * float(x1 - x0));
            if (fill > x0) oled_.DrawRect(x0, y + 1, fill, y + 6, true, true);
            if (!c.caught) {
                // Inverted where it crosses the filled bar so it always shows.
                const int tx = x0 + int(std::fmin(std::fmax(c.position, 0.f), 1.f) * float(x1 - x0));
                for (int ty = y - 1; ty <= y + 7; ty++)
                    oled_.DrawPixel(tx, ty, !(tx <= fill && ty >= y + 1 && ty <= y + 6));
            }

            std::snprintf(line, sizeof line, "%3d", int(std::lround(c.value * 100.f)));
            oled_.SetCursor(108, y);
            oled_.WriteString(line, Font_6x8, c.caught);
        }

        // Footer: toggle states, e.g. "LP Up Clean 150", plus CPU at the right.
        std::snprintf(line, sizeof line, "%d%%", status.cpuPercent);
        const int cpuX = 128 - 4 * int(std::strlen(line));
        oled_.SetCursor(cpuX, 59);
        oled_.WriteString(line, Font_4x6, true);

        x = 0;
        for (int t = 0; t < PageView::kToggles; t++) {
            const PageView::Control& c = v.toggles[t];
            if (!c.used) continue;
            const char* label = c.value >= 0.5f ? c.info.onLabel : c.info.offLabel;
            if (!label) label = "?";
            const int w = 4 * int(std::strlen(label));
            if (x + w > cpuX - 4) break; // out of room: never overprint the CPU figure
            oled_.SetCursor(x, 59);
            oled_.WriteString(label, Font_4x6, true);
            x += w + 6;
        }

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
    /** Text at (x, y) in Font_6x8, drawn inverted on a filled box when `highlight`. */
    void DrawLabel(int x, int y, const char* text, bool highlight) {
        const int w = 6 * int(std::strlen(text));
        if (highlight) oled_.DrawRect(x - 1, y, x + w, y + 7, true, true);
        oled_.SetCursor(x, y);
        oled_.WriteString(text, Font_6x8, !highlight);
    }

    using Oled = daisy::OledDisplay<daisy::SSD130xI2c128x64Driver>;
    Oled oled_;
};
