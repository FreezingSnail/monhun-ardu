#include "harness/fx_globals.hpp"
#include "perf_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    perf::test_perf(test);
    test.report(F("perf_test"));
}
void loop() { exit(0); }
