# monhun-ardu-fie.5 — Device: room bg blit + props + fade + mock-up art

Status: **PASS** (not committed; orchestrator commits)

## What changed

- `src/render.hpp`
  - `drawRoom(g, camX, camY)` replaces the `drawArena` dot field for every
    `MH_ROOM_BOUNDS` image (shipping/perf/hud/zones; parity + hub keep the
    legacy carve and do not include render). One stored layer is streamed per
    plane straight into framebuffer pages 1..7; page 0 stays HUD.
  - Room base/extent dispatch (`roomImageInfo`) uses the generated
    `ROOM_*_W/H` + `mh_map_*` symbols; no literal record indices.
  - Fused inline-asm reader (fie.7 v2b): `FX::seekData` prefetches column 0, a
    `SPIF` wait aligns to it, then the asm streams the row with the
    `mul b, 1<<(8-v)` split (`r0` -> dest page q-1, `r1` -> dest page q+1).
    Dest pages 1..7, page 0 untouched. All reads in the render pass between
    plane blits.
  - **Spike bug found + fixed**: the fie.7 cycle padding was one SPI-byte-time
    short (16 cycles at SPI2X/8 MHz). Every read lagged one column and the
    whole window shifted by one — the spike only modelled the data flow, never
    a pixel. `zones_test` caught it (camp marker column 63). The loops now keep
    an explicit safety margin over 16 cycles, and the first byte is aligned
    with a real `SPIF` wait. `readEnd()` drains the trailing kick each page.
  - `drawProps(g, camX, camY)`: the active room's prop records blitted as FX
    sprites over the blit and under the actors (`sprDraw` + `FRAME(frame)`).
    Sheet dispatch resolves the two shipped sheets (`mh_map_tent`, `fxpole`).
  - `drawFade(g)`: door-cross black wipe (arena `blk()` shade 0, HUD spared);
    no cart traffic while fading.
- `src/core/game.hpp`: `Game::roomFirstProp/roomPropCount` (+`fade`); room prop
  range cached like the door/heal ranges.
- `src/core/zones.hpp`: `ZoneProp` value struct + `zonePropRead` (AVR blob /
  host mirror, same pattern as `ZoneDoor`).
- `src/core/world.hpp` (`FADE_TICKS = 4`): `loadRoom` arms `g.fade` and caches
  the prop range; `stepGame` decays `g.fade` one tick per logic tick.
- `src/core/player.hpp`: `initGame` zeroes the new fields.
- `tools/gen-art.py`: authors the 32x24 mock-up camp tent (`map_tent` sheet,
  integer-only, transparent background so convert-sprite emits the mask plane).
- `images/blocks/mh_map_tent_32x24.png` (generated), `fxdata/*` regenerated:
  `blocks/Sprites.txt` + `fxdata.h`/`src/fxdata.h` now declare `mh_map_tent`;
  `zone_meta.hpp` resolves `SHEET_MH_MAP_TENT_RESOLVED = true`, offset 22217.
- `tst/fxdatatest/test_zones.ino` + `zones_test.hpp` (new suite).

## Device suite — `zones_test` (47 assertions)

- reads the `mhZones` header (magic/version/flags) and camp room record
  (`w/h/propCount`) straight off the cart at `zone::` offsets;
- camp view (128x56, `camY == 0`): every dest page 1..7 must byte-equal the
  source layer page read back with a *different* reader (`FX::readDataBytes`);
  page 0 untouched;
- area view (384x112, `camX=64 camY=52` -> `v=4 q0=6`, plane 1): every page
  must equal `(src[q0+j-1] >> v) | (src[q0+j] << (8-v))` computed in C;
- props: tent apex/body ink at the room coords, clear off-box, pole-room prop;
- fade: `drawFade` clears pages 1..7 and spares page 0; `fade == 0` no-op;
- door-cross smoke: camp -> area via `updateDoors`, `fade` armed then decays.

## Verification (exact tails)

- `FXTEST_ONLY=test_zones make fxtest-headless` ->
  `Sketch uses 22886 bytes` / `zones_test PASSED=47 FAILED=0` / `P` / PASS
- `FXTEST_ONLY=test_perf make fxtest-headless` ->
  `B pUs=6659 pHz=150 lHz=50 lTk=568 rMx=5552 rAv=5360 ram=463` /
  `perf_test PASSED=5 FAILED=0` / PASS
- `make fxtest-headless` (full, 16 suites) -> all PASS:
  assets 270, audio 17, boot 4, combat 293, data 368, hub 57, hud 17,
  menu_art 81, menu 78, monster_art 111, parity 660, perf 5, player_art 111,
  quests 50, screens 78, smith 66, **zones 47**.
- `make test` -> `Total Passed: 5361` / `Total Failed: 0`
- `make test-tools` -> `Ran 178 tests in 10.413s` / `OK`
- `make gen-check` -> `fxdata_manifest: PASS (82 generated artifacts unchanged)`
- `make size`:
  ```
  size: .text=29342 .data=40 .bss=1740
  size: flash=29382/29696 (314 free)  ram=1780/2560
  size: data facts: HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false HAS_GUARD_HP:false HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true HAS_HIT_STAGGER:false HAS_MULTI_STEP:false HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false HAS_STAGGER:true HAS_STEP_AFTER:false HAS_STEP_CHANCE:false HAS_WAIT_STEPS:false HAS_ZONES:true
  ```

## Budget

| | baseline d3f74d6 | after | delta |
|---|---|---|---|
| flash | 29026/29696 (670 free) | 29382/29696 (314 free) | **+356 B** |
| RAM | 1776/2560 | 1780/2560 | **+4 B** |
| render max | 4868 us | **5552 us** (<=7407) | +684 us |
| plane / logic Hz | 156 / 52 | **150 / 50** (>=135 / >=45) | — |
| perf free RAM | 572 B | **463 B** (>=300) | -109 B |

Net +356 B: the blit asm + prop/fade draw + `zonePropRead` + room-image
dispatch, minus the dropped `drawArena`. The 354 B fie.7 blit estimate holds;
props/fade/readers cost ~the same as the procedural arena they replace.
The tent sheet lives in the FX cart image, not AVR flash.

## Render/data review checklist (docs/dev-flow.md)

- state shades: room layers are 1bpp per plane from the same `(p+1)`-th ramp
  threshold as `convert-sprite get_shade` (generator); props come from
  `drawPlusMaskFX` triplane sheets, `FRAME()` selects the plane.
- anchors: props draw at their `data/map.json`/prop-record `(x, y)` top-left,
  translated by cam + `HUD_H` exactly like every other sprite.
- shade-0 erase: the room blit ORs into the plane cleared by ArduboyG's display
  pass; no stale pixels. Fade uses `blk(..., shade 0)` (an eraser).
- No float/double in core/device.

## Deviations / notes

- The camp tent sheet is authored under `images/blocks/` (existing
  convert-sprite pipeline, emitted into `blocks/Sprites.txt`) rather than
  `images/maps/`: `gen-zones.py` only emits room layer arrays today, and moving
  it would require new map-tooling paths + tests. The manifest maps the image
  dir to the declaration dir, so this is provenance-correct; a future bead can
  relocate it when gen-zones learns prop sheets.
- The fused asm deviates from the spike's exact loop by the safety padding +
  first-byte `SPIF` wait. This is the bug fix, not a new technique.
- The area view test proves the `v != 0` split path; the camp view proves the
  `v == 0` copy path and HUD page isolation.
- Not committed/pushed (orchestrator commits).
- `bd close` was **forced**: the bead still lists open dependency `fie.6`
  (demo flow routing); the implementation + gate are complete and independent.
