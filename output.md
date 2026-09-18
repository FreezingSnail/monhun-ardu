# monhun-ardu-6zb.9 — poles: wailing anywhere breaks the part; BREAK becomes a horn

Status: DONE. All gates green. No commit/push (orchestrator owns the commit).

## What changed

### Data (whole-pole breakable zone)
- `data/creatures/pole_sever.json`, `pole_break.json`, `pole_crack.json`: each now
  ships exactly ONE breakable `appendage` zone `{ox:-4, oy:-4, w:28, h:44}`
  (20x36 body + 4 px margin), `dmgMul 100`, `bodyShare 100`, `breakTypes`
  SLASH|BLUNT|SHOT, `broken {hurtOn:false}`. Pools unchanged: 60 / 40 / 30.
- The variants' separate mul-140 head zones are gone (no variant crit).
- `pole_break.json`: `stats` back to 20x36, `stats.brokenBody` removed (the horn
  is a stage, not a rect resize).
- `pole.json` (PLAIN) untouched: mul-140 head crit zone, no pool, byte parity.

### Core
- `src/core/combat.hpp` `combatZoneHitResolveAt`: the appendage candidate now
  also wins a mul tie with the implicit body when it has a live pool
  (`z.dmgMul == bestMul && best == COMBAT_NO_ZONE && z.hp > 0`). The head keeps
  losing an equal-mul tie to the body and never loses to an appendage, so the
  generic beast tie order is unchanged. This is what lets the mul-100 whole-pole
  zone drain on every landed hit through the shared resolver.
- `src/core/projectiles.hpp`, `src/core/game.hpp`, `src/render.hpp`: comments
  updated (whole-pole zone, no variant crit, no rect resize, BREAK = horn).
- No render code change: `drawPole`/`poleSheetFrame` already select the stage
  from the appendage pool.

### Art (`tools/gen-art.py`)
- `pole_break_frame` rebuilt as a 20x40 horn: a curved DARK horn on the LIGHT
  head block inside the 20 px width, a WHITE fracture marker on the shaft
  (chipped when damaged), broken = jagged DARK base stub on the block + the horn
  on the ground. `render_all` / `sheet_filename` now use 20x40 (old 28x40 arm
  PNG pruned by `clean_stale`).

### Mock (`mock/game.js`)
- `POLE_DEFS` mirrors the whole-pole zone (box -4,-4,28,44; mul-100 drain; rect
  20x36 for all; no `brokenW` resize). `damagePole` crits only kind 0 (PLAIN).
- `drawPole` kind 2 mirrors the horn: `POLE_HORN` runs, WHITE fracture marker,
  damaged chips, broken base stub + ground horn (exact `gen-art.py` mirror).

## Play-path coverage (acceptance 1)

Added for both engine (`tst/shells_test.hpp`) and mock (`mock/game.test.js`):
a hit landing at `(rect.x+10, rect.y+22)` — the natural mid-post attack-box
centre — drains and breaks SEVER/BREAK/CRACK after `ceil(pool/dmg)` hits, for
W_SWORD / W_FLAIL / W_GUN. Existing per-variant tests were re-pointed to the
same mid-post point (not the old small part boxes). The damaged-stage (hp/hpMax)
test is kept.

Interfaces:
- New generated constant `ZONE_POLE_SEVER_APPENDAGE` (replaces
  `ZONE_POLE_SEVER_HEAD`); `ZONE_POLE_BREAK_APPENDAGE` /
  `ZONE_POLE_CRACK_APPENDAGE` keep their names but carry the whole-pole box.
- `combat::ZONES_COUNT` 11 -> 9 (4 pole zones: plain head + 3 variant
  appendages). `CREATURE_POLE_BREAK_BROKEN_W/H` now 0 (no brokenBody).

## Verification (exact tails)

1. `make gen` (x2, two-pass equip offsets) then `make gen-check`:
   `fxdata_manifest: PASS (67 generated artifacts unchanged)`; exit 0.
2. `make test`: `Total Passed: 4666  Total Failed: 0`.
3. `make test-tools`: `Ran 141 tests ... OK`.
4. `node --test mock/game.test.js`: `tests 36  pass 36  fail 0`.
5. Device (targeted, not the full gate):
   - `FXTEST_ONLY=test_parity`: `parity_test PASSED=660 FAILED=0` -> PASS
   - `FXTEST_ONLY=test_assets`: `asset_test PASSED=259 FAILED=0` -> PASS
   - `FXTEST_ONLY=test_combat`: `combat_test PASSED=233 FAILED=0` -> PASS
   - `FXTEST_ONLY=test_data`:   `data_test PASSED=221 FAILED=0` -> PASS
   - `FXTEST_ONLY=test_menu_art`: `test_menu_art PASSED=81 FAILED=0` -> PASS
   Parity fixture regen `node tools/gen-parity-fixtures.js` (`scenes=20 ...`)
   leaves `tst/fxdatatest/parity_fixtures.hpp` with an empty diff.
6. `make size`:
   - baseline: flash 25244/29696 (4452 free), RAM 1744/2560
   - after:    `.text=25254 .data=40 .bss=1704`,
     `flash=25294/29696 (4402 free)  ram=1744/2560`
   - delta: **+50 B flash, 0 B RAM**; data facts unchanged (HAS_ZONES still on).

## Notes / deviations
- `make gen` must run twice because `gen-equipment.py` bakes sheet offsets from
  the previous `fxdata/fxdata.h` (documented two-pass); the second pass is what
  re-syncs `equip_meta.hpp` after the pole-break sheet width change.
- `tst/combat_pack_test.hpp` now also pins `ZONE_POLE_SEVER_APPENDAGE`.
- No float, no `/tmp`, tests permanent + co-located, no commit/push.
