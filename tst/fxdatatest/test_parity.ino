#include "harness/fx_globals.hpp"
#include "parity_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    parity::test_parity(test);
    test.report(F("parity_test"));
}
void loop() {
    exit(0);
}
