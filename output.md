# monhun-ardu-dlp.3 — hql.3 loop: hub reachable + quest board content/chain + full gate

Status: **DONE** (no commit/push — orchestrator owns the commit).

## What changed

### Wiring (hub back on the demo path)
- `src/app_state.hpp` — `appMenuAccept()` -> `APP_NAV_HUB`; `appHuntReturn()` ->
  `APP_NAV_HUB`. Header/flow comments rewritten: live flow is
  menu -> hub -> HUNT -> camp/area, hub -> QUESTS/SMITH -> hub, hub B -> menu,
  camp hold-B -> menu, hunt end + A -> hub. The 5r1 direct-to-hunt shortcut is
  noted as superseded.
- `monhun-ardu.ino` — comment block updated (menu A opens the hub; hub HUNT
  launches the loadout; hunt end returns to the hub; the hub is no longer "off
  the demo path"). No behavior change beyond the routing above.

### Content
- `data/screens/hub.json` — QUESTS row added (action `open_quests`, condition
  `always`, cost 0) -> rows HUNT / QUESTS / SMITH / ZENNY (row indices 0..3;
  ZENNY row stays row 3 with the `zenny` flag). B still leaves to the menu.
- `data/quests/*.json` (v2, 4 quests, unique ids 0..3, `[a-z][a-z0-9_]*.json`):
  - `slay_lunge` id 0: kill 3 lunge, 150z, no material, unlock 0.
  - `slay_sweep` id 1: kill 2 sweep, 250z + shell x1, unlock 1.
  - `gather_ore` id 2 (new): gather 3 ore, 200z + ore x2, unlock 2.
  - `crush_heavy` id 3 (was id 2): kill 1 heavy, 400z + scale x2, unlock 3.
  Chain: `unlockFlag = N` = prior quest index N-1 done -> 0 -> 1 -> 2 -> 3.
- `data/screens/quests.json` — 8 rows (take + turn-in per quest), page scrolls
  by 6. Take rows `param = quest` (need nibble 0, as gen-screens requires);
  turn-in rows `param = (need << 4) | quest`, `cost = rewardZenny`
  (48/33/50/19). Labels: SLAY 3 LUNGE / TURN IN 150 / SLAY 2 SWEEP /
  TURN IN 250 / GATHER 3 ORE / TURN IN 200 / CRUSH HEAVY / TURN IN 400.

### Tests
- `tst/app_state_test.hpp` — menu A -> hub (no hunt); hub HUNT -> hunt;
  hunt end -> hub; hub B -> menu; round-trip test reworked to the hub loop.
- `tst/fxdatatest/hub_test.hpp` — boot menu A -> hub (row count 4), hub HUNT
  starts the hunt, QUESTS row 1 -> board (row count 8), SMITH row 2, ZENNY
  row 3 pixel checks (y = 38), hunt end -> hub, fresh hunt from the hub.
- `tst/fxdatatest/screens_test.hpp` — hub row count 4 + r3 ZENNY row, nav wrap
  to 3, real QUESTS route (synthetic-row workaround removed), hub pixel rows
  shifted, quests board row count 8.
- `tst/fxdatatest/zones_test.hpp` — section 6 drives menu -> hub -> HUNT -> camp.
- `tst/fxdatatest/quests_test.hpp` — quest count 4, def assertions for all four
  (goal kinds, ore gather target, shell/ore/scale materials, unlock chain),
  board rows 8, plus a new gather E2E: unlock chain -> take `gather_ore` ->
  three ore through the real `applyGather` hook -> turn-in pays 200z + ore x2 ->
  quest 3 unlocks.
- `tst/screens_test.hpp` — comments only (menu A now routes to the hub).

### Docs
- `docs/quests-shops.md` — v2 record, goal kinds, material rewards, unlock rule,
  the live hub loop (5r1 superseded), scaffold limitations (dead-condition board
  rows still render; no inventory screen).

## Generated artifacts

`make gen` regenerated `quests.bin`/`screens.bin` + `quest_meta.hpp`/
`screen_meta.hpp`, and the larger blobs shifted later FX offsets
(`equip.bin`/`equip_meta.hpp`, `zone_meta.hpp` sheet/room offsets). gen-equipment
bakes the *previous* run's `fxdata.h`, so a second `make gen` was needed to
converge; `make gen-check` then passed (see tails).

## Verification (exact tails)

```
$ make gen-check
fxdata_manifest: fxdata/manifest.json up to date (52 images, 64 inputs, 28 outputs)
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (91 generated artifacts unchanged)

$ make test
Total Passed: 6391
Total Failed: 0

$ make test-tools
Ran 308 tests in 19.012s
OK

$ make fxtest-headless   (final full run)
asset_test PASSED=270 FAILED=0
test_audio PASSED=9 FAILED=0
test_boot PASSED=4 FAILED=0
combat_test PASSED=237 FAILED=0
data_test PASSED=348 FAILED=0
test_hub PASSED=75 FAILED=0
test_hud PASSED=29 FAILED=0
test_items PASSED=35 FAILED=0
test_menu_art PASSED=53 FAILED=0
menu_test PASSED=60 FAILED=0
test_monster_art PASSED=127 FAILED=0
perf_test PASSED=5 FAILED=0
test_player_art PASSED=120 FAILED=0
test_quests PASSED=87 FAILED=0
test_screens PASSED=88 FAILED=0
test_smith PASSED=115 FAILED=0
test_tell PASSED=18 FAILED=0
zones_test PASSED=83 FAILED=0

$ make size
size: .text=29250 .data=24 .bss=1685
size: flash=29274/29696 (422 free)  ram=1709/2560
size: data facts: HAS_CARVE:true HAS_ENRAGE:true HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false HAS_GUARD_FACING:true HAS_GUARD_HP:true HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true HAS_HIT_STAGGER:false HAS_MULTI_STEP:true HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false HAS_STAGGER:true HAS_STEP_AFTER:true HAS_STEP_CHANCE:true HAS_TURN_RATE:true HAS_WAIT_STEPS:true HAS_ZONES:true
```

Baseline at HEAD e80b487: flash 29272/29696 (424 free), RAM 1709/2560.
**Delta: +2 flash (.text), RAM unchanged.** Fits (422 free). Content is cart
data; the 2 bytes are the `appMenuAccept`/`appHuntReturn` routing.

## Blockers

None. All gates green; budget gate cleared.
