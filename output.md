# monhun-ardu-fie.10 — fix: beast takes no damage after camp

## Diagnosis (confirmed in code)

`updateActiveTarget()` (src/core/world.hpp) is the only place that wires
`Target::onHit/onShove/onStun` to the monster handlers. It ran from `newGame()`
only. `loadRoom()` cleared `g.target = Target{}` for a safe room (camp) but a
later beast-room load only refreshed `alive`/`rect` via the per-tick
`syncMonsterTarget()` and never re-wired the callbacks. After camp -> area the
attack path (`g.target.alive && overlap`, then `if (g.target.onHit)`) saw a live
rect with a null handler, so melee/whirl did nothing. (Call sites in
player.hpp ~341/~819 are guarded, so on AVR the symptom is no damage, not a
null call.)

## Fix — src/core/world.hpp

- Removed the duplicated safe-room `g.target = Target{}` in `loadRoom`.
- Added `updateActiveTarget(g)` at the end of `loadRoom`: safe room clears,
  beast room re-wires the monster callbacks + syncs alive/rect, train re-arms
  the pole. Monster state (hp/pos/FSM) is untouched — persistence by design.
- `loadRoom` early-returns when `!ROOM_BOUNDS_ENABLED`, so the parity image is
  unchanged.

## Test — tst/zone_test.hpp (permanent, co-located)

New helper `zswing()` drives the real full-flow melee path (stepGame, tap A,
run to idle).

- "camp -> area re-arms the target callbacks and melee damage lands":
  camp clears (alive 0, onHit null) -> area load re-arms alive 1,
  onHit==monsterOnHit, onShove==monsterOnShove, onStun==monsterOnStun,
  rect w/h > 0; beast parked in the swing arc at from_camp spawn -> melee drops
  hp; camp re-entry clears, second area load re-arms.
- "train load re-arms the pole target": pole_room load arms
  poleOnHit/poleOnShove/poleOnStun and a live target.

Mutant check: with the world.hpp fix stashed, the new suite fails 9/12
(`Passed: 3 Failed: 9`); with the fix, 12/12.

## Gate

- `make test`: `Total Passed: 5391 Total Failed: 0`.
- `make fxtest-headless`: 17 suites, all `FAILED=0` (test_zones `PASSED=69
  FAILED=0`).
- `make size`: flash=29258/29696 (438 free), .text=29218.
  Baseline (fix stashed): flash=29272/29696 (424 free), .text=29232 — net
  -14 bytes `.text` from folding the duplicate clear/call.
- `make gen-check`: `fxdata_manifest: PASS (82 generated artifacts unchanged)`.

No float/double introduced; no /tmp; no commit/push.
