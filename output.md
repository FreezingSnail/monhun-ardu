# monhun-ardu-prg.7 — smith v2: material recipes + camp smithy

Status: DONE (worker ran into an output loop mid-bead; the orchestrator finished
the remaining integration, updated the stale pins, and ran the full gate).

## What landed

- **Recipes**: `data/smith/*.json` gained `materials: [{item, count}]` (sword T1
  = 2 ore + 1 scale, T2 = 3 ore + 1 fang; flail/gun analog). `tools/gen-smith.py`
  validates ids against `data/items.json` (via `tools/gen-items-ids.py`) and
  packs the bill into the upgrade record.
- **Screen logic**: `ScreenRecipe` + `screenRecipeOk`/`screenRecipeDebit`
  (`src/screen_state.hpp`); `screenCondOk` now gates a COND_UPGRADE row on zenny
  AND materials, `screenApplyAction` debits both and commits the save once.
  Device path reads the packed bill (`src/screens.hpp`).
- **Camp smithy**: `data/map.json` camp smithy prop + rect; gen-zones packs the
  room smithy range (`ZoneSmithy`, ROOM_SIZE 18→21); `trySmithy` raises
  `Game::smithyRequest` on a sheathed B press inside the rect (tent heal wins on
  the same press). `appSmithyRequest` + `APP_NAV_CAMP` (orchestrator) open the
  smith screen from the camp and close it back into the camp sim; the hub route
  keeps its own back-step.
- **Docs**: `docs/quests-shops.md`, README flow text.

## Orchestrator completion (after the worker looped)

- `APP_NAV_CAMP` + `appSmithyRequest` in `src/app_state.hpp`; sketch wiring
  (`s_smithyFromCamp`, namespace typo fix) in `monhun-ardu.ino`.
- Host test for the camp route (`tst/app_state_test.hpp`).
- Stale pins fixed: `tools/tests/test_gen_zones.py` (ROOM struct 21 B, offsets,
  dump lines, blob 121 B, prop gather index), device `test_hub` (recipe
  materials seeded + debit pins; `MH_CHARGE 0` + `MH_CARVE 0` test-only carve to
  fit), `test_screens`/`test_smith` (recipe seeding + debit pins), `test_zones`
  (camp prop count 5).

## Gates

- `make gen` x2 + `make gen-check` PASS (86 artifacts unchanged)
- `make test` 6060/0
- `make test-tools` OK (250 tests)
- `make fxtest-headless` 18/18 suites PASS; `test_perf` `rMx=3344 rAv=3074`
- `make size` flash 29136/29696 (**560 free**), RAM 1610/2560

## Budget note

prg.7 cost ~800 B (recipe cart scan + smithy range + camp route). The armor
wave (arm.1-3, ~1.3-2 KB) does not fit in 560 B; a shipping trim decision is
needed (candidates: MH_CHARGE −432, zone part overlays −630, tell shapes −416,
menu art fold, audio cues).
