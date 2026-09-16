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
