# monhun-ardu-e4a — perf: bake HUD marker strip (4 glyph blits -> 1)

Status: DONE. All verifications below pass. No commit/push (orchestrator commits).

## What changed

- `tools/gen-art.py` — new `HUD_WEAPONS=("SWD","FLA","GUN")`, `HUD_MODES=("H","T")`,
  `hud_defs()` authors six 16x8 strips (frame = weapon*2 + mode) from the same
  `GLYPHS` table / `text_blocks()` lane as the menu bake; added to `icon_defs()`
  so `art_dims::hud_frame_w/h/frames` are emitted. New `check_hud_identity()`
  (menu-oracle pattern) cross-checks every 4x8 cell of every strip against the
  authored `fxfontw` sheet; called in `main()` after `check_menu_identity()`.
- `src/render.hpp` — `drawHud()` marker: 4 `hudPut()` calls (12-char if/else +
  mode char) replaced with one `sprDraw(fxhud, 46, 1, FRAME(wf*2+mf))`; the old
  "anything but sword/flail reads GUN" mapping and mode `T`/`H` mapping are
  preserved. `hudPut()`/`hudNum()` stay (gun reload/shell + train readouts).
- Generated (from `make gen`): `images/blocks/fxhud_16x8.png`,
  `fxdata/blocks/Sprites.txt`, `fxdata/fxdata{,-data}.bin`, `fxdata/fxdata.h`,
  `src/fxdata.h`, `fxdata/manifest.json`, `src/generated/art_dims.hpp`.
  `fxdata/tables/equip.bin` + `src/generated/equip_meta.hpp` re-emitted because
  the equip blob bakes absolute sheet offsets (`SHEET_OFF_*`) that shift when a
  new blocks sheet is inserted; all `static_assert`s hold.

## Verification

### 1. gen determinism
`make gen` x2 then `make gen-check`:
```
fxdata_manifest: fxdata/manifest.json up to date (32 images, 40 inputs, 11 outputs)
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (52 generated artifacts unchanged)
```

### 2. host + tooling
- `make test`: `Total Passed: 3119  Total Failed: 0`
- `make test-tools`: `Ran 81 tests ... OK`

### 3. device suites (`make fxtest-headless`, full)
```
test_hud PASSED=17 FAILED=0
test_parity PASSED=660 FAILED=0
B pUs=6501 pHz=153 lHz=51 lTk=984 rMx=5388 rAv=5028 ram=418
perf_test PASSED=5 FAILED=0
test_player_art PASSED=111 FAILED=0
asset 254/0  audio 14/0  boot 4/0  combat 195/0  data 221/0  menu 59/0
```
`test_hud` 17/0 and `test_player_art` 111/0 UNCHANGED (no golden regen).

### 4. perf (deterministic; re-runs byte-identical)
Baseline measured on HEAD `src/render.hpp` + the new fxdata (same toolchain):
`rMx=5496 rAv=5136 pUs=6614` (exactly the bead's stated baseline).
Baked: `rMx=5388 rAv=5028 pUs=6501`.
Delta: rMx **-108 us**, rAv -108 us, pUs -113 us (~1.6%).

### 5. size
`make build` + `make size`:
```
Sketch uses 26928 bytes (90%) of program storage space. Maximum is 29696 bytes.
size: .text=26870 .data=58 .bss=1960
size: flash=26928/29696 (2768 free)  ram=2018/2560
```
Flash 27000 -> 26928 (**-72 B**). Cart: `FX_DATA_BYTES` 123917 -> 124495
(**+578 B**); `fxdata/fxdata.bin` 124160 -> 124672 (+512 B page-aligned).
`mhEquip` pinned at `0x000525` (unchanged); new sprite section `fxhud` at
`0x009DB1`, blocks sections after the raw_t tables, no 16-bit window issue.

## Pixel-identity evidence (gen-art identity check is live)

Negative test: temporarily baked the HUD strips in `DARK` instead of `WHITE`;
`make gen` exited 2 with
`gen-art: HUD IDENTITY FAIL: hud frame 0 (1,0) 'S': got (85,85,85,255) want (255,255,255,255)`
(and the rest of the strip). Reverted -> `make gen` + `make gen-check` PASS.
This proves `check_hud_identity()` actually compares every strip cell against
`fxfontw`, the same sheet the old `hudPut()`/`textPut()` blitted.

## Perf analysis (measured, not claimed)

The bake removes 3 of the 4 `drawPlusMaskFX` calls per HUD draw, but the saving
is only 108 us, not the ~3x156 = ~468-600 us the bead estimated. Each removed
call = one `FX::seekData` + two header reads (`SpritesU.hpp:894`) + a 4x8 blit.
The surviving 16-wide blit paints the same total glyph pixels as the four 4-wide
blits, so the only saved work is 3 seeks + 6 header bytes. 108 us / 3 ~= 36 us
per seek+header on this path; the ~156 us/blit figure appears to describe a
larger/cold sprite, not the 4x8 font tile. The win is real and deterministic
(rMx improved 5496 -> 5388 > the 5496 acceptance bar) but ~an order of magnitude
below the estimate.

## Deviations / notes

- No test files changed; no goldens regenerated.
- `equip_meta.hpp` / `equip.bin` moved because the new blocks sheet shifts baked
  absolute sheet offsets; this is expected generator output (gen-check
  deterministic), not a behavior change.
- Did not commit, stage, or push.
