#pragma once

#include <cmath>
#include <vector>

/**
 * A synthetic fingerstyle-ish bass riff so there's something to listen to
 * before you've recorded a real DI track. Band-limited saw-like tone with a
 * pluck envelope; dynamics vary per note so the envelope filter has work to do.
 */
namespace testsignal {

inline std::vector<float> BassRiff(float sampleRate, int bars = 4) {
    struct Note { float midi, beats, velocity; };
    // E minor funk-ish line at 100 BPM, one bar (4 beats), repeated.
    const Note bar[] = {
        {28, 0.5f, 1.0f}, {28, 0.25f, 0.45f}, {40, 0.25f, 0.8f}, {28, 0.5f, 0.6f},
        {31, 0.5f, 0.9f}, {33, 0.5f, 1.0f},   {35, 0.25f, 0.5f}, {33, 0.25f, 0.7f},
        {31, 0.5f, 0.85f}, {26, 0.5f, 0.95f},
    };

    const float secondsPerBeat = 60.f / 100.f;
    const float pi = 3.14159265f;
    std::vector<float> out;

    for (int b = 0; b < bars; b++) {
        for (const Note& n : bar) {
            const float freq = 440.f * std::pow(2.f, (n.midi - 69.f) / 12.f);
            const size_t len = size_t(n.beats * secondsPerBeat * sampleRate);
            const int harmonics = int(5000.f / freq);

            for (size_t i = 0; i < len; i++) {
                const float t = i / sampleRate;
                float v = 0.f;
                for (int h = 1; h <= harmonics; h++) {
                    // Upper harmonics fade faster, like a real string.
                    v += std::sin(2.f * pi * freq * h * t) / h * std::exp(-t * 2.f * h);
                }
                const float pluck = std::exp(-t * 3.f) * std::fmin(1.f, t * 400.f); // ~2.5 ms attack
                const float release = std::fmin(1.f, (len - i) / (0.01f * sampleRate)); // no clicks at note end
                out.push_back(0.35f * n.velocity * v * pluck * release);
            }
        }
    }
    return out;
}

} // namespace testsignal
