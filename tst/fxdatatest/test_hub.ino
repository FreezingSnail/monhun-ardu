// Room-runtime carve (monhun-ardu-fie.4): monhun-ardu-087 keeps the room runtime
// ON here so the E2E can assert the hunt room (camp) and drive the camp hold-B
// resume path.
//
// monhun-ardu-087 headroom carve: including src/cards.hpp for the quest-card
// launch pushed this image past the board; the suite never fights a breakable-
// parts creature (its target is LUNGE), so the parts machinery folds out
// (shipping and test_combat keep it).
#define MH_COMBAT_PARTS 0
// prg.7 headroom carve: the hub/screen E2E never charges a weapon or carves a
// carcass, so those subsystems fold out of this image (shipping and the host
// suites keep them).
#define MH_CHARGE 0
#define MH_CARVE 0
// dzr headroom carve: the E2E drives the flow through appNavApply + a direct
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
