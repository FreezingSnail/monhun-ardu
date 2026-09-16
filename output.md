# monhun-ardu-kt7.3 — Render: fix arena modulo hotspot + effect cap

## Bead
`monhun-ardu-kt7.3` (slice of epic `monhun-ardu-kt7`). Fixes the render hotspot
that made the device perf gate `monhun-ardu-8v7` fail: render 13312 us/plane vs
<= 7407 budget, plane 82 Hz vs >= 135, logic 27 Hz vs >= 45. Render-only: core
sim and parity fixtures untouched.

## Files
- changed `src/render.hpp` — render-only:
  - `drawArena()`: the three per-dot signed 16-bit modulos (`(i*7)%3`,
    `(i*53)%WORLD_W`, `(i*29)%WORLD_H`) are gone. `(i*7)%3 == i%3` becomes a
    3-phase counter; the world coords are walked with incremental counters
    (`+53`/`+29` with one conditional wrap, since each step is < its modulus).
    The 1x1 dots now call `arduboy.drawPixel()` directly instead of `blk()` (the
    bounds test is already exact, so the redundant clip is skipped). Dot
    positions are mathematically identical: `wx`/`wy` at iteration `i` equal
    `i*53 % 256` / `i*29 % 112`, and skip is `i % 3 == 0`. Integer-only.
  - `drawEffects()`: render-side cap `MAX_FX_DRAW = 6`. The mock has **no** cap
    (unbounded `effects[]`); the device core caps at `MAX_EFFECTS = 12`, but the
    transient worst case (6 simultaneous damage numbers, each 2-3 FX glyph
    reads, + sparks) was the remaining render bottleneck. Only the newest 6
    effects are painted now. Sim semantics unchanged: every effect still ticks
    and expires in core; the cap is presentation-only.
- changed `output.md`.

## Profiler evidence (Ardens headless, `profiledump`, 3000 ms)
Command (from repo root, after `make build`):
```
Ardens headless=3000 display=ssd1306 fxport=d1 profiledump=build/profiler.txt \
  file=dist/monhun-ardu.ino.elf file=fxdata/fxdata.bin
```

### BEFORE (HEAD `114b8f0`)
```
cycles 48000936  cycles_with_sleep 48000936  cpu_active_pct 100.0
hotspots	count	pct	begin	end	name
14037605	29.24	0x0fc6	0x111c	mh::blk(long, long, long, long, unsigned char) (.part.11)
 5718340	11.91	0x111c	0x1150	abg_detail::...ArduboyG_Common...::paint(...)
 1532782	 3.19	0x331c	0x6a86	main
 1024592	 2.13	0x1b64	0x1e66	SpritesU::drawPlusMaskFX(int, int, uint24, unsigned int)
```

### AFTER
```
cycles 27005017  cycles_with_sleep 48000574  cpu_active_pct 56.3
hotspots	count	pct	begin	end	name
 7161772	14.92	0x112a	0x115e	abg_detail::...ArduboyG_Common...::paint(...)
 5101749	10.63	0x332a	0x6aa0	main
 2092942	 4.36	0x0ff2	0x112a	mh::blk(long, long, long, long, unsigned char) (.part.11)
 1281239	 2.67	0x1b72	0x1e74	SpritesU::drawPlusMaskFX(int, int, uint24, unsigned int)
  768842	 1.60	0x0fc6	0x0ff2	Arduboy2Base::drawPixel(int, int, unsigned char) (.part.1)
```

Share collapse: `mh::blk` **29.24% -> 4.36%** (absolute 14.04 M -> 2.09 M cycles),
total active cycles **48.0 M -> 27.0 M** (cpu_active 100% -> 56.3%). The arena dot
field no longer calls `__divmodhi4`, and it regressed out of the top-3 hotspots.
`paint` (the ArduboyG plane blit, outside the render scene) is now #1 and
unchanged in absolute terms (7.16 M) — the fixed floor.

## Perf gate (test_perf, Ardens cycle-accurate update)
`B pUs=6839 pHz=146 lHz=48 lTk=972 rMx=6008 rAv=5335 ram=478` — no `F` line;
`perf_test PASSED=5 FAILED=0`.

| gate | budget | before | after | result |
|---|---|---|---|---|
| render max fits 1/135 s | <= 7407 us | 13312 us | **6008 us** | PASS |
| plane rate | >= 135 Hz | 82 Hz | **146 Hz** | PASS |
| logic tick fits one logic frame | <= 19230 us | 988 us | 972 us | PASS |
| logic rate | >= 45 Hz | 27 Hz | **48 Hz** | PASS |
| free RAM | >= 300 B | 472 B | 478 B | PASS |

Test-tail line printed deterministically across repeat runs (same `B` line each
time).

## Test tails
`make test` (host C++17):
```
========== Total Counts ==========
Total Passed: 497
Total Failed: 0
```

`make fxtest-headless` (Ardens):
```
asset_test PASSED=30 FAILED=0  -> test_assets: PASS
test_audio PASSED=14 FAILED=0  -> test_audio: PASS
test_boot PASSED=4 FAILED=0    -> test_boot: PASS
parity_test PASSED=660 FAILED=0 -> test_parity: PASS
B pUs=6839 pHz=146 lHz=48 lTk=972 rMx=6008 rAv=5335 ram=478
perf_test PASSED=5 FAILED=0    -> test_perf: PASS
```

## Flash / RAM
Shipping (`make build`, render-only change):
```
Sketch uses 27698 bytes (93%) of program storage space. Maximum is 29696 bytes.
Global variables use 1941 bytes (75%) of dynamic memory, leaving 619 bytes for local variables. Maximum is 2560 bytes.
```
Bench image (`test_perf`):
```
Sketch uses 29646 bytes (99%) of program storage space. Maximum is 29696 bytes.
Global variables use 1912 bytes (74%) of dynamic memory, leaving 648 bytes for local variables. Maximum is 2560 bytes.
```
Flash +26 B shipping, +16 B bench vs the pre-fix build; both fit.

## Notes / semantics
- Dot pattern is unchanged (same 260-dot sequence, same skip, same coords); the
  optimization is a pure algebraic rewrite. No golden render test exists in the
  suite, so equivalence is by construction.
- `MAX_FX_DRAW = 6` is presentation-only and documented in-code; the mock has no
  equivalent cap, so this is the render-side cap the bead authorised. It only
  affects the transient overload case (>6 simultaneous effects); normal play is
  unaffected.
- No core/sim change; `tools/gen-parity-fixtures.js` fixtures and
  `tst/fxdatatest/parity_test.hpp` stay byte-identical (660 pass).
- No float/double anywhere in the change.

---

# monhun-ardu-8v7 — Gate: device perf bench (independently re-run & closed)

Independent re-run of the perf gate at HEAD `80bbfb0` (arena modulo fix +
`MAX_FX_DRAW=6`), confirming the kt7.3 numbers are stable and deterministic.

## Build / test at HEAD `80bbfb0`
`make build` (shipping `arduboy-fx`): flash **27698 B (93%)** / 29696, globals
**1941 B (75%)** / 2560, 619 B free for locals.
`make test`: **497 passed / 0 failed**.
`make fxtest-headless`: assets 30/0, audio 14/0, boot 4/0, parity 660/0,
perf 5/0 — all PASS.

## Perf gate — three independent runs, byte-identical
`test_perf.ino` (Ardens cycle-accurate, `captureserial=3000`):
```
run 1 (fxtest-headless): B pUs=6839 pHz=146 lHz=48 lTk=972 rMx=6008 rAv=5335 ram=478
run 2 (direct):          B pUs=6839 pHz=146 lHz=48 lTk=972 rMx=6008 rAv=5335 ram=478
run 3 (direct):          B pUs=6839 pHz=146 lHz=48 lTk=972 rMx=6008 rAv=5335 ram=478
```
Run-to-run variance **zero** on every field; `perf_test PASSED=5 FAILED=0` each time.

| gate | budget | result | verdict |
|---|---|---|---|
| render max fits 1/135 s | <= 7407 us | **6008 us** | PASS |
| plane rate | >= 135 Hz | **146 Hz** | PASS |
| logic tick fits one logic frame | <= 19230 us | **972 us** | PASS |
| logic rate | >= 45 Hz | **48 Hz** | PASS |
| free RAM (deepest SP) | >= 300 B | **478 B** | PASS |

## Independent profiler re-confirmation (headless Ardens, 3000 ms)
```
"$HOME/code/Ardens/build/Ardens.app/Contents/MacOS/Ardens" headless=3000 \
  display=ssd1306 fxport=d1 profiledump=build/profiler.txt \
  file=dist/monhun-ardu.ino.elf file=fxdata/fxdata.bin >/dev/null
```
Top rows (tab-separated):
```
cycles 27005017  cycles_with_sleep 48000574  cpu_active_pct 56.3
hotspots	count	pct	begin	end	name
 7161772	14.92	0x112a	0x115e	abg_detail::ArduboyG_Common<...>::paint(...)
 5101749	10.63	0x332a	0x6aa0	main
 2092942	 4.36	0x0ff2	0x112a	mh::blk(long, long, long, long, unsigned char) (.part.11)
 1281239	 2.67	0x1b72	0x1e74	SpritesU::drawPlusMaskFX(int, int, uint24, unsigned int)
  768842	 1.60	0x0fc6	0x0ff2	Arduboy2Base::drawPixel(int, int, unsigned char) (.part.1)
```
Arena/blk hotspot confirmed gone from the top: `mh::blk` 4.36% (vs 29.24%
pre-fix), no `__divmodhi4` anywhere in the report; total active cycles 27.0 M
(cpu_active 56.3%, vs 48.0 M / 100% pre-fix). `paint` (#1, 7.16 M) is the
unchanged ArduboyG plane-blit floor, outside the render scene. Matches kt7.3
evidence exactly.

## Bench flash / RAM
`test_perf` image: flash **29646 B (99%)** / 29696, globals **1912 B (74%)** /
2560, 648 B free for locals.

## Verdict
All perf budgets PASS with zero run-to-run variance; profiler confirms the
hotspot fix; host + device suites green. Gate closed.

---

# monhun-ardu-kt7.4 — Render: drop redundant black fills

## Bead
`monhun-ardu-kt7.4` (slice of epic `monhun-ardu-kt7`). ArduboyG's
`waitForNextPlane(BLACK)` already wipes the shared framebuffer to black during
each plane blit (`src/external/ArduboyG.h`, doDisplay/paint: clear flag + clear
color -> `st X,0`), so shade-0 "background" fills are wasted writes. Render-only
audit of `src/render.hpp`.

## Files
- changed `src/render.hpp`:
  - `drawHud()`: removed `blk(0, 0, SCREEN_W, HUD_H, 0)` (the `// strip
    background (black)` fill). It was not just redundant — `blk()` clamps the
    arena band to `y >= HUD_H`, so with `y=0, h=HUD_H` the call hit
    `y0 >= y1` and returned before `fillRect`: a pure call-overhead no-op. The
    HUD strip is painted black by the per-plane wipe that precedes every pass.
- changed `output.md`.

## Shade-0 audit (draw order)
Only three shade-0 draws existed; no direct `arduboy.fillRect(..., 0)`.
- `:531 blk(0,0,SCREEN_W,HUD_H,0)` — REDUNDANT, removed (see above).
- `:328 blk(shx-1, shy-7, 2, 14, 0)` — KEPT: in-frame eraser cutting the notch
  out of the shield block painted at `:327` in the same pass.
- `:335 blk(x+6, y+3, 4, 1, 0)` — KEPT: blink hole erased inside the player
  body painted earlier in the same pass.
No draw-order changes; no core/sim/fixture edits; `MAX_FX_DRAW` untouched.

## Profiler evidence (Ardens headless, 3000 ms)
```
Ardens headless=3000 display=ssd1306 fxport=d1 profiledump=build/profiler_after.txt \
  file=dist/monhun-ardu.ino.elf file=fxdata/fxdata.bin
```

BEFORE (`build/profiler_before.txt`):
```
cycles 27005017  cycles_with_sleep 48000574  cpu_active_pct 56.3
hotspots	count	pct	begin	end	name
 7161772	14.92	0x112a	0x115e	abg_detail::...ArduboyG_Common...::paint(...)
 5101749	10.63	0x332a	0x6aa0	main
 2092942	 4.36	0x0ff2	0x112a	mh::blk(long, long, long, long, unsigned char) (.part.11)
 1281239	 2.67	0x1b72	0x1e74	SpritesU::drawPlusMaskFX(int, int, uint24, unsigned int)
  768842	 1.60	0x0fc6	0x0ff2	Arduboy2Base::drawPixel(int, int, unsigned char) (.part.1)
```

AFTER (`build/profiler_after.txt`):
```
cycles 26952578  cycles_with_sleep 48000574  cpu_active_pct 56.2
hotspots	count	pct	begin	end	name
 7161772	14.92	0x112a	0x115e	abg_detail::...ArduboyG_Common...::paint(...)
 5100729	10.63	0x332a	0x6a84	main
 2038167	 4.25	0x0ff2	0x112a	mh::blk(long, long, long, long, unsigned char) (.part.11)
 1281313	 2.67	0x1b72	0x1e74	SpritesU::drawPlusMaskFX(int, int, uint24, unsigned int)
  768684	 1.60	0x0fc6	0x0ff2	Arduboy2Base::drawPixel(int, int, unsigned char) (.part.1)
```

Total active cycles **27005017 -> 26952578** (-52439, -0.19%); `mh::blk`
overhead **2092942 -> 2038167** (-54775). Because the removed call returned
before `fillRect` (arena clamp), the saving is the per-frame `blk` call/clamp
overhead, not a `fillRect` cycle — `paint` (the plane blit that performs the
black wipe) is unchanged at 7.16 M. Shipping flash **27698 -> 27670 B** (-28 B),
consistent with the dropped call and clamp path.

## Perf gate (`make fxtest-headless`, test_perf)
```
B pUs=6840 pHz=146 lHz=48 lTk=972 rMx=6000 rAv=5327 ram=478
perf_test PASSED=5 FAILED=0
```
| gate | budget | result | verdict |
|---|---|---|---|
| render max fits 1/135 s | <= 7407 us | **6000 us** | PASS |
| plane rate | >= 135 Hz | **146 Hz** | PASS |
| logic tick fits one logic frame | <= 19230 us | **972 us** | PASS |
| logic rate | >= 45 Hz | **48 Hz** | PASS |
| free RAM | >= 300 B | **478 B** | PASS |

## Test tails
`make test`: `Total Passed: 497 / Total Failed: 0`.
`make fxtest-headless`: assets 30/0, audio 14/0, boot 4/0, parity 660/0,
perf 5/0 — all PASS.
