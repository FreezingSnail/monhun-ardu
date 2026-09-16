# monhun-ardu-42n.2 — Assets: author remaining code-mocked block graphics as PNG sheets

Status: **done** (all gates green: gen deterministic, build 27282 B / 1941 B RAM unchanged,
host tests 658/0, device fxtest all PASS, PNG↔mock pixel evidence below).

- Repo: `/Users/connorfranc/monhun-ardu`
- HEAD before/after: `f0595a5` (unchanged; no commits made — orchestrator owns the commit)
- Date: 2026-09-16
- Working tree: left dirty intentionally

## What was implemented (per bead DESIGN)

1. **`tools/fxdump.cpp`** (new): host dumper that includes `src/core/game.hpp` and prints the
   core dimensions gen-art.py needs as JSON — per-weapon attack `hw/hh/reach` (3 combo + special),
   monster `lunge`/`sweep` `hw/hh/reach`, monster hurt box 32x24, whirl orbit radii 20/14/24.
   No number is duplicated in Python; `tools/gen.sh` builds it with host g++ and writes
   `build/fxdump.json`.
2. **`tools/gen-art.py`** (extended): consumes `build/fxdump.json` and authors 12 new
   overlay/effect sheets pixel-exact to the current `blk()` shapes in `src/render.hpp`
   (which mirror `mock/game.js`): `fxslash_24x24` (4 frames: combo 1/2/3 + special),
   `fxripspecial_24x24`, `fxparry_24x16`, `fxchain_40x16` (3 combo reaches), `fxwhirl_8x4`
   (2x2 orbit dot + 4x4 ball), `fxdeflect_24x16`, `fxguard_12x16` (plate+notch, guard, shove),
   `fxreload_10x8`, `fxerase_4x16` (shade-0 eraser: mask=1/data=0), `fxtrail_4x4` (light+dark puff),
   `fxtelegraph_32x24` (lunge windup + sweep, light/dark box + core), `fxchip_8x8` (4x4 white).
   Existing block/font sheets are byte-identical to HEAD (verified). Every sheet passed an
   authoring self-check (sheet dims == derived frame size; each blk rect lands in the typed
   plane; every other pixel stays transparent) and a re-read-from-disk pixel comparison.
   `--dump` prints the ASCII pixel dump used as evidence.
3. **`src/generated/art_dims.hpp`** (new, generated): per-frame core dims (`sword_atk0_hw` …,
   `monster_sweep_reach`, `whirl_orbit_rx`…) plus sheet frame layout (`slash_frame_w`,
   `slash_frames`, `telegraph_lunge_x`…). Emitted by the same run that authors the PNGs, so
   the header and the sheets cannot drift apart.
4. **`fxdata/fxdata.txt` / `fxdata/fxdata.h` / `src/fxdata.h`**: 18 sprite symbols now
   (12 new: `fxdeflect`, `fxparry`, `fxchip`, `fxguard`, `fxtrail`, `fxwhirl`, `fxripspecial`,
   `fxslash`, `fxtelegraph`, `fxreload`, `fxerase`, `fxchain`). FX image 13040 → 19124 B
   (`fxdata.bin` 13056 → 19200 B), still far under 64 KB. `fxdata/tables/*` unchanged.
5. **`tools/gen.sh`**: added the fxdump build/dump step; deletes `fxdata/*/Sprites.txt`
   before regenerating (convert-sprite.py appends, never truncates — a stale append kept old
   symbols for renamed sheets, found and fixed during this bead).
6. **`tst/art_dims_test.hpp`** (new, wired into `tst/main.cpp`): 161 new host checks.
   - dims header == core accessors for all 3 weapons × (3 attacks + special) and both monster
     attacks (drift test: change hw/hh/reach in game.hpp and this fails until `make gen`).
   - slash frame layout packs the core box centred with the white core at +2 and the riposte
     rim at the special box + 2 px per side.
   - parses `fxdata/blocks/Sprites.txt` (plus-mask blob format: 2-byte header, then per frame,
     per shade pass, per page, per column a data/mask pair) and asserts the declared boxes and
     decorations have ink exactly where the core dims say: slash box is light at the core-dim
     rect, chain dots at `(reach*i)>>2`, telegraph box at `(32-hw)/2, (24-hh)/2` with the core at
     the centre, guard notch is mask-only, i-frame erase row is mask-only, trail/whirl puffs and
     balls on the right planes, plus frame counts for truncation.
7. **`tst/fxdatatest/asset_test.hpp`** (extended): device byte checks for all 12 new sheets —
   header w/h + pinned body bytes dumped from the flashed image and cross-checked against the
   PNGs. asset suite 30 → 234 checks. Notable hardening: `blobBytes` reads per-byte
   (`readPendingUInt8`/`readEnd`) instead of `readBytesEnd`, whose inline asm corrupted the
   caller's stack in this call pattern (found via device bisect; per-byte matches the render
   path anyway).

## Files changed/added

```
 M fxdata/blocks/Sprites.txt        (18 symbols, 18908 -> 19124 B blob text region)
 M fxdata/fxdata-data.bin           (13040 -> 19124 B)
 M fxdata/fxdata.bin                (13056 -> 19200 B)
 M fxdata/fxdata.h                  (+12 sheet symbols; offsets shifted)
 M src/fxdata.h                     (generated copy)
 M tools/gen-art.py                 (fxdump JSON input, 12 new sheets, self-checks, dims header, dump)
 M tools/gen.sh                     (+fxdump step, Sprites.txt cleanup)
 M tst/main.cpp                     (+ artdimstest::ArtDimsSuite)
 M tst/fxdatatest/asset_test.hpp    (+204 checks; per-byte blob reader)
?? tools/fxdump.cpp                 (new host dumper)
?? src/generated/art_dims.hpp       (new generated dims header)
?? tst/art_dims_test.hpp            (new host dims-drift + sprite-contract suite)
?? images/blocks/fxslash_24x24.png      ?? images/blocks/fxripspecial_24x24.png
?? images/blocks/fxparry_24x16.png      ?? images/blocks/fxchain_40x16.png
?? images/blocks/fxwhirl_8x4.png        ?? images/blocks/fxdeflect_24x16.png
?? images/blocks/fxguard_12x16.png      ?? images/blocks/fxreload_10x8.png
?? images/blocks/fxerase_4x16.png       ?? images/blocks/fxtrail_4x4.png
?? images/blocks/fxtelegraph_32x24.png  ?? images/blocks/fxchip_8x8.png
```

No files left in `images/` outside the manifest-visible naming convention; no orphans (the
generator also removes stale `fx*_WxH.png` it no longer authors).

## Verification evidence

### 1. `make gen` determinism (three runs, byte-compare)

```
run N:  gen-art: wrote 18 block sheets (12 overlay/effect icons) + 2 font sheets
        gen-art: pixel check OK (20 sheets, disk-exact)
        Saving 19124 bytes FX data to .../fxdata-data.bin
        gen.sh: FX data + src/fxdata.h regenerated
```

Two full snapshot sets compared with `diff -r`: **identical** (GEN_DETERMINISTIC). Stable md5
(unchanged across runs):

```
f45f525bea7120e6956b4574023dfbac  fxdata/fxdata.bin
1703bde8c4067920f72e2b7c7d4f0be7  fxdata/blocks/Sprites.txt
ff8064cc5af14c8db50d75cf5a598565  src/fxdata.h
80b00ef48c5cd490a1bd50bf12fe2a7e  src/generated/art_dims.hpp
```

All 6 existing block sheets + 2 font sheets are pixel-identical to HEAD (Pillow compare), so
their blobs stayed byte-stable except for the shifted offsets.

### 2. `make build` — flash/RAM vs baseline

Baseline (HEAD f0595a5): flash **27282 B (91%)**, RAM **1941 B (75%)**. After:

```
arduino-cli compile --fqbn "arduboy-homemade:avr:arduboy-fx" --optimize-for-debug --output-dir dist
Sketch uses 27282 bytes (91%) of program storage space. Maximum is 29696 bytes.
Global variables use 1941 bytes (75%) of dynamic memory, leaving 619 bytes for local variables. Maximum is 2560 bytes.
```

Delta: **0 B flash, 0 B RAM** — expected: the sheets are only referenced by the new
`fxdata.h` constants, and the render bead (42n.3) is what starts drawing them.

### 3. `make test` — 497 -> 658 passed, 0 failed

```
Total Passed: 658
Total Failed: 0
```

`art dims` suite alone: 161 checks (dims header == core accessors, slash layout, blob-contract
pixel presence for all 12 sheets). Tuning any hw/hh/reach in `src/core/game.hpp` fails this
suite until `make gen` regenerates `src/generated/art_dims.hpp` and the PNGs.

### 4. `make fxtest-headless` (Ardens present, exit 0)

```
=== test_assets ===
asset_test PASSED=234 FAILED=0
P
test_assets: PASS
=== test_audio ===
test_audio PASSED=14 FAILED=0
P
=== test_boot ===
test_boot PASSED=4 FAILED=0
P
=== test_data ===
data_test PASSED=194 FAILED=0
P
=== test_parity ===
parity_test PASSED=660 FAILED=0
P
=== test_perf ===
B pUs=6379 pHz=156 lHz=52 lTk=988 rMx=3996 rAv=3868 ram=489
perf_test PASSED=5 FAILED=0
P
test_perf: PASS
```

Perf gates unchanged from the 42n.1 baseline (plane 156 Hz ≥ 135, logic 52 Hz ≥ 45, render max
3996 µs ≤ 7407, free RAM 489 B ≥ 300). Parity 660/0 — no sim change.

### 5. PNG sheets are pixel-exact to the mock/game.js shapes

The shapes in `gen-art.py` are the literal `blk()` rects from `src/render.hpp` with the core
dims substituted from fxdump (e.g. slash box = `hw x hh` centred in the frame, core at +2;
telegraph box = `(32-hw)/2, (24-hh)/2`; chain dots at `(reach*i)>>2`). `make gen` fails
loudly if any authored pixel deviates from that blk set. The device suite then pins the
flashed bytes against the PNG model. Independent host re-check of every PNG → Sprites.txt
blob (a from-scratch implementation of the convert-sprite layout): **all 18 sheets match**.

ASCII pixel dump (from `gen-art.py --dump`) proving the mock shapes; `l`=light, `W`=white,
`g`=dark, `K`=black eraser, `.`=transparent:

```
slash  frame 24x24  frames 4  size 96x24          telegraph  frame 32x24  frames 2
  ......llllllllllll............llllllllllll...    ....gggggggggggggggggggggggg....
  ......llWWWWllllll............llWWWWllllll...    ....gggggggggggllgggggggggggg....  <- lunge:
  ......llllllllllll............llllllllllll...    ....gggggggggggggggggggggggg....     24x22 dark box
  (frame 0: 12x10 light box, white 4x4 core)       ....llllllllllllllWWWWllllllll....  <- sweep:
                                                   ....llllllllllllllWWWWllllllll....     32x24 light box,
parry  frame 24x16  frames 1                       (right frame)                         white 4x4 core
  ...........WW...........
  .........llllll.........   <- white 2x14 blade + light 6x2 cap
  ...........WW...........

guard  frame 12x16  frames 3        chain  frame 40x16  frames 3        whirl  frame 8x4  frames 2
  .llllKKllll..WWWWWWWWWW..          ....................g....g....g...  ll......WWWW....
  .llllKKllll..WWWWWWWWWW..          (dots at cx+4/8/12 for reach 19)     ll......WWWW....
  (plate+black notch, guard, shove)                                     (2x2 dot, 4x4 ball)

erase  frame 4x16  frames 1   chip 8x8        reload 10x8      trail 4x4        deflect 24x16
  KKKK                        WWWW....        ..........       llgg             ..l..................l..
  ....                        WWWW....        llllllllll       (light, dark)    (two 1x12 light bars)
  ....  <- shade-0 eraser     ........
```

Byte-level evidence from the device (excerpt of the pinned checks):

```
FAIL-free run: asset_test PASSED=234 FAILED=0
e.g. fxslash f0 box cols 6..15 rows 7..14 pass0 = {128,128,128,128,128,128,128,128,...}
     fxguard f0 plate  = {0,0,254,254,254,254,254,254,254,254}   (light plane 0+1 lift)
     fxguard f0 notch  = {127,127,127,127,0,127,0,127}           (black eraser at row 7)
     fxerase row 0     = {0,1,0,1,0,1,0,1}                       (mask=1, data=0)
     fxtelegraph f0    = box starts at col 1 (lunge hw 24 => x=4) with the light core
```

## Notes / open items

- The render bead (42n.3) consumes `src/generated/art_dims.hpp` for frame constants and
  anchors; anchor conventions are documented next to each sheet in `gen-art.py`
  (frame origin == hit-box top-left / player centre / etc.). `fxdata.h` supplies the
  `*_WIDTH/*_HEIGHT` symbol constants.
- `blobBytes` in the device test deliberately avoids `readBytesEnd` (its AVR inline asm
  clobbered `z`/stack in this pattern); the render path reads per-byte through the same
  `mhFxRead*` helpers, so this matches shipping behaviour.
- `fxdata/blocks/Sprites.txt` is regenerated from scratch each `make gen` (gen.sh removes it
  first) because convert-sprite.py appends rather than truncates.
- No commit made; tree left dirty as instructed.
