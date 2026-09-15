# monhun-ardu-czb — Core: player FSM + weapons port

## Files
- added: `src/core/game.hpp` — shared structs + `WEAPON_DEFS[3]` (exact port of mock/game.js tables: sword spd 18, flail spd 15, gun spd 9; combo/special/branch attack tables; shells). `Rect` (+overlap), `circleRectOverlap`, `Input`, `Target` (hurt rect + onHit/onShove/onStun callback indirection for zq5/hrd), `Player` (inherits `fp::FpBody` + `fp::FpStam` so addMove/addVel/drainStam work directly), `Game`. Constants HOLD_TICKS=11, CHAIN_WIN=14, A_BUFFER=10, WORLD 256x112. No float, no Arduino.h, header-only.
- added: `src/core/player.hpp` — `initGame`, `stepPlayer` (edge detect + tick order identical to mock: timers, B, A, state switch, stance, clamp). States idle/attack/special/dodge(16t,iT14,vx54)/deflect(9t,30)/shove(10t)/stun; chain+chainWin; branch windows (attack recovery or chainWin); aBuffer 10; hold-vs-tap B; stances parry(34t cap, drain 2)/whirl(whirlTick, hit every 16t, drain 8)/guard(drain 1); stanceAuto release; stamina int + 1/16 sub, regen +8/tick, drainStam; move speed factors whirl 6/10, guard 4/10, parry 0; meleeHitbox application on Target; gunshield shell consume + reload + `Game.lastShot` stub for hrd; `playerHurt` with guard block cost 22 / chip 25% / break -> stun 45, parry riposte (riposteT 90), deflect response via Target::onStun.
- added: `tst/player_test.hpp` — `PlayerSuite(TestRunner&)`: weapon table equality, chain advance + window expiry, every branch (stepslash lunge, spincut, whirl, trip, pointblank + shell fallback to shove, guardbash), stance enter/auto-exit/cap, stamina drain-to-empty + regen, dodge roll distance (>10px/10t), hold-vs-tap B, canCancel, guard block cost + break stun, whirl periodic hit (dmg 8, push 8), shell consume + reload, riposte/deflect hooks, all-weapon smoke.
- changed: `tst/main.cpp` — wires `PlayerSuite` into the runner (header-only, no Makefile change needed).

## `make test` output (tail)
```
---------- all weapons step 60 ticks without breaking ----------
Passed: 3
Failed: 0
========== Total Counts ==========
Total Passed: 188
Total Failed: 0
```
Exit 0.

## Notes / TODOs for zq5 (monster) + hrd (pole)
- `Game.target` is the plug point: set `alive`, `rect` (hurt box), and `onHit` (damage + push + trip effect), `onShove` (gunshield shove push + freeze), `onStun` (deflect/parry monster stun). Currently no-ops in host tests.
- `Game.lastShot` (1=ball, 2=scatter) + `lastShotX/Y` record gunshield fires; hrd spawns projectiles/effects from those (mock: 3 dirs for scatter via rotFp, spawn 13/16 px along facing, `speedF` from ShellDef).
- Mock chain semantics kept: `chainWin` (14t) decrements every tick, so a 2nd combo attack (16–23t) always outlives the window and `chain` resets mid-attack; stage-2 branches (spincut/trip/guardbash) are reachable from the recovery path (`min(chain+1,2)`), exactly as in the mock. Do not "fix" this without touching mock.
- `fp::isqrt` deviation (seed 1<<14) already noted in fp.hpp/1mb report.