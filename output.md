# monhun-ardu-rze — Device: block-art render parity (mock blocks)

## Files
- changed: `monhun-ardu.ino` — full block-art `render()` ported from the
  `mock/game.js` render section, plus render-only helpers/tables above it.
  - `blk(x,y,w,h,shade)` is the single draw entry point: clips every rect to
    `[0,SCREEN_W) x [HUD_H,SCREEN_H)` and calls `arduboy.fillRect(...,shade)`.
    shade 0 clears pixels (mock black bodies carve holes in what is under them).
  - `drawArena` / `drawPole` / `drawMonster` / `drawPlayer` / `drawProjectiles`
    / `drawEffects` / `drawNumber`, matching mock draw order and shapes.
  - PROGMEM tables: `SIN256[256]` (Q4 sine, `cos(a)=SIN256[(a+64)&255]`),
    `FONT_DIG[10][5]` (3x5 digits), `RING6[6]` (whirl ring offsets).
  - `rndPx(v,sub)` reproduces mock `Math.round()` on the fixed-point position.
  - Camera clamp + tick-based shake + HUD-at-top offset; loop FX bracket and
    `run()` unchanged. `render()` reads `Game` only (no mutation, no static
    render state) and never branches on `currentPlane`.
- changed: `output.md` (this file).
- `mock/`, `src/`, `tst/` untouched; no core logic or numbers changed.

## Build (`rm -rf build && make build`)
```
Sketch uses 25566 bytes (86%) of program storage space. Maximum is 29696 bytes.
Global variables use 1888 bytes (73%) of dynamic memory, leaving 672 bytes for local variables. Maximum is 2560 bytes.
```
- Flash 25566 B < 29696 B (headroom 4130 B).
- Global RAM 1888 B, free **672 B** >= 300 B.

## Shade mapping (mock SHADES -> L4_Triplane)
Mock has exactly 4 grays, mapped 1:1 to the ArduboyG triplane levels:

| mock index | mock color | L4 Triplane level | planes set |
|---|---|---|---|
| 0 | `#000000` BLACK | 0 | none |
| 1 | `#4d4d4d` DARK_GRAY | 1 | plane 0 |
| 2 | `#b3b3b3` LIGHT_GRAY | 2 | planes 0+1 |
| 3 | `#ffffff` WHITE | 3 | planes 0+1+2 |

`ArduboyG::planeColor(plane, shade)` (`shade > plane`) resolves the level, so the
same shapes/colors are drawn on all three plane passes and composite identically.

## Mock draw-order deviations / notes
- **HUD position**: mock HUD is the bottom 8 px (`ARENA_H..H-1`); device reserves
  it at the **top** (`y 0..HUD_H-1`, set by the loop bead). World `y=0` therefore
  maps to screen `y=HUD_H`; the arena is shifted down 8 px and `blk` clips at
  `HUD_H`. No HUD pixels drawn this bead (bead 8ss).
- **Shake**: core `Game` has no `shake` field yet and `freeze` is not gated or
  decayed (deferred). The render derives a tick-based int offset amplitude from
  the decaying `monster.hitFlash` / `pole.hitFlash` (a ~4-tick kick after a hit)
  instead of mock `g.shake`. Player-hurt shake is not represented. Angles
  1.7/2.3 rad -> 69/94 steps in the 256-step table. TODO: real `Game::shake`.
- **Trig**: mock uses continuous `Math.sin/cos`; device uses a 256-step Q4 sine
  table with integer rate steps (0.35->14, 0.55->22, 0.30->12 units/tick). Only
  affects the spinning whirl ring / stun dots and shake, sub-pixel cosmetic.
- **Reach scaling**: mock `a.reach*0.6` (sword) / `*0.5` (flail) are integer
  (`*6/10`, `/2`) here; <=1 px vs mock.
- **Positions**: mock rounds floats; device rounds the fp body via `rndPx`.
  Projectiles use the pixel `pr.x/pr.y` directly (mock `pr.x>>4`).
- **Damage numbers**: mock draws full text; device renders the digit-only 3x5
  subset (damage values are integers). Position/rise/color match the mock.
- **Arena background**: mock's explicit black screen fill is omitted; the L4
  plane clear already leaves the buffer black before each pass.
- No wireframe debug overlay (bead d54), no pause/win/lose overlays, no HUD.

## TODOs
- **8ss (HUD)**: region is screen `y 0..HUD_H-1`; `blk` clips world draws there.
  Draw bars / weapon label / ammo / train stats with shade 0..3, untranslated by
  camera or shake (mock calls `ctx.restore()` before `drawHud`).
- **kt7.2 (sprites)**: replace `fillRect` block art with the 4-shade sprite sheet
  + FX-gen pipeline; the sine/digit tables here can be dropped once sprite art
  and baked damage-number glyphs exist.
- **hitstop/shake bead**: add a real decaying `Game::shake` (and freeze gating)
  and delete the render-derived shake from `render()`.

## Tests
- `make test` (host, C++17): 497 passed / 0 failed (unchanged).
- `make fxtest-headless` (Ardens, device serial): `test_boot PASSED=4 FAILED=0`,
  final marker `P` -> PASS.
