# monhun-ardu-087 — Hunt launches from the quest card; hub HUNT row removed

Status: DONE (verify/finish of an uncommitted partial after the primary worker
died mid-run on a provider HTTP 400; re-dispatched to slugineer-worker-go).

## Mid-run re-dispatch

The primary worker had already left a ~28-file uncommitted tree (hub.json + a
full `make gen` regen, sketch, `app_state.hpp`, `card_state.hpp`, `cards.hpp`,
host + device suites, test_hub/test_screens RAM/stack carve). I did NOT restart:
I diffed every source/test file against the frozen `--design`/`--acceptance`,
confirmed the behaviors below, then fixed the one real gap (README was not
updated) plus one missing host pin (`SCREEN_HUB_ROWS == 4`). No partial file was
wrong for the design; nothing was reverted.

## Frozen behaviors confirmed implemented

- Hub rows QUESTS(0) / MAP(1) / FORGE(2) / GEAR(3), cursor boots on QUESTS
  (`data/screens/hub.json`, `src/generated/screen_meta.hpp` SIZE 1516 / ROW_COUNT
  61 / `SCREEN_HUB_ROWS` 4, `app_state.hpp` `appScreenAccept`).
- Quest card A takes AND launches: `cardApply` -> `screenApplyAction` writes the
  save (fresh take) or no-ops (resume), then `appQuestCardLaunch` +
  `APP_NAV_HUNT` closes card+board and `startHuntFromSave()` arms
  huntStart/quest/upgrade/items/armor + `s_huntOver=false` (`monhun-ardu.ino`
  DETAIL_ACTION). Same helper drives the device E2E.
- `appQuestCardLaunch` == `ACTION_TAKE_QUEST && questIsActive(save, param & 15)`;
  fresh-take / resume / locked cases pinned on host + device.
- `HINT_GO` appended after `HINT_FORGE` (no renumber): `CARD_HINTS` gains
  `"A GO\0"`, `CARD_HINT_OFF[10] = {80,0,8,16,45,56,26,35,67,75}` (GO->75,
  NONE->80 = literal NUL). `cardHint(TAKE_QUEST)` = ACCEPT when cond-ok else GO
  when active else NONE (equivalent to the design's order: active implies
  !takeable, so cond is false either way). `cardDenied` unchanged.
- Hub no longer routes `ACTION_HUNT` (case removed; stray row -> `APP_NAV_NONE`).
  `APP_NAV_HUNT` stays in the enum + `appNavApply` (closes screen, returns true);
  the sketch's old hub "hunt requested" true-branch is gone and the screen-accept
  path keeps `appNavApply` + the GEAR readout.
- Loops unchanged (hunt end + A -> hub, camp hold-B -> hub, TURN_IN board rows,
  GEAR/FORGE/MAP).
- Extra trim the partial carried (not required by the design, verified working):
  `appNavApply` screen destinations collapsed to a PROGMEM `NAV_DESTS[8]` table
  (`static_assert(APP_NAV_HUB==1 && APP_NAV_MAP==8)`).

## Fixed in this pass

- `README.md` — the only real design gap. Updated the device-layer flow
  (`monhun-ardu.ino` / `app_state.hpp` bullets), the card hint list (`A GO`), the
  Hub controls table + "no HUNT row / quest card launches / A GO resume"
  paragraphs, the GEAR "next hunt" + detail-card bullets, the shipping history
  (29674/29696, 22 free), and the host/device test counts.
- `tst/app_state_test.hpp` — added the missing host pin `hub has 4 rows (087)`
  (`SCREEN_HUB_ROWS == 4`).

## Files touched (working tree, not committed)

data/screens/hub.json; docs/quests-shops.md; README.md; fxdata/fxdata-data.bin,
fxdata/fxdata.bin, fxdata/fxdata.h, fxdata/manifest.json,
fxdata/screens/Sprites.txt, fxdata/tables/{cards,equip,screens}.bin;
images/screens/mh_screen_hub_0_128x64.png; monhun-ardu.ino;
src/app_state.hpp, src/card_state.hpp, src/cards.hpp, src/fxdata.h,
src/generated/{art_sheets,equip_meta,screen_meta,zone_meta}.hpp;
tst/app_state_test.hpp, tst/card_state_test.hpp;
tst/fxdatatest/{cards_test,hub_test,screens_test,zones_test}.hpp;
tst/fxdatatest/test_hub.ino, tst/fxdatatest/test_screens.ino.

## Size

```
size: flash=29674/29696 (22 free)  ram=1920/2560
Sketch uses 29674 bytes (99%) of program storage space. Maximum is 29696 bytes.
Global variables use 1920 bytes (75%) of dynamic memory, leaving 640 bytes for local variables. Maximum is 2560 bytes.
```

Baseline 29672 (24 free). Delta **+2 B** (free 24 -> 22). RAM unchanged at 1920.
Fits, no BLOCKED.

## Gates (tails)

- `make gen-check` exit=0; `fxdata_manifest: PASS (222 generated artifacts unchanged)`;
  `src/fxdata.h == fxdata/fxdata.h`.
- `make test` -> `Total Passed: 6989  Total Failed: 0` (was 6973 pre-087).
- `FXTEST_ONLY="test_hub test_screens test_zones test_quests test_cards" make fxtest-headless`:
  ```
  test_cards   PASSED=85  FAILED=0
  test_hub     PASSED=97  FAILED=0
  test_quests  PASSED=112 FAILED=0
  test_screens PASSED=228 FAILED=0
  test_zones   PASSED=77  FAILED=0
  ```
  `fxtest_ram: OK` for all five (test_screens globals 2059, 501 B stack margin;
  test_hub globals 1334; test_zones globals 2017; test_cards 1207; test_quests 1209).
  test_hub carve: `MH_ROOM_BOUNDS` re-enabled, `MH_COMBAT_PARTS 0` added (kept;
  suite passes inside the board budget).
- `make test-tools`: not run (no `tools/` change).

## Wall time per phase (approx; fallback run was not instrumented)

- Diff + design audit: ~8 min
- `make gen-check`: ~50 s
- `make test`: ~2 min
- device suites (5, parallel): ~4 min
- `make size-line`: ~40 s
- README/acceptance-pin fixes + re-verify (`make test`, `gen-check`, `size-line`): ~4 min

## Next

Orchestrator: stage generated sets together (`git add -A`), run the full
19-suite gate, commit. Worker did NOT commit or push.
