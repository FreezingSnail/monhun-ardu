// The parity fixtures replay MON_LUNGE/SWEEP/HEAVY plus the plain training
// pole. The pole scenes now resolve through the shared 3-hitzone code
// (bead monhun-ardu-6zb.6), so this image keeps the generated combat facts
// (MH_COMBAT_PARTS default 1). The shipped-3 guards are dist-only and their
// attacks single-window, so the extra machinery is behavior-identical.
//
// Budget carve (udb): no fixture scene stows the weapon, so the sheathe input
// path folds out of this image to keep it inside the 29696 B board budget
// (MH_B_BRANCH_BUFFER stays on: the A-then-B scenes DO queue through the lock).
// Host tests (tst/player_test.hpp) cover sheathing (hold B + double-tap Down);
// the parity hash fields are still mirrored and read (sheathed is always false).
#define MH_SHEATHE 0

// Roll-attack carve (8xx): no fixture scene rolls into an A press or presses
// direction+A, so that evade->A branch and the alt table selection fold out of
// this image too. Host tests (tst/player_test.hpp) cover both; the parity hash
// fields are unaffected.
#define MH_ROLL_ALT 0

// Stage-3 finisher carve (7pw): no fixture scene B-taps after a finisher, so
// the finWin writes / idle stage-3 mapping / inLock extension fold out; the
// host suite covers the finisher path.
#define MH_STAGE3 0

// Charge carve (ynb): no fixture scene holds A past a swing, so the A-hold
// counter, PS_CHARGE entry and the charge release fold out of this image; the
// host suite (tst/player_test.hpp) covers the flail/gun charge paths.
#define MH_CHARGE 0

// Push-rule carve (bug fix 2026-09-19): no fixture scene walks the hunter into
// the beast (beast_no_shove_idle keeps the hunter still), so the per-tick
// player-move flag folds out of this image, which is at the board size limit.
// The carved path keeps the pre-fix give-way rule; host + mock tests cover the
// walking-into-beast case.
#define MH_PUSH_MOVE 0

// Active-room-bounds carve (monhun-ardu-fie.4): this image is at the board
// flash limit, so the room runtime (roomW/roomH clamps, camera clamp,
// projectile cull, door/heal/hold-B logic) folds back to the legacy WORLD_W/H
// constants. Every fixture extent equals WORLD_W/H and no scene calls loadRoom,
// so behavior is byte-identical. Shipping/perf keep the runtime room bounds.
#define MH_ROOM_BOUNDS 0

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
