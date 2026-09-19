# monhun-ardu-u14 — device: reclaim flash budget (lunge/attack-start factoring)

Baseline HEAD 1e46537 (clean tree; only a pre-existing untracked
`recording_20260918184558.gif`). Refactor-only, no commit/push.

## What changed

`src/core/player.hpp` only (33 insertions, 42 deletions):

- Added `static void applyLunge(Player&, const Attack*)` — the repeated
  `if (a.lunge) { p.vx = (p.fx*lunge)>>4; p.vy = (p.fy*lunge)>>4; }` block.
  Used in `startAttack`, `startChargeAttack`, `startRollAttack`, and the
  `tryBranch` attack entry.
- Added `static void beginAttack(Game&, const Attack*)` — shared attack-entry
  tail: stamina pay with the existing `(stam >= p.stam) ? 0 : p.stam-stam`
  clamp, then `PS_ATTACK` / `atk` / `t=0` / `hitDone=false`. Used in the same
  four sites. Each caller keeps its own guards and state clears explicit.
  (`tryBranch` keeps `atkStam` for its `<` guard; `beginAttack` recomputes the
  same `attackStam(atk)`, identical value after the guard.)
- `startRollAttack` keeps its explicit `chain/chainWin/chainLock` clears and
  deliberately does **not** clear `finWin` (mock's `startRollAttack` doesn't).
- Removed the redundant `p.chargeArmed = false;` inside the `PS_CHARGE` case:
  the top-of-tick `if (aR) p.chargeArmed = false;` already runs on the same
  `aR` edge in every state, so the second write was idempotent. Comment left.
- No changes to hashed fields' values; `finWin` retained as a field.

## Size delta breakdown (whole-image, measured by reverting each piece)

`make size` shipping (MH_SHEATHE/ROLL_ALT/STAGE3/CHARGE=0 carve = default):

| refactor | flash delta | kept |
|---|---|---|
| `applyLunge` helper (4 sites) | **-200 B** | yes |
| `beginAttack` helper (4 sites) | **-108 B** | yes |
| duplicate `chargeArmed` clear removal | **-2 B** | yes |
| **total** | **-310 B** | |
| `clearChain` helper (5 sites) | 0 B | reverted |
| charge `if (x<255) x++` saturation (chargeT/aHold) | **+14 B** (cost) | reverted |
| `startup+active` cached as `activeEnd` | 0 B | reverted |

The three rejected pieces were measured by building with only that piece
reverted; none helped, so all were dropped. Only the three kept pieces remain.

## Verification (exact)

### make test
Baseline 4981 / 0; after **4981 / 0** (`Total Passed: 4981`, `Total Failed: 0`).

### make size (shipping, before -> after)
```
BEFORE: size: .text=27224 .data=40 .bss=1719
        size: flash=27264/29696 (2432 free)  ram=1759/2560
AFTER : size: .text=26914 .data=40 .bss=1719
        size: flash=26954/29696 (2742 free)  ram=1759/2560
```
Delta: flash **-310 B** (free 2432 -> 2742), ram **+/-0 B**. All `HAS_*` data
facts unchanged.

### parity fixtures regen
```
node tools/gen-parity-fixtures.js
wrote tst/fxdatatest/parity_fixtures.hpp
scenes=20 ticks=1269 snapshots=32 cpFields=20
git diff --stat tst/fxdatatest/parity_fixtures.hpp   -> EMPTY
```

### make fxtest-headless FXTEST_ONLY=test_parity
```
Sketch uses 29618 bytes (99%) of program storage space. Maximum is 29696 bytes.
Global variables use 1816 bytes (70%) of dynamic memory, leaving 744 bytes ...
parity_test PASSED=660 FAILED=0
test_parity: PASS
```
Baseline re-measured at 29598 (via `git stash` of player.hpp): test_parity grew
**+20 B** (29598 -> 29618, 78 B free) even though shipping shrank; still passes
660/0. The growth comes from LTO codegen in the test build config
(CHARGE_ENABLED folds the charge micros out entirely there), not from behavior.

### node --test mock/game.test.js
Baseline 69 / 0; after **tests 69 / pass 69 / fail 0**.

### make gen-check
```
fxdata_manifest: PASS (68 generated artifacts unchanged)
```
No generated changes (only `src/core/player.hpp` modified).

## Notes / deviations

- `clearChain` was specified in the bead but measured 0 B, so it was not kept
  (the repeated clears stay inline, exact per-site).
- The requested charge saturation micros measured **+14 B** (bigger), so the
  original ternary clamp is retained; only the duplicate clear removal (the
  other charge micro) was kept.
- Behavior untouched: parity fixtures byte-identical and test_parity 660/0
  under the existing carves.
