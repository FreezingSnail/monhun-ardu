# monhun-ardu-kt7.6 — device: broken-part art overlays for chicken/bull zones

Status: **PASS** (not committed; orchestrator commits)

## What changed

- `tools/gen-art.py`: four new `icon_defs` 4-frame sheets in
  combatPartArtFrame order (east intact / east broken / west intact / west
  broken), authored east and mirrored west via `_zone_part_defs` +
  `_mirror_rect`:
  - `head_chicken` 11x8, `lunge.json` head box (18,0,11x7): baked white head +
    black eye/beak/wattle; broken erases the head and drops a dark neck stump.
  - `legs_chicken` 9x24, `lunge.json` appendage box (9,0,9x24): baked DARK legs
    + LIGHT shank/foot highlights; broken shears them below the thigh.
  - `head_bull` 12x16, `sweep.json` head box (17,-4,12x10): baked white
    horns/ear + head top; broken erases the horn band, draws short dark stumps,
    repaints the head top.
  - `hooves_bull` 20x16, `sweep.json` appendage box (4,12,20x10): baked DARK
    legs + LIGHT shanks + BLACK hooves; broken cuts stumps with no hooves.
  Broken frames erase the baked part with shade-0 (BLACK) pixels on all three
  planes before the damaged variant. Heights are multiples of 8 (SpritesU
  plus-mask page stride). Art is at `cell pixel - zone box origin`, clipped to
  the frame, so intact frames repaint the baked part.
- `src/render.hpp`: generalized the 4t4 heavy appendage overlay into one
  `drawZonePart(g, x, y, sheet, zoneIdx, zoneBit)` helper (cached zone box +
  `combatFaceOffset` + `combatPartArtFrame`); HEAVY output identical (skipped
  during spinning). Dispatch: LUNGE `fxhead_chicken` (head) + `fxlegs_chicken`
  (appendage); SWEEP `fxhead_bull` + `fxhooves_bull`; RAVAGER unchanged. Part
  overlays skip the whole-body attack sheets (`fxchickenatk`/`fxbullatk`) so the
  posed part is not overpainted. No mock/data/sim change; parity untouched.
- Generated via `make gen`: `images/blocks/fxhead_chicken_11x8.png`,
  `fxlegs_chicken_9x24.png`, `fxhead_bull_12x16.png`, `fxhooves_bull_20x16.png`,
  `fxdata/blocks/Sprites.txt`, `fxdata/fxdata.bin`, `fxdata/fxdata-data.bin`,
  `fxdata/fxdata.h`, `fxdata/manifest.json`, `fxdata/tables/equip.bin`,
  `src/fxdata.h`, `src/generated/art_dims.hpp`, `src/generated/equip_meta.hpp`.
- Tests (permanent, co-located):
  - `tst/art_dims_test.hpp`: `testZonePartSheets` + `partFrameMirrors` /
    `partErasePixels` helpers — dims/frames, exact E/W mirror, broken differs
    from intact, broken has erase pixels (73 asserts).
  - `tst/fxdatatest/asset_test.hpp`: `blobHeader` for the four new sheets.
  - `tst/fxdatatest/monster_art_test.hpp`: world-rect frame-pick checks per
    creature/zone E/W and broken vs intact (real drawMonster pixels).
- Docs: `docs/creature-framework.md` open item "Broken-part art path" marked
  done for the demo roster (heavy/ravager noted); `README.md` zone/overlay note.

## Verification (exact tails)

- `make gen-check` -> `fxdata_manifest: PASS (74 generated artifacts unchanged)` (exit 0)
- `make test` -> `Total Passed: 5271` / `Total Failed: 0`
- `make test-tools` -> `Ran 152 tests in 7.876s` / `OK`
- `make fxtest-headless FXTEST_ONLY=test_monster_art` -> `test_monster_art PASSED=91 FAILED=0`
- `make fxtest-headless FXTEST_ONLY=test_parity` -> `parity_test PASSED=660 FAILED=0`
- `node tools/gen-parity-fixtures.js` -> `scenes=20 ticks=1269 snapshots=32 cpFields=20`; `git diff --stat tst/fxdatatest/parity_fixtures.hpp` -> empty
- `make fxtest-headless` (full, extra) -> all 15 suites PASS
- `make size`:

```
size: .text=27348 .data=40 .bss=1719
size: flash=27388/29696 (2308 free)  ram=1759/2560
size: data facts: HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false HAS_GUARD_HP:false HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true HAS_HIT_STAGGER:false HAS_MULTI_STEP:false HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false HAS_STAGGER:true HAS_STEP_AFTER:false HAS_STEP_CHANCE:false HAS_WAIT_STEPS:false HAS_ZONES:true
```

## Budget

| | baseline 67fec8d | after | delta |
|---|---|---|---|
| flash | 27166/29696 (2530 free) | 27388/29696 (2308 free) | **+222 B** |
| RAM | 1759/2560 | 1759/2560 | 0 B |
| fxdata/fxdata.bin | 179712 | 182016 | +2304 B |
| fxdata/fxdata-data.bin | 179471 | 181927 | +2456 B |
| data facts | unchanged | unchanged | no HAS_* flip |

+222 B is the render helper + 4-way dispatch (sheets are cart image, not program
flash). Fits with 2308 B free; no zone data trimmed.

## Deviations / notes

- Chicken-head "(top 7 rows)" note holds exactly (box h 7, frame h 8). The bull
  notes said "top 10 rows", but the bull boxes are not cell-aligned: head
  (17,-4) sits 4 px above the cell, so baked horns/head-top land in frame rows
  8..15, and appendage (4,12) puts baked legs in frame rows 6..10. Art follows
  the design's hard "repaint as baked" rule at the exact frame sizes; the bull
  muzzle/eye fall below the 16-row frames and stay baked. Frame dims/origins are
  exactly as specified.
- `make gen` must run twice after a sheet-count change: `gen-equipment.py`
  (before `fxdata-build.py` in `tools/gen.sh`) reads the previous
  `fxdata/fxdata.h`, so the first pass emits stale `SHEET_OFF_*` asserts; the
  stable tree from the second pass passes `gen-check`.
- Overlays land at the cached rotated zone box (the exact rect the hit test
  uses), matching the 4t4 heavy-tail contract. On west facing an interior box
  (e.g. chicken head 18,0 -> dx -18) therefore sits left of the mirrored baked
  art; no sim/data change, cosmetic only.
