# monhun-ardu-6zb.10 — poles: part-locked zones + additive part graphics

Status: DONE. All required gates green. No commit/push (orchestrator owns it).

## What changed

### Data (part-locked zones)
`data/creatures/pole_sever.json` / `pole_break.json` / `pole_crack.json`: the
single `appendage` zone box is now locked to its additive part (dmgMul 101,
bodyShare 100, breakTypes SLASH|BLUNT|SHOT, `broken {hurtOn:false}`; pools
unchanged 60 / 40 / 30). A hit off the part resolves as body only — no pool
drain, no break.

- SEVER cap:  `{ox:-2, oy:0,  w:24, h:20}`
- BREAK horn: `{ox: 4, oy:0,  w:18, h:20}`
- CRACK collar: `{ox:-2, oy:12, w:24, h:16}`

`pole.json` (PLAIN) untouched (mul-140 head crit, no pool). No core logic
change was needed: the shared resolver already drains only the matched zone, and
dmgMul 101 beats the implicit body tie.

### Art (`tools/gen-art.py`)
Variant sheets widened 20x40 -> **24x40** (6 frames, stage*2+flash unchanged) so
the cap/collar are literally 24 px and protrude 4 px past the 16 px DARK post on
each side. Each part is additive and brighter than the post (LIGHT body + WHITE
highlight + BLACK seam) with the neutral 7-px fracture marker (BLACK on LIGHT):

- SEVER: 24-px cap (rows 0..14) on the post; broken = jagged stepped stump +
  cap on the ground.
- BREAK: thick curved LIGHT horn (`_BREAK_HORN` rows, 6 px thick, 13 tall)
  rising out of the upper post to the right, tip into the margin; broken = horn
  base stub + horn on the ground.
- CRACK: 24-px collar (rows 18..30) wrapping the post; broken = split collar
  with a displaced lower chunk + chunk on the ground.

Damaged = chipped marker (5 px) + extra crack lines in all cases; broken pieces
live in the 4 padding rows below the 36-tall art. PLAIN keeps the legacy 2-frame
20x40 `pole_frame` byte-identically.

### Core / render
- `src/render.hpp`: variant sheets draw at `rect.x - 2` (2 px baked margin keeps
  the 24 px part centered on the 20 px rect); comments updated.
- `src/core/game.hpp`, `src/core/projectiles.hpp`, `src/core/combat.hpp`:
  comments updated to part-locked / part-only drain. No behavior change.

### Mock (`mock/game.js`)
`POLE_DEFS` z boxes mirror the three part boxes. `drawPole` mirrors the additive
24 px shapes exactly (same runs/offsets, drawn at `x - 2`), including the marker,
chipped cracks, stumps/stubs/displaced chunks and ground pieces.

## Verification (exact tails)

1. `make gen` (x2) then `make gen-check`:
   `fxdata_manifest: PASS (67 generated artifacts unchanged)`; exit 0.
2. `make test`: `Total Passed: 4673  Total Failed: 0`; exit 0.
3. `make test-tools`: `Ran 141 tests in 7.314s` -> `OK`; exit 0.
4. `node --test mock/game.test.js`: `tests 36  pass 36  fail 0`.
5. Device (targeted):
   - `FXTEST_ONLY=test_parity`:   `parity_test PASSED=660 FAILED=0` -> PASS
   - `FXTEST_ONLY=test_assets`:   `asset_test PASSED=256 FAILED=0` -> PASS
   - `FXTEST_ONLY=test_combat`:   `combat_test PASSED=233 FAILED=0` -> PASS
   - `FXTEST_ONLY=test_data`:     `data_test PASSED=221 FAILED=0` -> PASS
   - `FXTEST_ONLY=test_menu_art`: `test_menu_art PASSED=81 FAILED=0` -> PASS
   - `FXTEST_ONLY=test_menu`:     `menu_test PASSED=78 FAILED=0` -> PASS
   `node tools/gen-parity-fixtures.js` (`scenes=20 ticks=1269 snapshots=32
   cpFields=20`) leaves `tst/fxdatatest/parity_fixtures.hpp` with an empty diff.
6. `make size`:
   - baseline: flash 25244/29696 (4452 free), RAM 1744/2560
   - after:    `size: .text=25222 .data=40 .bss=1704`
     `size: flash=25262/29696 (4434 free)  ram=1744/2560`
   - delta: **+18 B flash, 0 B RAM**; data facts unchanged (`HAS_ZONES:true`).

## Tests updated (permanent, co-located)
- `tst/shells_test.hpp`: SEVER/BREAK part-locked drain + lower-post no-drain,
  variant x weapon part-centre matrix, play-path part-centre break after
  ceil(pool/dmg) with lower-post safe, CRACK collar; stage/flash frame test kept.
- `tst/world_test.hpp`: BREAK break at horn centre.
- `tst/combat_test.hpp`: zone boxes -> cap/horn/collar; part-locked comments.
- `tst/art_dims_test.hpp` `testPoleSheets`: 24x40 six-frame art, marker 7/5/0,
  LIGHT part behind BLACK marker + WHITE flash, cap/collar margins + horn
  protrusion, broken ground pieces, stumps/stubs/displaced chunk.
- `tst/fxdatatest/{asset_test,menu_test}.hpp`: variant headers 24x40; menu
  `rect` stays 20x36 with pools on `COMBAT_ZONE_APPENDAGE`.
- `mock/game.test.js`: part boxes, part-centre drain/break matrix, lower-post
  no-drain, stage threshold kept.

## Notes / deviations
- The variant frame width is 24 (was 20) so the cap/collar are literally 24 px
  and stick 4 px out of the 16 px post; the render/mock anchor shifts 2 px left
  to stay centered on the 20 px pole rect. Zones use the owner-specified 24/18
  px boxes relative to the rect.
- Device `asset_test` frame-count-via-next-symbol was removed: block order
  follows `os.listdir` and is not stable across regens (the 24 px sheets
  reordered the section). `blobHeader` still pins 24x40; the host art suite
  parses the generated PNGs and pins the 6 frames + per-stage ink, and gen-check
  keeps PNG <-> blob in sync.
- No float, no `/tmp` test code, tests permanent + co-located, no commit/push.
