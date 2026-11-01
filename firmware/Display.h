#pragma once

// Optional 128×64 SSD1306 OLED on I2C1 (Seed D11 = SCL, D12 = SDA, 3V3, GND),
// address 0x3C. Built only with `make OLED=1` (implied by SCREEN_PANEL=1).
//
// The screens themselves are in ui/Screens.h, shared with the simulator.
// Everything here runs in the main loop: an Update() pushes 1 KB over I2C
// (~10 ms) and must never happen inside the audio callback.

#include "daisy_seed.h"
#include "dev/oled_ssd130x.h"
#include "ui/Screens.h"

class Display {
public:
    void Init() {
        Oled::Config cfg; // defaults: I2C1, PB8/PB9 (D11/D12), 1 MHz, 0x3C
        oled_.Init(cfg);
        oled_.Fill(false);
        oled_.Update();
    }

    void Draw(const bass::PageView& view, const ui::TunerNeedle& needle, float referenceA4, int cpuPercent) {
        ui::DrawScreen(oled_, view, needle, referenceA4, cpuPercent);
        oled_.Update();
    }

private:
    using Oled = daisy::OledDisplay<daisy::SSD130xI2c128x64Driver>;
    Oled oled_;
};
