# monhun-ardu-zza — bake opening-menu render into FX art

Worker report. No commit/push performed; pre-existing uncommitted changes
(unlimited ammo + HUD narrowing) left intact.

## What changed

- `tools/gen-art.py`: new opening-menu sheets authored from the same `GLYPHS`
  table as the font sheets (`text_blocks` / `MENU_ELEMENTS` / `MENU_OPTIONS` /
  `menu_defs`). New `check_menu_identity()` cross-compares every menu glyph cell
  against the authored `fontw`/`fontg` sheets (pixel-oracle link). New
  `images/menu/` directory routing (`sheet_kind`, `clean_stale(prefix)`).
- `tools/convert-sprite.py`: `parse_filename` now `rsplit('_', 1)` so
  `mh_menu_bg_128x64.png` yields symbol `mh_menu_bg` (backward compatible with
  all existing single-underscore names).
- `tools/gen.sh`: `mkdir -p fxdata/menu`, `rm -f fxdata/menu/Sprites.txt`
  (convert-sprite appends), convert `../images/menu -s 4`.
- `fxdata/fxdata.txt`: `include "menu/Sprites.txt"`.
- `tools/fxdata_manifest.py`: GENERATED_GLOBS += `images/menu/*.png`,
  `fxdata/menu/Sprites.txt`.
- `src/menu.hpp`: all layout tables, PROGMEM strings, `menuText`,
  `menuOptionRow` deleted. `drawMenu` is now 3 `sprDraw` calls (bg frame 0 +
  weapon sel tile + target sel tile). `fxfontg`/`fxfontw`/`textPut` untouched
  (HUD still uses them).

New art (cart cost ≈ 8.9 KB of the 16 MB image; fxdata-data.bin 21768 → 30604 B):

- `images/menu/mh_menu_bg_128x64.png` — static title/labels/light options/footer.
- `images/menu/mh_menu_sel_28x16.png` — 8 tiles: white glyphs at local (0,0) +
  white underline `len*4-1` px at local row 9 (indices match MENU_OPTIONS:
  SWD/FLS/GUN 11 px, LUNGE/SWEEP/HEAVY 19 px, RAVAGER 27 px, POLE 15 px).

## Deviations from the design doc

1. **sel tile is 28x16, not 28x10.** The plus-mask blitter computes the frame
   stride and page count as `h >> 3` (floor) while convert-sprite packs
   `ceil(h/8)` pages. A 10-high tile would drop the underline row (page 2) and
   corrupt frames 1+ (stride 56 B vs packed 112 B). Padding to 16 (rows 10..15
   fully transparent) is pixel-identical; cart cost is the same as a
   would-be correctly packed 10-high tile.
2. `convert-sprite.py` needed the `rsplit` fix above; without it
   `mh_menu_bg_128x64.png` could not parse (two underscores).

## Exact verification commands

1. `make gen` twice, second run deterministic:
   - `python3 tools/fxdata_manifest.py --snapshot build/zza-snap-1.json` (37 artifacts)
   - `make gen` (2nd run) → `--verify-snapshot` → `PASS (37 generated artifacts unchanged)`
2. `make gen-check` → final tail: `fxdata_manifest: PASS (37 generated artifacts unchanged)`
   (also `fxdata_manifest: fxdata/manifest.json up to date (21 images, 13 inputs, 9 outputs)`)
3. `make test` → `Total Passed: 3119` / `Total Failed: 0`
4. `make fxtest-headless` (full, all 11 suites PASS):
   - `asset_test PASSED=254 FAILED=0`
   - `test_audio PASSED=14`, `test_boot PASSED=4`
   - `combat_test PASSED=195` (`C reads spawn=6 attack=5 guard=2 hit=9 tick256=0 simAtk=5 simTk=0 winSw=1`)
   - `data_test PASSED=221`, `test_hud PASSED=17`
   - `menu_test PASSED=59 FAILED=0` (file untouched)
   - `parity_test PASSED=660 FAILED=0`
   - `perf_test PASSED=5 FAILED=0`
     `B pUs=6382 pHz=156 lHz=52 lTk=984 rMx=4944 rAv=4721 ram=403`
5. `node tools/gen-parity-fixtures.js` → sha256 before == after
   (`1cfd68e5…f25ec5`), i.e. regen byte-identical. `git diff --stat -- tst/fxdatatest/parity_fixtures.hpp`
   still shows the pre-existing uncommitted 15/15 ammo-scene fixture diff (no
   new change from this bead; hash unchanged by the regen).
6. `make build` / `make size`:
   - `size: .text=28028 .data=58 .bss=1960`
   - `size: flash=28086/29696 (1610 free)  ram=2018/2560`
   - delta vs baseline 28678 = **−592 B** (inside the expected ≈27940-28100
     band; stub ceiling was 27940). Perf numbers are the four from item 4
     (rMx 4944 vs 7407 budget; ram free 403 ≥ 300).

`make test-tools`: `Ran 49 tests … OK`.

## git diff --stat (tracked) and status

```
 fxdata/fxdata-data.bin             | Bin 21768 -> 30604 bytes
 fxdata/fxdata.bin                  | Bin 22016 -> 30720 bytes
 fxdata/fxdata.h                    |  14 +--
 fxdata/fxdata.txt                  |   1 +
 fxdata/manifest.json               |  43 +++++++---
 mock/game.js                       |   4 +-     (pre-existing)
 src/core/player.hpp                |   6 +-     (pre-existing)
 src/fxdata.h                       |  14 +--
 src/menu.hpp                       | 103 ++++++--------------
 src/render.hpp                     |  10 ++-    (pre-existing)
 tools/convert-sprite.py            |   2 +-
 tools/fxdata_manifest.py           |   2 +
 tools/gen-art.py                   | 169 +++++++++++++++++++++++------
 tools/gen.sh                       |   5 +-
 tst/fxdatatest/parity_fixtures.hpp |  30 +++----  (pre-existing)
 tst/player_test.hpp                |   6 +-     (pre-existing)
 tst/shells_test.hpp                |  26 ++++--  (pre-existing)
```
Untracked: `images/menu/`, `fxdata/menu/` (new generated sets), plus the
pre-existing `recording_20260917193141.gif`.

## Pixel-oracle note

`tst/fxdatatest/menu_test.hpp` is passed unchanged and green (59 asserts), but
it exercises `menu_state.hpp` only — it does not render `drawMenu` or read the
framebuffer (no device suite does). Pixel identity is therefore enforced at the
authoring step: `gen-art.py`'s `check_sheets` (every authored pixel == declared
block rects) plus the new `check_menu_identity` (every menu glyph cell
byte-equal to the fxfontw/fxfontg glyph tile, cells transparent elsewhere), and
the underlines are the exact old `blk(x, y+9, len*4-1, 1, shade 3)` rects baked
at local row 9. Both checks run inside `make gen`/`gen-check`. The shipping
`drawMenu` itself has no on-device pixel test (pre-existing gap, hud_test-style
oracle not added by this bead).

---

# monhun-ardu-3o9 — equipment catalog pipeline + single 8-angle base sheet

Worker (dispatched, cancelled mid-run) landed the pipeline; orchestrator finished
it inline after the base-template correction (user: base = ONE sheet, all 8
player angles).

## Changed
- `tools/gen-equipment.py`: strict schema -> `src/generated/equip_meta.hpp` +
  `fxdata/tables/equip.bin` + placeholder sheets in `images/equip/`. Slots now
  `player, shadow, body, head, weapon, offhand`; `player` is the base set
  (16x16, 8 facings, anchor 8,8).
- `tools/gen-base-sheet.py`: now emits ONE base template,
  `docs/art/player_base_16x16.png` (128x16, 8 angles, facing 0 filled) +
  `docs/art/guide_player_base_4x.png` (direction labels, anchors, palette).
- `data/equipment/`: `player_base.json` + three weapon records (was split
  shadow/body/head records).
- `tools/gen.sh` + `tools/fxdata_manifest.py` + `fxdata/fxdata.txt`: equip step,
  globs, `raw_t mhEquip`.
- `tools/tests/test_gen_equipment.py` + fixture tree: updated to the 4-item
  catalog (69 tool tests OK).

## Gates
- `make gen` deterministic; `make gen-check` PASS (45 artifacts)
- `make test` 3119/0; `make test-tools` 69 OK
- `make fxtest-headless` all PASS (asset 254, audio 14, boot 4, combat 195,
  data 221, hud 17, menu 59, parity 660, perf 5)
- `make build`/`make size`: flash 26668 (unchanged; blob is cart bytes),
  cart image 87593 B
