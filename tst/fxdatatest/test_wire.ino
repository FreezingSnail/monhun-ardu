#include "harness/fx_globals.hpp"
#include "wire_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    wire::test_wire(test);
    test.report(F("test_wire"));
}
void loop() {
    exit(0);
}
