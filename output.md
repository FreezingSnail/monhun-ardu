# monhun-ardu-isp.3 — hub GEAR screen: weapon select (save v4 weapon)

Baseline HEAD `cd190bf`, clean. **DONE — all gates green, no commit/push.**

## Size (final gate)

```
size: .text=28830 .data=18 .bss=1687
size: flash=28848/29696 (848 free)  ram=1705/2560
size: data facts: HAS_CARVE:true HAS_ENRAGE:true ... (all facts unchanged)
```

| | baseline | isp.3 | delta |
|---|---|---|---|
| flash | 28804 (892 free) | 28848 (848 free) | **+44 B** |
| RAM | 1701 (859 free) | 1705 (855 free) | **+4 B** |

The +44 B is the `ACTION_EQUIP_WEAPON` switch case + `APP_NAV_GEAR` route in
`appNavApply` (a new `screenReset(SCREEN_GEAR)` arm); the hub row, gear rows and
screen def all live in the cart blob (`fxdata/fxdata.bin`), so their byte cost is
data, not flash. No `HAS_*` data fact flipped.

## What changed

- `tools/gen-screens.py` — appended `equip_weapon` + `open_gear` to
  `ACTION_NAMES` (new ids 9/10; existing ids unchanged).
- `data/screens/gear.json` — **new**, id 3, title `GEAR`, rows `SWORD / FLAIL /
  GUN` (`equip_weapon`, `always`, param = weapon idx, cost 0) + `LEAVE`.
- `data/screens/hub.json` — added the `GEAR` row (`open_gear`) between `SMITH`
  and `ZENNY` → HUNT / QUESTS / SMITH / GEAR / ZENNY.
- `src/screen_state.hpp` — `ACTION_EQUIP_WEAPON`: rejects `param >=
  SAVE_TIER_COUNT` and the already-held weapon, else writes `save.weapon` and
  returns true (caller commits once).
- `src/app_state.hpp` — `APP_NAV_GEAR` enum, hub `ACTION_OPEN_GEAR →
  APP_NAV_GEAR`, `appNavApply` opens `screens::SCREEN_GEAR` with
  `SCREEN_GEAR_ROWS`; `appScreenBack` already sends any non-hub screen to the hub.
- `monhun-ardu.ino` — no change: the generic screen path calls
  `screenApplyAction(s_save, row)` for a non-routing A, which covers equip.
- Tests: host `tst/screens_test.hpp` (equip sets weapon / out-of-range no-op /
  same-weapon false) and `tst/app_state_test.hpp` (hub GEAR route, gear
  navApply + B back, equip is a save action); device
  `tst/fxdatatest/screens_test.hpp` (SCREEN_COUNT 4, hub row count 5, GEAR rows
  + cart equip write + pixel row), `tst/fxdatatest/hub_test.hpp` (GEAR label
  pixel, equip FLAIL → reload → next hunt uses it), `smith_test.hpp` (screen
  count); `tools/tests/test_gen_screens.py` (new action ids + compile cases).
- Docs: README hub input table + hub paragraph; `docs/quests-shops.md` layers /
  actions / gear-screen loop; armor/equipment stays on SMITH, items screen still
  a follow-up, no on-screen equipped-weapon mark yet.

## Gate tails

- `make gen-check` — `fxdata_manifest: PASS (91 generated artifacts unchanged)`.
  (A second `make gen` is required after a table-size change because
  `gen-equipment.py` bakes sheet offsets from the previous `fxdata.h`; see its
  stale-blob static_assert comment.)
- `make test` — `Total Passed: 6207  Total Failed: 0`.
- `make test-tools` — `Ran 310 tests ... OK`.
- `make fxtest-headless` (full, EXIT=0) — all 16 gate suites PASS: asset 270,
  audio 9, boot 4, combat 237, data 348, hub 81, hud 29, items 35,
  monster_art 127, perf 5, player_art 120, quests 87, screens 110, smith 115,
  tell 18, zones 82.
- `make size` — the line above; delta **+44 B flash / +4 B RAM**.
