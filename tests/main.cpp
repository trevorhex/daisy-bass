#include <cstdio>

#include "Harness.h"

void RunDspTests();
void RunTunerTests();
void RunControlTests();

int main() {
    RunDspTests();
    RunTunerTests();
    RunControlTests();

    const int f = harness::failures;
    std::printf("\n%s (%d failure%s)\n", f ? "FAILED" : "PASSED", f, f == 1 ? "" : "s");
    return f;
}
