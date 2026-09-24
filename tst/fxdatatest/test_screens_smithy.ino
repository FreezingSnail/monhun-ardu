// Smithy split (hbk.10): the FORGE submenu / CRAFT / ARMOR FORGE checks live in
// their own suite so each sketch keeps its own (small) setup frame -- the AVR
// test stack is tight and folding them into test_screens overflowed it.
#include "harness/fx_globals.hpp"
#include "screens_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    screenfx::test_screens_smithy(test);
    test.report(F("test_screens_smithy"));
}
void loop() {
    exit(0);
}
