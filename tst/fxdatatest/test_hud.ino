#include "harness/fx_globals.hpp"
#include "hud_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    hud::test_hud(test);
    test.report(F("test_hud"));
}
void loop() {
    exit(0);
}
