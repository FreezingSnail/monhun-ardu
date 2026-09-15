# monhun-ardu-8ss — Device: HUD parity (bars, weapon, ammo, train stats)

## Files
- changed: `monhun-ardu.ino` — HUD drawing added to `render()` (render-only,
  read-only on `Game`). No core, mock, or test files touched.
  - `#include "src/external/Font4x6.h"` (PROGMEM/MCU-flash text).
  - `static Font4x6 hudFont` + `hudPut(x,c)` / `hudDigits(v)` / `hudNum(x,v,n)`:
    `printChar(c, x, -1)` — the 4x8 glyph paints rows `y+1..y+7`, so cursorY
    `-1` puts its 6 px cap in HUD rows 0..5. Advance 5 px (4 px + 1 px spacing).
  - `hudBar(x,y,w,h,num,den,shade)`: mock `bar()` (shade-1 back, inner fill
    `round((w-2)*ratio)`), used for HP / stamina / monster HP.
  - `drawHud(g)`: black strip, divider, player HP, stamina, weapon marker,
    mode marker, gun ammo/reload, then monster HP bar (hunt) or `T<total>D<dps>`
    (train). Called last in `render()`, untranslated (mock restores the camera
    before `drawHud`).
- changed: `output.md` (this file).

## Build (`rm -rf build && make build`)
```
Sketch uses 28944 bytes (97%) of program storage space. Maximum is 29696 bytes.
Global variables use 1910 bytes (74%) of dynamic memory, leaving 650 bytes for local variables. Maximum is 2560 bytes.
```
- Flash 28944 B < 29696 B (headroom **752 B**). RAM free **650 B** >= 300 B.

## HUD layout (device top strip, y 0..7; mock was the bottom 8 px)
The mock HUD is the bottom strip; the device reserves the top 8 px (loop bead),
so the strip is mirrored vertically. The divider sits at the arena edge (`y=7`)
and the content fills rows 0..6. Everything is untranslated by camera/shake.

| item | device (x, y) | mock |
|---|---|---|
| background | `blk(0,0,128,8,0)` + divider `y=7` shade1 | black fill + 1 px line |
| player HP | `hudBar(1,2,28,4, hp, hpMax, 3)` white | `bar(2,ARENA_H+2,40,4, …,3)` |
| stamina | `hudBar(29,2,16,4, stam, stamMax, 2)` light gray | `bar(46,ARENA_H+2,30,4, …,2)` |
| weapon marker | 3 chars at x=46: `SWD`/`FLA`/`GUN` | full `def.name` at x=80 |
| mode marker | 1 char at x=61: `H`/`T` | implied (pole vs beast) |
| gun ammo | `RLD` while reloading (reload bar `x=67,y=6,w<=12` shade2) else `B`/`S` + count at x=67 | `RLD`/`name[0]+count` at x=112 |
| hunt: monster HP | `hudBar(82,2,44,3, m.hp, m.hpMax, 3)` | `bar(W-52,2,48,3, …,3)` |
| train: stats | `T<total>D<dps>`, right-aligned to x=126 | `LAST n` / `DPS n` at top-right |

- Bars are white (HP) and light gray (stamina); they are separated by the
  shade-1 bar borders, so no two grays sit adjacent (pitfall).
- Single 8 px row vs mock's two rows (`LAST` at y=2, `DPS` at y=10) forced the
  train readout onto one line: `T`=total (`g.train.total`, capped 9999 display)
  and `D`=dps (`mh::trainDps`). `LAST` is already surfaced by the rising damage
  Number effect, so the HUD shows cumulative total + DPS instead.
- Weapon full name shrank to 3 chars and weapon/mode became single glyphs to fit
  bars + ammo + right readout in 128 px (bd design allows `BALL/B/B2`-style
  simplification). All labels are short and use inline char literals.
- Text renders white (Font4x6 sets bits on every plane), so mock's shade-2 DPS
  gray is drawn white; bars keep the gray levels.

## Notes / TODOs
- **Flash**: Font4x6 + the Arduboy2 `Sprites` glyph blitter cost ~3.4 KB
  (25566 -> 28944), leaving only 752 B. If a later bead (kt7.2 sprites, d54
  overlay) needs flash, the sanctioned fallback is to drop Font4x6 and draw
  digits/stats with the existing 3x5 `FONT_DIG` (`drawNumber`) and block-art
  icons for weapon/mode (~2.8 KB recovered; no letter labels).
- **kt7.2 (sprites)**: replace the block-art scene with the 4-shade sprite sheet
  + FX pipeline. HUD can move to baked sprite glyphs then; `FONT_DIG`, `SIN256`
  and Font4x6 can be dropped once sprite art is in.
- **d54 (debug overlay)**: wireframe hurt/hit boxes + `state` text live in the
  arena band (`ARENA_H-7` in the mock). Keep the HUD strip rows 0..7 reserved;
  draw debug strings in the arena, not over the HUD.
- **Other TODOs inherited from rze**: real decaying `Game::shake` (render
  currently derives a tick-based kick from `hitFlash`); pause/win/lose overlays
  not ported.
- Display caps only: `T` saturates at 9999, `D` at 999 (render-only clamp; the
  core values are unbounded).

## Tests
- `make test` (host, C++17): 497 passed / 0 failed (unchanged).
- `make fxtest-headless` (Ardens, device serial): `test_boot PASSED=4 FAILED=0`,
  final marker `P` -> PASS (Font4x6 is unused by the boot test, so it is not
  linked there).
