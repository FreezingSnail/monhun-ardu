#include "harness/fx_globals.hpp"
#include "hub_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    hubfx::test_hub(test);
    test.report(F("test_hub"));
}
void loop() {
    exit(0);
}
