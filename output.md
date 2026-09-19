# monhun-ardu-fie.6 — Demo flow: menu -> camp -> area -> menu (POLE -> pole room -> menu)

Baseline HEAD 60b7636, tree clean. No commit/push (orchestrator commits).

## What changed

- `src/menu_state.hpp` — `menuStart()` now starts the picked scene in its demo
  room after `newGame()`: a beast pick `loadRoom(ROOM_CAMP, SPAWN_CAMP_ENTRY)`,
  a pole pick `loadRoom(ROOM_POLE_ROOM, SPAWN_POLE_ROOM_START)` then
  `initPoleKind()` (variant installed after the room load so the safe room's
  target clear cannot drop it).
- `src/app_state.hpp` — new `appMenuRequest(Game &) -> AppNav`: consumes
  `Game::menuRequest` exactly once and returns `APP_NAV_MENU` (else
  `APP_NAV_NONE`), so a held B cannot re-fire. Demo-flow header comment updated.
- `monhun-ardu.ino` — sim branch consumes `appMenuRequest(g)` after
  `stepGame()`; on a request it applies `APP_NAV_MENU` and returns (camp hold-B
  sheathed, pole-room door). Hunt win/loss + A still uses `appHuntReturn()`.
- Host tests `tst/app_state_test.hpp` — menu A now asserts camp start (roomId /
  safe), new pole-pick -> pole_room test, new `appMenuRequest` consume test,
  fresh-hunt restarts in camp.
- Device tests `tst/fxdatatest/menu_test.hpp` — `menuStart` room mapping (pole ->
  `ROOM_POLE_ROOM`, hunt -> `ROOM_CAMP`).
- Device tests `tst/fxdatatest/zones_test.hpp` — new section 6 demo app flow:
  menu A -> camp, hold-B -> `menuRequest` -> `appMenuRequest` -> menu, pole pick
  -> pole_room + door -> menu. Lives here (not `test_hub`) because `test_hub.ino`
  carries the intentional `MH_ROOM_BOUNDS 0` carve; this suite already compiles
  the room runtime and has headroom.

Core changes were already in place (fie.4/fie.5): camp/area/pole_room doors in
`data/map.json`, `Game::menuRequest`, safe rooms, monster persistence, tent heal.

## Interfaces

- `mh::appMenuRequest(Game &g) -> AppNav` (new): consumes the core menu request.
- `mh::menuStart(Game &, const MenuState &)`: now also selects the start room.

## Verification

`make test`
```
Total Passed: 5375
Total Failed: 0
```

`make fxtest-headless` (all 17 suites)
```
test_assets: PASS
test_audio: PASS
test_boot: PASS
test_combat: PASS
test_data: PASS
test_hub: PASS
test_hud: PASS
test_menu_art: PASS
test_menu: PASS
test_monster_art: PASS
test_parity: PASS
test_perf: PASS
test_player_art: PASS
test_quests: PASS
test_screens: PASS
test_smith: PASS
test_zones: PASS
```
New/updated device suites: `test_menu` PASSED=80 FAILED=0 (15996 B),
`test_zones` PASSED=61 FAILED=0 (25992 B).

`make size`
```
Sketch uses 29468 bytes (99%) of program storage space. Maximum is 29696 bytes.
Global variables use 1780 bytes (69%) of dynamic memory, leaving 780 bytes for local variables. Maximum is 2560 bytes.
size: .text=29428 .data=40 .bss=1740
size: flash=29468/29696 (228 free)  ram=1780/2560
```
Shipping delta vs baseline 29382 -> 29468 = +86 B; 228 B free (budget ok).

`make gen-check`
```
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (82 generated artifacts unchanged)
```

`git status --short`: only the 6 intended source/test files modified; no generated
artifact drift.

## Notes

- No float/double added; integer/door/camera paths unchanged.
- `test_hub` left untouched (its `MH_ROOM_BOUNDS 0` carve cannot host the room
  runtime within the 29696 B test-image limit; demo-flow room coverage added to
  `test_zones` instead).
