# monhun-ardu-42n.3 — Render: draw remaining overlays/effects from FX sheets; delete procedural shape code

Status: **done (review round 2)** — all five review drifts fixed, assets regenerated,
tests extended, full verification re-run. `make build` flash **26644 B** (baseline
27282, **-638 B**), RAM 1941 B unchanged; host tests **702/0** (658 + 44 new dims
checks); device suites all PASS (boot 4/0, assets **254/0**, audio 14/0, parity 660/0,
data 194/0, perf 5/0); perf B line `rMx=4736` (budget 7407), `pHz=156`, `lHz=52`,
`ram=493`; `make gen` deterministic across two runs; Ardens profiler re-recorded.
No commits — tree left dirty for the orchestrator.

- Repo: `/Users/connorfranc/monhun-ardu`
- HEAD: `9ed4285` (unchanged)
- Date: 2026-09-16

## Review fixes

1. **Sword branch attacks now have exact frames.** `tools/fxdump.cpp` dumps each
   weapon's `branches[]` (`id`/`hw`/`hh`/`reach`) from the core branch attacks;
   `gen-art.py` builds the fxslash frame list from the three main attacks + special +
   attack branches (id != ATK_NONE), deduped in order → **32x32 sheet, 5 frames**:
   `12x10 combo, 18x14 combo, 20x16 special, 14x12 step-slash, 28x26 spin-cut`. Box is
   centred (frame local 16,16 == hit-box centre) and the 4x4 white core is baked at the
   box centre (always (14,14)); render matches the live `hw/hh` **exactly** against the
   dumped dims (no size-class fallback) and anchors at `hx-16, hy-16`.
2. **Telegraph state shades restored.** fxtelegraph is now 4 frames — lunge windup,
   lunge attack, sweep windup, sweep attack — selected by `m.state` (MS_WINDUP vs
   MS_ATTACK) and `monsterAttackKind` (MK_LUNGE vs MK_SWEEP). Windup = shade-1 box with
   the 2x2 shade-2 core; attack = shade-2 box with the 4x4 shade-3 core; anchor stays
   box centre `(ax-16, ay-12)`.
3. **Chain dots are 1x1 light again.** fxwhirl gained a 1x1 LIGHT frame (frame 2, also
   used for the flail's idle hand dot); chain/throw dots draw it at the exact
   `rr=(reach*i)>>2` position. `fxchain` was removed end-to-end: gen-art authoring,
   `images/blocks/fxchain_40x16.png`, the Sprites.txt symbol, `fxdata.h`, the host dims
   test and the device asset test. No dead FX data remains (FX image 19124 → 21090 B
   net of all five fixes).
4. **Chip split into two frames.** fxchip: frame 0 = 3x3 white idle/aim chip, frame 1 =
   4x4 white ball. Render: sword idle + flail idle ball use frame 0 at the mock top-left;
   the flail attack/throw ball uses frame 1 at `tip-2`.
5. **Player stun sparkle is white.** fxwhirl frame 3 = 2x2 WHITE (player stun); frame 0
   stays the 2x2 LIGHT dot (whirl orbit + monster stun).

## Files changed/added

```
 M fxdata/blocks/Sprites.txt        (fxslash 32x32/5, fxtelegraph 4, fxwhirl 4, fxchip 2, fxchain gone)
 M fxdata/fxdata-data.bin           (19124 -> 21090 B)
 M fxdata/fxdata.bin                (19200 -> 21248 B)
 M fxdata/fxdata.h                  (symbols/offsets regenerated)
 M src/fxdata.h                     (generated copy)
 M src/generated/art_dims.hpp       (branch dims, slash 32x32/core, 4-frame telegraph, whirl/chip frames)
 M src/render.hpp                   (exact slash frames, state+kind telegraph, 1x1 chain dot, chip/whirl frames)
 M tools/fxdump.cpp                 (+ branch id/hw/hh/reach JSON)
 M tools/gen-art.py                 (branch frames, 4-frame telegraph, whirl/chip frames, fxchain removed)
 M tst/art_dims_test.hpp            (+branch dims, 5-frame slash, 4-frame telegraph, whirl/chip/stun)
 M tst/fxdatatest/asset_test.hpp    (+20 device pins, fxchain removed, new frame offsets)
 D images/blocks/fxchain_40x16.png  (removed; unused)
 D images/blocks/fxslash_24x24.png  (replaced)
?? images/blocks/fxslash_32x32.png  (new)
 M images/blocks/fxchip_8x8.png     M images/blocks/fxtelegraph_32x24.png
 M images/blocks/fxwhirl_8x4.png    M output.md
```

No core/sim file touched; parity fixtures byte-identical.

## Verification evidence

### 1. `make gen` determinism (two full runs, byte-compare)

```
gen-art: wrote 17 block sheets (11 overlay/effect icons) + 2 font sheets
gen-art: pixel check OK (19 sheets, disk-exact)
Saving 21090 bytes FX data to .../fxdata-data.bin
gen.sh: FX data + src/fxdata.h regenerated
```

Second run compared byte-for-byte (fx bins, headers, Sprites.txt, every PNG in
images/blocks + images/fonts): **GEN_DETERMINISTIC**. Stable md5s:

```
fe8fb7c181a055965ecd6b020d73c865  fxdata/fxdata.bin
4455d2cf9eb3c70567d3325da5a72d3d  fxdata/fxdata-data.bin
205df177038ec3418caaee59a986201f  src/fxdata.h
d3272fa0ddc841ab440bee089048acb1  src/generated/art_dims.hpp
453465604de6f805f62ecc1319003de9  fxdata/blocks/Sprites.txt
```

### 2. `make build` — flash/RAM vs baseline (27282 B / 1941 B)

```
Sketch uses 26644 bytes (89%) of program storage space. Maximum is 29696 bytes.
Global variables use 1941 bytes (75%) of dynamic memory, leaving 619 bytes for local variables. Maximum is 2560 bytes.
```

Delta: **-638 B flash**, 0 B RAM. (v1 of this bead was 26542; the exact-frame matching
and branch art cost +102 B, still far inside budget.) `test_perf` sketch: 29232 →
**28680 B** (96%).

### 3. `make test` — 702 passed / 0 failed (658 + 44 new dims checks)

```
========== Total Counts ==========
Total Passed: 702
Total Failed: 0
```

New coverage: `sword_branch0/1` dims drift checks; 5-frame slash layout (box centred,
core centre == box centre == frame centre, frame order pinned); 4-frame telegraph
(windup/attack shade + core for both attacks); whirl chain dot (1x1, plane-2 eraser) and
white stun frame; chip 3x3/4x4 frames. fxchain checks removed.

### 4. `make fxtest-headless` (Ardens present, exit 0)

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
data_test PASSED=194 FAILED=0
P
test_data: PASS
=== test_parity ===
parity_test PASSED=660 FAILED=0
P
test_parity: PASS
=== test_perf ===
B pUs=6388 pHz=156 lHz=52 lTk=988 rMx=4736 rAv=4573 ram=493
perf_test PASSED=5 FAILED=0
P
test_perf: PASS
```

Baselines rMx=3996 / pHz=156 / lHz=52 / ram=489 → after: **rMx=4736** (+740 µs for the
sprite draws, budget 7407), rates unchanged, ram=493. All gates PASS.

### 5. Ardens headless profiler (`profiledump`, 3000 ms)

BEFORE (baseline 9ed4285):
```
cycles 25423326  cpu_active_pct 53.0
7161772 14.92  abg_detail::...::paint(...)
4739633  9.87  main
1717933  3.58  mh::blk(long, long, long, long, unsigned char)
1254134  2.61  SpritesU::drawPlusMaskFX(int, int, uint24, unsigned int)
 505582  1.05  FX::readEnd()
```

AFTER (final):
```
cycles 25856784  cpu_active_pct 53.9
7161772 14.92  abg_detail::...::paint(...)
4752287  9.90  main
1755846  3.66  SpritesU::drawPlusMaskFX(int, int, uint24, unsigned int)
1557546  3.24  mh::blk(long, long, long, long, unsigned char)
 547556  1.14  FX::readEnd()
 186434  0.39  mh::hudBar(...) (.constprop.19)
 184880  0.39  mh::drawPlayer(...) (.constprop.37)
```

`mh::blk` 1.72 M → 1.56 M cycles (the residue is the HUD bars + arena border that stay
procedural); `drawPlusMaskFX` 1.25 M → 1.76 M (all overlay shapes are now sprites);
total active 25.42 M → 25.86 M (+1.7%). v1 dump kept at `build/profiler_42n3_v1.txt`,
final at `build/profiler_42n3_after.txt`, baseline at `build/profiler_42n3_before.txt`.

### 6. grep evidence — swapped call sites gone, only primitives remain

```
$ for f in drawPlayer drawMonster drawProjectiles drawEffects; do ... blk count ...
drawPlayer: blk=0 sprDraw=17
drawMonster: blk=0 sprDraw=3
drawProjectiles: blk=0 sprDraw=5
drawEffects: blk=0 sprDraw=1
```

Remaining `blk()` sites in `src/render.hpp`: the helper definition, `drawArena()` world
border (4), `drawDebug` wire (DEBUG_HURTBOXES only), `hudBar` (2), the HUD divider and
the HUD gun reload bar. fxchain references are gone from every file (only
`whirl_chain_frame` remains, the 1x1 chain dot).

### 7. Exact render anchors (mock/game.js shapes)

| shape | frame | anchor |
|---|---|---|
| sword combo box 12x10 / 18x14 / special 20x16 / step-slash 14x12 / spin-cut 28x26 | `fxslash` 0/1/2/3/4, exact hw/hh match | frame local (16,16) → `hx-16, hy-16` |
| riposte rim (special + 2 px/side) | `fxripspecial` f0 | `hx-hw/2-2, hy-hh/2-2` |
| parry blade | `fxparry` f0 | `cx-12, cy-12` |
| sword idle 3x3 white | `fxchip` f0 | computed top-left - 1 |
| flail chain/throw dots (1x1 light, any reach/facing) | `fxwhirl` f2 | exact `(cx+fx*rr>>4, cy+fy*rr>>4)` |
| flail idle hand dot 1x1 | `fxwhirl` f2 | exact computed position |
| flail attack/throw ball 4x4 white | `fxchip` f1 | tip - 2 |
| flail idle ball 3x3 white | `fxchip` f0 | computed - 1 |
| whirl ring 2x2 light + 4x4 white ball | `fxwhirl` f0/f1 | exact trig positions |
| monster telegraph (windup dark / attack light, per kind) | `fxtelegraph` f0/f1/f2/f3 | `ax-16, ay-12` |
| monster stun 2x2 light / player stun 2x2 white | `fxwhirl` f0/f3 | exact trig positions |
| i-frame erase 4x1 shade 0 | `fxerase` f0 | `x+6, y+3` |

Erase frame byte evidence (all three passes carry `(data,mask)=(0,1)` on row 0, so the
pixels are cleared on every plane; device asset test pins the row-0 pair):

```
fxerase 4x16: pass0/1/2 page0 = (0,1)(0,1)(0,1)(0,1) 0,0 0,0 0,0 0,0 ; rest transparent
```

### 8. Notes / scope

- FX cart grew 19124 → 21090 B (fxslash 32x32/5 frames + telegraph 4 frames + whirl/chip
  extras, less the removed fxchain blob) — still far under the 64 KB cart.
- `MAX_FX_DRAW`, `sprDraw` cull bounds (32x40 max used sheet = fxpole), camera/shake,
  reach/trig math and parity fixtures untouched.
- No Python/perl/ruby test harness; all tests are C++ in `tst/` (host + device).
- One pin offset slip during bring-up (fxslash f0 page-1 col-10 body index 276 → 84, the
  page stride is `w*2=64`) was caught by the device asset suite and fixed before final
  verification.
