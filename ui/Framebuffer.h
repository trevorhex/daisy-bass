#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "util/oled_fonts.h" // libDaisy's fonts: the real pedal's glyphs

namespace ui {

/**
 * A 128×64 one-bit screen in memory with the same drawing calls as
 * libDaisy's OledDisplay (same line, rectangle and text algorithms), so
 * ui/Screens.h draws pixel-identical frames for the simulator and tests.
 */
class Framebuffer {
public:
    static constexpr int kWidth = 128;
    static constexpr int kHeight = 64;

    void Fill(bool on) { std::memset(pixels_, on ? 1 : 0, sizeof pixels_); }

    void DrawPixel(int x, int y, bool on) {
        if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return;
        pixels_[y][x] = on ? 1 : 0;
    }

    bool Pixel(int x, int y) const { return x >= 0 && y >= 0 && x < kWidth && y < kHeight && pixels_[y][x]; }

    /** Bresenham, as in libDaisy's OneBitGraphicsDisplayImpl::DrawLine. */
    void DrawLine(int x1, int y1, int x2, int y2, bool on) {
        const int dx = std::abs(x2 - x1), dy = std::abs(y2 - y1);
        const int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
        int error = dx - dy;
        DrawPixel(x2, y2, on);
        while (x1 != x2 || y1 != y2) {
            DrawPixel(x1, y1, on);
            const int e2 = error * 2;
            if (e2 > -dy) {
                error -= dy;
                x1 += sx;
            }
            if (e2 < dx) {
                error += dx;
                y1 += sy;
            }
        }
    }

    void DrawRect(int x1, int y1, int x2, int y2, bool on, bool fill = false) {
        if (fill) {
            for (int x = x1; x <= x2; x++)
                for (int y = y1; y <= y2; y++) DrawPixel(x, y, on);
        } else {
            DrawLine(x1, y1, x2, y1, on);
            DrawLine(x2, y1, x2, y2, on);
            DrawLine(x2, y2, x1, y2, on);
            DrawLine(x1, y2, x1, y1, on);
        }
    }

    void SetCursor(int x, int y) {
        cursorX_ = x;
        cursorY_ = y;
    }

    /** As libDaisy: glyph cells are drawn opaque (background = !on); returns 0 if it doesn't fit. */
    char WriteChar(char ch, FontDef font, bool on) {
        if (ch < 32 || ch > 126) return 0;
        if (kWidth < cursorX_ + font.FontWidth || kHeight < cursorY_ + font.FontHeight) return 0;
        for (int i = 0; i < font.FontHeight; i++) {
            const uint32_t b = font.data[(ch - 32) * font.FontHeight + i];
            for (int j = 0; j < font.FontWidth; j++) DrawPixel(cursorX_ + j, cursorY_ + i, ((b << j) & 0x8000) ? on : !on);
        }
        cursorX_ += font.FontWidth;
        return ch;
    }

    char WriteString(const char* str, FontDef font, bool on) {
        for (; *str; str++)
            if (WriteChar(*str, font, on) != *str) return *str;
        return 0;
    }

private:
    uint8_t pixels_[kHeight][kWidth] = {};
    int cursorX_ = 0, cursorY_ = 0;
};

} // namespace ui
