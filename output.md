# monhun-ardu-ouq — device: sheathe + combo debounce + input buffers + guard strafe

Status: DONE (finished inline after the worker spawn was cancelled mid-run; all
gate commands below were run by the orchestrator on the finished tree).

## Delivered
- Sheathe ddab (double-tap Down + A+B chord) with stowed state: A draws into
  combo hit 1, B taps roll with sword numbers, no stance, run speed 24/16 px/t,
  weapon overlay hidden in `drawPlayer`.
- HEAVY combo debounce: CHAIN_GAP=9 after hits 1/2, COMBO_LOCK=24 after the
  finisher; chainWin only ticks in PS_IDLE and is armed to 14 when the lock ends.
- A_BUFFER 16 (was 10); B_BRANCH_BUFFER 36 queues branch taps made in attack
  recovery or the lock and fires them when the window opens. Early startup taps
  still dodge-cancel; hold B still enters the stance and drops the queue.
- Guard strafe: d-pad moves with facing locked while in ST_GUARD.
- Parity generator arms HEAVY per scene (mock module default stays OFF); hash
  extended with sheathed/chainLock/bBuffer, mirrored in `parity_test.hpp`.
- Budget: device test images now compile with the shipping size flags
  (`-mcall-prologues -mrelax`, no MH_NO_USB) in the Makefile; test_parity carves
  only the sheathe input path (MH_SHEATHE=0) since no fixture scene stows.

## Numbers
- `make test`: Total Passed 4835, Failed 0.
- `node --test mock/game.test.js`: 69 pass, 0 fail.
- `make gen-check`: PASS (68 generated artifacts unchanged).
- `make fxtest-headless` (all suites): every suite PASS; parity 660/0.
- test_parity image: 29540/29696 (156 free) at the new flags (was 29594 stock
  before the port, i.e. 102 free).
- `make size` (shipping): flash 26250/29696 (3446 free), ram 1754/2560.
  Baseline was flash 25492 (4204 free), ram 1746: +758 flash, +8 RAM.
- Fixture regen is deterministic (re-run produced the same diff).

## Notes / deviations
- Worker spawn was cancelled after most edits landed; the diff was reviewed
  against `mock/game.js`, one test expectation fixed (dodge t 16 -> 15,
  same-tick switch ticks once), seqDir/seqRel dropped (ddab only needs seq2).
- test_parity only carves the sheathe input path; the B-branch buffer must stay
  on because the A-then-B fixture scenes queue through the lock.
