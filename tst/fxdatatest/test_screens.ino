#include "harness/fx_globals.hpp"
#include "screens_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    screenfx::test_screens(test);
    test.report(F("test_screens"));
}
void loop() {
    exit(0);
}
