# monhun-ardu-prg.8 — trim: adopt reclaim set

HEAD at start: `8abac2e` (prg.1 spike). No commit/push (orchestrator commits).
End state: clean gate — gen-check, host, tooling, 17 device suites, size all
green.

## Applied cuts

| Cut | Disposition | Where |
|---|---|---|
| training mode + pole target | removed end to end | `MODE_TRAIN`, `struct Pole`, `Game::pole`, `initPole`/`syncPoleTarget`/`updatePole`/`damagePole`/`poleOnHit`/`poleOnShove`/`poleOnStun`/`armPoleTarget`, `projectiles.hpp`/`world.hpp` train branches |
| pole_room | removed from data | `data/map.json` room, `images/maps/mh_map_pole_room_128x56.png`, `data/creatures/pole.json`, `SCREATURE_POLE` record + its head zone |
| menu POLE row | target count 5→4 | `MENU_TARGET_COUNT 5→4`, `MENU_POLE_TARGET` deleted, `menuMode`/`menuMonsterKind`/`menuStart` simplified, `mh_menu_msel` rebuilt as 4 tiles (`tools/gen-art.py MENU_TARGETS`), `mh_menu_bg` POLE row gone |
| damage-number text | removed (sparks kept) | `Effect::text`, `addEffect` text arg, `drawEffects` number branch, audio text edge |
| screen shake | removed | `renderScene` tick-derived view offset + `ANG_SHAKE_*`/`sin256`/`cos256` use |
| weapon trail | removed | 3 trail puffs in `drawProjectiles` + `TRAIL_LIGHT`/`TRAIL_DARK` |
| procedural ground dots | **KEPT** | `MH_ROOM_IMAGE` default is 0, so `drawArena` IS the shipping ground; cutting it would blank the playfield. See "Deviation". |
| rare audio cues | removed | `CUE_BREAK`/`CUE_GATHER`/`CUE_EAT`/`CUE_WINDUP` + edges, `mhCueTable` 14→9 rows, `AudioState.itemHerb`/`poleBroken`/monsterState windup use |
| stage-3 finisher | `MH_STAGE3=0` shipping, carve kept | `src/core/game.hpp` |
| dir+A opener / roll attack | `MH_ROLL_ALT=0` shipping, carve kept | `src/core/game.hpp` |

Host suite now forces the carves on (`TEST_FLAGS += -DMH_STAGE3=1
-DMH_ROLL_ALT=1`) so `player_test.hpp` stays the coverage for both branches;
`test_parity` still carves them off.

Kept per scope: `MH_CHARGE`, telegraph tell shapes, `drawZonePart` overlays, room
bounds, combat parts, gather/items, sheathe, turn-rate.

## Deviation — score vs the spike

The prg.1 recB set budgeted **-1812 B** (including the ground-dot cut). The
shipping image reclaimed **-1448 B**, because (a) the procedural ground-dot
field is kept — `MH_ROOM_IMAGE` defaults to 0 so `drawArena` is the live
shipping ground and removing it would blank the arena; and (b) the spike's
per-cut deltas were measured independently and do not sum linearly under LTO.
Visual change from the adopted set: no pole/POLE row, no rising damage numbers,
no hit shake, no shell trail, POLE-room gone. Ground dots unchanged.

## Gates (tails)

`make gen-check`:
```
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (81 generated artifacts unchanged)
```

`make test`:
```
Total Passed: 5744
Total Failed: 0
```

`make test-tools`:
```
Ran 209 tests in 11.2s
OK
```

`make fxtest-headless` (full; test_parity excluded by design):
```
17/17 suites PASS: assets audio boot combat data hub hud menu_art menu
monster_art perf player_art quests screens smith tell zones
```

`test_perf` line:
```
B pUs=6348 pHz=157 lHz=52 lTk=480 rMx=3348 rAv=3075 ram=724
```
`rMx` 4768 → 3348 µs (budget 7407); strictly better (render subtraction).

`make size`:
```
size: .text=27108 .data=20 .bss=1564
size: flash=27128/29696 (2568 free)  ram=1584/2560
```

**Reclaimed: 1448 B flash (28576 → 27128), 43 B RAM (1627 → 1584).** ≥1400 B
target met; flash free 1120 → 2568.

## Docs/tests updated

- `README.md`: status line, shipping size, `projectiles.hpp`/`world.hpp` rows,
  menu controls + target count + sheet description, roster table (POLE row →
  RAVAGER hunt), "In game (hunt)".
- `docs/feel-design.md`: new "prg.8 trim — reclaim adopted" ledger section with
  the measured `make size` block and the 1448 B result.
- `docs/map-zones.md`: `pole` prop type marked legacy.
- `tst/fxdatatest/test_parity.ino`: roll-alt/stage3 carve comments note the
  prg.8 shipping default.

Generated sets staged together by the orchestrator (`git add -A`): combat,
zones, equip offsets, menu sheets and `fxdata.*` all regenerated.
