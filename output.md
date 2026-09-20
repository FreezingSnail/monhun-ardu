# monhun-ardu-feel.16 — input: double-tap d-pad rolls

HEAD at start: `56b34c3` (balance: slow weapon-out walk), clean tree. No
commit/push (orchestrator commits). Base flash `28720/29696 (976 free)`,
RAM `1744/2560`.

## What changed

- `src/core/game.hpp`: `DTAP_WIN = 10` next to the other input windows;
  `Player` gains `dTapDir` (last press-edge dir8, 0xFF none), `dTapT`
  (window countdown) and `pDir` (dir8 last tick, 0xFF idle), appended last so
  existing fields/sizes do not move.
- `src/core/player.hpp`: `Player::init` zeroes the three fields. In
  `updatePlayer`, between the sheathe block and the B handling, a press edge
  (`dirIndexFromInput >= 0 && != pDir`) inside `DTAP_WIN` of the previous
  same-dir edge calls the existing `tapDefense(g, def, inp)`; otherwise it arms
  the window. `dTapT` ticks down each tick, `pDir` updates every tick. A/B are
  never consumed and held directions never fire (no edge).
  - `tapDefense` keeps every gate/action untouched, so the double-tap gets the
    same weapon action, stamina and state checks as the B tap.
  - Interaction: the sheathe block watches its own Down edges, so double-tap
    Down arms the sheathe sequence AND rolls. The sheathe seq window (18t)
    outlives the 16t stow-roll, so the A+B chord still lands (host helper waits
    the roll out, see below). Documented in the code comment; revisit after
    playtest.
- `tst/player_test.hpp`: `doubleTap()` helper; the `stowWeapon()` sheathe
  helper now waits out the second-tap roll before the chord (behavior unchanged,
  just sequencing). New permanent tests below.
- `README.md`: in-game controls row for the d-pad double-tap.
- `docs/feel-design.md`: one-line player-input verb note at the end of section 3.

## New tests (all permanent, in `tst/player_test.hpp`)

- double-tap E: sword `PS_DODGE` + east vx + i-frames; flail `PS_DEFLECT` +
  west vx; gun `PS_SHOVE`.
- single tap idle; tap, wait `DTAP_WIN+1`, tap idle; tap E then tap W idle.
- held E 30t idle (no edge).
- zero stamina idle; flail mid-attack (`canCancel` false) stays `PS_ATTACK`.
- sheathed double-tap E: `PS_DODGE`, `iT 14`, stam `100-14=86`, vx > 40
  (54-based stowed numbers).
- Existing B-tap + sheathe tests stayed green (sheathe helper adjusted only).

## Verification tails

`make gen-check`:

```
fxdata_manifest: fxdata/manifest.json up to date (52 images, 62 inputs, 19 outputs)
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (82 generated artifacts unchanged)
```

`make test`:

```
Total Passed: 6532
Total Failed: 0
```
(new suites: 6+3+1+3+5 = 18 asserts, all pass)

`make test-tools`:

```
Ran 202 tests in 11.862s
OK
```

`make fxtest-headless` (full, 16 suites; parity excluded by design):

```
test_perf: B pUs=6369 pHz=157 lHz=52 lTk=456 rMx=4772 rAv=4593 ram=582
perf_test PASSED=5 FAILED=0
... every suite PASSED=... FAILED=0
```

`make size`:

```
Sketch uses 28796 bytes (96%) of program storage space. Maximum is 29696 bytes.
Global variables use 1747 bytes (68%) of dynamic memory, leaving 813 bytes.
size: flash=28796/29696 (900 free)  ram=1747/2560
```

## Budget

- Flash: `28720 -> 28796` = **+76 B** (target <=150 B). 900 free.
- RAM: `1744 -> 1747` = **+3 B** as designed (the three detector fields).
- No data/pack change; `make gen` was a no-op (manifest unchanged), `gen-check`
  passed with no generated diff.

## Notes

- `test_parity` remains excluded (AGENTS.md: not a gate). It was already over
  the board at baseline: `30258 bytes (101%)` on a stashed clean `56b34c3`;
  with this change `30298 bytes (101%)` (+40 B). No `MH_DTAP` carve added —
  the image cannot fit either way and is unmaintained diagnostics.
- No commit/push performed.
