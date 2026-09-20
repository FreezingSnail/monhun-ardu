# monhun-ardu-prg.4 — gather: item variety (blue mushroom, ore, bug)

HEAD at start: `5b23f56` (prg.3 carve). No commit/push (orchestrator commits).
End state: full gate green — gen (x2), gen-check, host, tooling, full
`fxtest-headless`, size. Flash delta **+144 B** (27770 -> 27914), RAM flat.

## What landed

- `data/map.json`: authored 6 new gather nodes alongside the 5 existing herb
  nodes (2 camp + 3 area), placed clear of spawns, doors, heal rects and the
  area monster start:
  - camp `PROP_CAMP_3` blue_mushroom x1 @ (24,8,8,8)
  - area `PROP_AREA_3` blue_mushroom x1 @ (96,80,8,8)
  - area `PROP_AREA_4` blue_mushroom x2 @ (216,16,8,8)
  - area `PROP_AREA_5` ore x1 @ (120,88,8,8)
  - area `PROP_AREA_6` ore x2 @ (352,16,8,8)
  - area `PROP_AREA_7` bug x1 @ (200,96,8,8)
  Zone blob 160 -> 226 B (12 props, +66 B of record data).
- Runtime gather needed **no** change: prg.2's `applyGather` already maps
  `prop.gatherItem - 1` onto the item table slot generically (verified in
  `src/core/items.hpp`). Only the stale "7 props" comment updated to 12;
  `gatherMask` u16 still fits (12 <= 16, static_assert holds).
- `src/render.hpp`: `drawGatherNode()` varies the procedural marker by packed
  item kind (no new art): herb stem/leaf/flower (unchanged), mushroom light
  stalk + white cap, ore squat dark rock + light facet, bug low wing + body.
  A 2x2 white prompt marks the node top while the hunter overlaps it; the
  overlap rect is the same one `gatherNodeAt` uses, so prompt == hit window.
  Picked nodes still draw nothing (ground blit from `drawRoom` shows through).
- HUD unchanged: `drawItemCount` keeps showing the herb count only (per task;
  the helper is already generic for later beads).
- `docs/map-zones.md`: node list table added, render note updated.

## Tests

- `tools/tests/test_gen_zones.py`: new `test_gather_every_item_packs_index_plus_one`
  round-trips herb/blue_mushroom/ore/bug -> codes 1/2/3/4 through the packed
  blob + emitted `GATHER_*` (existing blue_mushroom test kept).
- `tst/gather_test.hpp`: 4 new host tests — camp mushroom slot, area ore slot,
  area bug slot, second mushroom yield stacks to 2; each asserts the bound node
  symbol, correct inventory slot, herb slot untouched, depletion.
- `tst/zone_test.hpp`: gather-prop reader now pins camp mushroom + area
  mushroom/ore/bug records (symbolic ids only).
- `tst/fxdatatest/zones_test.hpp`: camp prop count 3 -> 4, camp prop range
  cached 3 -> 4, and cart spot checks for the camp mushroom + area
  mushroom/ore/bug records (`zonePropRead` through the mhZones blob).

## Verify (exact tails)

- `make gen` (x2): `gen-zones: 2 rooms, 2 doors, 4 spawns, 12 props, 1 heals, 226 B blob`.
- `make gen-check`: `fxdata_manifest: PASS (85 generated artifacts unchanged)`.
- `make test`: `Total Passed: 6009` / `Total Failed: 0`.
- `make test-tools`: `Ran 240 tests in 12.565s` / `OK`.
- `make fxtest-headless` (full):
  - `test_perf`: `B pUs=6348 pHz=157 lHz=52 lTk=480 rMx=3344 rAv=3074 ram=713`
    `perf_test PASSED=5 FAILED=0` -> PASS
  - `test_zones`: `zones_test PASSED=80 FAILED=0` -> PASS
  - all other maintained suites PASS (combat 237, data 368, hub 57, hud 25,
    items 35, menu_art 53, menu 60, monster_art 111, player_art 111, quests 50,
    screens 78, smith 66, tell 17).
- `make size`: `size: flash=27914/29696 (1782 free)  ram=1593/2560`
  (baseline flash 27770/1926 free -> **+144 B**, within the <=300 B target;
  RAM unchanged). Data facts unchanged from baseline.

## Deviations / notes

- No `--dump` invocation (the sandbox denies direct `python3`); the compiled
  node records are evidenced by `src/generated/zone_data.hpp` (`Prop[12]` with
  the item/yield codes) plus the fixture `--dump` smoke and the new round-trip.
- Parity image / `mock/` untouched.
