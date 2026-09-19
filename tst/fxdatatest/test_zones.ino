// Room-image / prop / fade device suite (bead monhun-ardu-fie.5). Reads the
// mhZones blob + room layers off the cart inside the render bracket and pins
// the framebuffer bytes the blit produces; see zones_test.hpp.
#include "harness/fx_globals.hpp"
#include "zones_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    zones::test_zones(test);
    test.report(F("zones_test"));
}
void loop() {
    exit(0);
}
