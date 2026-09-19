# monhun-ardu

Monster-Hunter-style top-down duel game for **Arduboy FX** (ATmega32u4), rendered
in 4-shade grayscale via ArduboyG `L4_Triplane`. The combat sim is a direct C++
port of a browser prototype (`mock/`), verified tick-for-tick against it.

---

## Status snapshot

| Item | State |
|---|---|
| Vertical-slice sim | Ported + parity-verified (20 scenes / 1269 ticks / 660 device asserts) |
| Device render + HUD + audio | Working (block/FX-sprite art, cue tones; HUD text/FX glyphs + bars — `7y3` clamp fixed) |
| Host unit tests | `make test` — **3144 passed / 0 failed** |
| Device tests (Ardens) | boot 4, assets 262, audio 14, menu 59, hud 17, parity 660, data 221, combat 184, perf 5 — all PASS |
| Perf gate (`monhun-ardu-8v7`, re-verified `42n.6` + `7y3` + `ljj.2` + `ljj.7` + `cgk`) | **PASS.** plane 153 Hz (≥135), logic 51 Hz (≥45), render max 5392 µs (≤7407), tick 988 µs, RAM free 409 B |
| Perf tooling | Headless Ardens profiler dump (`profiledump=<path>`, local patch) + on-device cycle bench (`test_perf`) |
| Shipping build | flash **25234 / 29696 B** (85%), RAM **1742 / 2560 B** (818 free); USB-free, see below |
| FX data image | **21768 B** of 16 MB used |

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
| `combat.hpp` | Combat blob loader (`ljj.2`, reworked by `cgk`): one production reader over the generated `combat_data.hpp` (host) / `mhCombat` blob (AVR), `creatureLoad`/`attackLoad` caches in `Game::combat`, guard eval with deterministic tick-derived chance, and the fixed 3-hitzone resolve (implicit body + optional head/appendage records). No per-tick cart reads |
| `input.hpp` | Edge flags + B-hold detection (`aP`, `bP`, `bR`, `bHeld`), no Arduino headers |
| `progmem.hpp` | Portable flash-read shim: `MH_PROGMEM` + typed `mhPgmRead*`; identity on host |

**Fixed-point discipline**: no float or double anywhere in the sim. Positions are
integer pixels plus a 1/16-px sub-pixel accumulator; projectile `x/y` are stored
directly in 1/16-px units and integrated by straight addition.

### Device layer

- `monhun-ardu.ino` — plane loop, input sampling, `stepGame()` + `audioUpdate()`
  in `run()`, `renderScene()` in `render()`. Boots into the opening menu; while
  it is active the sim/audio are skipped and `drawMenu()` replaces the scene.
  Menu A launches the picked hunt directly; win/loss + A returns to the menu
  (demo loop `5r1`). FX reads happen inside `FX::enableOLED()` /
  `waitForNextPlane()` / `FX::disableOLED()`.
- `src/menu_state.hpp` — host-testable menu FSM (`MenuState`/`menuStep`, pick →
  mode/kind mapping, post-over return edge); no Arduino.h.
- `src/menu.hpp` — menu render (FX glyph rows + selection underline), per plane.
- `src/app_state.hpp` — host-testable app routing (qs.4, demo flow `5r1`):
  menu → hunt → menu on the shipped path (a fresh hunt re-runs `newGame`), plus
  the shelf hub ↔ quests/smith graph kept compiled/tested but off the demo path,
  the held-button guards and the once-per-hunt progress commit.
- `src/app_setup.hpp` — device cart glue for a hunt start: arm the quest kill
  counter from the active `QuestDef` and resolve the smith tier multipliers.
- `src/render.hpp` — whole render path (also compiled into the perf bench so
  measured numbers describe the real loop). Arena, target, player, shells,
  effects, HUD.
- `src/audio.hpp` — cue detector diffing `Game` edges after `stepGame()`;
  non-blocking one-shot tones via ArduboyTones (Timer3, no Timer1 conflict).
  Mute with `-DMH_AUDIO=0`.
- **USB-free shipping main** (`monhun-ardu.ino`, `-DMH_NO_USB`): shipping builds
  compile the sketch's own `main()` so the core archive's `main.cpp.o` is never
  pulled in — that object is the only thing that calls `USBDevice.attach()` /
  `serialEventRun()` and drags in the CDC/PluggableUSB stack. The game never uses
  `Serial`, so this reclaims flash/RAM with no behavior change. `-DMH_NO_USB` is
  set only by the shipping flags (`Makefile SIZE_FLAGS`, used by
  `build`/`mini`/`size`/`debug`). The Ardens `fxtest` sketches are compiled by
  the separate `fxtest-build` arduino-cli invocation with stock flags, so they
  keep the core main and `captureserial` still works. Consequence: the shipping
  build has **no USB serial device** (no serial monitor / no OS port while the
  game runs); uploads go through the Cathy3K bootloader window
  (`arduino-cli upload` resets into it as usual).

### FX asset pipeline (all assets live on the FX chip)

```
mock/game.js + core dims ──tools/gen-art.py──► images/**/*.png
images/**/*.png ──tools/convert-sprite.py──► fxdata/*/Sprites.txt ──┐
                                                                    ▼
data/skeletons.json + data/creatures/*.json ──tools/gen-combat.py──►│
        ├─► fxdata/tables/combat.bin ──────────────────────────────┤
        └─► src/generated/combat_{data,meta,expect}.hpp            │
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
- Creature combat data is authored as JSON under `data/` and compiled by
  `tools/gen-combat.py` into the packed blob + generated headers described in
  `docs/creature-framework.md` (blob format §8, schema §§3-7, reference values
  §11). The blob is a `raw_t mhCombat` section of the one FX image (never a
  second flashable image); `combat_meta.hpp` carries VERSION/SIZE and the
  per-record offsets the loader uses, `combat_data.hpp` is the host mirror and
  `combat_expect.hpp` pins sizes, spot values and the blob sha256.
  `src/core/combat.hpp` is the production loader (host structs / AVR
  `mhFxRead*`), exercised by `tst/combat_test.hpp`,
  `tst/combat_pack_test.hpp` and the Ardens `test_combat`; the game runs the
  pattern interpreter from migrations `ljj.3`–`.5`; `data/creatures/
  ravager.json` was the first creature with zones (head + appendage, `cgk`, and
  `heavy.json` gained its appendage/long-tail zone in `4t4`), replacing the
  N-part machinery of `ljj.6`/`ljj.8`: body implicit, one u8 pool + one broken
  record per zone, broken-mask guards. The shipping heavy tail overlays the
  `fxtail_heavy` 24x16 sheet at the zone box (`src/render.hpp` drawMonster); the
  zones machinery is
  folded out of the `test_perf` and `test_parity` images with
  `-DMH_COMBAT_PARTS=0` (see `src/core/game.hpp`), whose scenes never run the
  ravager; shipping and `test_combat` keep it.
- Current blobs: `fxmonster`, `fxplayer`, `fxpole`, `fxball`, `fxscatter`,
  `fxspark`, `fxfontw`, `fxfontg`, the overlay/effect sheets and the raw
  content tables (`mhWeaponDefs`, `mhMonsterAttacks`, `mhMonsterDefs`,
  `mhCombat`) — 22719 B total.
- Regenerate with `make gen` (or `./tools/gen.sh`); bins are tracked despite
  `*.bin` being gitignored (force-added) so device tests are reproducible.
- `fxdata/manifest.json` (tracked) pins sha256+size for every source image,
  JSON source, fxdata declaration and generated artifact;
  `tools/fxdata_manifest.py --check` verifies it read-only and `make gen-check`
  re-runs the pipeline and fails if any generated artifact changes (staleness
  or nondeterminism).
- `make test-tools` runs the Python unittest suites in `tools/tests/`
  (manifest orphan/missing/malformed/staleness fixtures; gen-combat schema
  errors, id/ref errors, integer-only rejection, size limits, determinism,
  dump smoke, blob-ABI spot checks; contact-sheet determinism/layout).
- Authoring review without flashing:
  - `python3 tools/gen-combat.py --dump` validates the JSON and prints the
    compiled model (per-creature stats, attacks, windows, patterns and guards)
    without writing any artifact — the fast schema/id cross-ref check while
    tuning numbers.
  - `python3 tools/contact_sheet.py [--creature <id>] [--out build/x.png]`
    renders `data/skeletons.json` + `data/creatures/*.json` to a PNG contact
    sheet under `build/` (never committed): a tick timeline per attack
    (windup/active/recover shading, window spans) plus one 1:1 preview per hit
    window showing the body box and the window box, so a multi-window arc,
    part box or timing change can be reviewed as a picture. `--dump` prints a
    downsampled ASCII view for text evidence.
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
    - `test_data` — FX-cart weapon/monster tables match the mock values and packed layout
    - `test_combat` — combat blob loader: header/spot values, cross-refs, guard eval, damage routing, cache read counts
    - `test_parity` — replays mock-generated traces tick-by-tick vs core
   - `test_hud` — pins HUD bar/divider framebuffer bytes + world-clip control
   - `test_perf` — cycle-based bench + budget gates
   - Fixtures for parity are generated with
     `node tools/gen-parity-fixtures.js` (Node only produces fixtures; the test
     itself is C++/Ardens).
3. **Ardens** is required for tier 2; override with `ARDENS=/path/to/Ardens`.
   If it is missing, `fxtest-headless` skips cleanly.

### Hardware / cadence

- `L4_Triplane` + `ABG_TIMER1` + `ABG_SYNC_PARK_ROW` (`src/common.hpp`).
- Measured under load (bench): **156 Hz plane sweep, 52 Hz logic**, render max
  5056 µs/plane, logic tick 988 µs, 417 B free RAM (the `ljj.2` combat loader
  cache reserves 50 B until migrations use it). Mock runs 60 Hz; tick order
  is equivalent.
- Debug overlay `DEBUG_HURTBOXES=1` (hold A+B to toggle). Off by default; the
  overlay build is flash-tight and only for development.

---

## Controls

### Opening menu (boot)

| Input | Action |
|---|---|
| LEFT / RIGHT | cycle weapon: SWD (sword) / FLS (flail) / GUN (gunshield) |
| UP / DOWN | cycle target: LUNGE / SWEEP / HEAVY / RAVAGER beast, or POLE |
| A | launch the picked loadout directly into the hunt |

D-pad nav is debounced: a tap moves exactly one pick (immediate on the direction
change), while holding waits ~300 ms (16 logic ticks) and then repeats every
~115 ms (6 ticks). Reversing steps at once; releasing resets the hold timer; a
re-entry after win/loss resets it too, so a held d-pad cannot skip picks. A stays
edge-based: one launch per press. Picks wrap in both directions. A starts the
selected scene (demo flow, monhun-ardu-5r1), so targets LUNGE/SWEEP/HEAVY start
the matching beast variant in hunt mode and POLE starts train mode (the static
pole, no beast). After a win or loss, A returns to the menu with the picks kept
until reboot; the next A runs `newGame` again, so projectiles/effects/quest
counters start clean. While the menu is up the sim and audio are not stepped.

Menu v2 (monhun-ardu-2u8, name-only by 4t4) bakes the options into FX sheets:
`mh_menu_bg` (the title/labels/footer plus the dim light-gray options),
`mh_menu_wsel` (three 32x8 weapon tiles) and `mh_menu_msel` (five 64x8 target
tiles). Every option is a name only — SWD/FLS/GUN on the weapon row, CHICKEN/
BULL/LONGTAIL/RAVAGER/POLE in a 2-column beast grid — with the v2 icon slot left
clear and the text glyph-identical to `fxfontw`/`fxfontg` (gen-art
`check_menu_identity`). The picked weapon and target each draw a bright 1 px
frame plus cursor arrow; unpicked options stay dim. Footer: `A HUNT`.

### Hub / quests / smith (shelf code, qs.1–qs.4 — not on the demo path)

The data-driven hub/quests/smith screens and their EEPROM save stay in the tree
and unit/device-tested, but the shipped demo loop is menu → hunt → menu
(monhun-ardu-5r1) and never enters the hub. The shelf graph is:

| Input | Action |
|---|---|
| UP / DOWN | move the cursor (6 rows per page, scroll by 6) |
| A | accept the cursor row (start hunt / open a screen / buy / take quest) |
| B | back one level (quests/smith → hub; hub → opening menu) |

The hub shows HUNT / QUESTS / SMITH plus a ZENNY row that renders the live
`save.zenny` balance (dynamic value token). The quests board takes a kill quest
and turns it in for its reward; the smith sells weapon upgrade tiers. Every
state-changing action commits the 15-byte EEPROM save block once (write-on-
change + verify read). If a save already carries an active quest/tier it still
applies at hunt start and the hunt-end quest-progress commit still runs exactly
once per hunt; the demo path never takes/turns in a quest, so nothing can
double-count. There is no quit input in the demo — win/loss + A is the only
hunt exit.

### Target roster (`MONSTER_DEFS`, FX cart blob)

| Target | Mode | Size | HP | Spd | Attack |
|---|---|---|---|---|---|
| LUNGE | hunt | 32x24 | 200 | 5 | pecks inside 28 px, leaps 29..41 px (leap locks facing at windup) |
| SWEEP | hunt | 28x22 | 150 | 7 | stomps inside 24 px, gores 25..41 px (gore locks facing at windup) |
| HEAVY | hunt | 40x28 | 320 | 3 | lunges inside 24 px |
| POLE | train | 20x40 | — | — | static target, head zone = top 16 px |

### In game (hunt / train)

| Input | Action |
|---|---|
| D-pad | move |
| A | attack (in a stance: stance special) |
| B tap | dodge (sword) / deflect (flail) / shove (gunshield) |
| B hold ~11 ticks | enter stance (parry / whirl / guard); release exits |


---

## Commands

```sh
make test               # host unit tests (3117 asserts)
make fxtest-headless    # Ardens device tests (boot/assets/audio/menu/parity/data/combat/perf)
                        #   single suite: make fxtest-headless FXTEST_ONLY=test_combat
make size               # whole-image flash/RAM report + compile-time data facts
make gen-check          # regen determinism + generated header sync
make build              # compile shipping sketch (output in dist/)
make debug              # build, then open Ardens debugger (ELF + DWARF) with FX image
make mini               # compile for Arduboy Mini FQBN
make gen                # regenerate FX assets + fxdata.h/bin from images/
make hooks              # install git hooks (clang-format pre-commit), once per clone
make format             # format all tracked C-family sources
```

Workflow conventions (budget spikes, data facts, generated-artifact rules,
render review checklist) live in `docs/dev-flow.md`.

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
   Any new feature must fit flash (4462 B free) and keep the perf gates green.
2. **Flash headroom**: shipping 25234/29696 B (4462 B free) after the
   USB-stack removal (`42n.8`: custom USB-free `main()`, -2666 B flash /
   -140 B RAM, see the device-layer note), the content-table offload
   (`42n.1`-`42n.4`), the sine-LUT shrink (`42n.7`; the
   65 B quarter-wave table + sign fold), the opening menu (`6zb.2`, +1604 B
   for the menu state machine, FX-glyph render and the runtime monster-kind
   start path), d-pad nav debounce (`6zb.4`, +16 B for the per-axis hold
   timers) and the combat blob pipeline (`ljj.1`; blob is FX data, shipping
   flash unchanged). The 119 B of hot LUTs (`mh::SIN65`
   65 B, `fp::DIR8` 32 B, `mh::MH_MASK_TOP/BOT` 16 B, `mh::RING6` 6 B) stay in
   MCU flash by decision (`monhun-ardu-42n.5`): FX per-access reads measured
   ~150 cycles (~9 µs, 20-35x an LPM) and a SIN65 RAM cache would breach the
   300 B free-RAM gate. The 65-entry quarter-wave SIN65 table + quadrant sign
   folding (`42n.7`) is bit-identical to the old 256-byte table (pinned by the
   exhaustive host suite `tst/sin_test.hpp`). Any new feature must still budget
   flash, prefer FX data; debug-only code (`DEBUG_HURTBOXES`) must stay behind
   compile-time flags.
3. **RAM history**: constant tables originally sat in AVR `.rodata` (RAM) at
   2494 B used; moved to PROGMEM (MCU flash) via `progmem.hpp` → 1888 B. Audio
   added timers/state → 1941 B; the opening menu's 5 B `MenuState` → 1946 B; its
   per-axis nav hold state (`6zb.4`) → 1950 B; the `ljj.2` combat loader caches
   in `Game::combat` (22 B profile + 21 B attack/window + 7 B runtime = 50 B)
   → 2000 B. FX sprite data stays on the cart, so RAM grew little through the
   art pass; the USB-stack removal (`42n.8`) then dropped it to **1742 B
   (818 free)**.
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
7. **HUD bars were invisible on device** (fixed, `monhun-ardu-7y3`): `blk()`
   clamped every rect to y >= HUD_H, but `drawHud()` draws its divider at y=7 and
   the HP / stamina / monster / reload bars at rows 2..6, so those calls returned
   without painting; only the FX-glyph text showed. The clamp now lives in a
   shared `blkClamp()` core with two wrappers: `blk()` (world, y >= HUD_H) and
   `hudBlk()` (HUD strip, y >= 0); only the HUD call sites switched, so the
   arena border still cannot spill into rows 0..7. `test_hud` pins the real
   framebuffer bytes for every bar/divider on planes 0/1 plus the world-clip
   negative control; the perf gate absorbs the ~16 extra MH_MASK page
   reads/plane when the bars render (render max 4676 -> 5056 µs, inside the
   7407 µs budget).

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
- **Hot LUTs + procedural primitives** (`monhun-ardu-42n.5`): sine (`mh::SIN65`
  65 B after the `42n.7` quarter-wave shrink), `fp::DIR8`, `mh::MH_MASK_TOP/BOT`,
  `mh::RING6` (119 B total) stay in MCU flash as a documented exception —
  measured FX cost is ~150 cycles/access (~20-35x an LPM, worst plane +~580 µs
  if all were moved) and a sine RAM cache would breach the 300 B free-RAM gate.
  Arena dot field/border, HUD bars/reload bar and the `DEBUG_HURTBOXES` wire
  stay procedural; only sprite-like art moved to FX (`42n.3`). The
  256 B `mh::SIN256` table was replaced in place by the 65-entry quarter-wave
  table + quadrant sign folding (`monhun-ardu-42n.7`, -200 B shipping,
  bit-identical); never offload the sine LUT per-access.

---

## Repo layout

```
monhun-ardu.ino     device sketch (plane loop, input, run/render wiring)
src/core/           host-testable sim (no Arduino.h)
src/render.hpp      device render path (also in perf bench)
src/menu_state.hpp  opening-menu FSM (host-testable)
src/menu.hpp        opening-menu render (per plane)
src/audio.hpp       tone cue detector
src/external/       ArduboyG, SpritesU, SpritesABC
src/fxdata.h        generated FX offset constants
src/generated/      generated combat headers (data/meta/expect) + art dims
data/               creature combat JSON (skeletons + one file per creature)
docs/               creature-framework.md (combat blob/schema/reference values)
src/common.hpp      hardware config + FRAME macros
tst/                host suites + main
tst/fxdatatest/     Ardens device tests + harness
fxdata/             fxdata.txt, generated bins (tracked)
images/             source PNGs (blocks, fonts)
tools/              gen-art.py, gen-combat.py, gen.sh, convert-sprite.py,
                    fxdata_manifest.py, gen-parity-fixtures.js, tests/ (tooling
                    unittests)
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
