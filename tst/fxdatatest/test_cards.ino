#include "harness/fx_globals.hpp"
#include "cards_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    cardsfx::test_cards(test);
    test.report(F("test_cards"));
}
void loop() {
    exit(0);
}
