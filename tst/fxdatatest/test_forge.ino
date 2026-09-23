#include "harness/fx_globals.hpp"
#include "forge_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    forgefx::test_forge(test);
    test.report(F("test_forge"));
}
void loop() {
    exit(0);
}
