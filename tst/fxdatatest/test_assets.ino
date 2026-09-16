#include "harness/fx_globals.hpp"
#include "asset_test.hpp"

void setup() {
    fxTestSetup();
    arduboy.startGray();   // the asset read brackets on waitForNextPlane
    FxTest test;
    test_assets(test);
    test.report(F("asset_test"));
}
void loop() {
    exit(0);
}
