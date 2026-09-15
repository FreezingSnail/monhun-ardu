# monhun-ardu-3p1 — Device: loop integration (run/render at plane rate)

## Files
- changed: `monhun-ardu.ino` — core sim wired into the device loop.
  - `#include "src/core/world.hpp"`; single global `mh::Game g;`.
  - `setup()`: existing `boot()/startGray()/initRandomSeed()/FX::begin()/
    setCursorRange()` kept; then `mh::newGame(g, mh::W_SWORD, mh::MODE_HUNT)`.
  - `run()`: samples `arduboy.pressed()` into `mh::Input` (A -> `a`, B -> `b`,
    d-pad -> `mx`/`my`, -1/0/1 per axis) and calls `mh::stepGame(g, in)` once.
  - `render()`: read-only placeholder — player block + live target block
    (`g.target.alive`) both offset by `g.camX`/`g.camY` and `mh::HUD_H`, drawn
    with `arduboy.fillRect()` (ArduboyG picks the plane color, so identical
    shapes are issued on every plane with no per-plane state change). No Game
    mutation in render.
  - `loop()`: FX bracket unchanged — `FX::enableOLED()` /
    `arduboy.waitForNextPlane()` / `FX::disableOLED()`, then `needsUpdate()` ->
    `pollButtons()` + `run()`, then `render()` every plane.
- added: `output.md` (this file).

Logic/render split: `run()` (the only mutator) is gated by `needsUpdate()`; the
kernel never advances mid-plane because `render()` never touches `Game` and no
step is taken between planes.

## Build (`make build`, arduino-cli)
`make build` initially no-oped ("`build' is up to date") because a `build/`
directory shadows the (non-`.PHONY`) target; after `rm -rf build` the recipe
ran. Output tail:

```
arduino-cli compile --fqbn "arduboy-homemade:avr:arduboy-fx" --optimize-for-debug  --output-dir dist
Sketch uses 17264 bytes (58%) of program storage space. Maximum is 29696 bytes.
Global variables use 2494 bytes (97%) of dynamic memory, leaving 66 bytes for local variables. Maximum is 2560 bytes.
```

- Flash: 17264 / 29696 bytes (58%).
- Global RAM: 2494 / 2560 bytes. **66 bytes free — below the 300-byte target
  (see below).**

## Logic tick rate
`needsUpdate()` returns true once per full L4_Triplane sweep: `update_every_n`
defaults to 1 and `update_counter` is bumped once each time the plane index
wraps back to 0 (ABG `doDisplay`, `ABG_SYNC_PARK_ROW` path). So the core steps
exactly once per gray frame, never per plane.

- Plane writes: `ABG_REFRESH_HZ` = 156 Hz (SSD1306, no override) -> `render()`
  runs ~156x/s.
- Gray frame: 156 / 3 planes ≈ 52 Hz -> `run()`/`stepGame()` ≈ 52 ticks/s.
- Mock cadence is 60 Hz; 52 Hz is the triplane panel's nearest achievable
  per-frame cadence, so the logic tick order (one `stepGame` per frame, edge
  detect via the shared `inputEdges()`) matches the mock 1:1, just at the panel
  clock. No `setUpdateHz` retune was applied (it cannot exceed the frame rate
  and would only inject jitter).

No on-device instrumentation was added for this bead (Serial disabled, no
telemetry in scope); the rate above is derived from the ArduboyG frame cadence
and the `needsUpdate()` gating that was verified in the source. A device tick
counter belongs with the parity suite (monhun-ardu-p82).

## RAM headroom (target >= 300 bytes free NOT met: 66 free)
`avr-nm` section breakdown of the built ELF shows the pressure:

```
0080039c 00000400 b Arduboy2Base::sBuffer     1024
00800806 000002b8 b g                           696
0080015a 0000021c d mh::WEAPON_DEFS             540
0080039c ...        b Serial                     80
00800118 00000022 d mh::MONSTER_ATTACKS          34
0080013a 00000020 d fp::DIR8                     32
```

Root cause: on AVR, plain `const`/`constexpr` objects land in `.rodata`, which
avr-gcc places in **RAM** (copied from flash at startup). The three read-only
core tables (`WEAPON_DEFS` 540 B, `MONSTER_ATTACKS` 34 B, `fp::DIR8` 32 B =
606 B) therefore cost RAM even though they never change.

This bead is scoped to the `.ino` only and must not change core files or
behaviour, so the tables were left as landed. A behaviour-preserving follow-up
(mark the tables `PROGMEM` + `pgm_read_*` at the ~6 access sites, or a device
`Game` split) reclaims ~606 B and takes free RAM to ~670 B, clear of the 300 B
floor. Until then the 66 B gap is tight for ISR + call stack — flagged as the
top device risk.

## `make test` (host, C++17)
```
Total Passed: 497
Total Failed: 0
```
Exit 0. Unchanged from the 3p1 baseline.

## `make fxtest-headless` (Ardens, device serial)
```
test_boot
Sketch uses 11260 bytes (37%) of program storage space. Maximum is 29696 bytes.
Global variables use 1751 bytes (68%) of dynamic memory, leaving 809 bytes for local variables. Maximum is 2560 bytes.
=== test_boot ===
test_boot PASSED=4 FAILED=0
P
test_boot: PASS
```
Exit 0.

## TODO handed to rze (render parity)
- Replace the placeholder blocks with the mock block-art scene (tile/blob
  shapes, 8 px HUD strip, hit flash, damage numbers, projectiles, pole).
- Keep render read-only and plane-agnostic: build the scene from `const Game&`
  state only, issue the same shapes every plane; no `run()`/state changes.
- Apply camera + `HUD_H` to every world-space draw (already the pattern here).
- Consider `SpritesU` FX sprites for effects once parity shapes exist.
- Pair with p82: add an on-device tick counter / hashed state dump so the
  ~52 Hz logic cadence and sim parity are asserted from serial, not inferred.
