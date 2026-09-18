# monhun-ardu-836 — bake flail whirl ring into one sprite (6 dots -> 1 blit)

Worker report. No commit/push/`git add` performed.

## What changed

- **`tools/gen-art.py`** — new `whirlring` block sheet (48x32, 24 frames). The 6
  orbit dots of mock `drawPlayer` are pre-composited per phase:
  `a_i = bin_centre(f) + RING6[i]`, dot at
  `(mul_q4(cos256(a_i), rx) + 24, mul_q4(sin256(a_i), ry) + 16)` as a 2x2 LIGHT
  block. The Q4 table is **parsed out of `src/core/sin256.hpp`** (`load_sin65`)
  and `mul_q4` matches `render.hpp`, so the bake is the device math, not a
  re-derivation. `RING6` and `WHIRL_RING_FRAMES=24` are the source of truth now
  that the render no longer walks the ring.
- **`data/equipment/flail_ring.json`** — record now points at `fxwhirlring`
  (`cell [48,32]`, `anchor [24,16]`, `order pose`, `frames 24`, `variants
  0..23`). The part view's existing variant selector (same mechanism as the
  sword slash) drives the phase.
- **`src/render.hpp`** — whirl branch replaces the 6-dot loop with one
  `partVariantDraw(PART_FLAIL_RING, phase, cx, cy)`:
  `phase = ((whirlTick * ANG_WHIRL_RING) & 255) * art_dims::whirlring_frames >> 8`.
  Ball blit unchanged, so the stance is 2 `sprDraw`s instead of 7. Removed the
  now-dead `RING6`. The render comment keeps the mock's exact-angle reference
  (0.35/0.55 rad ellipses -> ANG 14/22, radii from `art_dims`/fxdump).
- **`tst/fxdatatest/player_art_test.hpp`** — goldens regenerated via the
  documented path (PRINT_GOLDENS true -> run -> copy `G` lines -> false), regen
  history note added.
- Generated: `fxwhirlring_48x32.png`, `fxdata/blocks/Sprites.txt`, `fxdata.h`,
  `art_dims.hpp` (`whirlring_frame_w/h/frames`), `equip_meta.hpp`/`equip.bin`,
  `fxdata.bin`/`fxdata-data.bin`, `manifest.json`.

## Verification (exact commands + tails/numbers)

### 1. `make gen` x2 -> `make gen-check`
```
make gen   # pass 1
make gen   # pass 2
make gen-check
fxdata_manifest: PASS (51 generated artifacts unchanged)
```
`fxdata_manifest: fxdata/manifest.json up to date (31 images, 40 inputs, 11 outputs)`.
31 images (was 30) = the new ring PNG.

### 2. `make test` + `make test-tools`
```
Total Passed: 3119
Total Failed: 0
```
```
Ran 81 tests in 5.059s
OK
```

### 3. `make fxtest-headless` (full)
```
=== test_parity ===
parity_test PASSED=660 FAILED=0
=== test_perf ===
B pUs=6614 pHz=151 lHz=50 lTk=984 rMx=5496 rAv=5136 ram=424
perf_test PASSED=5 FAILED=0
=== test_player_art ===
test_player_art PASSED=111 FAILED=0
```
- perf **5/5**; `rMx=5496 rAv=5136 pUs=6614` vs baseline `5812 / 5292 / 6771`
  (**rMx -316, rAv -156, pUs -157**; repeat run identical, stable).
- `test_player_art` **111/0 with regenerated goldens**.

### Golden index diff (exactly which cases changed)
The goldens diff is a single line; only **indices 21 and 22** changed:
| idx | case | old (p0,p1,p2) | new |
|----|------|----------------|-----|
| 21 | `W_FLAIL, PS_IDLE, ST_WHIRL, fx=16` | `909e0569 e234c969 e234c969` | `bdcdefbd 58a536bd 83571f35` |
| 22 | `W_FLAIL, PS_IDLE, ST_WHIRL, fx=-16` | `28a41e99 9e3f6599 9e3f6599` | `4649f76d d1a7a46d d12b30a5` |
Every other case is byte-identical (`git diff -U0` shows the changed values
adjacent to unchanged 20 and 23 in the same row). Both changed cases are flail
whirl; no non-whirl case moved.

### 4. `make build` + `make size`
```
Sketch uses 27020 bytes (90%) of program storage space. Maximum is 29696 bytes.
size: .text=26962 .data=58 .bss=1960
size: flash=27020/29696 (2676 free)  ram=2018/2560
```
Flash delta vs 27050: **-30 B** (dropped the 6-iteration ring loop, added the
phase mul + variant lookup).
Cart delta: `fxdata/fxdata.bin` 96256 -> **124160 (+27904 B)**;
`fxdata/fxdata-data.bin` 96243 -> **123917 (+27674 B)** = 27650 B ring blob
(48x32: 1152 B/frame x 24 + 2 B header) + 24 B equip.bin growth.
`fxdata/tables/equip.bin` 852 -> 876 (+24 B; +32 variant bytes -8 elsewhere).

### Targeted whirl/perf check
There is no dedicated whirl perf case; `primeHunt()` (`perf_test.hpp`) sets
`p.stance = ST_WHIRL` with `whirlTick = 3`, so the hunt plane is the whirl worst
case. Measured hunt-plane delta is the whole-suite delta above: **rMx -316 us
(5812 -> 5496)**, i.e. the ring bake is a real win, not a regression.

Analysis: replacing 5 tiny `sprDraw`s with one 48x32 blit. Cost model with the
measured fixed seek F ~= 156 us and per-byte b: old ring = `6*(F + 12b)`
(8x4 -> 12 B/plane-pass), new = `F + 1152b`; delta = `5F - 1080b = 316`
=> `b ~= 0.43 us/byte`. The larger box eats most of the 5 saved seeks but not
all. A 48x32 canvas is the minimum that holds the full ellipse (~42x30 px
extent), so the blit area is not reducible without clipping the orbit.

## Deviations / notes

- **24 phases, not 32.** 32 phases (36,864 B) pushed `mhEquip` to 0x107CF
  (67,535 B) past the 64 KiB window `core/fxmem.hpp` requires for its 16-bit
  fake cart pointers (the blocks section is packed before the `raw_t` tables in
  `fxdata.txt`). 24 phases (27,648 B) keeps every runtime table below the window
  (`mhEquip` 0x77CD -> 0xE3CF). Rationale is recorded in `gen-art.py`; the epic
  lists 24 as an accepted candidate. 256/24 = 10.67 units/frame vs the 14-unit
  tick step, so the ring still advances smoothly.
- Quantization is the intended visual change: baked frames sit at bin centres,
  worst-case error 256/(2*24) = 5.33 units (~7.5 deg).
- Pipeline bootstrap: adding a brand-new gen-art sheet is a documented two-pass
  operation; `make gen` ran first with the sheet added (no equipment reference)
  to publish the `fxwhirlring` symbol, then again with the equipment/render
  change. The committed state is a single-run fixed point (`make gen-check`).
  `gen-equipment.py` was not modified.
- No docs/ update (out of bead scope).
