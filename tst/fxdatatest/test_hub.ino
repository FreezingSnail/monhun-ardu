// Room-runtime carve (monhun-ardu-fie.4): the hub/screen suite never loads a
// map room, so the room runtime (bounds clamps + door/heal/hold-B logic) folds
// back to the legacy WORLD_W/H constants to keep this image inside the board
// flash budget. Host tst/zone_test.hpp and shipping keep the runtime bounds.
#define MH_ROOM_BOUNDS 0
#include "harness/fx_globals.hpp"
#include "hub_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    hubfx::test_hub(test);
    test.report(F("test_hub"));
}
void loop() {
    exit(0);
}
