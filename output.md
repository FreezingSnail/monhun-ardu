# monhun-ardu-rie — `make dev-hitboxes` (DEBUG_HURTBOXES overlay build)

Bead: monhun-ardu-rie. Repo HEAD deed0fb (branch weapon-art).
No commit, no push, no `gen`/`gen-check` (no data change).

## Files changed

- `Makefile` — added `dev-hitboxes` to `.PHONY`; new `dev-hitboxes` target
  after `dev` (copies dev's arduino-cli call, appends `-DDEBUG_HURTBOXES=1` to
  `compiler.cpp.extra_flags`, keeps `-DMH_DEV=1`; size line label
  `dev-hitboxes size: flash=%d/%d (%d free)  ram=%d/2560`; same FX-image check
  + Ardens launch as `dev`). Comment notes the overlay only fits the `MH_DEV`
  carve and why.
- `tst/fxdatatest/test_wire.ino` — new device suite entry (fx_globals + wire_test,
  FxTest report, `exit(0)`); auto-picked by the `test_*.ino` wildcard.
- `tst/fxdatatest/wire_test.hpp` — new suite: `#define DEBUG_HURTBOXES 1` before
  `#include "src/render.hpp"`; pins solid + dotted `wireBox` borders and the
  `renderScene` routing equality.
- `README.md` — hardware/cadence bullet: overlay ships behind `make
  dev-hitboxes`, always on, dev build only.
- `AGENTS.md` — commands block: `make dev | dev-hitboxes`.
- Pre-existing SPIKE TRIM in `monhun-ardu.ino` + `src/render.hpp` kept exactly
  as-is (untouched by this worker).

## Test suite (tst/fxdatatest/wire_test.hpp)

- (a) solid `wireBox(10,12,7,5,false)`: 20 perimeter pixels lit
  (2*7+2*5-4), strictly-interior clear, whole-fb lit count == 20 (nothing
  outside the 7x5 bbox); corner/edge spot pins.
- (b) dotted `wireBox(20,12,7,5,true)`: top/bottom x=20,22,24,26 lit and
  x=21,23,25 clear; left/right y=12,14,16 lit and y=13,15 clear; interior
  clear; whole-fb lit count == 10.
- (c) routing: scene pinned (newGame W_SWORD/HUNT, cam 0/0, fxN 0, hunter +
  beast on-screen); asserts `renderScene(g,true)` == `renderScene(g,false) |
  drawDebug(g,0,0)` for all 1024 buffer bytes, and the overlay is live
  (`B1 != B2`). RAM-tight test image, so the equality runs one 128 B page at a
  time (3 renders/page, `current_plane` pinned at 0; shade 3 sets bits on every
  plane, so pass order is irrelevant).

Note: the default spawn (player y=60) is off the 64 px screen once the debug
box adds HUD_H, so the suite pins the hunter/beast on-screen; the equality is
independent of placement.

## Verification (wall time per step)

| step | result | wall |
|---|---|---|
| `make test` | Passed 6815, Failed 0 | 2 s |
| `make test-tools` | Ran 388 tests, OK | 71 s |
| `FXTEST_ONLY=test_wire make fxtest-headless` | `test_wire PASSED=31 FAILED=0`, `P`, PASS | 2 s |
| `ARDENS=/usr/bin/true make dev-hitboxes` | exit 0 | 2–3 s |
| `make size` | shipping unchanged | 3 s |

### Tails / numbers

`make test`:
```
========== Total Counts ==========
Total Passed: 6815
Total Failed: 0
```

`make test-tools`:
```
Ran 388 tests in 22.428s
OK
```

`FXTEST_ONLY=test_wire make fxtest-headless`:
```
build: test_wire
Sketch uses 18758 bytes (63%) of program storage space. Maximum is 29696 bytes.
Global variables use 1909 bytes (74%) of dynamic memory, leaving 651 bytes for local variables. Maximum is 2560 bytes.
=== test_wire ===
test_wire PASSED=31 FAILED=0
P
test_wire: PASS
```

`make dev` (ARDENS=/usr/bin/true, for the delta):
```
dev size: flash=28428/29696 (1268 free)  ram=1814/2560
```

`ARDENS=/usr/bin/true make dev-hitboxes` (exit 0; ARDENS=/usr/bin/true is the
check — compile only, no GUI left open):
```
Sketch uses 29608 bytes (99%) of program storage space. Maximum is 29696 bytes.
Global variables use 1814 bytes (70%) of dynamic memory, leaving 746 bytes for local variables. Maximum is 2560 bytes.
dev-hitboxes size: flash=29608/29696 (88 free)  ram=1814/2560
```
Delta `dev-hitboxes` vs plain `dev`: **+1180 B flash**, **RAM unchanged
(1814/2560)**.

`make size` (shipping):
```
size: .text=29110 .data=50 .bss=1764
size: flash=29160/29696 (536 free)  ram=1814/2560
```
Shipping flash/RAM unchanged vs the spike baseline (29160/29696, 536 free).

## Acceptance

1. `make test` passes — 6815/0. ✅
2. `FXTEST_ONLY=test_wire make fxtest-headless` PASS — 31/0. ✅
3. `make size` shipping unchanged — `flash=29160/29696 (536 free)`. ✅
4. `ARDENS=/usr/bin/true make dev-hitboxes` prints `dev-hitboxes size: ...`
   and exits 0; no GUI left open. ✅
5. `dev-hitboxes` vs `dev`: +1180 B flash, RAM unchanged. ✅
6. This report with wall time. ✅
