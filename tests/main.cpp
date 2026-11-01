#include <cstdio>

#include "Harness.h"

void RunDspTests();
void RunTunerTests();
void RunControlTests();
void RunPedalTests();

int main() {
    RunDspTests();
    RunTunerTests();
    RunControlTests();
    RunPedalTests();

    const int f = harness::failures;
    std::printf("\n%s (%d failure%s)\n", f ? "FAILED" : "PASSED", f, f == 1 ? "" : "s");
    return f;
}
