# monhun-ardu-3fh — bake shadow into body art, drop one layer blit

Worker report. No commit/push/`git add` performed.

## What changed

- **`tools/gen-equipment.py` `body_cell(shade)`** — paints the ground-shadow bar
  `rect(2, 15, 12, 1, DARK)` (the exact rect the `shadow_base` sheet uses) into
  every authored body cell before the torso/legs, so both pose rows (idle row 0
  WHITE, dodge row 1 LIGHT) and all 8 facings carry it. The bar never overlaps
  torso/legs (bar y=15; legs y=13..14), so body+shadow composites identically at
  the same `(8,8)` anchor.
- **`data/equipment/sets/default.json`** — dropped the `"shadow"` entry
  (`body`/`head` remain). `shadow_base` JSON/sheet stay in `data/` as an unused
  default; its catalog item + part record are still emitted.
- **`tools/gen-equipment.py` default-set handling** — a layered slot omitted from
  `default.json` is now allowed (skipped); `emit_part_view` only emits
  `DEFAULT_<SLOT>` for present slots and `--dump` lists only present slots.
  (Unknown item id / wrong slot / non-string still error.)
- **`src/render.hpp` `drawPlayer`** — removed the
  `partDraw(equip::DEFAULT_SHADOW, ...)` blit; now body (baked shadow) + head,
  then the unchanged weapon overlays.
- **`tools/tests/test_gen_equipment.py`** — two new tests:
  `test_layered_body_bakes_shadow_row` (asserts the DARK bar at
  `x=2..13, y=15` in all 16 body frames, clear at x=0/x=14) and
  `test_layered_default_set_shadow_optional` (omitted shadow → no
  `DEFAULT_SHADOW`, `ITEM_SHADOW_BASE`/`PART_SHADOW_BASE` still present, dump
  prints `body=body_base, head=head_base`).
- **Not touched**: `tst/fxdatatest/player_art_test.hpp` goldens (byte-identical,
  per the acceptance oracle).

## Verification (exact commands + numbers)

### 1. `make gen` x2 → `make gen-check`
```
make gen   # pass 1 writes fxdata/equip/Sprites.txt, fxdata-*.bin, manifest, src/generated/equip_meta.hpp
make gen   # pass 2: mh_body_base_16x16.png (unchanged); equip.bin (unchanged); equip_meta.hpp (unchanged)
make gen-check
fxdata_manifest: PASS (50 generated artifacts unchanged)
```
`src/fxdata.h` unchanged (body sheet keeps its size, so every later FX offset is
stable); `fxdata.bin` 96256 → 96256 B, `fxdata-data.bin` 96243 → 96243 B.

### 2. `make test` + `make test-tools`
```
Total Passed: 3119
Total Failed: 0
```
```
Ran 81 tests in 4.866s
OK
```
(was 79 in the clean+layered suites; +2 new layered tests.)

### 3. `make fxtest-headless` (full)
```
=== test_parity ===
parity_test PASSED=660 FAILED=0
test_parity: PASS
=== test_perf ===
B pUs=6771 pHz=147 lHz=49 lTk=988 rMx=5812 rAv=5292 ram=406
perf_test PASSED=5 FAILED=0
test_perf: PASS
=== test_player_art ===
test_player_art PASSED=111 FAILED=0
test_player_art: PASS
```
- `test_player_art` **111/0 with UNCHANGED goldens** (file untouched) — the
  baked shadow composites byte-identically.
- `perf` 5/5; baseline was `rMx=5968 rAv=5451 pUs=6925`, now
  **`rMx=5812 rAv=5292 pUs=6771`** (repeat run identical; stable). rMx −156,
  rAv −159, pUs −154: the one dropped cart blit. Slightly above the ~5700
  estimate but directionally and consistently down.

### 4. `make build` + `make size`
```
Sketch uses 27050 bytes (91%) of program storage space. Maximum is 29696 bytes.
size: flash=27050/29696 (2646 free)  ram=2018/2560
```
Flash delta vs 27076 baseline: **−26 B**.

## Deviations
- `--dump` and default-set validation now accept an omitted layered slot; this
  was required to drop `shadow` from the shipped default while keeping the
  `shadow_base` catalog record. Covered by a new test.
- Updated the generator docstrings/comments mentioning `DEFAULT_SHADOW`. No
  docs/ update (out of bead scope; `docs/equipment-framework.md` already stale
  from the ikp implementation).
