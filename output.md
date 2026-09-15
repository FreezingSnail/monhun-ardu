# monhun-ardu-hrd — Core: projectiles, effects, training pole

## Files
- added: `src/core/projectiles.hpp` — hrd half of `mock/game.js`:
  - `initWorld(g, mode)` — clears shot/effect/train state, default pole
    `Rect{140,40,20,36}`, arms the pole target when `MODE_TRAIN`.
  - `spawnShot()` — consumes `Game::lastShot` (recorded by `player.hpp`
    `fireShell`): muzzle spark (`life 5`, `crit`) at centre + facing*10, then
    one ball (`heavy`) or three scatter pellets via `rotFp(fx,fy,15,±6)`.
  - `updateProjectiles()` — `fp::addVel` fixed motion, life `90`, 1/16 px
    spawn `centre + facing*13`, collision vs `Game::target` rect, culled at
    `WORLD_W/WORLD_H ± 8`.
  - `updateEffects()` — `t++` until `life`, dropping oldest past the cap.
  - `updatePole()` / `damagePole()` — hitFlash 4, head = top 16 px → x1.4
    (integer 14/10, min 1), freeze crit 5 / body 4, `train.total/last`,
    event ring, damage-number effect (`life 26`, at `hy-6`, `text` = total).
  - `trainDps()` — trailing 600-tick sum / 10, round half up.
  - `stepWorld()` — mock tick order for the owned parts: target sync, player,
    `spawnShot`, pole|monster, target re-sync, projectiles, effects.
- changed: `src/core/game.hpp` — `Mode`, caps (`MAX_PROJECTILES` 12,
  `MAX_EFFECTS` 12, `MAX_TRAIN_EVENTS` 24) and `PROJ_LIFE`/`POLE_HEAD`;
  `Projectile : fp::FpBody`, `Effect`, `Pole`, `TrainEvent`, `TrainStats`;
  `Game::mode/ proj/ projN/ fx/ fxN/ pole/ train` and the fire-time facing
  `lastShotFx/lastShotFy`.
- changed: `src/core/player.hpp` — `fireShell` stub now also records
  `lastShotFx/lastShotFy` (facing at fire time); no behaviour change.
- added: `tst/shells_test.hpp` — 12 suites / 115 asserts: shell tables, spawn
  geometry (centre + facing*13, 1/16 px, life 90, heavy), scatter 3-pellet
  spread (`cos15 sin±6`), guard+A consume + muzzle + reload, reload block,
  clip-empty block, pole head x1.4 vs body x1.0 + hitFlash 4, damage-number
  spawn/expire, DPS 600-tick window + rounding, pole targetable in train,
  hunt projectile collision + cull, pointblank consumes a ball + reload 45.
- changed: `tst/main.cpp` — include + run `ShellSuite`.
- changed: `output.md` (this file).

## `make test` output (tail)
```
---------- pointblank branch consumes a ball and arms reload 45 ----------
Passed: 9
Failed: 0
========== Total Counts ==========
Total Passed: 402
Total Failed: 0
```
Exit 0 (287 asserts before this bead; +115).

## `make fxtest-headless` output (tail)
```
test_boot
Sketch uses 9830 bytes (33%) of program storage space. Maximum is 29696 bytes.
Global variables use 1751 bytes (68%) of dynamic memory, leaving 809 bytes for local variables. Maximum is 2560 bytes.
=== test_boot ===
test_boot PASSED=2 FAILED=0
P
test_boot: PASS
```
Exit 0.

## Deviations (documented, no retuning)
- Projectile position uses the repo convention (integer px + 1/16 px
  remainder in `subX/subY`) and therefore moves `speedF/16` px per tick. The
  mock pre-multiplies `pr.x` by 16 at spawn and then runs the pixel-domain
  `addVel`/`>>4` over it, a latent double-scaling bug that made shots ~1/16
  speed; `mock/game.test.js` never checks projectile positions, so all
  published numbers (spawn offset 13, speedF 35/42, life 90, sizes, damage)
  are preserved with the intended fixed-point motion. Same class of fix as the
  earlier `isqrt` seed and `pushApart` notes.
- Device arrays are capped rings and drop the oldest entry when full (mock
  arrays are unbounded); not reachable with ball/scatter clips in normal play.
- `pointblank` (`shell: true`) consumes a ball + arms reload 45 but spawns no
  projectile, matching the mock (which only calls `fireShell` from the
  guard-stance special).

## TODOs for later beads
- 0ny (camera/world): read `Game::mode`, `Game::pole`, `Game::proj`, `Game::fx`
  in the render/scroll pass; no interface change needed here. `stepWorld()` is
  the integration point for camera update + freeze gating.
- Render beads: draw `proj[0..projN)` with `heavy` (big core) vs pellet (bright
  nose); smoke trail from `(vx,vy)`; `fx[i].text != 0` is a rising damage
  number (`y - (life-t)/3`), `text == 0` is a spark/muzzle (`crit` picks shade);
  pole body + head slab, `hitFlash` shade. HUD in train: `LAST`, `DPS`
  via `trainDps`, `RLD` + reload bar from `player.reload`.
- Integration: freeze is raised (pole crit 5 / body 4) but not consumed yet —
  full `step()` freeze gating belongs with the 0ny/full-loop bead; shake is
  render-only (not ported).
- Player/pole `pushApart` in train is not wired (pole is static; no overlap in
  the default layout).
