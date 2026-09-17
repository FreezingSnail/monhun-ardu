// Ardens device test: combat blob loader on the real FX cart (ljj.2).
// MH_FX_READ_COUNT enables the mhFxRead* access counter in src/core/fxmem.hpp
// so combat_test.hpp can gate the cache model (spawn burst, per-tick zero).
#define MH_FX_READ_COUNT
#include "harness/fx_globals.hpp"
#include "combat_test.hpp"

namespace mh {
uint16_t mhFxReadCount = 0;
}

void setup() {
    fxTestSetup();
    arduboy.startGray();   // the loader reads bracket on waitForNextPlane
    FxTest test;
    combatcheck::test_combat(test);
    test.report(F("combat_test"));
}
void loop() {
    exit(0);
}
