# monhun-ardu-8xx — device: roll attack + direction+A alt opener

Status: DONE. No commit/push (orchestrator commits between waves).

## Delivered

- `WeaponDef` gains `Attack roll;` + `Attack alt;` appended after `shells[2]`
  (host offsets preserved): AVR `static_assert(sizeof(WeaponDef) == 226)`,
  blob 180 -> 226 B/weapon, 540 -> 678 B total. Host table carries the exact
  mock `WEAPON_DEFS` values; accessors `weaponRoll(def)` / `weaponAlt(def)`.
- `src/core/player.hpp`:
  - `startRollAttack(g, def)` ported from `mock/game.js` (pays stam, sets
    `PS_ATTACK`, clears chain/chainWin/chainLock, applies lunge).
  - A handling order now: sheathed draw -> `PS_DODGE/PS_DEFLECT/PS_SHOVE` roll
    -> stance special -> normal/buffered attack. Roll branch and alt selection
    gated on `MH_ROLL_ALT` / `ROLL_ALT_ENABLED`.
  - `startAttack(g, def, alt = false)`: `alt && chain == 0` picks
    `weaponAlt(def)`, else the normal chain attack; applies the alt lunge
    (thrust 20). Draw call site stays `alt = false` (mock parity).
- `tools/gen-fxtables.cpp`: `putWeapon` appends roll then alt; per-struct
  require 180 -> 226; `WEAPON_DEFS_BYTES` 540 -> 678. `fxdump.cpp` untouched.
- `tst/fxdatatest/test_parity.ino`: `#define MH_ROLL_ALT 0` (carve comment); no
  fixture scene rolls into A or presses direction+A.
- `tst/fxdatatest/data_test.hpp`: sizeof 226, weapon strides 226/452,
  `weapon.roll` off 180, `weapon.alt` off 203, plus roll/alt value assertions
  for all three weapons (device-only guard for the appended blob fields).
- `tst/player_test.hpp`: 5 new tests (3 roll moves w/ id+box+values + i-frame
  tick, gun bash lunge, thrust alt opener, mid-combo normal data, lock gate).
- All new moves use `id = ATK_NONE`; render slot math (`3 + atkId` / combo
  frames) unchanged.

## Numbers (exact tails)

- `make test`: `Total Passed: 4876` `Total Failed: 0` (new tests confirmed
  running: roll attack x2, direction + A x2, debounce lock x1).
- `make gen` then `make gen-check`: `fxdata_manifest: PASS (68 generated
  artifacts unchanged)`.
  Blob delta: `weapondefs.bin` 540 -> 678 B (+138);
  `fxdata/fxdata-data.bin` 174363 -> 174501 B (+138); `fxdata.bin` 174592 B
  (same size, table content shifted); `fxdata.h`/`src/fxdata.h` content moved.
- `node tools/gen-parity-fixtures.js`: `scenes=20 ticks=1269 snapshots=32`
  then `git diff --stat tst/fxdatatest/parity_fixtures.hpp` -> EMPTY.
- `make fxtest-headless FXTEST_ONLY=test_parity`:
  `parity_test PASSED=660 FAILED=0`; image `29622 bytes (99%)`,
  RAM `1811 bytes (70%)`.
- `make fxtest-headless FXTEST_ONLY=test_data`:
  `data_test PASSED=280 FAILED=0`; image `19552 bytes (65%)`, RAM `1179`.
- `make fxtest-headless FXTEST_ONLY=test_player_art`:
  `test_player_art PASSED=111 FAILED=0`; image `13948 bytes (46%)`, RAM `2221`
  (sanity: no render change).
- `node --test mock/game.test.js`: `tests 69` `pass 69` `fail 0`.
- `make size` (shipping): `.text=26482 .data=40 .bss=1714`;
  `flash=26522/29696 (3174 free)`, `ram=1754/2560`.
  Baseline was flash 26250 (3446 free), ram 1754: roll/alt path costs
  **+272 flash, +0 RAM**. Parity test image is 29622/29696 (74 free).

## Notes / deviations

- Mock `startAttack` guards on `def.alt` truthiness; every shipped `WeaponDef`
  carries an alt, so the C++ selection is `alt && chain == 0` (presence is
  unconditional). Documented in a code comment.
- Mock `startRollAttack` does **not** check `chainLock` (it clears it); the
  "no attack mid-lock" host test therefore covers the normal/alt *entry* gate
  (`startAttack` `p.chainLock > 0` early return), which is the only path where
  the lock applies. The roll path is reachable only from an evade and resets
  chain state, matching the mock.
- `make gen` needed **two passes** to fully settle after the blob-size change:
  `gen-equipment.py` runs before `fxdata-build.py`, so the first pass baked the
  previous sheet offsets (138 B low) into `equip.bin`/`equip_meta.hpp`; the
  second pass converged. After that `make gen-check` is stable and PASSes
  (verified twice). Orchestrator should run `make gen` twice if starting from a
  tree where the previous pass predates this size change.
- Generated set was staged together conceptually; no commit performed.
