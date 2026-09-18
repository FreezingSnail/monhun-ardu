#include "harness/fx_globals.hpp"
#include "monster_art_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    monsterart::test_monster_art(test);
    test.report(F("test_monster_art"));
}
void loop() {
    exit(0);
}
