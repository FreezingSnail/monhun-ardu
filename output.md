# monhun-ardu-nch.1 — longtail: tail-spin + bite kit, window-shaped telegraph

Worker report. Epic monhun-ardu-nch. No commit/push by worker (orchestrator
commits). All acceptance commands run and green.

## What changed

- `data/creatures/heavy.json`: replaced `lunge`+`sweep` with `bite` (windup 30 /
  active 8 / recover 40, dmg 10, lunge speedF 26, track, window t[0,8]
  box(14,0,18,14)) and `tail_spin` (windup 42 / active 20 / recover 55, dmg 8,
  none, lock-at-windup, four contiguous 5-tick windows: w0 t[0,5]
  box(-20,0,24,16), w1 t[6,10] box(0,-22,16,24), w2 t[11,15] box(22,0,24,16),
  w3 t[16,20] box(0,22,16,24)). `zones.appendage.broken.disableAttacks`
  `["sweep"]` -> `["tail_spin"]`. Patterns (source order):
  `p_spin` {maxDist 24} -> tail_spin, `p_bite` {minDist 25, maxDist 255,
  hpBand [0,100]} -> bite.
- `src/core/combat.hpp`: added `COMBAT_FACING_TRACK=0` / `COMBAT_FACING_LOCK=1`
  (mirrors gen-combat FACINGS).
- `src/core/monster.hpp`: per-tick `m.fx/m.fy` recompute is skipped while the
  cached attack is locked (`g.combat.attack.facing == COMBAT_FACING_LOCK`) and
  the state is MS_WINDUP/MS_ATTACK; dist/di still computed for PURSUE. All
  track attacks (shipped lunge/sweep, heavy bite) unchanged.
- `src/render.hpp`: removed the fixed 32x24 `fxtelegraph` path and `spr::TELE_*`;
  the telegraph is now the cached window box drawn with `blk()` (windup shade1 +
  2x2 shade2 core, attack shade2 + 4x4 shade3 core, box centre = body centre +
  combatFaceOffset(win.box)). New `spr::SPIN_WEST/NORTH/EAST/SOUTH` and a
  `fxtail_spin` overlay drawn during MS_ATTACK of the locked attack: frame from
  the world direction of the window offset (|dx|>|dy| -> E/W else S/N), anchored
  at body centre - (12,12); the resting `tail_heavy` overlay is skipped while
  spinning.
- `tools/gen-art.py`: dropped the `telegraph` icon and its `art_dims`
  `telegraph_*` emission; added `tail_spin` (4x 24x24, anchor "body centre"),
  authored as the east tail rotated a quarter-turn CW about the frame centre
  (frames W/N/E/S).
- `mock/game.js`: added `bite`/`tailSpin` (windows[] path + `facing` lock) while
  keeping `lunge`/`sweep` value-identical (legacy reach/hw/hh path); heavy
  selects tailSpin at dist <= 24 else bite; mirrored the facing lock, the
  per-window hit test and the window telegraph. `monsterActiveWindow()` helper.
- Tests updated: `tst/combat_test.hpp`, `tst/fxdatatest/combat_test.hpp`,
  `tst/fxdatatest/monster_art_test.hpp` (spin overlay plane-2 tip-cap oracle),
  `tst/art_dims_test.hpp` (telegraph checks removed; new 24x24 world-direction
  sheet test), `tst/fxdatatest/asset_test.hpp` (telegraph blob checks removed;
  fxtail_spin header added), `tst/combat_pack_test.hpp` (record names/offsets),
  `tst/monster_test.hpp` (heavy split), `mock/game.test.js`.
- Regenerated (`make gen`): `fxdata/*`, `src/fxdata.h`, `src/generated/*`
  (combat blob 4->12 windows, art_dims, equip offsets).

## Acceptance evidence

### 1. Regen determinism
`make gen` x2, then `make gen-check`:
```
fxdata_manifest: PASS (64 generated artifacts unchanged)
gen-check exit=0
```
`src/fxdata.h == fxdata/fxdata.h` (gen-check `cmp` passed).

Parity fixture regen empty diff:
```
$ node tools/gen-parity-fixtures.js
wrote tst/fxdatatest/parity_fixtures.hpp
scenes=20 ticks=1269 snapshots=32 cpFields=20
$ git diff --stat tst/fxdatatest/parity_fixtures.hpp
(empty)
```
(20 legacy scenes only exercise MON_LUNGE + the manual sweep scene, so heavy's
kit swap does not move them.)

### 2. `make test`
```
Total Passed: 3793
Total Failed: 0
```

### 3. `make test-tools`
```
Ran 137 tests in 7.531s
OK
```

### 4. `FXTEST_ONLY=test_parity make fxtest-headless`
```
parity_test PASSED=660 FAILED=0
P
test_parity: PASS
```

### 5. Other device suites
```
test_combat       combat_test PASSED=232 FAILED=0      (C reads spawn=12 attack=5 guard=2 hit=0 tick256=0 simAtk=6 simTk=0 winSw=1)
test_monster_art  test_monster_art PASSED=27 FAILED=0
test_data         data_test PASSED=221 FAILED=0
test_menu_art     test_menu_art PASSED=60 FAILED=0
test_assets       asset_test PASSED=250 FAILED=0
```
`test_combat` now pins heavy bite/tail_spin (4 windows, facing track/lock) and
the heavy tail zone unlock mask -> tail_spin. `test_monster_art` pins the spin
overlay frame pick: west window -> plane-2 tip cap at (48,41), east -> (71,41),
north -> (59,30), south -> (59,52), and that the resting tail cap is skipped.

Mock suite (native node test, run manually): `node --test mock/game.test.js`
-> 25 tests, 25 pass, 0 fail.

### 6. `make size`
```
size: .text=27208 .data=66 .bss=1801
size: flash=27274/29696 (2422 free)  ram=1867/2560
size: data facts: HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false
  HAS_GUARD_HP:false HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true
  HAS_HIT_STAGGER:false HAS_MULTI_STEP:false HAS_MULTI_WINDOW:true
  HAS_SIMPLE_GUARDS:false HAS_STAGGER:true HAS_STEP_AFTER:false
  HAS_STEP_CHANCE:false HAS_WAIT_STEPS:false HAS_ZONES:true
```
Bead baseline 28242/29696 (1454 free) -> 27274/29696 (2422 free): **-968 B**
net. No `HAS_*` fact flipped; `HAS_MULTI_WINDOW` was already true (ravager), so
the 4-window spin adds data only. RAM +? baseline not recorded here; current
ram=1867/2560.

## Deviations / notes

- The spin overlay frame is smaller than the 40x28 heavy body, so during the
  spin the tail is always over the body sprite. The device oracle therefore
  shrinks the cached window box to 1x1 in the test-only setup: the frame pick
  only needs the face-relative offset, and a full-size telegraph attack box
  legally clears plane 2 under its shade-2 fill (which would hide the white tip
  cap). This is a test observation choice, not an engine change.
- `art_dims::monster_lunge_*` / `monster_sweep_*` constants remain (core table
  dims still consumed by `tst/art_dims_test.hpp`); only `telegraph_*` was
  removed.
- No float/double added; tests are permanent and co-located; no /tmp test code.

## Orchestrator review repair (post-worker)

Review found a mock-only defect: the windows-path telegraph fell through to the
legacy `a.reach` scalar during windup (countdown `t` overlaps window ticks) and
after the last window, producing NaN boxes in `drawMonster` (no tell drawn).
Fixed in `mock/game.js` with `monsterTellWindow()` mirroring the C++ RAM cache
(window 0 through windup, active window in attack, last window cached through
the attack tail) and `monsterHitsPlayer()` now returns false when a windows
attack has no active window (no NaN reach path). Added a permanent test in
`mock/game.test.js` ('window telegraph mirrors the C++ window cache').
Re-verified: `node --test mock/game.test.js` 26/26 pass; parity regen empty diff.
Full gate re-run on the unchanged C++/data/art state: gen-check PASS, host
3793/0, tools 137 OK, full fxtest-headless PASS, size 27274/29696 (2422 free).
