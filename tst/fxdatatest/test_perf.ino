// Perf-bench carve (mirrors perf_test.hpp's MH_AUDIO=0): the bench scene is
// MON_LUNGE, which never runs a zone/break path, so this image compiles the
// optional zones machinery out (MH_COMBAT_PARTS=0, see src/core/game.hpp and
// the ZONES_ENABLED gates) and keeps the flash headroom the full machinery
// needs. Shipping and test_combat keep it (test_parity carves it too: its
// fixtures also replay only the shipped 3). Measured costs are unchanged: the
// zone functions were never executed by this scene.
#define MH_COMBAT_PARTS 0
// monhun-ardu-9kn: shipping renders the stored room-image ground (SIZE_FLAGS),
// so the bench must compile the same path -- otherwise its rMx/pHz describe the
// retired dot field. Same reason test_zones.ino forces this on.
#define MH_ROOM_IMAGE 1
#include "harness/fx_globals.hpp"
#include "perf_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    perf::test_perf(test);
    test.report(F("perf_test"));
}
void loop() {
    exit(0);
}
