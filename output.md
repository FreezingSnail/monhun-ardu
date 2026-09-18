# monhun-ardu-px5 — FRAME()/FRAMESHIFT() precedence + retire flat workaround

Worker report. No commit/push/`git add` performed.

## What changed

- `src/common.hpp`: parenthesized both macros —
  `#define FRAME(x) ((x) * 3 + arduboy.currentPlane())` and
  `#define FRAMESHIFT(x) ((x) + (2097152 * arduboy.currentPlane()))`.
  `FRAMESHIFT` has no call sites; existing `FRAME` call sites all pass constant
  or variable args, so no rendering change from the parenthesization alone.
- `data/equipment/gun_guard.json`: single record now covers the whole 3-frame
  `fxguard` sheet — `frames: 3`, `poseMap: { "guard": 1, "shove": 2, "idle": 0 }`
  (was `frames: 1`, `poseMap: {idle:0}`).
- `data/equipment/gun_guard_white.json`: **deleted** (retired flat workaround).
- `data/equipment/gun_shove.json`: **deleted**; shove is now frame 2 of the same
  `gun_guard` record (`poseMap.shove`), since the requested poseMap is
  `{guard:1, shove:2, idle:0}` on the single record.
- `src/render.hpp`:
  - `PartRec` drops the `flat` byte (ABI now sheet u24 + anchorX i8 + anchorY i8
    + frame[12] = 17 B); `static_assert(sizeof(PartRec) == equip::PART_SIZE)`
    still holds (17).
  - `partDraw` always applies `FRAME(fr)` — no flat branch.
  - Gun: `partDraw(PART_GUN_GUARD, stance==ST_GUARD ? POSE_GUARD : POSE_IDLE,
    shx, shy)`. Shove: `partDraw(PART_GUN_GUARD, POSE_SHOVE, shx2, shy2)` with a
    `+1` on `shx2` — the shove plate is drawn 1 px left inside its 12x16 cell
    (old anchor [5,8] vs the guard frame's [6,8]), so the reference compensates
    and the shove pixels are unchanged.
- `tools/gen-equipment.py`: removed the `flat` key (schema, validation, part
  dict), the packed `flat` byte, and `PART_FLAT_OFF`; `PART_SIZE` 18 -> 17,
  `PART_FRAME_OFF` 6 -> 5; docstring updated. Two-pass note + AVR stale-blob
  static_assert unchanged.
- `tools/tests/test_gen_equipment.py`: part-view expectations moved to the 17 B
  layout (offsets 103/120/124, blob 130 B); `test_gen_art_honours_flat_key`
  replaced by `test_gen_art_flat_key_rejected` (`unknown key 'flat'`).
- `tst/fxdatatest/player_art_test.hpp`: regenerated via the documented
  `PRINT_GOLDENS=true` path; regen history comment added.

## Golden diff (regen path: `PRINT_GOLDENS=true` + `FXTEST_ONLY=test_player_art`)

Old vs new GOLDEN array, all 37 cases x 3 planes compared:

```
changed indices: 29
```

Index **29 only** = case `{W_GUN, PS_IDLE, ST_GUARD, ...}` (gun idle + guard
stance). Old `{e56a836f, 1d4a306f, 1d4a306f}` (flat guard-white workaround
blitted the white plate's raw frame on every plane) -> new
`{30469935, 51a89135, 51a89135}` (per-plane stride). Every other case
(including gun shove, case 32) is byte-identical.

## Verification (exact tails)

1. `make gen` x2 -> `make gen-check`
   `fxdata_manifest: PASS (45 generated artifacts unchanged)`
2. `make test` -> `Total Passed: 3119` / `Total Failed: 0`
   `make test-tools` -> `Ran 73 tests in 4.449s` / `OK`
3. `make fxtest-headless` (full, all 10 suites):
   - assets `254/0`, audio `14/0`, boot `4/0`, combat `195/0`, data `221/0`,
     hud `17/0`, menu `59/0`, `parity_test PASSED=660 FAILED=0`
   - perf `B pUs=6562 pHz=152 lHz=50 lTk=984 rMx=5472 rAv=4974 ram=411` -> 5/5
   - `test_player_art PASSED=111 FAILED=0` (37 cases x 3 planes; 1 golden
     entry regenerated)
4. `make build` + `make size`
   `size: .text=26938 .data=58 .bss=1960`
   `size: flash=26996/29696 (2700 free)  ram=2018/2560`
   flash delta vs **26998 baseline: -2 B**. Parity-fixture regen: empty diff.
   `SHEET_OFF_FXGUARD` + AVR static_assert still present and passing.

## Deviations / notes

- **`gun_shove.json` deleted** (not named in the issue's delete list) so that the
  requested poseMap `{guard:1, shove:2, idle:0}` is the single record that owns
  all three `fxguard` frames. Shove is drawn from `gun_guard` with a `+1`
  reference offset; the shove golden (case 32) is unchanged.
- Flash is 26996, only 2 B under the 26998 baseline (2700 B free vs the 29696
  cap). Keeping `gun_shove` as its own record instead would have measured lower,
  but contradicts the "single `gun_guard` record" scope.
- `FRAME` parenthesization is correctness/cleanup here: no current call site
  passed a ternary, so the intentional pixel change comes from the data/poseMap
  fix, not the macro.
