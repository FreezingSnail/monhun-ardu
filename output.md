# monhun-ardu-kt7.2 — FX sprite + font assets, drop vendored font

## Bead
`monhun-ardu-kt7.2` (slice of epic `monhun-ardu-kt7`). ACCEPTANCE AMENDMENT:
**all assets live on the FX chip** — 4-shade block sprites *and* fonts/text.

## Files
- added `tools/gen-art.py` — deterministic author of the 4-shade PNG sheets from
  the mock shapes (`mock/game.js` drawPlayer/drawMonster/drawPole/
  drawProjectiles/drawEffects) and the mock `FONT` table. Palette maps 1:1 to the
  L4 triplane levels (transparent=mask 0, black=0, dark=plane0, light=planes0+1,
  white=planes0+1+2).
- added `images/blocks/*.png` — `fxplayer_16x16` (2 frames), `fxmonster_32x24`
  (8: idle/recover/flash/dead x east/west), `fxpole_20x40` (2: normal/flash),
  `fxball_7x8`, `fxscatter_4x8`, `fxspark_4x4` (2).
- added `images/fonts/{fxfontw,fxfontg}_4x8.png` — 128 ASCII-ordered 4x8 glyph
  tiles (white / light-gray) from the mock FONT.
- added `fxdata/blocks/Sprites.txt`, `fxdata/fonts/Sprites.txt` — generated
  plus-mask triplane blobs (2-byte `[w,h]` header, then per shade plane:
  interleaved data+mask).
- changed `tools/convert-sprite.py` — restored the 2-byte `[sw,sh]` header
  (matches `PowerRogue/tools/convert-sprite.py`; `SpritesU::drawPlusMaskFX`
  reads exactly those two bytes then draws from `image + 2`).
- changed `tools/gen.sh` — runs gen-art.py, converts `images/{blocks,fonts}` with
  `convert-sprite.py -s 4`, packs `fxdata.txt`, and copies the header to `src/`.
- changed `fxdata/fxdata.txt` — includes the two Sprites.txt files; the unused
  `fontTrimmed` (ArduFontTrimmed) registration is dropped.
- changed `src/fxdata.h`, `fxdata/fxdata.h` — generated header (offsets below).
- changed `monhun-ardu.ino` — sprite/font draws (see render notes).
- deleted `src/external/Font4x6.{h,cpp}` — no remaining reference (host tests
  never included it); recovers the vendored-font flash.
- added `tst/fxdatatest/test_assets.ino`, `asset_test.hpp` — device asset test.
- changed `output.md` (this file).

## gen.sh output
```
gen-art: wrote images/blocks (6 sheets) and images/fonts (2 sheets)
FX data build tool version 1.15 by Mr.Blinky May 2021 - Jan 2023
Using Python version 3.14.5
Building FX data using /Users/connorfranc/monhun-ardu/fxdata/fxdata.txt
Including file /Users/connorfranc/monhun-ardu/fxdata/blocks/Sprites.txt
Including file /Users/connorfranc/monhun-ardu/fxdata/fonts/Sprites.txt
Saving FX data header file /Users/connorfranc/monhun-ardu/fxdata/fxdata.h
Saving 12466 bytes FX data to /Users/connorfranc/monhun-ardu/fxdata/fxdata-data.bin
Saving FX development data to /Users/connorfranc/monhun-ardu/fxdata/fxdata.bin
gen.sh: FX data + src/fxdata.h regenerated
```
`convert-sprite.py` prints a Pillow 12 `getdata` DeprecationWarning; warnings do
not fail the `set -e` script and the output is unaffected.

## Generated symbols (`src/fxdata.h`, `FX_DATA_BYTES = 12466`)
```
fxmonster 0x000000   fxball    0x0016E6
fxpole    0x001202   fxplayer  0x001712
fxspark   0x0016B4   fxscatter 0x001894
fxfontg   0x0018AE   fxfontw   0x0024B0
```
Raw `uint8_t` arrays only carry an offset (fxdata-build emits Width/Height/Frames
only for `image_t`); frame sizes are compile-time constants in the sketch and the
blobs self-describe via their 2-byte header, so `drawPlusMaskFX` reads the true
w/h from FX. Sizes: player 16x16 x2, monster 32x24 x8, pole 20x40 x2 (20x36 art),
ball 7x8, scatter 4x8 (4x4 art), spark 4x4 x2, font 4x8 x128.

## Flash / RAM (`rm -rf build && make build`)
```
before (6b2bf5c): Sketch uses 28944 bytes (97%)   Global variables 1910 bytes (650 free)
after:            Sketch uses 25934 bytes (87%)   Global variables 1884 bytes (676 free)
```
- Flash headroom **3762 B (3.67 KB) >= 3.5 KB**; recovered **3010 B >= 3 KB**.
- `Font4x6` (~3.4 KB) is gone. The only remaining sizeable flash tables are the
  core `WEAPON_DEFS` and the render `SIN256`; no glyph/bitmap array lives in MCU
  flash or RAM (all sprites + fonts are FX-side).
- Small extra recovery: dropped `arduboy.initRandomSeed()` — the core is fully
  deterministic and never calls `random()`, so seeding only pulled the AVR
  `random`/`random_r` code into flash (~310 B). No gameplay change.

## Render notes
- `sprDraw()` culls fully off-screen sprites, then `SpritesU::drawPlusMaskFX(x, y,
  img, FRAME(i))`; `FRAME(i) = i*3 + currentPlane()` selects the plane data, so the
  three render passes composite the 4 shades exactly like the old `fillRect`.
- Sprite substitutions: pole (whole post/bands/head/eye/base), monster
  (feet/body/head/eyes + dead heap, per state and facing), player shadow+body
  (normal / dodge), ball + scatter, effect spark. `drawHud()` still runs last and
  its black strip erases any sprite overhang into rows 0..7, so the HUD stays
  clean despite sprites not being clipped to the arena band.
- Text: `textPut()`/`hudPut()` draw ASCII-indexed 4x8 FX glyphs, advance 4 px
  (mock `drawText` scale 1). HUD uses the white sheet; rising damage numbers pick
  white (crit) vs light-gray (normal) to match the mock.
- Still procedural (`fillRect`), deliberately: arena dots/border, HUD bars +
  reload bar, sword arc/parry, flail chain, whirl ring, gun shield, deflect and
  i-frame marks, stun sparkles, and the monster windup/attack telegraph boxes.
  These are dynamic (aim/reach/rotation) and/or sub-4 px primitives, not "block
  art" assets; the design's enumerated sprites (player/monster/pole/ball/scatter/
  spark) are all FX-side.
- Deviation to note: the mock's effect spark expands 4 one-pixel dots with radius
  `r`; a fixed FX sprite cannot animate scale, so the spark is drawn as a fixed
  4x4 sprite (light/white). TODO left in code.
- Pole art: the mock base overhangs 2 px each side (`x-2, w+4`); a 20 px-wide
  sprite cannot, so the base is drawn within the 20 px column. Pole height is
  padded 20x36 -> 20x40 (converter requires a multiple of 8); rows 36..39 are
  transparent. Ball/scatter padded 7x6 -> 7x8, 4x4 -> 4x8 likewise, with the art
  in the top rows so the draw offset is unchanged.

## Tests
- `make test` (host C++17): **497 passed / 0 failed**.
- `rm -rf build && make build`: flash 25934 B, RAM 1884 B (676 free).
- `make fxtest-headless` (Ardens device serial):
  - `test_assets PASSED=30 FAILED=0` -> final `P` PASS. Reads the `fxscatter`
    blob (16 bytes run + header) and the `fxfontw` header/glyph-0 bytes from FX
    inside the `enableOLED / waitForNextPlane / disableOLED` bracket the render
    loop uses.
  - `test_boot PASSED=4 FAILED=0` -> `P` PASS (unchanged).

## TODO / follow-ups
- Animate the effect spark radius (needs either 3 same-size growth frames or a
  procedural fallback drawn behind the sprite).
- Player per-weapon/stance/attack-phase silhouettes are approximated by the
  body sheet; weapon overlays stay procedural because reach/aim exceed a 16x16
  frame. If a later bead wants baked weapon frames, they need per-direction art.
- `text2bmp.py` was evaluated but not used: it emits opaque RGB (no plus-mask
  alpha) and indexes tiles by raw `ord(c)` requiring a 128-tile ASCII source;
  the glyph sheets are authored directly from the mock FONT instead, through the
  same `convert-sprite.py -> fxdata` pipeline.
- `fxdata/*.bin` are force-added (repo `.gitignore` ignores `*.bin`) so the
  device FX image and `make fxtest-headless` work from a clean clone without
  running `make gen` first.
