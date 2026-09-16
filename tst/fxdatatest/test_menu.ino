#include "harness/fx_globals.hpp"
#include "menu_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    menu::test_menu(test);
    test.report(F("menu_test"));
}
void loop() {
    exit(0);
}
