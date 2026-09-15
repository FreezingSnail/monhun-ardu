# monhun-ardu-zq5 — Core: monster FSM + hit resolution port

## Files
- added: `src/core/monster.hpp` — FSM ported 1:1 from `mock/game.js` half:
  `updateMonster` (idle → pursue → windup → attack → recover; perpendicular
  circle with `circleDir`; `stun` counter; `MS_DEAD`), `chooseAttack` (dist > 32
  → lunge else sweep), `startMonsterAttack` (lunge `lvx/lvy = (face*speedF)>>4`),
  `monsterHitsPlayer` (reach-anchored box on the fixed face), `damageMonster`
  (crit when projection on facing > 3 px → ×1.4 as integer 14/10; `hitFlash`;
  death → `MS_DEAD` + `over = OVER_WIN`), `knockMonsterAway`, `pushApart`
  (pole immovable; attacking/windup beast shoves player; otherwise beast gives
  way so idle players are never shoved), and `stepHunt` (mock order: sync target,
  player, monster). Registers `monsterOnHit`/`monsterOnShove`/`monsterOnStun`
  into `Game::target` so the player FSM resolves against the beast and hrd's pole
  can plug into the same callback trio later.
- added: `tst/monster_test.hpp` — 17 suites: table + init fields, tell timing
  (40/48 counts), recovery windows (attack cd `55+tick%40` + `circleDir` flip;
  recover cd 55), stun → recover 24, crit zone ×1.4 (both facing sides + floor),
  death/win, lunge velocity ints, `addVel` sub-pixel carry, push rule both ways,
  sweep hitting the player, player→monster damage via `Game::target`, and
  playerHit routing (parry 60 / deflect 28 / guard chip / 34 i-frame gate /
  knockback −35).
- changed: `src/core/game.hpp` — added `MonsterAttack`/`MONSTER_ATTACKS`,
  `MState`, `Over`, `Monster` (embedded in `Game`), and `Game::over`. Shared
  structs header, same pattern as the existing embedded `Player`. `Player`'s
  `initGame` intentionally untouched (zq5 integration calls `initMonster`).
- changed: `tst/main.cpp` — include + run `MonsterSuite`.

## `make test` output (tail)
```
---------- idle hunt runs: beast engages, hunter survives ----------
Passed: 3
Failed: 0
========== Total Counts ==========
Total Passed: 287
Total Failed: 0
```
Exit 0 (188 asserts before this bead; +99).

## `make fxtest-headless` output (tail)
```
test_boot
Sketch uses 9806 bytes (33%) of program storage space. Maximum is 29696 bytes.
Global variables use 1751 bytes (68%) of dynamic memory, leaving 809 bytes for local variables. Maximum is 2560 bytes.
=== test_boot ===
test_boot PASSED=2 FAILED=0
P
test_boot: PASS
```
Exit 0.

## Notes / deviations
- `game.hpp` gained the `Monster` struct + `Game::monster`/`Game::over`: `Game`
  must own the beast so `Target` callbacks (which take `Game&`) can reach it and
  the player FSM's `Game::target` melee path resolves against it. No `player.hpp`
  / `fp.hpp` / `mock/` changes; no retuning.
- `playerHurt` in `player.hpp` dropped mock's `lose()`; `monster.hpp` clamps
  player hp and sets `over = OVER_LOSE` after routing a monster hit, so the
  death contract still holds.
- `stepHunt` syncs `Game::target.rect` before `stepPlayer`, matching the mock
  where the player sees the beast's pre-move position (monster moves after the
  player each tick). `updateMonster` re-syncs after moving.

## TODOs
- `freeze`/`shake`/`effects`/projectiles live in hrd; monster only raises
  `freeze` (crit 6, normal 4, death 12) and leaves visuals to hrd render.
- Train mode / pole target selection (hrd) overrides `Game::target`; monster
  stays hunt-only this bead.
