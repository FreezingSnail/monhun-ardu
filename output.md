# monhun-ardu-nch.2 — longtail spin: turn away from player, drop box telegraph

Worker report. Epic monhun-ardu-nch, follow-up to nch.1 (2916e6a). No
commit/push by worker (orchestrator commits). All acceptance commands run and
green.

## What changed

- `data/creatures/heavy.json`: `tail_spin.facing` `lock-at-windup` -> `lock-away`.
- `tools/gen-combat.py`: `FACINGS += {"lock-away": 2}`.
- `src/core/combat.hpp`: added `COMBAT_FACING_LOCK_AWAY = 2` and
  `combatFacingLockV(uint8_t)` (true for either lock mode).
- `src/core/monster.hpp`:
  - per-tick `m.fx/m.fy` recompute skipped for BOTH lock modes through
    `MS_WINDUP` + `MS_ATTACK` (`combatFacingLockV`).
  - new `monsterFacingWindup(g)`: a lock-away attack negates the just-computed
    tracked vector once (`m.fx=-m.fx`, `m.fy=-m.fy`); called right after
    `monsterAttackSet` in `patternStepsSingle` + `patternStepsGeneric`. Track and
    lock-at-windup attacks byte-identical.
  - lock-away window hit pushes the hunter radially along the beast->player
    `DIR8` (`fp::dir8X/Y(di)`); legacy/track keep the facing-vector push.
- `src/render.hpp`: removed both `blk()` window FILLS; telegraph is only the core
  marker at the cached window centre (windup 2x2 shade 2, attack 4x4 shade 3).
  `fxtail_spin` now draws during `MS_WINDUP` as well as `MS_ATTACK` for either
  lock mode (frame from the cached `win.box` world direction; windup caches
  window 0 so the tail points at the hunter); resting `tail_heavy` skipped while
  the spin tell is up.
- `mock/game.js`: `tailSpin.facing='lock-away'`; `lockFace` covers both lock
  modes; `chooseAttack` negates `m.face` once on lock-away windup entry;
  `playerHit(g,dmg,m,knock)` takes a knock vector (radial `DIR8[di]` for
  lock-away, `m.face` otherwise); `drawMonster` drops the fill and keeps only
  the 2x2/4x4 core. Legacy lunge/sweep path untouched.
- Tests: `tools/tests/test_gen_combat.py` (unknown-facing rejection + lock-away
  encodes byte 2), `tst/combat_test.hpp` + `tst/fxdatatest/combat_test.hpp`
  (heavy spin decode == lock-away == 2), `tst/fxdatatest/monster_art_test.hpp`
  (windup spin overlay + no box fill regression), `tst/monster_test.hpp` (host
  turn-away/frozen-facing + radial knock), `mock/game.test.js` (turn-away,
  frozen facing, radial knock). `tst/art_dims_test.hpp` untouched — art dims did
  not shift.
- Docs: `docs/creature-framework.md` nch.1 section extended with lock-away +
  no-box core telegraph.
- Regenerated via `make gen`: `fxdata/tables/combat.bin`,
  `src/generated/combat_data.hpp` (tail_spin facing byte 1 -> 2),
  `src/generated/combat_expect.hpp` (sha), `fxdata/*`, `manifest.json`.

## Acceptance evidence

### 1. Regen determinism
`make gen` x2, then `make gen-check` -> exit 0:
```
fxdata_manifest: PASS (64 generated artifacts unchanged)
gen-check exit=0
```
`src/fxdata.h == fxdata/fxdata.h` (gen-check `cmp`).

Parity fixture regen empty diff:
```
$ node tools/gen-parity-fixtures.js
wrote tst/fxdatatest/parity_fixtures.hpp
scenes=20 ticks=1269 snapshots=32 cpFields=20
$ git diff --stat tst/fxdatatest/parity_fixtures.hpp
(empty)
```

### 2. `make test`
```
Total Passed: 3802
Total Failed: 0
```
(baseline 3793, +9 new assertions.)

### 3. `make test-tools`
```
Ran 139 tests in 7.038s

OK
```
(baseline 137, +2.)

### 4. `node --test mock/game.test.js`
```
ℹ tests 27
ℹ pass 27
ℹ fail 0
```
(baseline 26.)

### 5. Targeted device suites
```
FXTEST_ONLY=test_parity make fxtest-headless
  parity_test PASSED=660 FAILED=0
  test_parity: PASS

FXTEST_ONLY=test_combat
  C reads spawn=12 attack=5 guard=2 hit=0 tick256=0 simAtk=6 simTk=0 winSw=1
  combat_test PASSED=233 FAILED=0      (baseline 232)

FXTEST_ONLY=test_monster_art
  test_monster_art PASSED=31 FAILED=0  (baseline 27)

FXTEST_ONLY=test_data
  data_test PASSED=221 FAILED=0

FXTEST_ONLY=test_menu_art
  test_menu_art PASSED=60 FAILED=0
```
Full `make fxtest-headless` NOT run (orchestrator owns the full gate).

The `test_monster_art` windup oracle renders the FULL window 1 box (x52..67,
y8..31) on all 3 triplanes and asserts the x52..67 y10..17 region has 0 ink
(no fill), plus the windup north spin cap at (59,30) and south cap clear.

### 6. `make size`
```
size: .text=27234 .data=66 .bss=1801
size: flash=27300/29696 (2396 free)  ram=1867/2560
size: data facts: HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false
  HAS_GUARD_HP:false HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true
  HAS_HIT_STAGGER:false HAS_MULTI_STEP:false HAS_MULTI_WINDOW:true
  HAS_SIMPLE_GUARDS:false HAS_STAGGER:true HAS_STEP_AFTER:false
  HAS_STEP_CHANCE:false HAS_WAIT_STEPS:false HAS_ZONES:true
```
Baseline 27274/29696 (2422 free) -> 27300/29696 (2396 free): **+26 B**; RAM
unchanged at 1867/2560. No `HAS_*` fact flipped.

## Deviations / notes

- Added two permanent host behavior tests to `tst/monster_test.hpp` (not in the
  design file list) so acceptance #2 (turn-away, frozen through windup+attack,
  radial knock) is pinned in the shipping C++ engine, not only the mock.
- `tst/art_dims_test.hpp` was left unchanged: no art/dims moved in nch.2.
- The `test_monster_art` no-fill oracle iterates planes 0..2 (L4_Triplane has 3
  planes); an initial 0..3 loop asked `renderMonster` for a nonexistent plane 3
  and hung the harness. Fixed to 0..2.
- No float/double added; tests permanent and co-located; no /tmp; parity regen
  empty; no commit/push.
