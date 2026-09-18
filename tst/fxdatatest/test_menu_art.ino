#include "harness/fx_globals.hpp"
#include "menu_art_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    menuart::test_menu_art(test);
    test.report(F("test_menu_art"));
}
void loop() {
    exit(0);
}
