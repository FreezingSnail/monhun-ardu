# monhun-ardu-1mb — Core: fixed-point math module (port from mock)

## Files
- added: `src/core/fp.hpp` — header-only fixed-point layer, includes only `<stdint.h>` (no float, no Arduino.h, no `<math.h>`). FP=16, `tdiv`, `addMove`, `addVel` (signed `% 16`, never `& 15`), `DIR8` table, `dirIndexFromInput`, `dirIndexFromDelta`, `isqrt` (bit method), `rotFp` (integer cos/sin), `drainStam` (1/16 rollover). `FpBody` and `FpStam` structs.
- added: `tst/test.hpp` — Test/TestSuite/TestRunner harness, adapted from `~/code/CreatureGathererFX/tst/test.hpp` (duplicate `RESET` define and stray `;` dropped; single-template assert kept; std-only).
- added: `tst/fp_test.hpp` — `FpSuite(TestRunner&)`: tdiv sign cases, addMove left/right and up/down symmetry (28 ticks @ spd 18, within 1 px), DIR8 table + input mapping for all 8 dirs + idle, dirIndexFromDelta cases, isqrt squares/non-squares, rotFp, drainStam exact 1/16 rollover.
- added: `tst/main.cpp` — runs FpSuite, prints summary, returns non-zero on failure.
- changed: `Makefile` — `TEST_SOURCES = tst/main.cpp`; `run_test` macro compiles to `build/tests/host` (mkdir -p, gitignored) and runs it. No artifacts in `tst/`. Other beads can extend later.

## `make test` output (tail)
```
---------- isqrt on perfect squares and non-squares ----------
Passed: 13
Failed: 0
---------- rotFp rotates unit vector with integer cos/sin ----------
Passed: 6
Failed: 0
---------- drainStam decrements stamina exactly in 1/16 units ----------
Passed: 11
Failed: 0
========== Total Counts ==========
Total Passed: 85
Total Failed: 0
```
Exit 0.

## Deviations
- `isqrt` seed changed from mock's `1 << 15` to `1 << 14`. Mock's seed is 2^15 (odd exponent); its `>> 2` chain lands on 8 and 2, which are not powers of 4, so the bit method returns wrong results for small inputs — verified in node: `isqrt(1)=0`, `isqrt(9)=4`, `isqrt(65535)=362`. The mock only used isqrt for mid-range distance thresholds where the error was latent, so behaviour there is unchanged in practice. Fixed seed keeps the bit method and is correct for all n in [0, 65535] (device-safe). Deviation noted in a comment in `fp.hpp`.
- Everything else ports 1:1 from `mock/game.js` (mock untouched).