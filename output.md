# monhun-ardu-nch.10 — bull: attack art overlay (stomp + gore)

Status: **PASS** (not committed; orchestrator commits)

## What changed

- `tools/gen-art.py`: new `_bull_attack_east` stomp/gore poses + `bullatk_frames()`
  ([stomp E, stomp W, gore E, gore W], authored east, `_beast_frame` west mirror) and a
  `bullatk` icon_defs entry (32x24, 4 frames, body top-left anchor). `make gen`
  emits `fxbullatk`, `images/blocks/fxbullatk_32x24.png`, and
  `art_dims::bullatk_frame_w/h/frames`.
  - stomp: rear legs planted, both front hooves raised and tucked back over the
    chest, body pitched forward (rear-low barrel + raised front shoulder), head/horns
    held high for the slam windup.
  - gore: head lowered, horns driven forward along the facing edge, body leaned
    1 px forward, tail raised.
- `src/render.hpp` `drawMonster`: when `monsterKind == MON_SWEEP` and state is
  `MS_WINDUP`/`MS_ATTACK` and `atkIdx != COMBAT_NO_ATTACK`, draw `fxbullatk` instead
  of the `BEAST_POSES` beast frame. Ordinal = `m.atkIdx -
  combatCreatureFirstAttack(combat::CREATURE_SWEEP)` (0 = stomp, 1 = gore, following
  the JSON attack order stomp/gore); frame = `(ordinal << 1) | (m.fx < 0 ? 1 : 0)`.
  Windup/attack share the pose (no windup-flash frame; tell + telegraph carry timing,
  fxtailspin/fxchickenatk trade). No hit-test/window/cart change beyond the frame pick.
- Host oracle `tst/art_dims_test.hpp`: `testBullAttackSheet` (frame layout, stomp/gore
  signatures, exact west mirror, all 4 frames distinct).
- Device tests: `tst/fxdatatest/asset_test.hpp` blobHeader `fxbullatk` (32x24);
  `tst/fxdatatest/monster_art_test.hpp` `setupBullAttack` + frame-pick checks
  (stomp/gore E/W, windup+attack).
- Generated sets regenerated: `images/blocks/fxbullatk_32x24.png`, `fxdata/**`,
  `src/fxdata.h`, `src/generated/art_dims.hpp`, `src/generated/equip_meta.hpp`
  (equip sheet offsets shift with the new blob; static_asserts pin them).

Cosmetic only: `mock/`, `data/creatures/`, sim logic untouched. Parity fixtures
byte-identical (empty diff after regen). No `git add`/commit.

## Budget (flash-gated)

Baseline HEAD `e2e34c6` (re-measured, matches the task):

```
size: flash=27042/29696 (2654 free)  ram=1759/2560
size: data facts: HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false HAS_GUARD_HP:false HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true HAS_HIT_STAGGER:false HAS_MULTI_STEP:false HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false HAS_STAGGER:true HAS_STEP_AFTER:false HAS_STEP_CHANCE:false HAS_WAIT_STEPS:false HAS_ZONES:true
```

After:

```
size: .text=27126 .data=40 .bss=1719
size: flash=27166/29696 (2530 free)  ram=1759/2560
size: data facts: HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false HAS_GUARD_HP:false HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true HAS_HIT_STAGGER:false HAS_MULTI_STEP:false HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false HAS_STAGGER:true HAS_STEP_AFTER:false HAS_STEP_CHANCE:false HAS_WAIT_STEPS:false HAS_ZONES:true
```

- **Flash delta: +124 B** (27042 -> 27166; free 2654 -> 2530). Fits with 2530 B free.
- **RAM delta: 0 B** (1759/2560, 801 free).
- **No `HAS_*` fact flipped.**
- FX cart blob grew (not MCU flash): `FX_DATA_BYTES` 177165 -> 179471 (+2306;
  4 frames x 32x24 plus-mask = 2304 B + 2 B header). Rides `fxdata.bin`, headroom ample.

## Verification (exact tails)

`make gen-check`:

```
fxdata_manifest: fxdata/manifest.json up to date (44 images, 59 inputs, 17 outputs)
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (70 generated artifacts unchanged)
```

`make test`:

```
---------- bull attack overlay is a 4-frame stomp/gore mirror sheet ----------
Passed: 27
Failed: 0
...
========== Total Counts ==========
Total Passed: 5198
Total Failed: 0
```

`make test-tools`:

```
----------------------------------------------------------------------
Ran 152 tests in 8.584s

OK
```

`make fxtest-headless FXTEST_ONLY=test_monster_art`:

```
=== test_monster_art ===
test_monster_art PASSED=69 FAILED=0
P
test_monster_art: PASS
```

`make fxtest-headless FXTEST_ONLY=test_parity` (fixtures unchanged):

```
=== test_parity ===
parity_test PASSED=660 FAILED=0
P
test_parity: PASS
```

`node tools/gen-parity-fixtures.js` + `git diff --stat tst/fxdatatest/parity_fixtures.hpp`:

```
wrote tst/fxdatatest/parity_fixtures.hpp
scenes=20 ticks=1269 snapshots=32 cpFields=20
(no diff output -> empty diff)
```

Extra (device sheet-metadata suite touched): `make fxtest-headless FXTEST_ONLY=test_assets`:

```
=== test_assets ===
asset_test PASSED=262 FAILED=0
P
test_assets: PASS
```

## Deviations / notes

- `make gen` must run twice on a fresh sheet insert: the first pass emits the new
  fxdata blob but `gen-equipment` still bakes the previous `mh_body_base` offset; the
  second pass updates `fxdata/tables/equip.bin` + `equip_meta.hpp` (the nch.8 snapshot
  race, not nondeterminism). Final `make gen-check` passes with no diff.
- Art test depth mirrors nch.8 (host pixel oracle + device blobHeader + device frame
  pick), which is more than bare sheet-metadata headers; it pins the new render branch
  permanently.
- Unrelated pre-existing dirty files (`.gitignore`, `docs/dev-flow.md`, `.github/`,
  `recording_20260918184558.gif`, `tools/package-arduboy.py`,
  `tools/tests/test_package_arduboy.py`) left untouched, unstaged, unreverted.
