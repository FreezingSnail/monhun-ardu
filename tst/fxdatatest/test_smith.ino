#include "harness/fx_globals.hpp"
#include "smith_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    smithfx::test_smith(test);
    test.report(F("test_smith"));
}
void loop() {
    exit(0);
}
