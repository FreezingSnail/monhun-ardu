# monhun-ardu

Monster-Hunter-style top-down duel game for **Arduboy FX** (ATmega32u4), rendered
in 4-shade grayscale via ArduboyG `L4_Triplane`. The combat sim is a direct C++
port of a browser prototype (`mock/`), verified tick-for-tick against it.

---

## Status snapshot

| Item | State |
|---|---|
| Vertical-slice sim | Ported + parity-verified (20 scenes / 1269 ticks / 660 device asserts) |
| Device render + HUD + audio | Working (block/FX-sprite art, HUD bars, cue tones) |
| Host unit tests | `make test` — **497 passed / 0 failed** |
| Device tests (Ardens) | boot 4, assets 30, audio 14, parity 660 — all PASS |
| Perf gate (`monhun-ardu-8v7`) | **FAIL — open.** Render/plane 13312 µs vs ≤ 7407; plane 82 Hz vs ≥ 135; logic 27 Hz vs ≥ 45 |
| Shipping build | flash **27672 / 29696 B** (93%), RAM **1941 / 2560 B** (619 free) |
| FX data image | **12466 B** of 16 MB used |

Speculative gameplay status: combat (sword / flail / gunshield), monster FSM,
training pole + DPS mode, camera/world clamps, HUD, audio cues all in place.
Not yet done: real art pass (`vx2`, human), feel tuning (`1to`, human),
EEPROM save (`qyb`, deferred).

---

## Architecture

```
mock/game.js ──port──► src/core/*.hpp ──shared verbatim──► host tests (tst/*.hpp)
                              │                              and device .ino
                              │
                    device layer (monhun-ardu.ino)
                    ├─ input sampling (pollButtons → mh::Input)
                    ├─ render.hpp (arena, actors, FX sprites, HUD)
                    ├─ audio.hpp (edge-diff cues → ArduboyTones)
                    └─ ArduboyG plane loop + FX/OLED bracket
```

### Core (`src/core/`, header-only, no `Arduino.h`)

| Module | Responsibility |
|---|---|
| `fp.hpp` | Fixed-point layer: FP=16 (1/16 px), `tdiv`, `addMove`, `addVel`, `DIR8`, 8-way dir math, `isqrt`, `rotFp`, stamina drain, `FpBody`/`FpStam` |
| `game.hpp` | `Game` state, `Player`, weapon defs + monster attack tables (PROGMEM), `Rect`, hit callbacks, `HOLD_TICKS=11`, `WORLD_W=256`, `WORLD_H=112` |
| `player.hpp` | Player FSM: chains/branches/stances, stamina, guard/parry/deflect, gunfire flags |
| `monster.hpp` | Monster FSM, attack cycle, windup, hit resolution, `pushApart` |
| `projectiles.hpp` | Shells (`ball`/`scatter`), effects, training pole, damage numbers, `trainDps`, `stepWorld` |
| `world.hpp` | Screen geometry constants, camera (int px, clamped), mode handling (hunt/train), `newGame`, `withWeapon`, `resetHunt`, `stepGame` |
| `input.hpp` | Edge flags + B-hold detection (`aP`, `bP`, `bR`, `bHeld`), no Arduino headers |
| `progmem.hpp` | Portable flash-read shim: `MH_PROGMEM` + typed `mhPgmRead*`; identity on host |

**Fixed-point discipline**: no float or double anywhere in the sim. Positions are
integer pixels plus a 1/16-px sub-pixel accumulator; projectile `x/y` are stored
directly in 1/16-px units and integrated by straight addition.

### Device layer

- `monhun-ardu.ino` — plane loop, input sampling, `stepGame()` + `audioUpdate()`
  in `run()`, `renderScene()` in `render()`. FX reads happen inside
  `FX::enableOLED()` / `waitForNextPlane()` / `FX::disableOLED()`.
- `src/render.hpp` — whole render path (also compiled into the perf bench so
  measured numbers describe the real loop). Arena, target, player, shells,
  effects, HUD.
- `src/audio.hpp` — cue detector diffing `Game` edges after `stepGame()`;
  non-blocking one-shot tones via ArduboyTones (Timer3, no Timer1 conflict).
  Mute with `-DMH_AUDIO=0`.

### FX asset pipeline (all assets live on the FX chip)

```
images/**/*.png ──tools/convert-sprite.py──► Sprites.txt ──┐
images/**/*.png ──tools/text2bmp.py────────► font sheets ──┤
                                                           ▼
                     fxdata/fxdata.txt ─► tools/gen.sh ─► fxdata-build.py
                                                           │
                              src/fxdata.h (offsets) ◄─────┤
                              fxdata/fxdata.bin      ◄─────┘
```

- Sprites and fonts are packed for `SPRITESU_FX`; drawn with
  `SpritesU::drawPlusMaskFX(x, y, img, FRAME(i))` where
  `FRAME(x) = x*3 + arduboy.currentPlane()`.
- MCU flash holds only `src/fxdata.h` offset constants and code. No glyph or
  bitmap arrays in MCU flash or RAM.
- Current blobs: `fxmonster`, `fxplayer`, `fxpole`, `fxball`, `fxscatter`,
  `fxspark`, `fxfontw`, `fxfontg` (12466 B total).
- Regenerate with `make gen` (or `./tools/gen.sh`); bins are tracked despite
  `*.bin` being gitignored (force-added) so device tests are reproducible.
- Fonts are drawn from the FX cart (vendored `Font4x6` was deleted after the
  asset pass; it cost ~3 KB of flash).

### Test tiers

1. **Host unit tests** — `make test`. `tst/test.hpp` (Test/TestSuite/TestRunner),
   suites in `tst/*_test.hpp`, `tst/main.cpp`. Binary in `build/tests/host`.
   Runs the core headers unmodified.
2. **Device tests** — `make fxtest-headless`. Stages each
   `tst/fxdatatest/test_*.ino` under `build/fxtest/<name>/`, compiles with
   arduino-cli, boots it in Ardens with the FX image on `d1`, and requires a
   final bare `P` (pass) or `F` (fail) marker over serial.
   - `test_boot` — boots, camera pin
   - `test_assets` — reads FX blobs inside the OLED bracket, checks plane bytes
   - `test_audio` — cue-map asserts with real tones
   - `test_parity` — replays mock-generated traces tick-by-tick vs core
   - `test_perf` — cycle-based bench + budget gates
   - Fixtures for parity are generated with
     `node tools/gen-parity-fixtures.js` (Node only produces fixtures; the test
     itself is C++/Ardens).
3. **Ardens** is required for tier 2; override with `ARDENS=/path/to/Ardens`.
   If it is missing, `fxtest-headless` skips cleanly.

### Hardware / cadence

- `L4_Triplane` + `ABG_TIMER1` + `ABG_SYNC_PARK_ROW` (`src/common.hpp`).
- 156 Hz plane sweep; logic steps once per `arduboy.needsUpdate()` → **~52 Hz
  logic** at idle (mock runs 60 Hz; tick order is equivalent).
- Debug overlay `DEBUG_HURTBOXES=1` (hold A+B to toggle). Off by default; the
  overlay build is flash-tight and only for development.

---

## Commands

```sh
make test               # host unit tests (497 asserts)
make fxtest-headless    # Ardens device tests (boot/assets/audio/parity/perf)
make build              # compile shipping sketch (output in dist/)
make mini               # compile for Arduboy Mini FQBN
make gen                # regenerate FX assets + fxdata.h/bin from images/
```

Notes:
- `build`, `mini`, `gen`, `test`, `fxtest*` are all `.PHONY`, so `make build`
  always recompiles even when `build/` (host tests, staged fxtests) exists.
- `make build` does not tolerate a stale `dist/`; it overwrites as needed.

---

## Challenges / known issues

1. **Perf gate failing (`8v7`, P1 follow-up).**
   `drawArena()` spends **~6044 µs/plane** on three signed 16-bit modulo
   expressions per dot (`__divmodhi4`) across 260 dots — the dominant cost even
   in the base scene. Combined with worst-case FX load, the plane rate drops to
   **82 Hz**. Renderer-only fixes (unsigned/bit ops or a precomputed dot table)
   are projected to free 4–5 ms without touching sim semantics.
   Effect-count caps for worst-case scenes are a second lever; damage-number
   glyphs (one FX read per digit) are the most expensive effect.
2. **Flash headroom** is thin: shipping 27672/29696 B (2024 B free). The
   `DEBUG_HURTBOXES=1` and `test_perf` images sit at 99% — any new feature must
   budget flash, prefer FX data.
3. **RAM history**: constant tables originally sat in AVR `.rodata` (RAM) at
   2494 B used; moved to PROGMEM (MCU flash) via `progmem.hpp` → 1888 B. Audio
   added timers/state → 1941 B. FX sprite data stays on the cart, so RAM grew
   little through the art pass, but the margin is ~600 B.
4. **CX accuracy vs speed**: the sim is parity-locked to the mock by 660 device
   asserts. Any future tuning change must either update the mock + fixtures in
   the same commit or be expressed as render/parameter-only changes.
5. **FX/OLED SPI sharing**: all FX reads must stay inside the
   enable/park/disable bracket; reads measured at ≤255 µs, so fine at current
   rates, but it constrains where asset reads can happen.
6. **Parity fixes discovered real bugs**: hitstop gating, projectile cull
   boundary (int px vs 1/16 px), and missing player hurt sparks were fixed in
   core to match the mock; the mock itself had an isqrt seed bug and a
   projectile-speed double-scaling bug, both fixed (`6c9371e`, `4ac3f5b`).

---

## Design decisions (recorded in bd)

- **Grayscale mode**: keep `L4_Triplane` + `ABG_TIMER1` + park row; accept
  plane-bound cadence (52 Hz logic / 156 Hz render). Revisit only if the perf
  gate stays red after renderer fixes. (`monhun-ardu-b3t`)
- **World**: scrolling camera (mock parity). FX has ample room for map growth;
  a single-screen pen would mean retuning sim extents and monster ranges away
  from mock truth. (`monhun-ardu-kpi`)
- **Assets**: everything (sprites, fonts, text) on the FX chip; MCU flash holds
  code and PROGMEM tables only. (`monhun-ardu-kt7.2`)
- **Audio**: procedural one-shot tones (ArduboyTones), edge-diffed from sim
  state — no audio calls inside core logic. (`monhun-ardu-6zc`)

---

## Repo layout

```
monhun-ardu.ino     device sketch (plane loop, input, run/render wiring)
src/core/           host-testable sim (no Arduino.h)
src/render.hpp      device render path (also in perf bench)
src/audio.hpp       tone cue detector
src/external/       ArduboyG, SpritesU, SpritesABC
src/fxdata.h        generated FX offset constants
src/common.hpp      hardware config + FRAME macros
tst/                host suites + main
tst/fxdatatest/     Ardens device tests + harness
fxdata/             fxdata.txt, generated bins (tracked)
images/             source PNGs (blocks, fonts)
tools/              convert-sprite.py, text2bmp.py, gen.sh, gen-art.py,
                    gen-parity-fixtures.js
mock/               JS prototype (source of truth) + node tests
dist/               compile output
output.md           most recent worker report (scratch, overwritten per task)
```

## Beads / tracker

Work is tracked with `bd` (epic `monhun-ardu-kt7`). Remaining open items:
`8v7` perf gate (blocked by the render-hotspot fix), `vx2` real art (human),
`1to` feel playtest (human), `qyb` EEPROM save (deferred).
