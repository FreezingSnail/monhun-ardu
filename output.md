# monhun-ardu-nch.8 — chicken: attack art overlay (peck + leap)

Status: **PASS** (not committed; orchestrator commits)

## What changed

- `tools/gen-art.py`: new `_chicken_attack_east` peck/leap poses + `chickenatk_frames()`
  ([peck E, peck W, leap E, leap W], authored east, `_beast_frame` west mirror) and a
  `chickenatk` icon_defs entry (32x24, 4 frames, body top-left anchor). `make gen`
  emits `fxchickenatk`, `images/blocks/fxchickenatk_32x24.png`, and
  `art_dims::chickenatk_frame_w/h/frames`.
- `src/render.hpp` `drawMonster`: when `monsterKind == MON_LUNGE` and state is
  `MS_WINDUP`/`MS_ATTACK` and `atkIdx != COMBAT_NO_ATTACK`, draw `fxchickenatk` instead
  of the `BEAST_POSES` beast frame. Ordinal = `m.atkIdx -
  combatCreatureFirstAttack(combat::CREATURE_LUNGE)` (0 = peck, 1 = leap, following the
  authored attack order); frame = `(ordinal << 1) | (m.fx < 0 ? 1 : 0)`. Windup/attack
  share the pose (no windup-flash frame; tell + telegraph carry timing, fxtailspin
  trade). No hit-test/window/cart change beyond the frame pick.
- Host oracle `tst/art_dims_test.hpp`: `testChickenAttackSheet` (frame layout, peck/leap
  signatures, exact west mirror, all 4 frames distinct).
- Device tests: `tst/fxdatatest/asset_test.hpp` blobHeader `fxchickenatk` (32x24);
  `tst/fxdatatest/monster_art_test.hpp` `setupChickenAttack` + frame-pick checks
  (peck E/W, leap E/W, windup+attack).
- Generated sets regenerated: `images/blocks/fxchickenatk_32x24.png`, `fxdata/**`,
  `src/fxdata.h`, `src/generated/art_dims.hpp`, `src/generated/equip_meta.hpp`
  (equip sheet offsets shift with the new blob; static_asserts pin them).

Cosmetic only: `mock/`, `data/creatures/`, sim logic untouched. Parity fixtures
byte-identical (empty diff after regen). No `git add`/commit.

## Budget (flash-gated)

Baseline HEAD `d6a6ef7` (re-measured, matches the task):

```
size: flash=26954/29696 (2742 free)  ram=1759/2560
size: data facts: HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false HAS_GUARD_HP:false HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true HAS_HIT_STAGGER:false HAS_MULTI_STEP:false HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false HAS_STAGGER:true HAS_STEP_AFTER:false HAS_STEP_CHANCE:false HAS_WAIT_STEPS:false HAS_ZONES:true
```

After:

```
size: .text=27002 .data=40 .bss=1719
size: flash=27042/29696 (2654 free)  ram=1759/2560
size: data facts: HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false HAS_GUARD_HP:false HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true HAS_HIT_STAGGER:false HAS_MULTI_STEP:false HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false HAS_STAGGER:true HAS_STEP_AFTER:false HAS_STEP_CHANCE:false HAS_WAIT_STEPS:false HAS_ZONES:true
```

- **Flash delta: +88 B** (26954 -> 27042; free 2742 -> 2654). Fits with 2654 B free.
- **RAM delta: 0 B** (1759/2560, 801 free).
- **No `HAS_*` fact flipped.**
- FX cart blob grew (not MCU flash): `FX_DATA_BYTES` 174810 -> 177116 (+2306;
  4 frames x 32x24 plus-mask = 2304 B + 2 B header). Rides `fxdata.bin`, headroom ample.

## Verification (exact tails)

`make gen-check`:

```
fxdata_manifest: fxdata/manifest.json up to date (43 images, 59 inputs, 17 outputs)
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (69 generated artifacts unchanged)
```

`make test`:

```
---------- chicken attack overlay is a 4-frame peck/leap mirror sheet ----------
Passed: 27
Failed: 0
...
Total Passed: 5017
Total Failed: 0
```

`make test-tools`:

```
----------------------------------------------------------------------
Ran 152 tests in 8.714s

OK
```

`make fxtest-headless FXTEST_ONLY=test_monster_art`:

```
=== test_monster_art ===
test_monster_art PASSED=53 FAILED=0
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
asset_test PASSED=260 FAILED=0
P
test_assets: PASS
```

## Deviations / notes

- Design says `g.monsterKind == mh::MON_CHICKEN`; the roster enum has no `MON_CHICKEN` —
  the chicken is `MON_LUNGE` (SKELETON_CHICKEN, lunge.json). Used `mh::MON_LUNGE`.
- Peck "head shifted ~+5 px": the base chicken head/beak already reach the 32 px cell
  edge, so the literal shift is capped. The peck reads via a stretched neck, a head
  moved ~+4 px forward/down, and the beak driven down the facing edge; beak/wattle/eye
  stay in-cell and readable. The design's clipping trade for placeholder poses was not
  needed.
- `_beast_tone` maps `lo` to BLACK for the DARK idle body, so the peck beak/wattle/eye
  are the opaque black erasers of the base chicken art (readable against the white
  head) — host/device reds adjusted accordingly.
- Added a host art oracle plus lean device frame-pick checks (monster_art_test) beyond
  pure blob headers, so the new render branch has a permanent gate.
- Note: the first `make gen-check` after the first `make gen` reported stale
  equip/fxdata artifacts; consecutive `make gen`+hash runs are byte-identical and the
  re-run passes (snapshot race, not nondeterminism).
