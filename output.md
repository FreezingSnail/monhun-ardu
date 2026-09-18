# monhun-ardu-mgn — qs.4 hub integration: boot flow, screen routing, return paths

## Summary

Wired the data-driven screens into the real boot flow and made the hub the
central menu. Implemented x1, mocked x0, faked x0.

- `data/screens/hub.json` is now the real hub: `HUNT` / `QUESTS` / `SMITH`
  navigation rows plus a `ZENNY` dynamic-value row.
- Boo flow: opening menu `--A-->` hub; `HUNT` starts the picked loadout;
  `QUESTS`/`SMITH` open their screens; `B` backs one level (screens -> hub,
  hub -> menu). `LEAVE` rows follow the same rule.
- Hunt end (win/lose) `--A-->` hub; EEPROM quest progress committed exactly once
  per hunt.
- Removed the `MENU_SCREEN` B-edge stub from `menu_state.hpp` (it was only the
  qs.1 test entry into the hub; A -> hub is the real entry now and B is no
  longer a menu action).

## Changes

- `data/screens/hub.json` — real hub rows (HUNT / QUESTS / SMITH / ZENNY).
- `tools/gen-screens.py` — action enum extended with `none`, `hunt`,
  `open_quests`, `open_smith`; new `ROW_F_ZENNY` flag token (dynamic value:
  draw the live `save.zenny` balance in the cost column).
- `src/generated/screen_meta.hpp`, `fxdata/tables/screens.bin` (generated) —
  4 hub rows, blob 294 -> 297 B.
- `src/menu_state.hpp` — `MENU_SCREEN` removed; `MENU_START` -> `MENU_ACCEPT`
  (A opens the hub). B is silent in the menu.
- `src/screen_state.hpp` — new pure `screenReset(s, screen, rowCount)` helper
  (cart-free); `screenEnter()` now calls it.
- `src/screens.hpp` — renderer reads the row flags; `ROW_F_ZENNY` rows draw the
  live save balance instead of the packed cost.
- `src/app_state.hpp` (new, host-testable) — `AppNav` boot-flow routing
  (`appMenuAccept`, `appScreenBack`, `appScreenAccept`, `appHuntReturn`,
  `appNavApply`, `appHuntCommit`). Transition input seeds the new owner's A/B
  edge flags so a held button cannot re-fire through the new screen; hunt-end
  progress commit is once-per-hunt latched.
- `src/app_setup.hpp` (new, device-only) — `questApplyToGame` / `upgradeApplyToGame`
  cart glue moved out of the sketch so the device E2E suite drives the real path.
- `monhun-ardu.ino` — run() dispatches menu/screen/sim through `app_state.hpp`;
  hunt end returns to the hub; `appHuntCommit` owns the save commit.
- `tools/gen-art.py` — menu footer `A START` -> `A HUB`.
- Tests: `tst/app_state_test.hpp` (new host suite, 86 asserts), `tst/screens_test.hpp`
  menu-edge test updated, `tst/menu_test.hpp` + `tst/fxdatatest/menu_test.hpp`
  `MENU_ACCEPT` rename, `tst/fxdatatest/screens_test.hpp` hub rows + zenny pixel
  checks, `tst/fxdatatest/hub_test.hpp` + `test_hub.ino` (new device E2E),
  `tools/tests/test_gen_screens.py` nav-action/zenny-flag tests.
- `README.md`, `src/menu.hpp` — flow/docs updated.

## Verification (exact commands + tails)

1. `make gen` twice then `make gen-check`:
   `fxdata_manifest: PASS (59 generated artifacts unchanged)` — PASS.

2. `make test`: `Total Passed: 3422  Total Failed: 0` (3363 + 59 new app/nav).
   `make test-tools`: `Ran 137 tests ... OK`.

3. `make fxtest-headless` (full, 14 suites) — all PASS:
   assets 262, audio 14, boot 4, combat 184, data 221, **hub 41**, hud 17,
   menu 59, parity 660, perf 5, player_art 111, quests 50, screens 78, smith 66.
   perf: `B pUs=6376 pHz=156 lHz=52 lTk=528 rMx=4968 rAv=4756 ram=606` —
   identical to baseline `rMx=4968 rAv=4756 pUs=6376`.

4. `make build` + `make size`:
   `size: flash=26456/29696 (3240 free)  ram=1863/2560`.
   Delta vs baseline 26048/3648: **flash +408 B, RAM +8 B**.

5. Parity fixtures: `node tools/gen-parity-fixtures.js` -> `git diff --stat
   tst/fxdatatest/parity_fixtures.hpp` empty — byte-identical.

## Deviations / notes

- `MENU_START` was renamed `MENU_ACCEPT` rather than kept as `MENU_START`; the
  only users were menu_test/screens_test (updated). `menuStart()` (the sim
  mapping) is unchanged and still used by the HUNT row.
- `questApplyToGame`/`upgradeApplyToGame` were extracted from the sketch to
  `src/app_setup.hpp` unchanged so the new device E2E suite can exercise them.
- Budget: +408 B flash (mostly the routing switch, `app_setup.hpp` inlining and
  the extra hub row/flag handling); 3240 B free. Perf and parity untouched.
