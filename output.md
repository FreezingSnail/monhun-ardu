# monhun-ardu-prg.2 — items: item table + inventory (herb migrates)

HEAD at start: `cd5ba11` (prg.8 trim). No commit/push (orchestrator commits).
End state: clean gate — gen (x2), gen-check, host, tooling, 18 device suites,
size all green.

## What landed

- `data/items.json` (new): 8 records `{id, kind, heal, stam, sell}` in source
  order — herb (consumable, heal 20), blue_mushroom (consumable, heal 10), ore,
  bug, scale, shell, fang, tail (materials). Item index == record index;
  `ITEM_MAX` 16 caps the inventory.
- `tools/gen-items.py` (new): validates id/kind/ranges/unknown keys, packs the
  8 B header + 5 B `{kind,heal,stam,sell}` records into `fxdata/tables/items.bin`,
  emits `src/generated/items_data.hpp` (host struct + array), `items_meta.hpp`
  (ABI + `ITEM_COUNT`/`ITEM_SIZE`/`ITEM_<NAME>` indices), `items_expect.hpp`
  (sizes + spot pins + sha256).
- `src/core/items.hpp`: `ItemInfo` + `itemRead`/`itemKind`/`itemHeal`/`itemSell`
  via the host/AVR shim (host `item_data::ITEMS`, AVR `mhItems` + `mhFxRead*`),
  plus generic `itemCount`/`itemAdd`/`itemConsume`. `applyGather` uses `itemAdd`;
  `applyItemUse` consumes one herb and heals `itemHeal(ITEM_HERB)` (20, from the
  table). Herb path behavior byte-identical: gather -> count -> hold-B eat 20.
- `src/core/game.hpp`: includes `items_meta.hpp`; `ITEM_COUNT`/`ITEM_HERB`/... are
  aliases of the generated `item::ITEM_*` ids; `Game::items[ITEM_COUNT]`;
  `HERB_HEAL` removed (table owns the value).
- `src/render.hpp`: generic `drawItemCount(x,y,count)` helper; the herb HUD calls
  it (same 1 px glyph + digit, pixel-identical).
- `tools/gen-zones.py`: reads `data/items.json`; `GATHER_ITEMS` is now
  `(herb, blue_mushroom, ore, bug)` and each `zone::GATHER_<NAME>` is that item's
  index + 1. Zone blob unchanged for the shipped map (herb still code 1).
- Wiring: `fxdata/fxdata.txt` `raw_t mhItems`, `tools/gen.sh` (runs first, before
  the fxdump host build that includes `game.hpp`), `tools/fxdata_manifest.py`
  OUTPUT_PATHS, and the manifest fixture.
- Tests: host `tst/items_test.hpp` (table pins + inventory verbs + gather-code
  mapping), device `tst/fxdatatest/items_test.hpp` + `test_items.ino` (blob bytes
  + shipping readers + inventory verbs), `tools/tests/test_gen_items.py`,
  gen-zones items dependency tests, manifest fixture/count updates.

## Gates (tails)

`make gen` (x2, then stable) + `make gen-check`:
```
fxdata_manifest: fxdata/manifest.json up to date (51 images, 60 inputs, 23 outputs)
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (85 generated artifacts unchanged)
```

`make test`:
```
Total Passed: 5851
Total Failed: 0
```

`make test-tools`:
```
Ran 231 tests in 13.927s
OK
```

`make fxtest-headless` (full; test_parity excluded by design):
```
18/18 suites PASS: assets audio boot combat data hub hud items menu_art menu
monster_art perf player_art quests screens smith tell zones
test_items PASSED=25 FAILED=0
```
`test_perf` line:
```
B pUs=6348 pHz=157 lHz=52 lTk=480 rMx=3348 rAv=3075 ram=719
```
`rMx`/`rAv` unchanged from prg.8 (3348/3075, budget 7407).

`make size`:
```
size: .text=27160 .data=20 .bss=1571
size: flash=27180/29696 (2516 free)  ram=1591/2560
```

## Budget

Baseline prg.8 `flash=27128 (2568 free) ram=1584`; this bead **+52 B flash /
+7 B RAM** (the +7 is `Game::items[8]` vs `items[1]`; the item reader + generic
helpers are the flash). Well under the 900 B target. `zones.bin` is byte-identical
to prg.8; `equip_meta.hpp`/`equip.bin` shifted because `mhItems` sits before the
sprite sections (baked offsets, two-pass `make gen` converges).

## Deviation

Item ids are 0-based (herb index 0, gather code 1) to keep the shipped zone blob
and herb path byte-identical; `bd` design's "herb becomes item id 1" is read as
the gather code (index+1), which is unchanged.
