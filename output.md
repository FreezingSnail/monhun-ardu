# monhun-ardu-7y3 — HUD bars/divider clipped by blk() arena-band clamp

Status: **DONE** (tree dirty, not committed; Ardens device suite green incl. new
`test_hud`).

## Root cause

`blk()` clamped every rect to the arena band (`y0 < HUD_H -> y0 = HUD_H`), but
`drawHud()` targets rows 2..7: divider `blk(0,7,128,1)`, `hudBar(1,2,28,4)` /
`hudBar(29,2,16,4)`, monster `hudBar(82,2,44,3)`, gun reload `blk(67,6,bw,1)`.
All of them returned before touching the framebuffer. HUD text is sprite-based
(FX glyphs) and was unaffected, which is why only the text showed.

## Fix (src/render.hpp)

Byte-identical rasterizer factored into `blkClamp(x, y, w, h, shade, minY)` (one
new `minY` parameter replacing the hardcoded `HUD_H` clamp) with two thin
wrappers:

- `blk(...)` = `blkClamp(..., mh::HUD_H)` — world/arena, unchanged clip (the
  arena's vertical borders must still not paint rows 0..7 when camY > 0).
- `hudBlk(...)` = `blkClamp(..., 0)` — HUD strip, rows 0..7 paintable.

Only HUD call sites switched to `hudBlk`: divider, `hudBar` back + fill, gun
reload bar. All world call sites (drawArena, DEBUG wire, menu underline) still
use `blk()`. No other behavior change.

## Files changed

- `src/render.hpp` — blk -> blkClamp + blk/hudBlk wrappers; 5 HUD call sites.
- `tst/fxdatatest/hud_test.hpp` — new device framebuffer suite (17 asserts).
- `tst/fxdatatest/test_hud.ino` — new test entry (harness/fxtest.hpp, bare P/F).
- `README.md` — challenge item 7 rewritten; status/perf/build numbers refreshed.

No commit, no push; `git status` shows only these 4 paths (2 modified, 2 new).

## Flash / RAM delta (`make build`)

Baseline: flash 28132/29696 B, RAM 1950/2560 B.
After fix: flash **28378/29696 B (95%)**, RAM **1950/2560 B** (610 free).
Delta: **+246 B flash, +0 B RAM** — fits, no gate change.

## 1. `make test` (host) — 0 failed

```
Total Passed: 1490
Total Failed: 0
```

## 2. `make build` — fits

```
Sketch uses 28378 bytes (95%) of program storage space. Maximum is 29696 bytes.
Global variables use 1950 bytes (76%) of dynamic memory, leaving 610 bytes for local variables. Maximum is 2560 bytes.
```

## 3. `make fxtest-headless` (Ardens) — all suites PASS

```
=== test_assets ===
asset_test PASSED=254 FAILED=0
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
=== test_data ===
data_test PASSED=221 FAILED=0
P
test_data: PASS
=== test_hud ===
test_hud PASSED=17 FAILED=0
P
test_hud: PASS
=== test_menu ===
menu_test PASSED=55 FAILED=0
P
test_menu: PASS
=== test_parity ===
parity_test PASSED=660 FAILED=0
P
test_parity: PASS
=== test_perf ===
B pUs=6389 pHz=156 lHz=52 lTk=988 rMx=5056 rAv=4809 ram=467
perf_test PASSED=5 FAILED=0
P
test_perf: PASS
```

Perf vs baseline `B pUs=6383 pHz=156 lHz=52 lTk=988 rMx=4676 rAv=4476 ram=494`:

| metric | baseline | now | budget | result |
|---|---|---|---|---|
| pHz | 156 | 156 | >= 135 | PASS |
| lHz | 52 | 52 | >= 45 | PASS |
| rMx | 4676 | 5056 | <= 7407 | PASS |
| lTk | 988 | 988 | — | — |
| ram | 494 | 467 | >= 300 | PASS |

The render delta (+380 µs max, +333 µs avg) is the expected cost of the HUD
shapes now actually rasterizing: ~16 extra MH_MASK page reads/plane for the
divider/bar/reload `blk` loops). FX is untouched. Ardens present at
`~/code/Ardens/...`, no BLOCKED fallback needed.

## 4. `make gen-check` — PASS

```
fxdata_manifest: fxdata/manifest.json up to date (19 images, 6 inputs, 5 outputs)
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (30 generated artifacts unchanged)
```

## 5. Evidence summary — exact buffer bytes pinned by `test_hud`

Setup: `newGame(W_GUN, MODE_HUNT)`, hp=hpMax, stam=stamMax, monster hp=hpMax,
`reload=35` (ball shell reload 70 -> bar width `(12*35+35)/70 = 6`); render via
the real `renderScene(g, false)`; buffer layout pixel(x,y) =
`buf[(y>>3)*128 + x]`, bit `y&7`.

Plane 0 (all shades 1..3 set; divider adds bit 7 across the strip):

| shape | coords | pinned bytes/bits | result |
|---|---|---|---|
| divider | y=7, x=0..127 | bit 7 set every column | 128/128 lit |
| player HP bar | x=1..28, rows 2..5 + divider | byte `0xBC` (0x3C back/fill + 0x80) | 28/28 |
| stamina bar | x=29..44, rows 2..5 + divider | byte `0xBC` | 16/16 |
| monster HP bar (hunt) | x=82..125, rows 2..4 + divider | byte `0x9C` | 44/44 |
| gun reload bar | x=67..72, y=6 | bit 6 set | 6/6 |

Plane 1 (shade 1 clears, shades 2/3 set — isolates the fills from the backs):

| shape | coords | pinned bytes/bits | result |
|---|---|---|---|
| player HP fill | x=2..27, rows 3..4 | exact byte `0x18` | 26/26 |
| stamina fill | x=30..43, rows 3..4 | exact byte `0x18` | 14/14 |
| monster HP fill | x=83..124, row 3 | exact byte `0x08` | 42/42 |
| reload bar (shade 2) | x=67..72, y=6 | bit 6 set | 6/6 |
| HP back edges x=1,x=28, divider y=7 | | bit clear | 0 lit (cleared) |

Negative control (world path still clips): `blk(10, 0, 20, 16, 3)` leaves page 0
(rows 0..7) at x=10..29 all `0x00` (20/20) and paints page 1 (rows 8..15) all
`0xFF` (20/20) — no HUD spill. Positive control: `hudBlk(10, 0, 20, 2, 3)` sets
page 0 x=10..29 to `0x03` (20/20) and leaves page 1 at `0x00` (20/20).

Bug-catch proof: with `hudBlk` temporarily re-clamped to `HUD_H` (simulating the
pre-fix code; HEAD's `blk` does not even declare `hudBlk`), `test_hud` FAILED 10
asserts (divider, all three bar backs, all three fills, both reload checks, the
`hudBlk` positive control) and reported F; the world-clip negative control still
passed. Suite is permanent, lives in `tst/fxdatatest/`, no /tmp, no scripting
harness.

## 6. README

Challenge item 7 rewritten: bug, root cause, the blkClamp/blk/hudBlk split, the
`test_hud` framebuffer evidence and the 4676 -> 5056 µs / 494 -> 467 B perf
trade; status snapshot refreshed (flash 28378, device tests + hud 17, perf
gate line), device-test list gains `test_hud`, hardware/cadence numbers updated.

## Blockers

None.
