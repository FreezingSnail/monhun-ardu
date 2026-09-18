# monhun-ardu-6zb.8 — poles: all weapons break every variant, neutral fracture marker

Status: DONE. All gates green; parity regen empty diff. No commit/push (orchestrator).

## What changed

- `data/creatures/pole_sever.json`, `pole_break.json`, `pole_crack.json`:
  `breakTypes` -> `["SLASH","BLUNT","SHOT"]` on the breakable zone. PLAIN untouched.
- `src/core/combat.hpp`: removed the pole-only `gateBreak` parameter from
  `combatZoneHitResolveAt` — poles now use the shared monster rule (any landed
  hit drains the pool; the broken bit needs the hit's phys in `breakTypes`,
  which is all three for every variant). Comment updated.
- `src/core/projectiles.hpp`: `damagePole` calls the 9-arg resolver (no
  gate arg). Zero behavior change for PLAIN (hp 0, breakTypes 0) so parity holds.
- `tools/gen-art.py`: dropped the blade/mace/gun emblems. New neutral 7-px
  jagged fracture stamp (`_FRACTURE`/`_fracture`) placed on the ACTUAL
  breakable part: SEVER top block (BLACK on LIGHT head), BREAK side arm (BLACK
  on LIGHT arm), CRACK band (WHITE on DARK post). Damaged stage chips the two
  far-end pixels (5 px) + adds contrast-matched crack lines; broken stage keeps
  the detached piece on the ground. 6-frame `stage*2+flash` layout unchanged;
  PLAIN's legacy 2 frames byte-identical.
- `mock/game.js`: `POLE_DEFS` breakTypes -> 7 (all three); removed the
  `PHYS_BIT_BY_WEAPON & breakTypes` early-return in `poleOnHit` (all-weapons
  drain/break). Replaced the three emblem mirrors with `poleFracture` at the
  same per-part anchors/contrast shades as gen-art.
- Tests: `tst/shells_test.hpp` wrong-phys block replaced with a 3-variant x
  3-weapon drain+break matrix; `tst/combat_test.hpp` pole zone breakTypes
  asserts `PHYS_SLASH|PHYS_BLUNT|PHYS_SHOT` and resolver calls drop the gate
  arg; `tst/art_dims_test.hpp` `testPoleSheets` now pins per-part marker
  position, shade/contrast (BLACK on LIGHT head/arm, WHITE on DARK band),
  7 px intact / 5 px damaged / 0 broken, plus the existing stage-distinctness
  and ground-piece checks; `mock/game.test.js` wrong-phys test replaced with
  the all-weapons matrix + `breakTypes === 7`.

Regenerated set (`make gen`): images/blocks/fxpole_{sever,break,crack}_*.png,
fxdata blocks/fxdata.bin/data.bin/manifest.json, tables/combat.bin,
src/generated/combat_{data,expect}.hpp.

## Verification (exact tails)

1. `make gen` (x2) then `make gen-check`:
   `fxdata_manifest: PASS (67 generated artifacts unchanged)` — exit 0.
   `fxdata/fxdata.h == src/fxdata.h` (gen-check cmp passes).
2. `make test`: `Total Passed: 4669` / `Total Failed: 0`.
3. `make test-tools`: `Ran 141 tests in 8.102s` — `OK`.
4. `node --test mock/game.test.js`: `tests 35`, `pass 35`, `fail 0`.
5. Device (targeted):
   - `test_parity`  PASSED=660 FAILED=0  (PASS)
   - `test_assets`  PASSED=259 FAILED=0  (PASS)
   - `test_combat`  PASSED=233 FAILED=0  (PASS)
   - `test_data`    PASSED=221 FAILED=0  (PASS)
   - `test_menu_art` PASSED=81 FAILED=0  (PASS)
   - `node tools/gen-parity-fixtures.js` -> `parity_fixtures.hpp` empty git diff.
6. `make size`:
   - before: flash 25290/29696 (4406 free)  ram 1744/2560
   - after:  flash 25244/29696 (4452 free)  ram 1744/2560
   - delta: -46 flash, RAM flat. Data facts unchanged.

No float, no /tmp, tests permanent + co-located.
