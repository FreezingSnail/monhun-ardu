# monhun-ardu-5r1 — demo flow: menu -> hunt -> menu (bypass hub)

Epic: monhun-ardu-nch. No commit/push/git-add per worker protocol.

## What changed

- `src/app_state.hpp`
  - `appMenuAccept()` now returns `APP_NAV_HUNT` (was `APP_NAV_HUB`).
  - `appHuntReturn()` now returns `APP_NAV_MENU` (was `APP_NAV_HUB`).
  - `APP_NAV_HUNT` case sets `menu.active = false` (the menu itself launched
    the hunt in the demo flow; previously the menu->hub step closed it).
  - Header/flow comments rewritten: demo loop + shelf graph.
- `monhun-ardu.ino`
  - Menu branch: `appNavApply(appMenuAccept(), ...)` -> when it reports a hunt
    start, arm `questApplyToGame`/`upgradeApplyToGame` and clear `s_huntOver`
    (same block the screen branch used). Comments updated.
  - Hun-end comment now says returns to the menu.
- `src/menu_state.hpp` — comments only (A -> hunt), behavior unchanged.
- `tst/app_state_test.hpp` — re-pointed menu A / hunt end; added demo round
  trip with death -> menu and fresh-hunt state-reset asserts; kept the shelf hub
  graph (hub/smith detour still starts the picked hunt); `appHuntReturn() ==
  APP_NAV_MENU` asserted explicitly.
- `tst/fxdatatest/hub_test.hpp` — boot menu A -> hunt directly (screen inactive);
  shelf hub/quests/smith/EEPROM + hub pixel checks retained via a direct
  `APP_NAV_HUB` entry; second hunt from menu with quest/tier armed; win + commit;
  over + A returns to the menu; fresh-hunt reset asserts.
- `tst/screens_test.hpp` — comments only (menu A routes to hunt).
- `README.md`, `docs/quests-shops.md` — document the flow change; hub/quests/
  smith marked shelf code off the demo path; no quit input (win/loss + A is the
  only hunt exit).

## State reset / quest-commit semantics

- `appNavApply(APP_NAV_HUNT)` -> `menuStart` -> `newGame` -> `initGame` +
  `initMonster` + `initWorld`: clears projectiles, effects, pole/train, tick,
  over and quest counters. Pinned by new asserts (host + device).
- Quest/zenny commit: unchanged. On the demo path no quest can be taken (hub is
  unreachable), but if EEPROM already carries an active quest/tier it still
  applies at hunt start and `appHuntCommit` writes progress exactly once per
  hunt. No double count. Hub/quests/smith code stays compiled and tested.

## Verification (exact commands + tails)

1. `make gen` x2 then `make gen-check` — PASS
   - `fxdata_manifest: PASS (62 generated artifacts unchanged)`
2. `make test` — `Total Passed: 3521  Total Failed: 0` (was 3500/0)
   `make test-tools` — `Ran 137 tests in 8.007s  OK`
3. `make fxtest-headless` — all PASS
   ```
   test_assets PASSED=270 FAILED=0      test_audio PASSED=14 FAILED=0
   test_boot PASSED=4 FAILED=0          test_combat PASSED=184 FAILED=0
   test_data PASSED=221 FAILED=0        test_hub PASSED=57 FAILED=0
   test_hud PASSED=17 FAILED=0          test_menu PASSED=59 FAILED=0
   test_parity PASSED=660 FAILED=0      test_perf PASSED=5 FAILED=0
   test_player_art PASSED=111 FAILED=0  test_quests PASSED=50 FAILED=0
   test_screens PASSED=78 FAILED=0      test_smith PASSED=66 FAILED=0
   ```
   perf tail: `B pUs=6375 pHz=156 lHz=52 lTk=528 rMx=4968 rAv=4757 ram=602`
   (baseline `rMx=4968 rAv=4757 pUs=6375` — identical)
4. `make build` + `make size`
   - `Sketch uses 26436 bytes (89%)` / `Global variables use 1863 bytes`
   - `size: flash=26436/29696 (3260 free)  ram=1863/2560`
   - vs baseline 26588/3108: flash delta **-152 B**, headroom +152
5. Parity fixtures — `node tools/gen-parity-fixtures.js`
   - `scenes=20 ticks=1269 snapshots=32 cpFields=20`
   - `git status --short` shows `parity_fixtures.hpp` NOT modified -> empty diff,
     byte-identical.

## Deviations / notes

- Host assert count moved 3500 -> 3521 (re-pointed + added flow/reset asserts;
  no coverage deleted).
- Flash dropped 152 B net despite the added branch: LTO folds the menu path now
  that it no longer bridges menu -> hub.
- `quit`: the demo has no quit input; win/loss `over` + A is the only hunt exit.
  Documented in README.
- Untracked/none: no generated artifact changed; no commit made.
