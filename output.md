# monhun-ardu-ryh.3 — spike: hitbox-mask converter + chicken

## What changed

The mask pipeline is end to end for one creature. `images/masks/*.png` are now
the geometry source of truth; `tools/gen-hitboxes.py` derives every box into
`build/hitboxes.json`, which `tools/gen-combat.py` packs (record shapes
unchanged) and `tools/gen-art.py` reads to crop the breakable-part overlays.

- **`tools/gen-hitboxes.py` (new).** Reads `images/masks/<art-sheet>_<W>x<H>.png`
  (one facing, three stacked layers top-to-bottom: collision / hitbox / hurtbox)
  plus the creature JSON, and writes `build/hitboxes.json` with the body box
  (green bbox), zone boxes (red=head, blue=appendage bbox), collide box (yellow
  bbox) and window rects (orange/violet/cyan/magenta per hitbox column, mapped
  to the attack whose `art.frame // 2` selects that column, then converted back
  to the packed body-centre-relative `windows[].box` form). `--render` bootstraps
  missing masks (JSON boxes -> PNG) into `build/scratch/masks/` and writes the
  composite review PNG.
- **`images/masks/fxmonster_lunge_32x24.png` + `images/masks/fxchickenatk_32x24.png`
  (new committed sources).** Chicken beast mask + attack mask.
- **`tools/gen-combat.py`.** `load_hitboxes()` + `apply_hitboxes()`: a creature
  with a `build/hitboxes.json` entry takes its `stats.w/h`, zones, collide and
  windows from the masks; the JSON geometry keys stay at the shipped
  pre-migration values (the ryh.4 cleanup deletes them). A missing
  `build/hitboxes.json` (schema fixtures) leaves every hand box in place.
- **`tools/gen-art.py`.** `load_part_boxes()` + `_zone_part_defs_crop()`: the
  head/legs part sheets are authored cell-absolute and cropped to the
  mask-derived zone bbox (frame = box.w x pad8(box.h)). Feet/world positions are
  unchanged; only the legs frame shrank 9x24 -> 9x16.
- **`tools/gen.sh`.** Runs `gen-hitboxes.py` after `fxdump` and before `gen-art`
  / `gen-combat`.
- **`tools/fxdata_manifest.py`.** `images/masks/**` are excluded from the
  shipped-image provenance scan and added as tracked inputs (a mask edit without
  a regen fails `make gen-check`).
- **`Makefile`.** `hitboxes-render` (dev-only review target).
- **`tst/hitbox_reach_test.hpp` (new).** Reachability guard + `cey` pin.
- Tests/docs updated to the tightened numbers.

## Chicken numbers landed through the mask

`build/hitboxes.json` (from the mask, verified in a fresh `make gen`):

| field | value |
|---|---|
| body | (0, 0, 32, 24) |
| head | (18, 0, 11, 7) |
| appendage | (9, 13, 9, 9) |
| collide | (9, 13, 9, 9) |
| peck window | 12x10 @ (14, -6) |
| leap window | 18x16 @ (12, -2) |
| wing_beat window | 26x18 @ (-8, 0) |

## Mask format + validations

- Dims: `W = (cellW + 2*8) * source_columns`, `H = cellH * 3`, layers
  collision / hitbox / hurtbox top-to-bottom; exact palette per the epic. The
  8 px margin exists because attack windows are body-centre relative and reach
  behind the sprite cell (wing_beat left edge is 5 px left of the cell).
- Hard failures: (1) dims + symbol resolution + a beast mask required with any
  attack mask; (2) every head/appendage/collide/window region is one solid rect
  (the green body is the fallback **painted underneath** the zones and may be
  overpainted); (3) head/appendage do not overlap (they may sit on the body);
  (4) collision/hurtbox identical across a sheet's columns; (5) hitbox columns
  match the attacks by `art.frame` (single-window only; multi-window kits stay
  hand-authored until phase 2); (6) round trip: bootstrap from the shipped JSON
  reproduces the zone/body/collide/window records bit-for-bit.

## Guard (`tst/hitbox_reach_test.hpp`)

- Every shipped zone (8) is hittable from at least one stance: scan of positions
  x DIR8 facings x every weapon's attack boxes through the real `meleeHitbox()`
  math, testing the zone's face-relative world rect. No escape-hatch flag.
- `cey` invariant: for every zone that draws a part overlay, the overlay sheet
  frame width == zone box width and frame height == pad8(zone box height) (the
  overlay anchor is the zone box origin, so the painted part sits on the exact
  rect the hit test uses).

## Verification (exact tails)

`make gen`:
```
gen-hitboxes: 1 masked creature(s) [lunge] -> build/hitboxes.json
gen.sh: FX data + src/fxdata.h regenerated
```

`make gen-check`:
```
fxdata_manifest: PASS (163 generated artifacts unchanged)
```

`make test`:
```
Total Passed: 6815
Total Failed: 0
```

`FXTEST_ONLY=test_monster_art make fxtest-headless`:
```
test_monster_art PASSED=180 FAILED=0
test_monster_art: PASS
```

`FXTEST_ONLY=test_combat make fxtest-headless`:
```
combat_test PASSED=252 FAILED=0
test_combat: PASS
```

`FXTEST_ONLY=test_assets make fxtest-headless` (extra: the FX layout shifted):
```
asset_test PASSED=264 FAILED=0
test_assets: PASS
```

`make size` / `make size-line`:
```
size: .text=29420 .data=50 .bss=1764
size: flash=29470/29696 (226 free)  ram=1814/2560
```

## Size delta

| | baseline (ryh.2) | after | delta |
|---|---|---|---|
| flash | 29470/29696 (226 free) | 29470/29696 (226 free) | **0 B** |
| RAM | 1814/2560 | 1814/2560 | 0 B |
| fxdata image | 410880 B | 410624 B | -256 B |

Flash is exactly unchanged: the change is data/tooling only, the combat record
sizes are identical, and the shipped `windows[].box` values are unchanged. The
FX image shrank 256 B because the legs part sheet went 9x24 (3 pages) ->
9x16 (2 pages); the offset shift is absorbed by the regenerated
`equip_meta.hpp` / `zone_meta.hpp` / table blobs (all in this staged set).
Clears the ~150 B wave floor.

## Review image

`build/scratch/hitbox_review.png` (448x400): top panel = lunge beast (sprite row
+ collision/hitbox/hurtbox mask rows + head/appendage/collide boxes on the art);
bottom panel = `fxchickenatk` (sprite row + hitbox mask row + the three window
rects). Regenerate with `make hitboxes-render`.

## Notes / deviations

- `data/creatures/lunge.json` keeps the pre-migration geometry
  (appendage `9,0,9,24`, collide `9,11,12,13`): the mask is the source for the
  shipped tightened records, and the round-trip validation proves the converter
  against those shipped values. ryh.4 deletes the hand keys.
- The other beasts + pole stay on their hand boxes (their masks arrive in ryh.4);
  `--render` still renders their review panels from the committed masks only.
- Related suites updated to the tightened collide/appendage geometry:
  `tst/combat_test.hpp`, `tst/monster_test.hpp`, `tst/world_test.hpp`,
  `tst/zone_test.hpp`, `tst/art_dims_test.hpp`, `tst/fxdatatest/combat_test.hpp`,
  `tst/fxdatatest/asset_test.hpp`, `docs/feel-design.md`.
- No commit/push (orchestrator owns the wave commit).
