#include "harness/fx_globals.hpp"
#include "player_art_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    player_art::test_player_art(test);
    test.report(F("test_player_art"));
}
void loop() {
    exit(0);
}
