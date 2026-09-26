#include "harness/fx_globals.hpp"
#include "screens_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    screenfx::test_screens(test);
    screenfx::test_screens_pixels(test);
    screenfx::test_screens_gear(test);
    screenfx::test_screens_hint(test);
    test.report(F("test_screens"));
}
void loop() {
    exit(0);
}
