# monhun-ardu-d54 — Device: wireframe debug overlay (hurt/hit)

## Bead
`monhun-ardu-d54` (slice of epic `monhun-ardu-kt7`). 1-bit wire boxes at the
same int rects the sim uses, compile-time gated so the release build excludes
them entirely.

## Files
- changed `monhun-ardu.ino` — `DEBUG_HURTBOXES` gate (default 0), 1-bit
  wireframe overlay (`wireSolid` / `wireDot` / `drawDebug`), A+B hold toggle,
  `drawDebug()` call in `render()` after effects and before the HUD.
- changed `output.md` (this file).

No core (`src/core/*`) changes; overlay/render only. No float/double, no
retuning.

## Gate
```c
#ifndef DEBUG_HURTBOXES
#define DEBUG_HURTBOXES 0
#endif
```
- `0` (default / release): `drawDebug`, the wire helpers, `s_wire`, and
  `pollDebugToggle` are preprocessed out — zero flash/RAM cost.
- `1`: overlay compiled in. Build with the define temporarily set to `1` (or
  `-DDEBUG_HURTBOXES=1`); the committed value is `0`.

## Overlay contents (drawn in world space, after the scene, before the HUD)
Camera + HUD translation is applied exactly as the sprite/blk scene
(`x - camX`, `y - camY + HUD_H`). All rects are the sim's raw int coordinates
(no `rndPx` sub-pixel smoothing), and every shape is drawn on every plane so
the L4 triplane pass composites a single image. Render stays read-only.

No color on device, so hurt vs hit is edge style:
- **solid** border = hurt box
- **dotted** border = hit box

| Box | Source | Style |
|---|---|---|
| Player hurt | `p.x/y/w/h` | solid |
| Target hurt | `g.target.rect` if `g.target.alive` (monster body in hunt, pole in train) | solid |
| Player active attack | `mh::meleeHitbox(p, p.atk)` when `PS_ATTACK`/`PS_SPECIAL` | dotted |
| Monster windup/attack (telegraph) | `m.x/y + facing * monsterAttackReach`, `hw x hh` when hunt and `MS_WINDUP`/`MS_ATTACK` | dotted |
| Shells / projectiles | `pr.x/y +/- pr.w/h` for each live `g.proj[i]` | dotted |
| Flail whirl radius | 48x48 centered on player when `p.stance == ST_WHIRL` | dotted |
| Hit-spark markers | small white plus at each live non-text `g.fx[i]` | solid plus |

Runtime toggle: hold **A+B for 30 ticks** to flip. `run()` only *observes* the
buttons (`pollDebugToggle`) and still passes the same `mh::Input` to
`stepGame`, so normal input is never eaten. Overlay defaults visible when the
debug build is compiled in.

## Flash / RAM (`rm -rf build && make build`)
```
DEBUG_HURTBOXES 0: Sketch uses 25934 bytes (87%)  Global variables 1884 bytes (676 free)
DEBUG_HURTBOXES 1: Sketch uses 28074 bytes (94%)  Global variables 1885 bytes (675 free)
```
- Disabled config is unchanged from the landed build (25934 B) and well under
  the ~27700 B cap.
- Enabled config adds 2140 B flash (wire helpers + drawDebug + toggle), fits
  under the 29696 B program max, and adds 1 B RAM (`s_wireHold`; `s_wire` is a
  bool merged into adjacent storage).
- Headroom: disabled 3762 B; enabled 1622 B.

## Tests
- `make test` (host C++17): **497 passed / 0 failed** (unchanged).
- `make fxtest-headless` (Ardens device serial): **green** —
  `test_assets PASSED=30 FAILED=0`, `test_boot PASSED=4 FAILED=0` (both final `P`).

## TODO / follow-ups
- Overlay is compile-time only; a runtime default-on/off persisted choice is
  not needed for the slice.
- The mock's debug also prints the player/monster state strings; the device
  overlay omits them (no state-text budget on the 8 px HUD) — boxes only.
- Damage-number effects are deliberately skipped as spark markers (`e.text`
  filtered); they already render as numbers.
