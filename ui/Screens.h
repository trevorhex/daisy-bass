#pragma once

// Every screen the pedal shows, drawn on any 128×64 one-bit canvas with
// libDaisy's OledDisplay drawing calls: the real OLED on the pedal,
// ui::Framebuffer in the simulator and tests. Main loop only (never the
// audio callback).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../control/PageView.h"
#include "../dsp/Tuner.h"
#include "util/oled_fonts.h"

namespace ui {

/** The tuner as displayed: analyzes ~20×/s and glides the needle. Main loop only. */
struct TunerNeedle {
    bass::Tuner::Reading reading;
    float cents = 0.f;
    uint32_t nextAnalyze = 0;

    void Update(bass::Tuner& tuner, uint32_t nowMs) {
        if (int32_t(nowMs - nextAnalyze) < 0) return;
        nextAnalyze = nowMs + 50;
        const bass::Tuner::Reading r = tuner.Analyze();
        // Glide the needle on the same note; snap when the note changes.
        cents = (r.valid && reading.valid && r.midiNote == reading.midiNote) ? cents + 0.35f * (r.cents - cents)
                                                                             : r.cents;
        reading = r;
    }
};

namespace detail {

inline int TextWidth(const char* s, const FontDef& f) { return int(std::strlen(s)) * f.FontWidth; }

/** Text in Font_6x8, drawn inverted on a filled box when `highlight`. */
template <class Canvas>
void Label(Canvas& d, int x, int y, const char* text, bool highlight, const FontDef& font = Font_6x8) {
    const int w = TextWidth(text, font);
    if (highlight) d.DrawRect(x - 1, y, x + w, y + font.FontHeight - 1, true, true);
    d.SetCursor(x, y);
    d.WriteString(text, font, !highlight);
}

/** Divider that doubles as a level meter: solid up to `level` (0..1), dotted beyond. */
template <class Canvas>
void Meter(Canvas& d, int y, float level) {
    const int lit = level < 0.f ? 127 : int(std::fmin(level, 1.f) * 127.f);
    for (int px = 0; px < 128; px++) d.DrawPixel(px, y, px <= lit || px % 4 == 0);
}

} // namespace detail

/** One effect page: name, on/off, page number, a row per pot, toggle states. */
template <class Canvas>
void DrawPage(Canvas& d, const bass::PageView& v, int cpuPercent) {
    using namespace detail;
    char line[32];
    d.Fill(false);

    // Header: name, on/off, page. "ON" is inverted so it reads at a glance.
    d.SetCursor(0, 0);
    d.WriteString(v.name, Font_6x8, true);
    std::snprintf(line, sizeof line, "%d/%d", v.page + 1, v.pages);
    int x = 128 - TextWidth(line, Font_6x8);
    d.SetCursor(x, 0);
    d.WriteString(line, Font_6x8, true);
    Label(d, x - 6 - 6 * (v.enabled ? 2 : 3), 0, v.enabled ? "ON" : "OFF", v.enabled);

    Meter(d, 9, v.activity);

    // One row per pot: name, bar, value. Before an un-caught pot takes over,
    // a tick shows where the pot physically is: turn it to the end of the bar.
    for (int pot = 0; pot < bass::PageView::kPots; pot++) {
        const bass::PageView::Control& c = v.pots[pot];
        if (!c.used) continue;
        const int y = 11 + pot * 8;
        d.SetCursor(0, y);
        d.WriteString(c.info.name, Font_6x8, true);

        if (c.info.isSwitch) {
            // A switch on a pot: both labels, the active one inverted. Left half of the travel = first label.
            const bool on = c.value >= 0.5f;
            Label(d, 40, y, c.info.offLabel ? c.info.offLabel : "Off", !on);
            Label(d, 80, y, c.info.onLabel ? c.info.onLabel : "On", on);
            continue;
        }

        const int x0 = 36, x1 = 104;
        d.DrawRect(x0, y + 1, x1, y + 6, true, false);
        const int fill = x0 + int(c.value * float(x1 - x0));
        if (fill > x0) d.DrawRect(x0, y + 1, fill, y + 6, true, true);
        if (!c.caught) {
            // Inverted where it crosses the filled bar so it always shows.
            const int tx = x0 + int(std::fmin(std::fmax(c.position, 0.f), 1.f) * float(x1 - x0));
            for (int ty = y - 1; ty <= y + 7; ty++) d.DrawPixel(tx, ty, !(tx <= fill && ty >= y + 1 && ty <= y + 6));
        }
        std::snprintf(line, sizeof line, "%3d", int(std::lround(c.value * 100.f)));
        d.SetCursor(108, y);
        d.WriteString(line, Font_6x8, c.caught);
    }

    // Footer: toggle states at the left; mode, preset and CPU at the right.
    std::snprintf(line, sizeof line, "%sP%d%s %d%%", v.editMode ? "EDIT " : "", v.preset + 1, v.modified ? "*" : "",
                  cpuPercent);
    const int rightX = 128 - TextWidth(line, Font_4x6);
    d.SetCursor(rightX, 58);
    d.WriteString(line, Font_4x6, true);

    x = 0;
    for (int t = 0; t < bass::PageView::kToggles; t++) {
        const bass::PageView::Control& c = v.toggles[t];
        if (!c.used) continue;
        const char* label = c.value >= 0.5f ? c.info.onLabel : c.info.offLabel;
        if (!label) label = "?";
        const int w = TextWidth(label, Font_4x6);
        if (x + w > rightX - 4) break; // out of room: never overprint the right side
        d.SetCursor(x, 58);
        d.WriteString(label, Font_4x6, true);
        x += w + 6;
    }
}

/** Preset view: big number, which effects are on, unsaved-changes star. */
template <class Canvas>
void DrawPreset(Canvas& d, const bass::PageView& v) {
    using namespace detail;
    char line[16];
    d.Fill(false);

    d.SetCursor(0, 0);
    d.WriteString("PRESET", Font_6x8, true);
    if (v.modified) {
        const char* edited = "edited";
        d.SetCursor(128 - TextWidth(edited, Font_6x8), 0);
        d.WriteString(edited, Font_6x8, true);
    }
    Meter(d, 9, v.chainActivity);

    // "<  3*  >": FS 1 / FS 2 step through presets.
    d.SetCursor(2, 17);
    d.WriteString("<", Font_11x18, true);
    d.SetCursor(128 - 2 - 11, 17);
    d.WriteString(">", Font_11x18, true);
    std::snprintf(line, sizeof line, "%d", v.preset + 1);
    const int w = TextWidth(line, Font_16x26);
    d.SetCursor(64 - w / 2, 13);
    d.WriteString(line, Font_16x26, true);
    if (v.modified) {
        d.SetCursor(64 + w / 2 + 2, 13);
        d.WriteString("*", Font_11x18, true);
    }

    // Effects in the chain: inverted = on. Two rows of small labels.
    int x = 0, y = 41;
    int shown = 0;
    for (int e = 0; e < v.numEffects; e++) {
        const bass::PageView::EffectSummary& fx = v.effects[e];
        const int lw = TextWidth(fx.name, Font_4x6);
        if (x + lw > 127) {
            x = 0;
            y += 8;
            if (y > 49) break;
        }
        Label(d, x + 1, y, fx.name, fx.enabled, Font_4x6);
        x += lw + 6;
        shown += fx.enabled ? 1 : 0;
    }
    if (shown == 0) {
        d.SetCursor(0, y);
        d.WriteString("all off (clean)", Font_4x6, true);
    }

    d.SetCursor(0, 58);
    d.WriteString("hold FS1: edit  both: save", Font_4x6, true);
}

template <class Canvas>
void DrawTuner(Canvas& d, const TunerNeedle& t, float referenceA4) {
    char line[24];
    d.Fill(false);

    d.SetCursor(0, 0);
    d.WriteString("TUNER", Font_6x8, true);
    std::snprintf(line, sizeof line, "A=%d", int(std::lround(referenceA4)));
    d.SetCursor(128 - 6 * int(std::strlen(line)), 0);
    d.WriteString(line, Font_6x8, true);

    // Needle track: center line plus ticks every 10 cents.
    const int cx = 64, y = 52;
    d.DrawLine(4, y, 123, y, true);
    for (int c = -50; c <= 50; c += 10) {
        const int tx = cx + c * 6 / 5;
        d.DrawLine(tx, y - (c == 0 ? 6 : 2), tx, y + (c == 0 ? 6 : 2), true);
    }

    const bass::Tuner::Reading& r = t.reading;
    if (!r.valid) {
        d.SetCursor(46, 22);
        d.WriteString("- -", Font_11x18, true);
        return;
    }

    const float cents = t.cents;
    const bool inTune = std::fabs(cents) <= 3.f;
    std::snprintf(line, sizeof line, "%s%d", r.NoteName(), r.Octave());
    const int w = 16 * int(std::strlen(line));
    if (inTune) d.DrawRect(cx - w / 2 - 3, 12, cx + w / 2 + 2, 40, true, true);
    d.SetCursor(cx - w / 2, 13);
    d.WriteString(line, Font_16x26, !inTune);

    const int nx = cx + int(std::lround(std::fmax(-50.f, std::fmin(50.f, cents)) * 1.2f));
    d.DrawRect(nx - 1, y - 8, nx + 1, y + 8, true, true);

    std::snprintf(line, sizeof line, "%+d", int(std::lround(cents)));
    d.SetCursor(cents < 0 ? 104 : 4, 13);
    d.WriteString(line, Font_6x8, true);
}

/** A boxed notice over whatever is on screen, e.g. "Saved P3". */
template <class Canvas>
void DrawMessage(Canvas& d, const char* text) {
    const int w = detail::TextWidth(text, Font_7x10);
    const int x = 64 - w / 2;
    d.DrawRect(x - 6, 20, x + w + 5, 41, false, true);
    d.DrawRect(x - 5, 21, x + w + 4, 40, true, false);
    d.SetCursor(x, 26);
    d.WriteString(text, Font_7x10, true);
}

/** Whatever the pedal should show right now. */
template <class Canvas>
void DrawScreen(Canvas& d, const bass::PageView& v, const TunerNeedle& tuner, float referenceA4, int cpuPercent) {
    switch (v.screen) {
        case bass::PageView::Screen::Tuner: DrawTuner(d, tuner, referenceA4); break;
        case bass::PageView::Screen::Page: DrawPage(d, v, cpuPercent); break;
        case bass::PageView::Screen::Preset: DrawPreset(d, v); break;
    }
    if (v.message[0]) DrawMessage(d, v.message);
}

/**
 * The stock panel's two LEDs (no screen):
 *   Tuner         LED 1 blinks = flat, LED 2 blinks = sharp, both solid = in tune, slow pulse = no note
 *   Preset mode   LED 1 blinks the preset number after a change, then stays lit; LED 2 = activity
 *   Edit mode     LED 1 = current effect on, LED 2 = its activity
 */
inline void ComputeLeds(const bass::PageView& v, const TunerNeedle& t, uint32_t nowMs, bool leds[2]) {
    if (v.screen == bass::PageView::Screen::Tuner) {
        const bass::Tuner::Reading& r = t.reading;
        if (!r.valid) {
            leds[0] = leds[1] = (nowMs % 1000) < 100;
        } else if (std::fabs(t.cents) <= 3.f) {
            leds[0] = leds[1] = true;
        } else {
            // Blink period: 80 ms when way off, 600 ms when nearly there.
            const float k = std::fmin(1.f, (std::fabs(t.cents) - 3.f) / 47.f);
            const uint32_t period = uint32_t(600.f - 520.f * k);
            const bool blink = (nowMs % period) < period / 2;
            leds[0] = t.cents < 0 && blink;
            leds[1] = t.cents > 0 && blink;
        }
    } else if (v.editMode) {
        leds[0] = v.enabled;
        leds[1] = v.enabled && v.activity > 0.5f;
    } else {
        const uint32_t blinkMs = 300, blinks = uint32_t(v.preset + 1);
        const uint32_t age = v.presetAgeMs;
        if (age < blinks * blinkMs) leds[0] = (age % blinkMs) < blinkMs / 2; // N blinks for preset N
        else leds[0] = age >= blinks * blinkMs + 2 * blinkMs;              // short gap, then solid
        leds[1] = v.chainActivity > 0.5f;
    }
}

} // namespace ui
