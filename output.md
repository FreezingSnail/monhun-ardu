# monhun-ardu-dx5.7 — perf: drawArena dots -> direct framebuffer writes

Status: DONE. All gates green, budget fits. Tree left dirty (no commit/push).
HEAD: cafe2af (baseline for all deltas below).

## Files / lines

- `src/render.hpp`, `drawArena()` only (dot loop, now lines 262-289). Single change:
  - Hoist the dot shade's plane/color mapping once per call:
    `const bool dotLit = arduboy.color(1) != 0;` (== `planeColor(current_plane, 1)`),
    plus one `uint8_t *const fb = arduboy.getBuffer();`.
  - Replace the ~173/plane `arduboy.drawPixel(sx, sy, 1)` calls with a direct
    page-major OR: `fb[((sy>>3)<<7) | sx] |= (uint8_t)(1u << (sy & 7));`.
  - Existing bounds checks (`sx>=0 && sx<SCREEN_W && sy>=HUD_H && sy<SCREEN_H`) and
    the incremental phase/wx/wy counter math are unchanged. `blk()` borders and all
    other drawing untouched. No `drawPixel` calls changed elsewhere.

## Deviation from the bead's prescribed code (measured, deliberate)

The bead prescribed `mh::mhBit8(sy)` for the one-hot bit (core/bitlut.hpp). That
variant was implemented first and measured a **regression**, because `mhBit8`
reads through `mhPgmReadU8`, which is deliberately `__attribute__((noinline))` on
AVR (progmem.hpp: -4 B whole-image). One out-of-line `rcall`+`lpm` per dot costs
more than the inline bit chain in `Arduboy2Base::drawPixel`'s asm.

Measured on test_perf (deterministic, 3 identical runs each):

| variant | rMx | rAv |
|---|---|---|
| baseline (`drawPixel`) | 3140 | 2632 |
| prescribed: `mhBit8(sy)` | 3200 | 2722  (regression, +60/+90) |
| shipped: `1u << (sy & 7)` | 3004 | 2550  (win, -136/-82) |

GCC lowers `1u << (sy & 7)` to the same inline branch chain drawPixel uses, so no
shift loop and no call. `bitlut.hpp` include removed (unused). The index
expression stays exactly as the bead specified. The `mhBit8` variant is available
in this report if the orchestrator wants it re-applied.

## Verification (exact commands, in order)

1. `make test` — `Total Passed: 6285  Total Failed: 0`
2. `make size` — `flash=28810/29696 (886 free)  ram=1704/2560`
3. `make fxtest-headless FXTEST_ONLY=test_hud` — `test_hud PASSED=29 FAILED=0` / `PASS`
4. `make fxtest-headless FXTEST_ONLY=test_player_art` — `test_player_art PASSED=120 FAILED=0` / `PASS`
5. `make fxtest-headless FXTEST_ONLY=test_perf` —
   `B pUs=6342 pHz=157 lHz=52 lTk=184 rMx=3004 rAv=2550 ram=608` / `perf_test PASSED=5 FAILED=0`
6. `make fxtest-headless` (full) — all suites PASS, 0 FAILED:
   assets 270, audio 9, boot 4, combat 237, data 348, hub 63, hud 29, items 35,
   menu_art 53, menu 60, monster_art 127, perf 5, player_art 120, quests 50,
   screens 85, smith 115, tell 18, zones 80. (test_parity excluded by design.)

## Before / after

- Size: flash 28792 -> 28810 (**+18 B**, 904 -> 886 free); ram 1704 -> 1704.
  Not negative, but near-flat (+0.06%) with a clear CPU win, per the dx5.6 spike
  bar ("both improve or flash stays flat with clear CPU win").
- Perf: rMx 3140 -> 3004 (**-136 us**), rAv 2632 -> 2550 (**-82 us**);
  plane pUs 6342 -> 6342, pHz/lHz/lTk unchanged, ram 608 (perf image) unchanged.

## Pixel semantics

Color 1 lights only where `planeColor(plane, 1) != 0` (plane 0 in L4_Triplane);
the hoisted `dotLit` reproduces that exactly. drawArena is the first draw of the
scene (before props/monster/player/effects) and the plane buffer is cleared by
`waitForNextPlane`, so the old drawPixel's clear-on-zero writes were no-ops;
skipping them is pixel-identical. test_hud/test_player_art/test_zones pixel
assertions all pass.
