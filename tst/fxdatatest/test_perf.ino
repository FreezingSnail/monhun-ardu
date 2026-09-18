// Perf-bench carve (mirrors perf_test.hpp's MH_AUDIO=0): the bench scene is
// MON_LUNGE, which never runs a breakable-part path, so this image compiles the
// optional parts machinery out (MH_COMBAT_PARTS=0, see src/core/game.hpp and
// the PARTS_ENABLED gates) and keeps the flash headroom the full machinery
// needs. Shipping and test_combat keep it (test_parity carves it too: its
// fixtures also replay only the shipped 3). Measured costs are unchanged: the
// parts functions were never executed by this scene.
#define MH_COMBAT_PARTS 0
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
