# monhun-ardu-z99 — Device: input layer (dpad + A/B, hold detect)

## Files
- added: `src/core/input.hpp` — pure input layer, `namespace mh`, only
  `<stdint.h>` (no `Arduino.h`, no float):
  - `struct Input { int8_t mx, my; bool a, b; }` (mx/my -1/0/1 per axis).
    Moved here from `game.hpp`; `game.hpp` now `#include "input.hpp"`.
  - `struct InputState { bool prevA, prevB; uint8_t bHeld; bool bReady,
    bLocked; bool aP, bP, bR; }` + `reset()`. `aP/bP/bR` are the per-tick mock
    edges; `bHeld` counts held B ticks and saturates at the threshold; `bReady`
    arms on press; `bLocked` latches once the threshold is reached so a hold
    fires exactly once until release.
  - `inputEdges(in, prevA, prevB, aP, bP, bR)` — stateless mock `step()` edge
    rule (`aP = a && !prevA`, `bP = b && !prevB`, `bR = !b && prevB`).
  - `stepInput(InputState&, const Input&, uint8_t holdTicks)` — one tick of
    edges + hold counting. `holdTicks` is `HOLD_TICKS` (game.hpp, 11); no
    second copy of the constant.
- changed: `src/core/game.hpp` — `Input` now comes from `input.hpp` (textbook
  fields `int8_t` instead of `int16_t`, as the bead specifies); no behaviour
  change.
- changed: `src/core/player.hpp` — `stepPlayer()` computes its edges via the
  shared `inputEdges()` helper instead of duplicating the four lines. The
  Player still owns `bHeld/bReady/bLocked` for the FSM (as instructed); this
  layer does not mirror or mutate them, so there is one edge rule and one
  owner of the FSM hold state.
- added: `tst/input_test.hpp` — 7 permanent tests / 51 asserts.
- changed: `tst/main.cpp` — include + run `InputSuite`.

## Test coverage (`tst/input_test.hpp`)
- A edge fires on press only, no repeat while held, fires again on re-press.
- B edge `bP` and release edge `bR` fire once each.
- Press + release across a tick boundary stays sub-threshold -> a tap.
- Hold counter hits `HOLD_TICKS` on the 11th held tick, latches, and does not
  re-fire while held (saturates).
- Release clears counter + latch; re-press counts and fires again.
- 10 held ticks never latch; d-pad leaves A/B untouched.
- A and B edges independent (A+B combo is not consumed).

## `make test` output (tail)
```
++++++++++ Input layer: edges + B hold detection (src/core/input.hpp) ++++++++++
---------- A and B edges are independent (A+B combo not consumed) ----------
Passed: 4
Failed: 0
========== Total Counts ==========
Total Passed: 497
Total Failed: 0
```
Exit 0 (446 before; +51).

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
Exit 0.

## Adapter the device loop bead (monhun-ardu-3p1) must write
`monhun-ardu.ino` keeps the FX bracket
(`FX::enableOLED` / `arduboy.waitForNextPlane()` / `FX::disableOLED`) and calls
`run()` only when `arduboy.needsUpdate()`. Inside `run()`:

```cpp
// BtnA -> a (attack), BtnB -> b (defense/stance), d-pad -> mx/my.
// pollButtons() must already have been called this frame.
mh::Input in;
in.mx = (arduboy.pressed(RIGHT_BUTTON) ? 1 : 0)
      - (arduboy.pressed(LEFT_BUTTON)  ? 1 : 0);
in.my = (arduboy.pressed(DOWN_BUTTON)  ? 1 : 0)
      - (arduboy.pressed(UP_BUTTON)    ? 1 : 0);
in.a  = arduboy.pressed(A_BUTTON);
in.b  = arduboy.pressed(B_BUTTON);
mh::stepGame(g, in);   // stepPlayer() applies the same edge rule internally
```

- Hardware A -> `Input::a`, B -> `Input::b`; poll exactly once per logic tick.
- `stepGame()`/`stepPlayer()` compute `aP/bP/bR` through `mh::inputEdges()`, so
  the device does **not** need to build an `InputState` for the FSM. A+B is left
  untouched for the future debug toggle (both edges surface independently).
- No `Arduino.h` (or any hardware symbol) leaks into `src/core/`; `Input` is a
  plain struct, so host tests and AVR share the identical edge/hold rule.

## Notes
- No float/double, no retuning; `HOLD_TICKS` stays 11 in `game.hpp`.
- `mock/` untouched; player/monster/projectile/world behaviour unchanged
  (446 original asserts still green).
