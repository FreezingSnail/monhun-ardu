# monhun-ardu-6k2 — Cut the MAP screen (hub MAP row + SCREEN_MAP + baked pages)

Status: DONE

## Scope landed (frozen design followed, no reinterpretation)

- Hub rows now QUESTS(0) / FORGE(1) / GEAR(2); cursor boots on QUESTS.
  `data/screens/hub.json` lost the `open_map` row; `SCREEN_HUB_ROWS` 4 -> 3.
- MAP screen deleted: `data/screens/map.json`, `images/screens/mh_screen_map_{0..4}_128x64.png`,
  the generated map entries (`SCREEN_MAP*`, `mh_screen_map_*` in
  `fxdata/screens/Sprites.txt` + `fxdata.h`); `SCREEN_COUNT` 8 -> 7. MAP was the
  last screen id, so no other screen index moved. `PAGE_MAX 5` /
  `SCREEN_PAGE_STRIDE 16` kept (no ABI churn).
- `src/screens.hpp`: dropped `screenMapPage()` / `screenMapQuestRoom()`, the MAP
  page pick (pageIdx is now `scroll / SCREEN_ROWS`), the MAP early return, and the
  now-unused `generated/zone_meta.hpp` + `core/progmem.hpp` includes.
- `src/app_state.hpp`: dropped `APP_NAV_MAP`, the pre-switch `ACTION_OPEN_MAP`
  special case, and the NAV_DESTS MAP entry; NAV_DESTS is 7, static_assert is
  `APP_NAV_HUB == 1 && APP_NAV_ARMOR_FORGE == 7` (APP_NAV_HUNT == 8). Generated
  `ACTION_OPEN_MAP = 17` stays in the gen-screens enum (unused, zero flash).
- Quest room-hint meta dropped end-to-end: `tools/gen-quests.py`
  (`load_room_ids`/`read_room_hint`/`ROOM_HINT_NONE`/`MAP_REL`/`MAP_ROOM_COUNT`/
  `QUEST_ROOM_HINT[QUEST_COUNT]`, and the `progmem.hpp` include it needed),
  `data/quests/*.json` `roomHint` keys, and `tools/tests/test_gen_quests.py`
  (room-hint cases replaced by a `roomHint`-is-now-unknown-key regression).
- `data/map.json` + gen-zones + zone runtime untouched (room graph is core).

## Files

- data/screens/hub.json (rows), data/screens/map.json (deleted),
  images/screens/mh_screen_map_{0..4}_128x64.png (deleted)
- data/quests/{crush_heavy,gather_ore,slay_lunge,slay_sweep,train_pole}.json
- src/screens.hpp, src/app_state.hpp
- src/generated/{screen_meta.hpp,quest_meta.hpp,art_sheets.hpp} + src/fxdata.h
- fxdata/{fxdata.h,fxdata.bin,fxdata-data.bin,manifest.json},
  fxdata/screens/Sprites.txt, fxdata/tables/screens.bin,
  images/screens/mh_screen_hub_0_128x64.png
- tools/gen-quests.py, tools/tests/test_gen_quests.py (+ deleted fixture
  tools/tests/fixtures/gen_quests/clean/data/map.json)
- tst/app_state_test.hpp, tst/fxdatatest/{hub_test,screens_test}.hpp
- README.md, docs/quests-shops.md, docs/ui-design.md

## Size (acceptance 6)

Baseline shipping 29674/29696 (22 free) -> now:

```
size: flash=29634/29696 (62 free)  ram=1920/2560
```

delta = **-40 B flash, RAM unchanged**. Net-negative as expected. FX image shrank
15,360 B (5 baked map pages): 3,004,672 -> 2,989,312 B.

## Gates

- `make gen` converged (2 passes: first pass re-resolves page addresses from the
  pre-existing `fxdata.h`, known `cqw`), then `make gen-check`:
  `fxdata_manifest: PASS (217 generated artifacts unchanged)`; `fxdata.h ==`
  `fxdata/fxdata.h` (asserted by gen-check's cmp). Generated set staged together.
- `make test`: `Total Passed: 6984 / Total Failed: 0`.
- `make test-tools`: `Ran 407 tests ... OK`.
- Device suites (touched), `FXTEST_ONLY="test_boot test_data test_hub test_screens
  test_screens_smithy test_zones test_quests test_cards"`:
  - test_boot PASSED=4 FAILED=0
  - test_data PASSED=356 FAILED=0
  - test_hub PASSED=86 FAILED=0  (was 97; -11 MAP asserts)
  - test_screens PASSED=214 FAILED=0  (was 228; -14 MAP page/index asserts)
  - test_screens_smithy PASSED=102 FAILED=0
  - test_zones PASSED=77 FAILED=0
  - test_quests PASSED=112 FAILED=0
  - test_cards PASSED=85 FAILED=0
  All `fxtest_ram: OK`. Full 19-suite gate left to the orchestrator.

## Docs

- README.md: host/device test counts, demo-content line (MAP removed), shipping
  size 29634/62 free, FX image size + 12 screen pages, hub flow/controls
  (QUESTS/FORGE/GEAR), demo-wave + flash history (6k2 -40 B), `dap` trim leads
  (MAP page pick lead removed).
- docs/quests-shops.md: layer diagram + hub-rows bullet (MAP + room-hint meta gone;
  room graph stays).
- docs/ui-design.md: "MAP screen (monhun-ardu-imx)" section replaced by a
  "removed (monhun-ardu-6k2)" stub. docs/map-zones.md untouched (documents the
  room graph, which stays).

## Wall time (approx, worker)

- recon/read: ~6 min
- code + data + test edits: ~6 min
- gen (first pass + converge + gen-check): ~5 min
- host + tools + size: ~3 min
- device suites (8): ~4 min
- docs: ~5 min
- final re-verify (host + size + test_screens): ~2 min
- total: ~31 min

## Deviations

- None. `make gen-check` needed two `make gen` passes to become a fixed point
  because removing the map pages shifted every later FX address; this is the
  known one-pass-convergence gap (`cqw`), not a regression.
