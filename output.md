# monhun-ardu-feel.21 — data: gather nodes (map props gain item/yield fields)

HEAD at start: `3de714b` (feel.20), clean tree. No commit/push (orchestrator commits).

## Schema / packing

- `data/map.json` props gain optional `"gather": {"item": "herb", "yield": N}`.
  `tools/gen-zones.py` validates `item` against `GATHER_ITEMS = ("herb",)`,
  `yield` 1..9, unknown keys rejected (prop `gather` block and the prop itself).
  The prop's own rect is the gather rect and keeps the existing inside-room check.
- Prop record 9 B -> 11 B: `gatherItem u8` (0 = `GATHER_NONE`, else item index+1)
  + `gatherYield u8`. `PROP_SIZE = 11`.
- Generated: `GATHER_NONE = 0`, `GATHER_HERB = 1`,
  `PROP_GATHER_ITEM_OFF = 9`, `PROP_GATHER_YIELD_OFF = 10`; host `zone_data::Prop`
  gains `gatherItem`/`gatherYield`; `mh::ZoneProp` gains both fields and both
  readers (`zonePropRead`) surface them.

## Authored nodes

- camp 2: `(8,8,8,8)` yield 1, `(72,40,8,8)` yield 2 (clear of tent/heal/door/spawns).
- area 3: `(40,16,8,8)` yield 1, `(160,40,8,8)` yield 2, `(280,88,8,8)` yield 3
  (clear of the door, both spawns and the monster start at 320,72).
- pole_room 0 (unchanged). Nodes reuse the resolved `mh_map_tent` sheet so the
  prop-sheet index order (`SHEET_MH_MAP_TENT` 0, `SHEET_FXPOLE` 1) is unchanged
  and `drawProps`/`propSheet` are untouched — no new art sheet this bead.

`gen-zones --dump` tail:

```
room area: 384x112 ... props 3 ...
  prop 0: post rect(40,16,8,8) sheet mh_map_tent frame 0 gather herb x1
  prop 1: post rect(160,40,8,8) sheet mh_map_tent frame 0 gather herb x2
  prop 2: post rect(280,88,8,8) sheet mh_map_tent frame 0 gather herb x3
room camp: 128x56 ... props 3 ...
  prop 1: post rect(8,8,8,8) sheet mh_map_tent frame 0 gather herb x1
  prop 2: post rect(72,40,8,8) sheet mh_map_tent frame 0 gather herb x2
gen-zones: 3 rooms, 3 doors, 5 spawns, 7 props, 1 heals, 203 B blob
```

## Tests

- `tools/tests/test_gen_zones.py`: item/yield round-trip + determinism, unknown
  item, zero/above-max yield, unknown gather key, missing item, out-of-room rect.
  Fixture blob 113 -> 115 B.
- `tst/zone_test.hpp`: host reader pins (tent `GATHER_NONE`; camp herbs herb x1/x2;
  area herb x1; pole prop none).
- `tst/fxdatatest/zones_test.hpp`: camp prop count 1 -> 3 + cart reader spot checks.

## Verification (exact tails)

`make gen` (x3 until stable) + `make gen-check`:

```
gen-zones: 3 rooms, 3 doors, 5 spawns, 7 props, 1 heals, 203 B blob
fxdata_manifest: PASS (82 generated artifacts unchanged)
```

`make test`:

```
Total Passed: 5982
Total Failed: 0
```

`make test-tools`:

```
Ran 209 tests in 11.916s

OK
```

`make fxtest-headless` (full; test_parity excluded per AGENTS.md):

```
asset_test PASSED=270 FAILED=0
test_audio PASSED=17 FAILED=0
test_boot PASSED=4 FAILED=0
combat_test PASSED=237 FAILED=0
data_test PASSED=368 FAILED=0
test_hub PASSED=57 FAILED=0
test_hud PASSED=17 FAILED=0
test_menu_art PASSED=60 FAILED=0
menu_test PASSED=66 FAILED=0
test_monster_art PASSED=111 FAILED=0
perf_test PASSED=5 FAILED=0
test_player_art PASSED=111 FAILED=0
test_quests PASSED=50 FAILED=0
test_screens PASSED=78 FAILED=0
test_smith PASSED=66 FAILED=0
test_tell PASSED=17 FAILED=0
zones_test PASSED=76 FAILED=0
test_perf: B pUs=6367 pHz=157 lHz=52 lTk=452 rMx=4744 rAv=4514 ram=703
```

`make size`:

```
size: .text=27544 .data=28 .bss=1594
size: flash=27572/29696 (2124 free)  ram=1622/2560
```

Flash delta vs feel.20 baseline: **0 B** (cart data only; zones.bin 144 -> 203 B,
+59 B, which shifts the baked equip sheet offsets +59 in `equip_meta.hpp`/`equip.bin`
— both regenerated, not hand-edited).

## Docs

`docs/map-zones.md`: prop blob row 9 -> 11 B, `gather` schema bullet, `gatherItem`/
`gatherYield` semantics, test-coverage note.

## Notes

- `gatherItem` is the render signal for the feel.22 procedural plant; the sheet
  field is a placeholder (`mh_map_tent`) until then, so no new art sheet was added.
- Generated sets staged together (`fxdata.bin`, `fxdata-data.bin`, `fxdata.h` /
  `src/fxdata.h`, `manifest.json`, `zones.bin`, `zone_*.hpp`, `equip.*`).
