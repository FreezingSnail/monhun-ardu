# monhun-ardu-isp.1 — hub is the root; menu deleted; save v4 weapon

Baseline HEAD `04c2c63`, clean. **DONE — all gates green, no commit/push.**

## Size (final gate)

```
size: flash=28804/29696 (892 free)  ram=1701/2560
size: .text=28790 .data=14 .bss=1687
```

| | baseline | isp.1 | delta |
|---|---|---|---|
| flash | 29274 (422 free) | 28804 (892 free) | **-470 B reclaimed** |
| RAM | 1709 (851 free) | 1701 (859 free) | **-8 B** |

The deleted opening-menu FSM (`menu_state.hpp`), its FX render (`menu.hpp`) and
the runtime menu branch reclaimed the flash; the v4 save adds one byte
(`weapon`) while the removed `MenuState`/`menu` state offsets the RAM.

## What changed

- **A. Save v4** (`src/core/save.hpp`): `SAVE_VERSION = 4`; `weapon` u8 at byte
  26 (`SAVE_WEAPON_OFF`), checksum at 27, `SAVE_BYTES = 28`. v3 migration
  decodes the full tail against the legacy checksum at 26 with `weapon = 0`
  (`saveDecodeV3`); v1/v2 keep their shared-prefix path (`saveDecodePrefix`
  validates magic + the byte-26 checksum). `saveEncode`/`saveDecode`/
  `saveDefaults` + header comment updated.
- **B. `src/app_state.hpp`**: `APP_NAV_MENU`/`appMenuAccept` deleted;
  `appScreenBack(HUB) == APP_NAV_NONE` (root), hub LEAVE row -> `APP_NAV_NONE`;
  `appNavApply(AppNav, ScreenState&, SaveBlock&, Game&, Input&)` drops the
  `MenuState`, `APP_NAV_HUNT` closes the screen and returns true (caller starts
  the hunt); `appMenuRequest` -> `appHubRequest` (returns `APP_NAV_HUB`);
  `menuReturnStep` replaced by `appOverReturnStep(bool over, const Input&,
  bool &prevA)`.
- **C. `src/app_setup.hpp`**: `huntStart(Game&, const SaveBlock&)` — active
  `QuestDef` -> `kind = GOAL_KILL ? def.target : MON_LUNGE`; `newGame(g,
  save.weapon, MODE_HUNT, kind)`; `loadRoom(ROOM_CAMP, SPAWN_CAMP_ENTRY)`.
- **D. `monhun-ardu.ino`**: menu branch/`s_menu`/`drawMenu`/menu includes
  removed; `setup()` boots the hub (`screenEnter(SCREEN_HUB)`); hub HUNT ->
  `huntStart` + quest/upgrade/items/armor arming + `s_huntOver = false`; camp
  hold-B -> `appHubRequest` -> hub; hunt end -> `appOverReturnStep` +
  `appNavApply(appHuntReturn())` (own `s_huntPrevA` edge flag).
- **E. Deleted**: `src/menu.hpp`, `src/menu_state.hpp`, `tst/menu_test.hpp`
  (+ `tst/main.cpp` registration), `tst/fxdatatest/menu_test.hpp` +
  `test_menu.ino`, `tst/fxdatatest/menu_art_test.hpp` + `test_menu_art.ino`.
  FX menu art sheets/manifest left for `hml.2`.
- **F. Suites**: `tst/app_state_test.hpp` rewritten (hub root, HUNT returns
  true, `appHubRequest`, `appOverReturnStep`, no `MenuState`);
  `tst/screens_test.hpp` (v4 offsets, weapon round-trip, v3 migration, menu
  block removed); `tst/quests_test.hpp` (v4 checksum offset);
  `tst/carve_test.hpp` (`appOverReturnStep`);
  `tst/fxdatatest/hub_test.hpp` (boot->hub, hub HUNT, hub B no-op, arming);
  `tst/fxdatatest/screens_test.hpp` (hub B -> NONE, weapon EEPROM round-trip);
  `tst/fxdatatest/zones_test.hpp` (camp hold-B -> hub; `huntStart` weapon +
  kill-target + camp). `asset_test.hpp` menu-blob checks stay (sheets remain
  until `hml.2`).
- **G. Docs**: `README.md` (device layer, controls/hub section, suite+size
  snapshot, repo layout, flash/RAM history), `docs/quests-shops.md` (save v4
  layout + hub-root flow), `docs/feel-design.md` (stale `menu_state` note).

## Verification (exact tails)

`make test`:
```
Total Passed: 6188
Total Failed: 0
```

`make gen-check`:
```
fxdata_manifest: PASS (91 generated artifacts unchanged)
```

`make fxtest-headless` (16 suites; `test_parity` excluded per AGENTS.md):
```
test_assets: PASS (270)
test_audio: PASS (9)
test_boot: PASS (4)
test_combat: PASS (237)
test_data: PASS (348)
test_hub: PASS (70)
test_hud: PASS (29)
test_items: PASS (35)
test_monster_art: PASS (127)
test_perf: PASS (5)
test_player_art: PASS (120)
test_quests: PASS (87)
test_screens: PASS (89)
test_smith: PASS (115)
test_tell: PASS (18)
test_zones: PASS (82)
```
1645 asserts, 0 failures.

`make size`: flash `28804/29696 (892 free)`, RAM `1701/2560`.

## Notes / deviations

- `test_hub.ino` compiles with `MH_ROOM_BOUNDS 0` (pre-existing flash carve), so
  the camp-room start is verified in `test_zones` (bounds on) instead; `hub_test`
  verifies the save weapon / quest-kind arming. The two standalone `huntStart`
  blocks added to `hub_test` were removed to fit that image's stock-flag flash
  budget (was 29986 B > 29696); coverage moved to `test_zones`.
- No blockers.
