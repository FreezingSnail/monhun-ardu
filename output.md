# monhun-ardu-5co.6 — ui.3.1 trim: armor craft via card, remove smith screen

Status: DONE. HEAD fbb636f + working tree (no commit/push, per orchestrator
flow). Armor crafting moved off the SMITH screen rows onto the GEAR armor card
(the craft bill bakes into the `mhCards` record); the SMITH screen, its hub row
and the whole `APP_NAV_SMITH` routing were removed. FORGE replaces SMITH in
ui.4 (README + docs/quests-shops.md + docs/equipment-framework.md say so).

## Size

Baseline (HEAD fbb636f, ui.3):
```
size: flash=29680/29696 (16 free)  ram=1762/2560
```
After:
```
size: flash=29172/29696 (524 free)  ram=1764/2560
```
**Reclaimed: 508 B flash** (+2 B RAM: `CardItem` grew 6 B for the bill, offset
by the removed `Game::smithyRequest` bool). Target was ~400 B.

Reclaim breakdown (whole-image deltas, `make size`):
- `COND_ARMOR` + `ACTION_CRAFT_ARMOR` + `screenRowArmorRecipe` removed, card
  craft re-added (`armorCardState`/`cardArmorApply`, 176 B): net −200.
- SMITH screen + hub SMITH row + `APP_NAV_SMITH`/`APP_NAV_CAMP`/
  `appSmithyRequest` routing: net −~50 (screen data is cart bytes, not flash).
- dead `COND_CRAFTED` + `ACTION_EQUIP_ARMOR` switch cases: −~60.
- card hint state sharing (ArmorCardState mirrors CardHint 1:1, one gate
  decode): −56.
- camp smithy producer (`trySmithy` + `zoneSmithyRead` + `smithyRequest`):
  **−194** (measured by stubbing the `trySmithy` call). See the scope note.

## What changed

Data + generated:
- `tools/gen-cards.py` — `ITEM_SIZE` 27 → 33: appends an armor craft bill
  (u16 zenny + `CRAFT_MAT_SLOTS`×`{itemIdx+1, count}`) to every record; quests
  carry a zero bill. New `card_meta.hpp` constants `CRAFT_MAT_SLOTS`,
  `ITEM_CRAFT_OFF`, `ITEM_CRAFT_COST_OFF`, `ITEM_CRAFT_MAT_OFF/STRIDE`.
- `data/screens/smith.json` deleted; `data/screens/hub.json` loses the SMITH
  row (HUNT/QUESTS/GEAR + ZENNY); `data/screens/gear.json` armor rows switch
  `crafted` → `always` (the card gates craft).
- `tools/gen-screens.py` — dropped `open_smith`/`craft_armor` actions and the
  `armor`/`crafted` conditions (ids renumber; all references are generated
  constants). Regenerated set staged together (`fxdata/*`, `src/generated/*`,
  `src/fxdata.h`).

Runtime:
- `src/card_state.hpp` — `CardItem.craft[6]` (byte-identical record tail),
  `ArmorCardState` (DEAD/CRAFT/EQUIP/UNEQUIP/NEED_PARTS/NEED_ZENNY),
  `armorCardCost`/`armorCardState` (one gate decode), `cardArmorApply` (craft
  from the baked bill then `armorEquipToggle`), `cardHint(save, row, item)`.
  `ArmorCardState` mirrors `CardHint` 1:1 so the hint is a cast.
- `src/cards.hpp` — `drawCard` passes the cached record to `cardHint`; hint
  string offset table reordered to the new `CardHint` order.
- `src/screen_state.hpp` — removed the `COND_ARMOR`/`COND_CRAFTED` conditions,
  the `ACTION_CRAFT_ARMOR`/`ACTION_EQUIP_ARMOR` action cases and the now-dead
  `screenRecipeOk`/`screenRecipeDebit`; `ScreenRow.recipe[]` now only carries
  the quest turn-in reward.
- `src/screens.hpp` — removed `screenRowArmorRecipe` + the `smith.hpp` include;
  `screenReadRow` only fills the quest fields.
- `src/app_state.hpp` — removed `APP_NAV_SMITH`, `APP_NAV_CAMP`,
  `appSmithyRequest` and their `appScreenAccept`/`appNavApply` cases.
- `monhun-ardu.ino` — card A dispatches armor to `cardArmorApply`, quests to
  `screenApplyAction`; camp-smith/`s_smithyFromCamp` branches removed.
- `src/core/world.hpp` / `game.hpp` / `player.hpp` — the camp smithy producer
  (`trySmithy`, `Game::smithyRequest`) was removed with its only consumer (the
  SMITH screen); the room smithy rect data stays for the ui.4 FORGE trees.

Tests (permanent, co-located, native frameworks, no /tmp):
- `tst/card_state_test.hpp` — new armor card helpers (`armorCard`, `helmCard`)
  and `cardArmorApply` coverage (craft gate/debit/equip, crafted toggle, blocked
  zenny/parts, bad ids); hint rule rewritten for the card bill.
- `tst/armor_engine_test.hpp` — dropped the `COND_ARMOR`/`ACTION_CRAFT_ARMOR`
  row test (the craft moved to the card); engine/aggregation coverage unchanged.
- `tst/screens_test.hpp` — dropped the `COND_CRAFTED` gear-row test (armor is
  card-only now).
- `tst/app_state_test.hpp` — SMITH/APP_NAV_SMITH/APP_NAV_CAMP/camp-smithy tests
  removed; hub graph is now hub/quests/gear.
- `tst/fxdatatest/cards_test.hpp` — gear armor row → card → `cardArmorApply`
  craft E2E (debits zenny+materials, sets the bit, auto-equips, drops PARTS,
  EEPROM roundtrip); baked-bill ABI asserts.
- `tst/fxdatatest/screens_test.hpp` — 3 screens, gear index 2, 4-row hub,
  always-live armor rows; the direct armor equip E2E moved to the card.
- `tst/fxdatatest/hub_test.hpp` — SMITH detour replaced by a hub→gear→hub
  round trip; the card craft E2E lives in cards_test (keeps test_hub's flash
  budget).
- `tst/fxdatatest/smith_test.hpp` — SMITH screen rows/nav/craft removed; keeps
  the upgrade table + multiplier + cart armor aggregation (crafts via
  `saveSetCrafted` + `armorEquipToggle`).
- `tools/tests/test_gen_cards.py` — ITEM_SIZE 33, craft-bill parse + asserts,
  updated meta offsets. `tools/tests/test_gen_screens.py` — new action ids,
  `equip_armor` slot validation, removed open_smith/craft_armor/armor.

Docs: `README.md` (status/architecture/pipeline/counts), `docs/quests-shops.md`
(status/layers/conditions/actions/cards/gear/smith), `docs/equipment-framework.md`
(recipe + craft/equip sections).

## Scope note (justified deviation)

The bead listed `COND_ARMOR`/`ACTION_CRAFT_ARMOR`/`screenRowArmorRecipe`, the
smith armor rows and the SMITH screen + `APP_NAV_SMITH` routing. The camp
smithy producer (`Game::smithyRequest` / `trySmithy` / the zone smithy rect
read) had exactly one consumer — the SMITH screen — so it was dead after the
removal; it was deleted too (**−194 B**, needed to clear the ~400 B target). The
zone smithy rect *data* stays for the ui.4 FORGE trees; the ui-design places
FORGE on the hub, so the camp forge access was not re-wired here. If ui.4 wants
the camp forge back, it re-adds a small consumer for `smithyRequest` (the data
is untouched).

## Gate tails

`make gen` (card line):
```
gen-cards: 9 items, 32 pages, 305 B blob (magic 0x4341 version 1)
gen-cards: src/generated/card_meta.hpp
```
`make gen-check`:
```
fxdata_manifest: PASS (122 generated artifacts unchanged)
```
`make test`:
```
Total Passed: 6232
Total Failed: 0
```
`make test-tools`:
```
Ran 324 tests in 18.204s
OK
```
`make fxtest-headless` (full, 17/17):
```
test_assets PASSED=264  test_audio PASSED=9    test_boot PASSED=4
test_cards PASSED=83    test_combat PASSED=237 test_data PASSED=348
test_hub PASSED=77      test_hud PASSED=29     test_items PASSED=35
test_monster_art PASSED=127  test_perf PASSED=5  test_player_art PASSED=120
test_quests PASSED=87   test_screens PASSED=136  test_smith PASSED=51
test_tell PASSED=18     test_zones PASSED=82
```
`make size`:
```
size: .text=29156 .data=16 .bss=1748
size: flash=29172/29696 (524 free)  ram=1764/2560
```

## Blockers

None. Free headroom is now 524 B (was 16 B); the next bead (ui.4 FORGE trees +
save v5) has its budget pool.
