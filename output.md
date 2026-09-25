# monhun-ardu-ryh.7 — room masks: props/doors/gather/heal rects as map layers

## Status: DONE

## Change

Room geometry stops being hand numbers. `data/map.json` now carries behaviour
only, and every rect of both rooms comes from a painted mask:

- **Source** `images/masks/mh_map_<room>_<W>x<H>.png` (camp 128x56 -> 128x224,
  area 384x112 -> 384x448). Same solids-only idea as the creature masks: the
  room grid stacked as four bands top-to-bottom.

  | band | colour | feeds |
  |---|---|---|
  | props | one colour per prop type | non-gather prop rects |
  | gather | one colour per gather item | gather-node prop rects |
  | doors | one colour per door (room-local order) | door rects |
  | heal | one colour per heal rect | heal rects |

  Palette (exact RGB): props `tent (204,102,0)`, `door (102,51,0)`,
  `pole (51,25,0)`, `post (0,204,204)`, `smithy (153,0,204)`; gather
  `herb (0,204,0)`, `blue_mushroom (0,102,255)`, `ore (170,170,170)`,
  `bug (204,204,0)`; door0..7 `(255,0,128),(128,255,0),(0,128,255),
  (255,255,128),(255,128,255),(128,128,255),(255,64,64),(64,255,255)`;
  heal0..7 `(0,255,128),(128,255,128),(128,0,255),(255,192,0),(0,192,255),
  (255,64,192),(192,255,0),(64,128,255)`.

- **gen-zones.py** parses the masks (`parse_room_mask`), validates, and fills
  x/y/w/h (`apply_room_mask`). A masked room authors behaviour only; `heal` and
  `smithy` are derived (heal from the mask band, smithy from the `type=smithy`
  prop). `data/map.json` keeps `props[].type/sheet/frame/gather`,
  `doors[].to/toSpawn`, `spawns`, `monster`.
- Prop/gather rects are matched to the JSON entries in canonical order
  (left-to-right, top-to-bottom) against the JSON's source order; door/heal use
  a per-entry colour so pairing is order-independent. A room without a mask
  keeps the legacy hand-geometry path (the unit-test fixtures), so
  `--bootstrap` (author a starting mask from current geometry) and `--render`
  (review PNG) mirror `gen-hitboxes.py`.
- `gen-hitboxes.py` skips `mh_map_*` masks — they share `images/masks/` but are
  compiled by gen-zones.
- Docs: `docs/map-zones.md` (new "Room masks" section), `docs/creature-framework.md`
  (mask section updated), gen.sh comment.

**Validations (hard fail):** mask dims == `W x 4H`; every painted pixel exactly
one palette colour; every connected region one solid rect; every rect inside the
room; gather rects non-empty and <= the pack limits; each door touches the room
edge it leaves through; per-class rect counts match the JSON; hand geometry keys
(`x`/`y`/`w`/`h`, plus `heal`/`smithy`) rejected on a masked room. Overlapping
doors cannot be authored: painted geometry is single-valued (one colour per
pixel), so a repaint resolves the overlap — a missing/extra door rect is caught
by the per-index count check instead.

## Derived rects (after `--dump`, masked path)

| room | record | rect |
|---|---|---|
| camp | prop 0 tent | (40,8,32,24) |
| camp | prop 1 post/herb x1 | (8,8,8,8) |
| camp | prop 2 post/herb x2 | (72,40,8,8) |
| camp | prop 3 post/blue_mushroom x1 | (24,8,8,8) |
| camp | prop 4 smithy | (88,40,16,16) |
| camp | door 0 -> area.from_camp | (120,24,8,24) |
| camp | heal 0 | (40,8,32,24) |
| camp | smithy 0 | (88,40,16,16) |
| area | prop 0 herb x1 | (40,16,8,8) |
| area | prop 1 herb x2 | (160,40,8,8) |
| area | prop 2 herb x3 | (280,88,8,8) |
| area | prop 3 blue_mushroom x1 | (96,80,8,8) |
| area | prop 4 blue_mushroom x2 | (216,16,8,8) |
| area | prop 5 ore x1 | (120,88,8,8) |
| area | prop 6 ore x2 | (352,16,8,8) |
| area | prop 7 bug x1 | (200,96,8,8) |
| area | door 0 -> camp.from_area | (0,72,8,24) |

Review image: `build/scratch/roommask_review.png` (room art + the four mask
bands + derived rects outlined; 2x). Generated with
`python3 tools/gen-zones.py --render`.

## Byte-identical gate

`make gen` output: `fxdata/tables/zones.bin (unchanged)`,
`src/generated/zone_data.hpp (unchanged)`, `src/generated/zone_meta.hpp
(unchanged)`, `fxdata/maps/Sprites.txt (unchanged)`. `git diff --stat fxdata/`
shows **only** `fxdata/manifest.json` (12 insertions, 2 deletions): the
`data/map.json` input hash/size changed and the two new mask PNGs are now
tracked inputs. `git diff --exit-code` on the blob + both headers + Sprites is
empty (IDENTICAL-OK). The packed blob is 249 B, identical to the checkpoint.

## Verification (exact)

- `make gen` — zones section unchanged (see above).
- `git diff --stat fxdata/` — only `fxdata/manifest.json`.
- `make gen-check` — `fxdata_manifest: PASS (164 generated artifacts unchanged)`.
- `make test` — `Total Passed: 6815 / Total Failed: 0`.
- `make test-tools` — `Ran 383 tests ... OK` (10 new `GenZonesMaskTests`,
  including a byte-for-byte legacy-vs-masked blob round trip).
- `FXTEST_ONLY=test_zones make fxtest-headless` — `zones_test PASSED=82 FAILED=0`,
  `test_zones: PASS`.
- `FXTEST_ONLY=test_assets make fxtest-headless` — `asset_test PASSED=264 FAILED=0`,
  `test_assets: PASS`.
- `make size` — `flash=29426/29696 (270 free)  ram=1814/2560`; `size-line`
  rebuild identical (cold-check). **Delta: +0 B flash / +0 B RAM** — data +
  tooling only, no firmware change.

## Files

Modified: `data/map.json`, `fxdata/manifest.json`, `tools/gen-zones.py`,
`tools/gen-hitboxes.py`, `tools/gen.sh`, `tools/tests/test_gen_zones.py`,
`docs/map-zones.md`, `docs/creature-framework.md`.
Added (sources): `images/masks/mh_map_camp_128x56.png`,
`images/masks/mh_map_area_384x112.png`.

No git commit/push (orchestrator handles it).
