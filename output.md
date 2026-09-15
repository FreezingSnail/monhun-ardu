# monhun-ardu-0ny — Core: world, camera, mode toggles

## Files
- added: `src/core/world.hpp` — the 0ny half of `mock/game.js`:
  - geometry: `SCREEN_W 128`, `SCREEN_H 64`, `HUD_H 8`, `ARENA_H 56`,
    `CAM_MAX_X = WORLD_W-SCREEN_W = 128`, `CAM_MAX_Y = WORLD_H-ARENA_H = 56`.
  - `updateCamera(g)` — mock `updateCamera()`: centre on the player, clamp
    `x 0..128`, `y 0..56`, int px only (no float).
  - `updateActiveTarget(g)` — mock `activeTarget()`: arms `Game::target` at the
    pole in train, the live beast in hunt (`alive=false` once dead), re-arming
    each mode's `onHit/onShove/onStun` callbacks.
  - `activeTargetRect(g)` — read-only `const Rect*` view (null when dead/absent).
  - `newGame(g, weapon, mode)` — fresh world: `initGame` + `initMonster` +
    `initWorld` + target arm + camera 0,0.
  - `withWeapon(g, weapon)` / `resetHunt(g)` — mock wrappers that re-init with
    the **current mode** (prototype bug fix: `newGame` defaults to hunt).
  - `stepGame(g, inp)` — full tick: `updateCamera` then `stepWorld` (mock order).
- changed: `src/core/game.hpp` — `Game::camX, camY` (int16 camera top-left).
- added: `tst/world_test.hpp` — 9 suites / 44 asserts: world+camera constants,
  camera order (1-tick trail), x follow+clamp, y clamp both ends, idle no-slide
  (120 ticks), train monster frozen + pole target, activeTarget dead=null,
  weapon-swap/reset keep mode, projectile cull `world+8` margin.
- changed: `tst/main.cpp` — include + run `WorldSuite`.
- changed: `tst/fxdatatest/boot_test.hpp` — includes `world.hpp` (compiles it for
  AVR) and pins the start camera `(40,40)` to the host value.
- changed: `output.md` (this file).

## Camera-order choice
`updateCamera` runs **before logic**, exactly like mock `step()` (camera call
sits above `updatePlayer`). The camera therefore shows the player position from
the previous tick — a deliberate 1-tick trail, not lag to be fixed:
- keeps the device bit-identical to the browser prototype for scroll feel;
- is testable and pinned by "camera order" (`camX == 40` while the player has
  already stepped to `x == 97`).
The device loop should call `stepGame()` (camera + `stepWorld`), not
`stepWorld()` directly, to preserve the order.

## R key semantics (for the device bead)
Mock `boot()` maps `KeyR -> resetHunt()`, `Digit1..3 -> withWeapon()`, and
`KeyK -> newGame(weapon, other mode)`. Port equivalents:
- R = `resetHunt(g)` — restart the current area with the current weapon (keeps
  both weapon and mode). Not `newGame`, which would reset the area to hunt.
- 1/2/3 = `withWeapon(g, 0|1|2)` — keeps the current area.
- K (if wired) = `newGame(g, weapon, other mode)`.

## `make test` output (tail)
```
---------- projectile cull bounds carry the mock world+8 px margin ----------
Passed: 4
Failed: 0
========== Total Counts ==========
Total Passed: 446
Total Failed: 0
```
Exit 0 (402 asserts before this bead; +44).

## `make fxtest-headless` output (tail)
```
test_boot
Sketch uses 11260 bytes (37%) of program storage space. Maximum is 29696 bytes.
Global variables use 1751 bytes (68%) of dynamic memory, leaving 809 bytes for local variables. Maximum is 2560 bytes.
=== test_boot ===
test_boot PASSED=4 FAILED=0
P
test_boot: PASS
```
Exit 0 (2 device checks before; +2).

## Notes
- No float/double, no `Arduino.h`; ints + `fp.hpp` only. No retuning.
- `mock/`, `fp.hpp`, and player/monster/projectile behaviour untouched.
- `stepWorld()` still owns the per-tick target sync and the train-skips-monster
  branch, so neither the beast AI nor any collected damage changes.
