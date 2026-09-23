# monhun-ardu-z5i — gun shove thrusts the shield forward (feel.24)

## What changed

`src/render.hpp` (gunshield branch of `drawPlayer`)

- Before: the base guard/idle plate was drawn every tick AND the POSE_SHOVE
  plate at a static forward offset `(p.fx * 4) >> 4` (+1 cell compensation), so
  the bash read as a wide static plate for all 10 shove ticks.
- After: during `PS_SHOVE` only the POSE_SHOVE plate is drawn, at a forward
  offset that retracts over the shove. `p.t` counts 10 -> 1 (PS_SHOVE case in
  `player.hpp`), so `thrust = 4 + (p.t * 6) / 10` starts at 10 px (E facing) on
  the first tick and retracts to 4 px + 1 by the last. The +1 cell compensation
  is kept (the shove frame is drawn 1 px left inside its cell, anchor 5 vs the
  idle/guard 6).
- No sim change: the 38 px reach check, push 10, stamina and state timing are
  untouched.

`tst/fxdatatest/player_art_test.hpp`

- Regenerated exactly one oracle golden: matrix index 32 = `{W_GUN, PS_SHOVE,
  ST_NONE, E}` (the row the bead text called "case 33"; 33 is gun-special W).
  New hashes `{0x4f202fb3, 0x670510b3, 0x670510b3}` — `p.t == 0` in the oracle,
  the fully retracted 4 px pose with the guard/idle plate gone. Regen-history
  note added to the file header. Every other case byte-identical.

## Gate (exact)

1. `make test`: `Total Passed: 6298  Total Failed: 0`
2. `make fxtest-headless FXTEST_ONLY=test_player_art`: `PASSED=120 FAILED=0`
3. `make fxtest-headless`: 18/18 suites PASS
4. `make gen-check`: `fxdata_manifest: PASS (91 generated artifacts unchanged)`
5. `make size`: `size: flash=28920/29696 (776 free)  ram=1704/2560`
   — +46 B vs the 28874 (822 free) pre-change baseline.

Worker-run bead; orchestrator re-ran the full gate on the final tree.
