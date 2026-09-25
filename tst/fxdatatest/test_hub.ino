// Room-runtime carve (monhun-ardu-fie.4): the hub/screen suite never loads a
// map room, so the room runtime (bounds clamps + door/heal/hold-B logic) folds
// back to the legacy WORLD_W/H constants to keep this image inside the board
// flash budget. Host tst/zone_test.hpp and shipping keep the runtime bounds.
#define MH_ROOM_BOUNDS 0
// prg.7 headroom carve: the hub/screen E2E never charges a weapon or carves a
// carcass, so those subsystems fold out of this image (shipping and the host
// suites keep them; the feel.13 table-driven pass already reclaimed the
// assert-side cost). Room bounds 0 also folds the camp smithy rect path out;
// the smithy route is covered by the host app_state/screens suites.
#define MH_CHARGE 0
#define MH_CARVE 0
// dzr headroom carve: this image is at the board flash limit after the profile
// grew to 26 B. The E2E drives the flow through appNavApply + a direct
// damageMonster, never the sheathe verb or a body-overlap push, so both fold
// out here (shipping + host suites keep them).
#define MH_SHEATHE 0
#define MH_PUSH_MOVE 0
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
