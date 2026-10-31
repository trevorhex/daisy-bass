#pragma once

// Tiny test harness: no framework, just CHECK and named test blocks.
// The test binary's exit code is the number of failed checks.

#include <cmath>
#include <cstdio>
#include <functional>

namespace harness {

inline int failures = 0;
inline const char* current = "";

inline void Test(const char* name, const std::function<void()>& body) {
    current = name;
    const int before = failures;
    body();
    std::printf("%s %s\n", failures == before ? "  ok  " : "  FAIL", name);
}

constexpr float kRate = 48000.f;
constexpr float kPi = 3.14159265f;

/** RMS of a process run on a sine, measured over the second half (after settling). */
inline float SineRms(float freq, const std::function<float(float)>& process, float seconds = 0.5f) {
    const size_t n = size_t(seconds * kRate);
    double sum = 0;
    for (size_t i = 0; i < n; i++) {
        const float y = process(std::sin(2.f * kPi * freq * float(i) / kRate));
        if (i >= n / 2) sum += double(y * y);
    }
    return float(std::sqrt(sum / double(n - n / 2)));
}

inline float Db(float ratio) { return 20.f * std::log10(ratio); }

} // namespace harness

#define CHECK(cond)                                                                                 \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            std::printf("  FAIL [%s] %s:%d  %s\n", harness::current, __FILE__, __LINE__, #cond);    \
            harness::failures++;                                                                    \
        }                                                                                           \
    } while (0)

// Like CHECK, but prints the actual value: CHECK_NEAR(reading.cents, 12.f, 1.f)
#define CHECK_NEAR(actual, expected, tolerance)                                                     \
    do {                                                                                            \
        const double a_ = double(actual), e_ = double(expected);                                    \
        if (!(std::fabs(a_ - e_) <= double(tolerance))) {                                           \
            std::printf("  FAIL [%s] %s:%d  %s = %.4f, expected %.4f ± %.4f\n", harness::current,   \
                        __FILE__, __LINE__, #actual, a_, e_, double(tolerance));                    \
            harness::failures++;                                                                    \
        }                                                                                           \
    } while (0)
