# monhun-ardu-7pw — device: stage-3 finisher branch (A A A then B)

Baseline aca88e5. No git commit/push (orchestrator commits).

## What changed

- `src/core/game.hpp`
  - `WeaponDef::branches[2] -> branches[3]` (appended in place; region up to
    `branches` unchanged, everything after shifts +27).
  - Host stage-3 branch values, exactly from mock `WEAPON_DEFS`:
    - sword `helmsplit` 8/4/20 26d reach16 hw20 hh22 stam18
    - flail `earthslam` 10/6/24 32d reach24 hw32 hh24 stam24 trip push12
    - gun `cannonblast` 6/3/20 30d reach16 hw24 hh18 stam16 push16
  - Mock names have no device `AtkId`; stage-3 branches carry `ATK_NONE`
    (same convention as roll/alt from 8xx), so the sword slash sheet does not
    gain a frame and render slot math is untouched.
  - AVR static_assert `WeaponDef` 226 -> 253; packed blob 678 -> 759 B.
  - `MH_STAGE3` carve (default 1) + `STAGE3_ENABLED`.
  - `Player::finWin` bool (init false).
- `src/core/player.hpp`
  - `finWin` set at attack completion when `chain >= 2` (before the reset),
    cleared on window expiry, on branch fire (stance + attack), at `startAttack`,
    and on special completion. `trySheathe` / `startRollAttack` untouched
    (mock does not clear there).
  - `tryBranch` idle stage: `(STAGE3_ENABLED && finWin) ? 3 : chain`; branch
    scan bound `STAGE3_ENABLED ? 3 : 2`.
  - B buffer inLock: `chainLock > 0 && (chain > 0 || (STAGE3_ENABLED && finWin))`.
- `tools/gen-fxtables.cpp`: putWeapon loops 3 branches, require 253,
  `WEAPON_DEFS_BYTES` 678 -> 759.
- `tools/fxdump.cpp`: branch dump loop 2 -> 3.
- `tst/fxdatatest/data_test.hpp`: WeaponDef 253, strides 253/506, branch stride
  still 27 (+ `branches[2]` stride), offsets after branches +27
  (`canCancel` 176, `shells` 177, `roll` 207, `alt` 230), plus full stage-3
  value assertions per weapon.
- `tst/fxdatatest/test_parity.ino`: `#define MH_STAGE3 0` + one-line comment.
- `tst/player_test.hpp`: stage-3 table entries + two host tests mirroring
  mock/game.test.js under HEAVY (finisher branch per weapon via buffered B;
  finWin armed/cleared on expiry, on branch use, and by a fresh attack).

## Budget carve note (extra beyond the listed carve)

With only `MH_STAGE3=0`, test_parity built at 29700 B (4 over the 29696 board).
`tryBranch`'s scan still ran the 3rd branch entry during stage-3 recovery.
Added `branchCount = STAGE3_ENABLED ? 3 : 2` so the stage-3 scan folds out of
the parity image; test_parity dropped to 29626 B and the recovery path is
byte-identical to pre-7pw behavior for that image.

## Generated

`make gen` was run twice: the first pass left `fxdata/tables/equip.bin` /
`src/generated/equip_meta.hpp` baked against the previous image, so a second
`make gen` was required to settle the equip offsets (as anticipated).
`make gen-check` then PASSes.

Also regenerated: `fxdata/tables/weapondefs.bin` 678 -> 759 B,
`fxdata/fxdata*.bin`, `fxdata/fxdata.h`, `src/fxdata.h`,
`src/generated/art_dims.hpp` (new `*_branch2_*` constants; no authored art PNG
changed because stage-3 uses `ATK_NONE`).

## Verification

`make test`:
```
Total Passed: 4929
Total Failed: 0
```
New suites:
```
---------- stage-3 finisher: A A A then B runs the finisher branch (all weapons) ----------
Passed: 30
Failed: 0
---------- stage-3 finWin: cleared on window expiry and by a fresh attack ----------
Passed: 13
Failed: 0
```

`make gen` then `make gen-check` (second `make gen` pass needed to settle equip):
```
fxdata_manifest: PASS (68 generated artifacts unchanged)
```

`node tools/gen-parity-fixtures.js` + diff:
```
wrote tst/fxdatatest/parity_fixtures.hpp
scenes=20 ticks=1269 snapshots=32 cpFields=20
git diff --stat tst/fxdatatest/parity_fixtures.hpp   # (empty)
```

`make fxtest-headless FXTEST_ONLY=test_parity`:
```
Sketch uses 29626 bytes (99%) of program storage space. Maximum is 29696 bytes.
Global variables use 1812 bytes (70%) of dynamic memory, leaving 748 bytes for local variables. Maximum is 2560 bytes.
parity_test PASSED=660 FAILED=0
test_parity: PASS
```
(baseline stated ~74 B free -> 29622 B; now 29626 B, 70 B free, +4 B.)

`make fxtest-headless FXTEST_ONLY=test_data`:
```
Sketch uses 22070 bytes (74%) of program storage space. Maximum is 29696 bytes.
Global variables use 1179 bytes (46%) of dynamic memory, leaving 1381 bytes for local variables. Maximum is 2560 bytes.
data_test PASSED=326 FAILED=0
test_data: PASS
```

`make fxtest-headless FXTEST_ONLY=test_player_art`:
```
Sketch uses 13906 bytes (46%) of program storage space. Maximum is 29696 bytes.
Global variables use 2222 bytes (86%) of dynamic memory, leaving 338 bytes for local variables. Maximum is 2560 bytes.
test_player_art PASSED=111 FAILED=0
test_player_art: PASS
```

`node --test mock/game.test.js`:
```
tests 69
pass 69
fail 0
```

`make size` (shipping, MH_STAGE3=1):
```
size: .text=26620 .data=40 .bss=1715
size: flash=26660/29696 (3036 free)  ram=1755/2560
```
vs baseline flash=26522 (3174 free) / ram=1754: **flash +138 B, ram +1 B**
(the finWin bool). HAS_* data facts unchanged.

## Blockers

None.
