// Perf/parity-bench carve (mirrors test_perf.ino and perf_test.hpp's
// MH_AUDIO=0): the parity fixtures replay only MON_LUNGE/SWEEP/HEAVY, which
// never run a zone/multi-window/stagger/zones-guard path, so this
// image compiles the ravager machinery out (MH_COMBAT_PARTS=0, see
// src/core/game.hpp) and keeps the flash headroom the full machinery needs.
// The shipped-3 guards are dist-only and their attacks single-window, so the
// folded paths are behavior-identical; test_combat keeps the full machinery.
#define MH_COMBAT_PARTS 0
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
