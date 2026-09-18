// The parity fixtures replay MON_LUNGE/SWEEP/HEAVY plus the plain training
// pole. The pole scenes now resolve through the shared 3-hitzone code
// (bead monhun-ardu-6zb.6), so this image keeps the generated combat facts
// (MH_COMBAT_PARTS default 1). The shipped-3 guards are dist-only and their
// attacks single-window, so the extra machinery is behavior-identical.
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
