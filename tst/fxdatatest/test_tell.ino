#include "harness/fx_globals.hpp"
#include "tell_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    tell::test_tell(test);
    test.report(F("test_tell"));
}
void loop() {
    exit(0);
}
