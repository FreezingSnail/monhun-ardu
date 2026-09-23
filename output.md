# monhun-ardu-eqf — gun guard persists after a shot while B is held (feel.24)

## What changed

`src/core/player.hpp`

- `stanceSpecial()` returns bool. The gun arrowshot branch no longer always runs
  `exitStance` + `bLocked`: while B is physically held (`inp.b`) the guard stays
  up through the shot; off a non-held stance it keeps the old exit + lock.
- The bR release path is untouched, so releasing B still drops the stance
  (`bHeld >= HOLD_TICKS` branch), and a stance broken by chip damage / stamina
  is unchanged.
- An A tap that lands while the stance verb is busy (shot recovery / nock)
  arms `aBuffer` instead of being dropped, and the buffered flush in a stance
  fires the stance verb instead of a plain melee swing (guard can outlive its
  own verb now, so the flush must dispatch by stance).

`tst/hitscan_test.hpp`

- Guard-held assert on the shot (`stance == ST_GUARD`, `bLocked == false`).
- New: releasing B drops the guard mid-shot, shot still resolves.
- New: an A tap during the shot buffers and the guard fires again, not a melee
  swing (`reload == ARROW_NOCK_TICKS - 4`, `chain == 0`).
- Reworked nock test: hold B through the special + nock, fresh A tap fires with
  no release/re-hold.

## Gate (exact)

1. `make gen-check`: `fxdata_manifest: PASS (91 generated artifacts unchanged)`
2. `make test`: `Total Passed: 6298  Total Failed: 0`
3. `make fxtest-headless`: every suite PASS
4. `make size`: `size: flash=28874/29696 (822 free)  ram=1704/2560`
   — +64 B vs the pre-change 28810 (886 free).

Implemented inline by the orchestrator (not a worker); the bead was opened
retroactively so the commit carries the id.
