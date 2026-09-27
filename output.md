# monhun-ardu-nx9 — Owner playtest fixes: ground revert, hunt-start A lock, moving-room perf guard

## Status

DONE. No commit/push (orchestrator commits). No generated-artifact changes
(flags/comments/tests only; `make gen-check` green with the tree's committed
set).

## Player-visible fixes

1. **Ground back to the procedural dot field + border** (`drawArena`), the
   pre-`9kn` look the owner preferred. Makefile `SIZE_FLAGS` / `dev` /
   `dev-hitboxes` / `demo` now pass `-DMH_ROOM_IMAGE=0`; the stored-room-image
   blit (`drawRoom`), its generated `mh_map_*` layers and `test_zones` (forces
   1) stay in the tree as the documented carve. `test_perf` forces 0 so the
   bench describes the shipped path.
2. **The launch A can no longer draw the stowed weapon.** `newGame()` clears
   `Game::prevA/prevB`; the A press that launched a hunt (demo GO row or quest
   card) therefore read as a fresh edge on the first `stepGame()` tick.
   `mh::primeHuntInput(g, in)` (`src/core/world.hpp`) seeds the fresh world's
   edges from the launching sample, and the sketch calls it right after both
   launch paths (`demoLaunch`, `startHuntFromSave`) — the same held-button
   guard `appNavApply()`/`demoEnter()` already use for screen changes. The
   release still tracks, so the next deliberate A draws.

## Stutter evidence (moving-room perf phase)

`perf_test.hpp` gained a moving-room phase (owner playtest scenario): a hunt in
the real 512x112 area room, hunter walking RIGHT from mid-room, beast parked at
its home spawn off-screen — on the exact shipping loop shape. It covers what the
fixed-camera pressure scene cannot (camera-follow scroll, the area's door cart
reads in logic, its prop records in render) and adds the 6th perf assert
(moving render max ≤ 1/135 s).

`FXTEST_ONLY=test_perf`, Ardens cycle model, before → after the ground revert:

| metric | stored image (9kn) | dot field (nx9) | delta |
|---|---|---|---|
| `rMx` (pressure scene) | 4284 µs | 3408 µs | **−876** |
| `rAv` (pressure scene) | 3710 µs | 2830 µs | **−880** |
| `mRrMx` (moving area room) | 3812 µs | 3040 µs | **−772** |
| `mRrAv` (moving area room) | 3160 µs | 2247 µs | **−913** |
| `mLgMx` (moving logic max) | 440 µs | 440 µs | 0 |
| bench free RAM | 481 B | 563 B | **+82** |
| plane rate / logic | 157 / 52 Hz | 157 / 52 Hz | 0 |

The stored-image blit streamed 1024 B/plane off the cart (~880 µs/plane of CPU
inside the frame); with the dot field the frame has that margin back, so a
scrolling camera cannot push a plane past the ISR cadence. Flash also drops.

## Files

- `Makefile` — ground flags → `-DMH_ROOM_IMAGE=0` (+ comment); flash headroom
  note.
- `monhun-ardu.ino` — `primeHuntInput(g, in)` after `demoLaunch()` (demo) and
  after `startHuntFromSave()` (quest card).
- `src/core/world.hpp` — new `primeHuntInput()` helper (documented hunt-start
  edge guard).
- `src/render.hpp` — ground-carve comment updated (shipping default is the dot
  field again; image path is the `-DMH_ROOM_IMAGE=1` carve).
- `tst/world_test.hpp` — prime test: control (unprimed held A draws) vs primed
  (stowed stays, next press draws).
- `tst/demo_menu_test.hpp` — demo E2E: GO A + `primeHuntInput` → no draw, next
  A draws.
- `tst/fxdatatest/perf_test.hpp` — moving-room phase + `mRrMx/mRrAv/mLgMx`
  printout + 6th assert.
- `tst/fxdatatest/test_perf.ino` — forces `MH_ROOM_IMAGE 0` (shipped path).
- `tst/fxdatatest/zones_test.hpp` — comment refresh only (still forces 1).
- `README.md` — status snapshot (host 7115, perf numbers, shipping 29628/68
  free), ground paragraphs, hardware/cadence numbers, flash/RAM history.
- `docs/map-zones.md` — shipping-render note now `-DMH_ROOM_IMAGE=0`.

## Verification (full gate, build/nx9_gate.log)

- `make gen-check` — `fxdata_manifest: PASS (217 generated artifacts unchanged)`;
  no generated churn (flags/comments/tests only).
- `make test` — `Total Passed: 7115  Total Failed: 0` (7106 + 9 new asserts).
- `make test-tools` — OK (unittest discover, exit 0).
- `make fxtest-headless` — **19 suites, 2153 asserts, 0 FAIL**; every suite
  `: PASS`. `test_perf` now `PASSED=6` (moving-room render gate):
  `B pUs=6344 pHz=157 lHz=52 lTk=176 rMx=3408 rAv=2830 mRrMx=3040 mRrAv=2247 mLgMx=440 ram=563`.
  `test_zones PASSED=100` (stored-image blit pixels still pinned with
  `MH_ROOM_IMAGE 1`).
- `make size` — `size: flash=29628/29696 (68 free)  ram=1867/2560`
  (was 29666/29696, 30 free with the stored-image ground).
- Builds outside the gate: demo flags `arduino-cli compile` → 23290 B flash /
  1674 B RAM; dev-hitboxes flags → 26382 B / 1786 B RAM (both fit).
- README device-suite row corrected to the measured per-suite counts (forge 79,
  quests 110; previously stale 75/112) and the new 2153 total.

## Wall time

- Diagnosis + fixes + benches + gate: one inline orchestrator session (no
  workers dispatched — one small bead, three layers). Targeted perf runs
  (~40 s each incl. compile) for the before/after ground numbers.
