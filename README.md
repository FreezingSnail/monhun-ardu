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
| Device tests (Ardens) | boot 4, assets 30, audio 14, parity 660, perf 5 — all PASS |
| Perf gate (`monhun-ardu-8v7`) | **PASS — closed.** plane 156 Hz (≥135), logic 52 Hz (≥45), render max 4736 µs (≤7407), tick 988 µs, RAM free 493 B |
| Perf tooling | Headless Ardens profiler dump (`profiledump=<path>`, local patch) + on-device cycle bench (`test_perf`) |
| Shipping build | flash **26644 / 29696 B** (90%), RAM **1941 / 2560 B** (619 free) |
| FX data image | **12466 B** of 16 MB used |

Speculative gameplay status: combat (sword / flail / gunshield), monster FSM,
training pole + DPS mode, camera/world clamps, HUD, audio cues all in place.
Perf-verified on device. Remaining: real art pass (`vx2`, human), feel tuning
(`1to`, human), EEPROM save (`qyb`, deferred).

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
mock/game.js + core dims ──tools/gen-art.py──► images/**/*.png
images/**/*.png ──tools/convert-sprite.py──► fxdata/*/Sprites.txt ──┐
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
- `fxdata/manifest.json` (tracked) pins sha256+size for every source image,
  fxdata declaration and generated artifact; `tools/fxdata_manifest.py --check`
  verifies it read-only and `make gen-check` re-runs the pipeline and fails if
  any generated artifact changes (staleness or nondeterminism).
- `make test-tools` runs the Python unittest suite in `tools/tests/`
  (manifest orphan/missing/malformed/staleness fixtures).
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
- Measured under load (bench): **156 Hz plane sweep, 52 Hz logic**, render max
  4736 µs/plane, logic tick 988 µs, 493 B free RAM. Mock runs 60 Hz; tick order
  is equivalent.
- Debug overlay `DEBUG_HURTBOXES=1` (hold A+B to toggle). Off by default; the
  overlay build is flash-tight and only for development.

---

## Commands

```sh
make test               # host unit tests (497 asserts)
make fxtest-headless    # Ardens device tests (boot/assets/audio/parity/perf)
make build              # compile shipping sketch (output in dist/)
make debug              # build, then open Ardens debugger (ELF + DWARF) with FX image
make mini               # compile for Arduboy Mini FQBN
make gen                # regenerate FX assets + fxdata.h/bin from images/
make hooks              # install git hooks (clang-format pre-commit), once per clone
make format             # format all tracked C-family sources
```

`make debug` launches Ardens on `dist/monhun-ardu.ino.elf` (DWARF debug info
for source view, symbols, globals, call stack) + `fxdata/fxdata.bin`
(windowed, FX cart on `d1`, SSD1306). Debugger keys: `F5` pause/continue,
`F8` reset, `O` settings, `F11` fullscreen.

Also available there:
- **CPU profiler** — instruction-level inclusive CPU load and raw cycle counts,
  hotspot list, annotations on source/disassembly. Open via the debugger menu
  (Profiler) or set `open_profiler=1` in `Ardens.ini`. **Headless dump** (no
  window, scriptable/agent-readable):
  ```sh
  "$ARDENS" headless=3000 display=ssd1306 fxport=d1 \
      profiledump=build/profiler.txt \
      file=dist/monhun-ardu.ino.elf file=fxdata/fxdata.bin
  ```
  Writes tab-separated `count/pct/begin/end/symbol` rows (top 50, sorted) plus
  total cycles and CPU-active %. Requires the ELF from `make build`. Note: the
  `profiledump` parameter is a local Ardens patch (uncommitted in
  `~/code/Ardens/src/headless.cpp` as of this writing).
- **Auto-breaks**: stack overflow, null deref, out-of-bounds, SPI write
  collision, FX busy access — useful when touching render/FX code.
- Snapshots (`F4`), display screenshots (`F2`), GIF recording (`F3`).

Notes:
- `build`, `mini`, `gen`, `debug`, `hooks`, `format`, `test`, `fxtest*` are all
  `.PHONY`, so `make build` always recompiles even when `build/` (host tests,
  staged fxtests) exists.
- `make build` does not tolerate a stale `dist/`; it overwrites as needed.
- **Formatting**: `.clang-format` (LLVM base, 4-space, 200 col, LF) defines the
  style. `make hooks` sets `core.hooksPath=.githooks`; the pre-commit hook runs
  `clang-format` on staged C/C++/`.ino` files and restages them. Vendored
  (`src/external`, `Arduboy-Python-Utilities`) and generated files
  (`src/fxdata.h`, `fxdata/fxdata.h`, `tst/fxdatatest/parity_fixtures.hpp`) are
  skipped. Override the binary with `CLANG_FORMAT=/path/to/clang-format`.

---

## Challenges / known issues

1. **Perf resolved, headroom watched.** Two render fixes landed:
   `drawArena()`'s per-dot signed modulo field (~6044 µs/plane) became
   incremental counters plus a `MAX_FX_DRAW=6` render-side effect cap
   (`80bbfb0`), and `blk()`'s `fillRect`/`drawFastVLine` path was replaced with
   direct masked framebuffer writes (`816767d`) — render max 13312 → 3984 µs,
   plane 82 → 156 Hz, logic 27 → 52 Hz, profiler `mh::blk` share 29% → 3.6%.
   Any new feature must fit flash (3052 B free) and keep the perf gates green.
2. **Flash headroom** is thin: shipping 26644/29696 B (3052 B free) after the
   content-table offload (`42n.1`-`42n.4`; `.text` 26598 + `.data` 46). The
   310 B of hot LUTs (`mh::SIN256` 256 B, `fp::DIR8` 32 B,
   `mh::MH_MASK_TOP/BOT` 16 B, `mh::RING6` 6 B) stay in MCU flash by decision
   (`monhun-ardu-42n.5`): FX per-access reads measured ~150 cycles (~9 µs,
   20-35x an LPM) and a SIN256 RAM cache would breach the 300 B free-RAM gate.
   The epic's ≤26600 B line is 44 B below the current tree; the sanctioned fix
   is the in-flash quarter-wave SIN256 shrink (191 B, bit-identical),
   `monhun-ardu-42n.7`. Any new feature must still budget flash, prefer FX data;
   debug-only code (`DEBUG_HURTBOXES`) must stay behind compile-time flags.
3. **RAM history**: constant tables originally sat in AVR `.rodata` (RAM) at
   2494 B used; moved to PROGMEM (MCU flash) via `progmem.hpp` → 1888 B. Audio
   added timers/state → 1941 B. FX sprite data stays on the cart, so RAM grew
   little through the art pass, but the margin is ~600 B.
4. **Mock accuracy vs speed**: the sim is parity-locked to the mock by 660 device
   asserts. Any future tuning change must either update the mock + fixtures in
   the same commit or be expressed as render/parameter-only changes.
5. **FX/OLED SPI sharing**: all FX reads must stay inside the
   enable/park/disable bracket; per-access cost measured at ~150 cycles (~9 µs
   at 16 MHz: 4-byte seek command at 8 MHz SPI + `readEnd`; static count from
   the `avr-objdump` disassembly of `FX::seekData`/`readEnd`, cross-checked with
   the Ardens headless profiler), ~20-35x an LPM. That per-access cost is why
   the hot LUTs stay in MCU flash (`monhun-ardu-42n.5`) and why asset reads may
   only run between plane blits, never during the paint.
6. **Parity fixes discovered real bugs**: hitstop gating, projectile cull
   boundary (int px vs 1/16 px), and missing player hurt sparks were fixed in
   core to match the mock; the mock itself had an isqrt seed bug and a
   projectile-speed double-scaling bug, both fixed (`6c9371e`, `4ac3f5b`).
7. **HUD bars are invisible on device** (known bug, `monhun-ardu-7y3`): `blk()`
   clamps every rect to y >= HUD_H, but `drawHud()` draws its divider at y=7 and
   the HP / stamina / monster / reload bars at rows 2..6, so those calls return
   without painting; only the FX-glyph text shows. Fixing it re-adds ~16
   MH_MASK page reads/plane when the bars render.

---

## Design decisions (recorded in bd)

- **Grayscale mode**: keep `L4_Triplane` + `ABG_TIMER1` + park row; accept
  plane-bound cadence (measured 156 Hz plane / 52 Hz logic under load).
  (`monhun-ardu-b3t`)
- **World**: scrolling camera (mock parity). FX has ample room for map growth;
  a single-screen pen would mean retuning sim extents and monster ranges away
  from mock truth. (`monhun-ardu-kpi`)
- **Assets**: everything (sprites, fonts, text) on the FX chip; MCU flash holds
  code and PROGMEM tables only. (`monhun-ardu-kt7.2`)
- **Audio**: procedural one-shot tones (ArduboyTones), edge-diffed from sim
  state — no audio calls inside core logic. (`monhun-ardu-6zc`)
- **Hot LUTs + procedural primitives** (`monhun-ardu-42n.5`): `mh::SIN256`,
  `fp::DIR8`, `mh::MH_MASK_TOP/BOT`, `mh::RING6` (310 B total) stay in MCU flash
  as a documented exception — measured FX cost is ~150 cycles/access (~20-35x
  an LPM, worst plane +~580 µs if all were moved) and a SIN256 RAM cache would
  breach the 300 B free-RAM gate. Arena dot field/border, HUD bars/reload bar
  and the `DEBUG_HURTBOXES` wire stay procedural; only sprite-like art moved to
  FX (`42n.3`). If the epic flash line needs the 44 B, shrink SIN256 in place to
  a 65-entry quarter-wave table + sign folding (`monhun-ardu-42n.7`), never
  offload it per-access.

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
tools/              gen-art.py, gen.sh, convert-sprite.py, fxdata_manifest.py,
                    gen-parity-fixtures.js, tests/ (tooling unittests)
mock/               JS prototype (source of truth) + node tests
dist/               compile output
output.md           most recent worker report (scratch, overwritten per task)
```

## Beads / tracker

Work is tracked with `bd` (epic `monhun-ardu-kt7`). Everything autonomous is
closed, including the perf chain: `kt7.1` PROGMEM/RAM, `kt7.2` FX assets,
`kt7.3` arena hotspot, `kt7.4` redundant black fills, `kt7.5` fast rect fill,
and the gates `b3t`/`kpi`/`8v7`. Remaining:

- `vx2` — real 4-shade art pass (human)
- `1to` — device feel playtest + tuning (human)
- `qyb` — EEPROM save (deferred, out of slice)
