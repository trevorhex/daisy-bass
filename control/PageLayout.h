#pragma once

#include "../dsp/Effect.h"

namespace bass {

/**
 * Spreads an effect's parameters over pages of pots and toggles.
 *
 * Page 1 matches the panel: the first continuous params go on the pots and
 * the first switch params on the toggles. Whatever doesn't fit (switches
 * beyond the toggles, continuous params beyond the pots) goes on extra
 * pages, on the pots, in parameter order. A switch on a pot flips at the
 * pot's midpoint (see PotSwitch).
 *
 * With four toggles the Env Filter fits on one page. With two, Mode and
 * Sweep stay on the toggles and Lows + Xover move to page 2.
 */
struct PageLayout {
    static constexpr int kMaxPots = 6;
    static constexpr int kMaxToggles = 4;
    static constexpr int kMaxPages = 4;

    struct Page {
        int pot[kMaxPots];       // param index per pot, -1 = unused
        int toggle[kMaxToggles]; // param index per toggle, -1 = unused
    };

    Page pages[kMaxPages];
    int numPages = 0;

    void Build(const Effect& fx, int numPots, int numToggles) {
        if (numPots > kMaxPots) numPots = kMaxPots;
        if (numToggles > kMaxToggles) numToggles = kMaxToggles;
        for (Page& p : pages) {
            for (int& i : p.pot) i = -1;
            for (int& i : p.toggle) i = -1;
        }
        numPages = 1;

        int pot = 0, toggle = 0, extra = 0;
        for (int i = 0; i < fx.NumParams(); i++) {
            const bool isSwitch = fx.Param(i).isSwitch;
            if (isSwitch && toggle < numToggles) {
                pages[0].toggle[toggle++] = i;
            } else if (!isSwitch && pot < numPots) {
                pages[0].pot[pot++] = i;
            } else if (numPots > 0) {
                const int page = 1 + extra / numPots;
                if (page >= kMaxPages) break;
                pages[page].pot[extra % numPots] = i;
                if (page + 1 > numPages) numPages = page + 1;
                extra++;
            }
        }
    }
};

} // namespace bass
