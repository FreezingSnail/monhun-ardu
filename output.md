# monhun-ardu-prg.11 — trim: charge-lite + animation tells + branch/push carves

HEAD at start: `3d98de4` (prg.10 spike), clean tree. No commit/push
(orchestrator commits). Adopts the prg.10 measured set (a)+(b1)+(d1)+(d2).

## What changed

**(a) charge-lite** — single-level melee charge.
- `src/core/game.hpp`: removed `CHARGE_L2`; removed the `weaponChargeShell`
  accessor and `weaponHasChargeShells`; `weaponCharge` reads slot 0 only.
- `src/core/player.hpp`: `startChargeAttack` fires `weaponCharge(def, 0)`;
  removed `fireChargeShot`; PS_CHARGE release + stance entry gate on
  `weaponHasCharge(def)` only.
- `src/core/projectiles.hpp`: `spawnShot` accepts shot codes 1/2 only (removed
  the `shot >= 3` charged-ball branch).
- `src/render.hpp`: charge bar fills to `CHARGE_MIN` in shade 2 (no white L2
  state).
- The gun has no charge at all now (its only charge was the ball); the flail
  keeps `chargeslam1` (`charge[0]`, dmg 24). `chargeShells` stays as dead packed
  data (no accessor).

**(b1) tell → animation.**
- `src/render_math.hpp`: removed the shape geometry
  (`tellNeedsWindow`/`tellLineDash`/`tellRingHalf`/`tellArcSeg`/`tellRectOrigin`);
  added `TELL_WINDUP_NONE`, `TELL_FRAMES_AUTHORED` (0 in shipping),
  `tellHasAuthoredFrame`, `tellWindupFrame` — `combat.attack.tell` is now a
  windup animation-frame selector.
- `src/render.hpp`: removed `drawMonsterTell` + `tellOutline`; added
  `drawAttackMarker` (MS_ATTACK 4x4 shade-3; MS_WINDUP legacy 2x2 shade-2 core
  when the tell is unauthored). The chicken/bull windup pose path consumes
  `tellWindupFrame` (`tellSlot`); the heavy spin sheet keeps its locked-facing
  windup frame (the selector cannot index the 8-direction sheet) and falls back
  to the core marker.
- `src/core/combat.hpp`: `Tell` enum kept (values unchanged, now frame ids);
  comments updated.
- prg.12 authors the per-attack frames; unauthored tells fall back to the core
  marker.

**(c) branch/push carves.**
- `src/core/game.hpp`: `MH_B_BRANCH_BUFFER` default 1 → 0, `MH_PUSH_MOVE`
  default 1 → 0 (carves kept, mirroring MH_STAGE3/MH_ROLL_ALT).
- `Makefile`: `TEST_FLAGS` forces `-DMH_B_BRANCH_BUFFER=1 -DMH_PUSH_MOVE=1` for
  the host suite.

**(d) pins/docs.** `tst/render_math_test.hpp` (frame-selector pin),
`tst/fxdatatest/tell_test.hpp` (core-marker/attack-marker bytes + selector),
`tst/player_test.hpp` (charge-lite + gun-no-charge), `tst/shells_test.hpp`
(codes 3/4 inert), `tst/fxdatatest/data_test.hpp` (chargeslam1 only),
`tools/contact_sheet.py` (+`tell_class` window-class note) and
`tools/tests/test_contact_sheet.py`, `docs/feel-design.md`, `docs/dev-flow.md`,
`README.md`.

## Contact-sheet review (`tools/contact_sheet.py`, three beasts)

Rendered `build/review_{lunge,sweep,heavy}_prg11.png` + `review_all_prg11.png`.
`tell_class()` note per attack — pose/window-class match:

| beast | attack | tell | window class | match |
|---|---|---|---|---|
| lunge | peck | dot | core | generic coil / core marker (small forward jab) |
| lunge | leap | line | ray | forward lunge |
| lunge | wing_beat | arc | sweep | body-wide behind |
| sweep | stomp | ring | aoe | centred 36×26 slam |
| sweep | gore | line | ray | forward lunge |
| sweep | rear_kick | arc | sweep | behind |
| heavy | bite | line | ray | forward lunge |
| heavy | tail_spin | arc | sweep | 4-window rotation |
| heavy | tail_slam | ring | aoe | hop slam 36×28 |

All nine demo attacks name the class their hit window uses; every tell is
currently unauthored so the note is `FRAME-PRG12` and the core marker draws.

**Ring decision (reported cost).** The bull stomp's `ring` tell loses its
procedural AoE read. Keeping ONE small static window outline measured
**+144 B** (`make size` 28418, delta **−718 B**), which drops the reclaim under
the ≥800 B acceptance. prg.11 therefore takes the core-marker fallback and
prg.12 restores the stomp read with the authored windup pose (the designed path).

## Verification (exact tails)

`make gen` / `make gen-check` (no generated data changed; gen ran once inside
gen-check):
```
fxdata_manifest: PASS (86 generated artifacts unchanged)
```

`make test`:
```
Total Passed: 6020
Total Failed: 0
```
(6060 → 6020: the removed L2/charged-ball tests; live coverage unchanged.)

`make test-tools`:
```
Ran 251 tests in 15.487s

OK
```

`make fxtest-headless` (full, all suites PASS):
```
asset_test PASSED=270 FAILED=0
test_audio PASSED=10 FAILED=0
test_boot PASSED=4 FAILED=0
combat_test PASSED=237 FAILED=0
data_test PASSED=343 FAILED=0
test_hub PASSED=63 FAILED=0
test_hud PASSED=25 FAILED=0
test_items PASSED=35 FAILED=0
test_menu_art PASSED=53 FAILED=0
menu_test PASSED=60 FAILED=0
test_monster_art PASSED=111 FAILED=0
perf_test PASSED=5 FAILED=0
test_player_art PASSED=111 FAILED=0
test_quests PASSED=50 FAILED=0
test_screens PASSED=85 FAILED=0
test_smith PASSED=70 FAILED=0
test_tell PASSED=14 FAILED=0
zones_test PASSED=80 FAILED=0
```
`test_perf` line:
```
B pUs=6348 pHz=157 lHz=52 lTk=480 rMx=3348 rAv=3075 ram=710
perf_test PASSED=5 FAILED=0
```
(rMx 3344 → 3348, rAv 3074 → 3075: noise; pHz 157≥135, lHz 52≥45, ram 710≥300.)

`make size` (baseline prg.10: flash 29136/29696, 560 free; ram 1610/2560):
```
size: .text=28254 .data=20 .bss=1590
size: flash=28274/29696 (1422 free)  ram=1610/2560
```
**Reclaimed: 862 B flash (−2.96%), 0 B RAM.** Target ≥800 B met (without the
ring outline; +144 B if kept, see above).

## Interfaces

- `mh::tellWindupFrame(uint8_t tell, uint8_t authored) -> uint8_t` — bespoke
  windup frame index or `TELL_WINDUP_NONE` (0xFF).
- `mh::tellHasAuthoredFrame(uint8_t tell, uint8_t authored) -> bool`.
- `mh::TELL_WINDUP_NONE`, `mh::TELL_FRAMES_AUTHORED` (0; prg.12 raises it).
- `drawAttackMarker(const mh::Game&, int16_t x, int16_t y)` replaces
  `drawMonsterTell`.
- `tools/contact_sheet.tell_class(tell)` → `core/ray/sweep/aoe/rect/?`.

## Notes

- `test_parity` / `mock/` untouched (excluded from the gate).
- No generated artifacts changed; `git status` shows only source/test/doc edits.
- No commit/push (orchestrator commits).
