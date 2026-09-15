# monhun-ardu-8v7 — Gate: device perf bench (plane rate, logic Hz, RAM)

## Bead
`monhun-ardu-8v7` (slice of epic `monhun-ardu-kt7`). Worst-case on-device bench:
plane rate, logic rate, free RAM, render/logic/FX cost, measured with a
permanent `tst/fxdatatest/test_perf.ino`. Budgets come from the bead design
(logic >= 45 Hz, planes stable ~135 Hz, RAM >= 300 B free, render/FX inside the
plane budget).

## Files
- added `src/render.hpp` — the block-art renderer (sprites/art/HUD/scene) lifted
  verbatim out of `monhun-ardu.ino` so the perf bench times the **real** shipping
  render path instead of a drifting copy. Pure move: release build is
  byte-identical (flash 27672 / RAM 1941 before and after). Core sim untouched.
- changed `monhun-ardu.ino` — drops the moved render body, includes
  `src/render.hpp`, `render()` now calls `mh::renderScene(g, wire)`. The
  `DEBUG_HURTBOXES` overlay and `pollDebugToggle()` stay in the sketch.
- added `tst/fxdatatest/perf_test.hpp` — on-device bench: worst-case scene
  (max-effect hunt + pole train), shipping-loop timing, stack-painted RAM
  watermark, budget gates. Integer-only timing via `micros()` (Timer0 /64,
  independent of ArduboyG TIMER1 / ArduboyTones TIMER3).
- added `tst/fxdatatest/test_perf.ino` — harness entry, picked up by the
  `test_*.ino` wildcard.
- changed `output.md`.

## Method / caveats
- Measured on the Ardens cycle-accurate ATmega32u4 model (`make fxtest-headless`).
  No physical unit was attached; the bead's "not emulator" pitfall could not be
  honoured literally. Numbers are CPU cycles from the modelled timers, so the
  absolute us are cycle counts, not wall clock.
- Plane rate = achieved shipping-loop rate (bracket + render + logic/3), i.e.
  the actual display refresh under load. The flat TIMER1 plane ISR rate is
  ~160 Hz (bare `waitForNextPlane` = 6228 us).
- Free RAM measured by painting unused SRAM and scanning for the deepest SP
  inside the render call tree, not from `loop()`.
- Audio cue playback is excluded from the bench image (flash: the full render
  stack + sim + Serial already need ~27.7 KB of 29.7 KB, so ArduboyTones cannot
  fit). `audioUpdate()`'s real edge-detect path still runs and is timed; only
  the one-shot tone() arming is out. Cue playback is covered by `test_audio`.

## Measured numbers (Ardens, ATmega32u4 @16 MHz)
Test output line:
`B pUs=12087 pHz=82 lHz=27 lTk=988 rMx=13312 rAv=10624 ram=472`

| quantity | value |
|---|---|
| plane period under load | **12087 us** -> **82 Hz** |
| logic rate under load | **27 Hz** (plane-bound: planes/3) |
| logic tick max (in-loop) | **988 us** (0.062 logic frame) |
| logic tick isolated (avg/max) | 288 / 316 us |
| render in-loop avg / max | **10624 / 13312 us** |
| render isolated, base hunt | 6614 us |
| render isolated, base train (pole) | 7638 us |
| render isolated, bead scene (3 shells + 6 sparks) hunt | 7790 us |
| render isolated, stress hunt (3 shells + 12 fx) | 10558 us |
| render isolated, stress train | 10355 us |
| free RAM at deepest SP | **472 B** (test image; shipping ~443 B) |
| FX asset read: 32x24 monster sprite | 255 us avg / 264 max |
| FX asset read: 4x8 font glyph | 47 us |
| FX enable/disable bracket overhead | < 4 us (below micros resolution) |
| `drawArena` alone | **6044 us/plane** |

### Hotspot found (instrument-only, not fixed)
`drawArena()` (~6044 us of the ~6614 us base plane) evaluates three **signed
16-bit modulo** expressions per iteration (`(i*7)%3`, `(i*53)%WORLD_W`,
`(i*29)%WORLD_H`) over 260 dots. On AVR these lower to `__divmodhi4`; the dot
loop is the single dominant render cost. This is the reason even the *base*
scene sits at ~6.6 ms (just over the 6.4 ms nominal plane), and why any added
FX pressure drops the loop below 135 Hz.

## Budget table (bead thresholds)
| gate | budget | measured | result |
|---|---|---|---|
| render max fits 1/135 s | <= 7407 us | 13312 us | **FAIL** |
| plane rate | >= 135 Hz | 82 Hz | **FAIL** |
| logic tick fits one logic frame (3 planes) | <= 19230 us | 988 us | PASS |
| logic rate | >= 45 Hz | 27 Hz | **FAIL** |
| free RAM | >= 300 B | 472 B | PASS |
| FX asset read fits plane budget | <= 7407 us | 255 us | PASS |

FAIL bitmask from the test: `13` = render (bit 0) + plane rate (bit 2) + logic
rate (bit 3). Logic-rate failure is a consequence of the render overrun (logic
is bound to the 1:3 plane cadence), not of the logic tick itself.

## Flash / RAM
Shipping (`make build`, unchanged by the extraction):
```
Sketch uses 27672 bytes (93%) of program storage space. Maximum is 29696 bytes.
Global variables use 1941 bytes (75%) of dynamic memory, leaving 619 bytes for local variables. Maximum is 2560 bytes.
```
Bench image (`test_perf`, tight by design — full render + sim + Serial):
```
Sketch uses 29594 bytes (99%) of program storage space. Maximum is 29696 bytes.
Global variables use 1912 bytes (74%) of dynamic memory, leaving 648 bytes for local variables. Maximum is 2560 bytes.
```

## Test tails
`make test` (host C++17):
```
========== Total Counts ==========
Total Passed: 497
Total Failed: 0
```

`make fxtest-headless` (Ardens):
```
=== test_assets ===
asset_test PASSED=30 FAILED=0
P
test_assets: PASS
=== test_audio ===
test_audio PASSED=14 FAILED=0
P
test_audio: PASS
=== test_boot ===
test_boot PASSED=4 FAILED=0
P
test_boot: PASS
=== test_parity ===
parity_test PASSED=660 FAILED=0
P
test_parity: PASS
=== test_perf ===
B pUs=12087 pHz=82 lHz=27 lTk=988 rMx=13312 rAv=10624 ram=472
F 13
perf_test PASSED=0 FAILED=5
F
test_perf: FAIL
make[1]: *** [fxtest-run] Error 1
```

## Follow-up notes (gameplay NOT retuned)
1. `drawArena()` signed-modulo dot field is the root cause of the base render
   cost (~6 ms/plane). A renderer-only fix (unsigned/bit ops or a precomputed
   dot table) would free ~4-5 ms of the plane budget without touching sim
   semantics.
2. Worst-case simultaneous FX (max effect/projectile counts + damage-number
   glyphs) drops the loop to ~82 Hz; damage-number text is the most expensive
   effect (one FX glyph read per digit).
3. The `DEBUG_HURTBOXES` overlay build overflows flash (already true before this
   bead); only the debug build is affected.

## Result
**Budgets FAIL.** Per bead instructions the gate is not retuned; follow-ups are
above. `monhun-ardu-8v7` is left **OPEN** with blockers (render/plane/logic-rate
budgets) so the feel gate (`monhun-ardu-1to`) can decide.
