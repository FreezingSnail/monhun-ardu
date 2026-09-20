# monhun-ardu-feel.20 — trim: plain pole (drop 4-variant break stages + train DPS)

HEAD at start: `477bfdf` (feel.19), clean tree. No commit/push (orchestrator commits).

## Data

- Deleted `data/creatures/pole_break.json`, `pole_crack.json`, `pole_sever.json`.
- `data/creatures/pole.json` is now the only train prop: body 20x36 at (140,40),
  one head zone `{ox:-128, oy:0, w:255, h:16, dmgMul:140, bodyShare:100}`.
- `combat.bin`: 1309 -> 1111 B. `CREATURES_COUNT` 8 -> 5, `ZONES_COUNT` 11 -> 8,
  `SKELETONS_COUNT` 5 (pole skeleton kept).

## Engine

- `src/core/game.hpp`: removed `PoleKind`, `Pole::kind`, `TrainEvent`,
  `TrainStats`, `MAX_TRAIN_EVENTS`, and `Game::train`.
- `src/core/projectiles.hpp`: `initPoleKind(kind)` + `poleCreatureId` replaced by a
  single `initPole(Game&)` (loads `CREATURE_POLE` via the shared loader);
  removed `poleDamageStage`/`poleStageFrame`, `trainAdd`/`trainDps`,
  `poleBreakBurst`, the broken-rect swap and the train-stat writes. `damagePole`
  keeps the shared `combatZoneHitResolveAt` path, `hitFlash=4`, crit freeze 5 /
  body 4 and the rising damage number.
- `src/core/world.hpp`: `withWeapon`/`resetHunt` no longer carry a pole kind.
- `src/audio.hpp`: `AudioState::trainTotal` removed; the train blip is derived
  from a fresh pole damage-number effect (`text != 0`), same crit mapping.
- `src/menu_state.hpp`: `MENU_TARGET_COUNT` 8 -> 5, `menuPoleKind` removed,
  `menuStart` relies on `initWorld`'s plain-pole install.

## Render / menu art

- `src/render.hpp`: `drawPole` draws `fxpole` with the normal/flash frame only;
  removed variant sheet dispatch + stage frame. HUD train DPS number removed
  (train shows no bar).
- `tools/gen-art.py`: `MENU_TARGETS` back to 5 (CHICKEN/BULL/LONGTAIL/RAVAGER/
  POLE); menu bg + `mh_menu_msel` regenerated (5 tiles). Unused variant pole
  sheets remain on the FX cart.

## Tests updated intentionally (no coverage weakened)

Host: `shells_test`, `world_test`, `combat_test`, `combat_pack_test`, `menu_test`,
`app_state_test`. Device: `combat_test` (sweep profileIdx now symbolic),
`audio_test` (train cue from a fresh damage-number effect), `menu_test`,
`menu_art_test`, `zones_test`. `art_dims_test`/`asset_test` still pin the variant
sheet headers because those sheets remain on the cart.

## Verification (exact tails)

`make gen` (x2) + `make gen-check`:

```
fxdata_manifest: PASS (82 generated artifacts unchanged)
```

`make test`:

```
Total Passed: 5973
Total Failed: 0
```

`make test-tools`:

```
Ran 202 tests in 11.894s

OK
```

`make fxtest-headless` (full; test_parity excluded per AGENTS.md):

```
asset_test   PASSED=270 FAILED=0
audio_test   PASSED=17  FAILED=0
boot_test    PASSED=4   FAILED=0
combat_test  PASSED=237 FAILED=0
data_test    PASSED=368 FAILED=0
hub_test     PASSED=57  FAILED=0
hud_test     PASSED=17  FAILED=0
menu_art     PASSED=60  FAILED=0
menu_test    PASSED=66  FAILED=0
monster_art  PASSED=111 FAILED=0
player_art   PASSED=111 FAILED=0
quests_test  PASSED=50  FAILED=0
screens_test PASSED=78  FAILED=0
smith_test   PASSED=66  FAILED=0
tell_test    PASSED=17  FAILED=0
zones_test   PASSED=69  FAILED=0
test_perf: B pUs=6367 pHz=157 lHz=52 lTk=452 rMx=4744 rAv=4514 ram=705
```

`make size`:

```
size: .text=27544 .data=28 .bss=1594
size: flash=27572/29696 (2124 free)  ram=1622/2560
```

| | before (28724/1743) | after | reclaimed |
|---|---:|---:|---:|
| flash | 28724 | 27572 | **1152 B** (target >=500) |
| RAM | 1743 | 1622 | **121 B** (target >=90) |

## Notes

- Legacy `test_parity` (`tst/fxdatatest/parity_test.hpp`) still references the
  removed `g.train.*`/`trainDps` symbols, so it no longer compiles. Per AGENTS.md
  it is not a gate and its fixtures are frozen/unmaintained; left untouched.
- README status/table lines updated for the plain pole and refreshed build/FX
  sizes. `docs/feel-design.md` does not list the pole kit (no change needed).
