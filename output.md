# monhun-ardu-nch.11 — longtail: tail-inclusive collide box (closes 7vr)

Status: **PASS** (not committed; orchestrator commits)

## What changed

- `data/creatures/heavy.json`: added top-level `"collide": { "ox": -8, "oy": 3,
  "w": 48, "h": 22 }` (default body box was `{0,0,40,28}`; negative ox extends
  behind the body over the tail base). Numbers exactly as owner-approved draft.
- `mock/game.js`: `MONSTER_DEFS[2]` (kind `heavy`) got the mirrored
  `collide: { ox: -8, oy: 3, w: 48, h: 22 }`; comment note added.
- `mock/game.test.js`: permanent test
  `HEAVY tail-inclusive collide blocks behind, tail still reachable` — asserts the
  def collide values, that a hunter fully behind the body is outside the
  body-only box but inside the extended box, that `pushApart` resolves the
  overlap, and that the tail zone (`ox -24, oy 0, 24x16`) still routes the
  appendage at mul 150 from behind.
- `docs/creature-framework.md`: demo-collide table long-tail row updated from
  "collide still body-only" to done with the box values.
- `tst/fxdatatest/combat_test.hpp`: added symbolic heavy-collide record spot
  checks (`CREATURE_HEAVY_COLLIDE_*`); updated the two `initMonster(MON_HEAVY)`
  target-rect pins that expected the body box to expect the collide box
  (`w` 40->48, `x` m.x -> m.x-8).
- `make gen` regenerated (never hand-edited): `fxdata/tables/combat.bin`,
  `fxdata/fxdata.bin`, `fxdata/fxdata-data.bin`, `fxdata/manifest.json`,
  `src/generated/combat_data.hpp`, `src/generated/combat_expect.hpp`
  (`CREATURE_HEAVY_COLLIDE_OX=-8 OY=3 W=48 H=22`; `BLOB_SIZE` unchanged 1019,
  sha256 updated).

No README change: the target-roster section does not mention collide boxes.
No engine/render change. Data-only.

## Verification (exact tails)

`node --test mock/game.test.js`:
```
ℹ tests 77
ℹ pass 77
ℹ fail 0
```

`node tools/gen-parity-fixtures.js` (run twice) + empty diff:
```
$ git diff --stat tst/fxdatatest/parity_fixtures.hpp
(no output, exit 0)
$ git status --short tst/fxdatatest/parity_fixtures.hpp
(no output)
```
Parity fixtures byte-identical (no parity scene spawns def 2).

`make gen-check`:
```
fxdata_manifest: PASS (70 generated artifacts unchanged)
EXIT=0
```

`make test`:
```
Total Passed: 5198
Total Failed: 0
EXIT=0
```

`make fxtest-headless FXTEST_ONLY=test_combat`:
```
combat_test PASSED=293 FAILED=0
test_combat: PASS
EXIT=0
```
(First run FAILED=2 on the two target-rect pins that still expected the body
box; updated to the collide box, now 293/0.)

`make fxtest-headless FXTEST_ONLY=test_parity`:
```
parity_test PASSED=660 FAILED=0
test_parity: PASS
EXIT=0
```

`make size`:
```
size: .text=27126 .data=40 .bss=1719
size: flash=27166/29696 (2530 free)  ram=1759/2560
size: data facts: HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false HAS_GUARD_HP:false HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true HAS_HIT_STAGGER:false HAS_MULTI_STEP:false HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false HAS_STAGGER:true HAS_STEP_AFTER:false HAS_STEP_CHANCE:false HAS_WAIT_STEPS:false HAS_ZONES:true
```
Flash/RAM identical to baseline `dbabff4` (27166/29696, 2530 free; RAM
1759/2560, 801 free): collide numbers are a same-size field edit, no fact flip,
no engine path change.

## Deviations

None. Owner-approved draft numbers kept exact; no test required a box change.

## Notes

- Unrelated pre-existing dirty files left untouched: `.gitignore`,
  `docs/dev-flow.md`, `.github/`, `recording_20260918184558.gif`,
  `tools/package-arduboy.py`, `tools/tests/test_package_arduboy.py`.
- No `git add` / commit / push.
- Closes `monhun-ardu-nch.11` and `monhun-ardu-7vr` (all three demo monsters now
  carry per-creature collide/hurt boxes: chicken lunge, bull sweep, long-tail
  heavy).
