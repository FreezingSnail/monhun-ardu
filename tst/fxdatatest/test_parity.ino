// The parity fixtures replay MON_LUNGE/SWEEP/HEAVY plus the plain training
// pole. The pole scenes now resolve through the shared 3-hitzone code
// (bead monhun-ardu-6zb.6), so this image keeps the generated combat facts
// (MH_COMBAT_PARTS default 1). The shipped-3 guards are dist-only and their
// attacks single-window, so the extra machinery is behavior-identical.
//
// Budget carve (udb): no fixture scene stows the weapon, so the sheathe input
// path folds out of this image to keep it inside the 29696 B board budget
// (MH_B_BRANCH_BUFFER stays on: the A-then-B scenes DO queue through the lock).
// Host tests (tst/player_test.hpp) cover sheathing; the parity hash fields are
// still mirrored and read (sheathed is always false here).
#define MH_SHEATHE 0

#include "harness/fx_globals.hpp"
#include "parity_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    parity::test_parity(test);
    test.report(F("parity_test"));
}
void loop() {
    exit(0);
}
