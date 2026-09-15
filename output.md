# monhun-ardu-p82 — Test: on-device sim parity suite (Ardens)

## Bead
`monhun-ardu-p82` (slice of epic `monhun-ardu-kt7`). Proves the C++ core sim
matches the JS prototype (`mock/game.js`, the source of truth) for fixed,
scripted input sequences, tick by tick, on real AVR hardware via Ardens.

## Files
- added `tools/gen-parity-fixtures.js` — Node fixture generator; steps the mock
  over 20 deterministic scenarios and writes the C header below (JS only to
  produce fixtures; the test itself is C++/Ardens).
- added `tst/fxdatatest/parity_fixtures.hpp` — generated, permanent fixtures in
  flash (`MH_PROGMEM`): per-tick input bytes, per-tick 16-bit full-state hashes,
  20-field snapshots, start-state overrides, scene index.
- added `tst/fxdatatest/parity_test.hpp` — replays the fixture scripts through
  `mh::stepGame` and compares.
- added `tst/fxdatatest/test_parity.ino` — Ardens entry point (`P`/`F` report).
- changed `src/core/world.hpp` — `stepGame()` now implements the mock's
  over/freeze (hitstop) gates.
- changed `src/core/projectiles.hpp` — `stepWorldBody()` split out of
  `stepWorld()`; projectile cull compares the 1/16 px field like the mock.
- changed `src/core/player.hpp` — init `g.over`; hit-spark effects on
  deflect/parry/player hurt.
- changed `src/core/monster.hpp` — hit-spark effect on monster damage.

No Makefile change needed: `test_parity.ino` is picked up automatically by
`FXTEST_INOS = $(wildcard tst/fxdatatest/test_*.ino)`, so it runs alongside
`test_boot` / `test_assets` in `fxtest-build` / `fxtest-run`.

## Fixture generation
```
node tools/gen-parity-fixtures.js
# -> wrote tst/fxdatatest/parity_fixtures.hpp
#    scenes=20 ticks=1269 snapshots=32 cpFields=20
```

## What is compared
Scenarios (same seeds/inputs as the mock; no RNG): player chains per weapon
(sword/flail/gunshield), sword stepslash/pointblank branches, dodge i-frames,
parry/whirl/guard stances, shell fire + reload + ball speed, monster attack
cycle + windup + sweep hit, pole head/body damage + train DPS, train-mode
freeze, guard stamina drain, world clamp, beast push-apart.

- **Every tick**: a 16-bit FNV-1a hash of the full sim state (game scalars,
  player/ monster/attack FSM, pole/train, projectiles, effects). Fixture reads
  are flash loads; no fixture copy lands in RAM.
- **Every 64 ticks + final tick**: 20 packed field asserts — player x/y, hp,
  stam, state, stance, chain, ball/scatter ammo, reload, projectile count,
  train total/last, monster x/y/state/hp/stun, camera x/y.

Tests live permanently in `tst/fxdatatest/`; no temp files, no Python. Integers
only, no float.

## Mismatches found and fixed (core bugs vs. mock)
1. **Missing over/freeze gating** — `stepGame()` ran the full sim every tick,
   but the mock's `step()` skips `updatePlayer`/target/projectiles while
   `freeze > 0` and after `over`, and applies a `shake`/effects-only path (the
   prototype's hitstop). Diverged on every hit. Fixed in `world.hpp`:
   tick++/edges/camera run first, then the over and freeze gates.
   `stepWorldBody()` was split out so host sub-system tests keep their direct,
   ungated entry point. This is the *gate* only; real `Game::shake` remains the
   separate hitstop TODO in `monhun-ardu.ino`.
2. **Projectile cull margin** — the mock culls on the 1/16 px position
   (`> (WORLD_W+8)<<4`), so a shot in the last sub-pixel of the margin lives one
   more tick; the port compared integer px and kept/culled a tick early
   (scene 10, tick 86). Fixed to compare `pr.x*16 + pr.subX`.
3. **Missing hit-spark effects** — the mock's `playerHit`/`damageMonster` push a
   spark into `effects`; the port omitted them (scene 8 tick 17, scene 12 tick
   1). Added `addEffect(...)` (forward-declared in `player.hpp`, defined in
   `projectiles.hpp`) on monster damage, and on deflect/parry/normal player
   hurt. Guard path spawns no spark, matching the mock.

## Test tails
`make test` (host C++17):
```
========== Total Counts ==========
Total Passed: 497
Total Failed: 0
```

`make fxtest-headless` (Ardens, all three device suites):
```
=== test_assets ===
asset_test PASSED=30 FAILED=0
P
test_assets: PASS
=== test_boot ===
test_boot PASSED=4 FAILED=0
P
test_boot: PASS
=== test_parity ===
parity_test PASSED=660 FAILED=0
P
test_parity: PASS
```

Flash/RAM of the parity sketch: 28932 B / 29696 B program (764 B headroom),
1883 B globals (677 B stack). Checkpoint stride is 64 to keep the suite inside
the FX ROM budget; per-tick hashes still cover all 1269 ticks.
