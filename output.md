# monhun-ardu-1du — Demo build: `make demo` picker (weapon 1/3 + beast 1/4)

## Status

DONE. `make demo` compiles clean and boots into the picker; shipping stays
byte-identical (29666/29696). No commit/push (orchestrator commits).

## Files

- `Makefile` — new `demo` target (PHONY): shipping flags + `-DMH_DEMO=1`
  (`-DMH_NO_USB -DMH_AUDIO=0 -DMH_ROOM_IMAGE=1` kept), output to `dist/`, then
  the `demo size: flash=.../29696 (... free)  ram=.../2560` line.
- `monhun-ardu.ino` — `MH_DEMO` default 0; under `MH_DEMO` the
  screens/cards/app_state/app_setup/quest includes and the
  save/screen/card/GEAR-readout/hunt-arm globals compile out. `setup()` boots the
  picker (`demoInit`); `run()` runs the picker → GO → `demoLaunch`, camp hold-B →
  picker, hunt-end + fresh A → picker; `render()` draws `drawDemoPicker` while the
  picker is up. Shipping/dev paths unchanged (`#else`).
- `src/demo_menu.hpp` (new) — host-testable picker logic: `demoWrap` (wrap both
  ways), `demoInit` / `demoEnter` (held-button guard), `demoStep` (row nav +
  weapon/beast cycle + GO `DEMO_LAUNCH`), `demoOverReturnStep` (the demo's own
  hunt-end edge latch), `demoHomeSpawn` (generated `zone::SPAWN_*` home-room
  start table), `demoLaunch` (`newGame` + `loadRoom(beastHomeRoom, home spawn)`).
- `src/render.hpp` — `drawDemoPicker` under `#if MH_DEMO` (title DEMO, rows at
  y=11+9*i; `hudBlk` cursor chip + `textPut`/`fxfontw` words from MH_PROGMEM
  tables; no cart data / no prebaked art).
- `tst/demo_menu_test.hpp` (new) + `tst/main.cpp` registration — 10 tests: wrap
  both ways, row nav wrap, weapon wrap 3 / beast wrap 4, GO launch once per
  press, A edge-once, B inert, held-button guard on re-entry, hunt-end edge
  once, home-spawn mapping (generated constants), and launch room/kind/spawn/
  identity-multiplier/empty-inventory.
- `README.md` — "Demo build (`make demo`)" section (outputs, controls, publish
  via `tools/package-arduboy.py`).
- `mock/`, `fxdata/`, generated headers: untouched.

## Verification

- `make demo` (clean compile):
  `Sketch uses 23258 bytes (78%) of program storage space.`
  `demo size: flash=23258/29696 (6438 free)  ram=1674/2560`
- `make size` (shipping byte-identical): `size: flash=29666/29696 (30 free)  ram=1867/2560`;
  data facts unchanged. `make build` unchanged.
- `make dev` (compile step verified directly; the target then launches Ardens and
  blocks on the GUI): `Sketch uses 28946 bytes (97%)... RAM 1867`.
- `make test`: `Total Passed: 7074  Total Failed: 0` (new demo suite included).
- `make gen-check`: `fxdata_manifest: PASS (217 generated artifacts unchanged)`.
- Device/Ardens smoke image is OUT of scope (no device suite touched; no device
  tests run per the bead).

## Wall time

- worker (implement + host/device-build gates + size + docs): ~20 min
  (first file 18:04, report 18:18).

## Deviations

None.
