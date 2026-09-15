# monhun-ardu-ahf — Adopt host unit + Ardens fxtest harness

## Files
- added: `tst/fxdatatest/harness/fxtest.hpp` — device assertion harness copied from `~/code/CreatureGathererFX/tst/fxdatatest/fxtest.hpp`: `FxTest` with `expectEq`/`expectEqIdx`/`expectVersion`, `ok()`, and `report()` that prints `PASSED=n FAILED=m` then a final bare `P` or `F` line (with `Serial.flush()`). No deps beyond Arduino.h/Serial.
- added: `tst/fxdatatest/harness/fx_globals.hpp` — device global instance + `fxTestSetup()`, adapted from CreatureGathererFX `harness/fx_globals.hpp`. Defines ABG_IMPLEMENTATION/SPRITESU_IMPLEMENTATION, includes `../src/common.hpp` + `../src/fxdata.h` + `../src/core/{game,player}.hpp`, declares `decltype(arduboy) arduboy;`. Setup mirrors `monhun-ardu.ino`: `Serial.begin(9600); arduboy.begin(); FX::begin(FX_DATA_PAGE); FX::setCursorRange(0, 32767);` (single-arg `FX::begin` — this repo has no FX_SAVE_PAGE).
- added: `tst/fxdatatest/boot_test.hpp` — first smoke suite `test_boot(FxTest&)`: `initGame(g, W_SWORD)`, record `startX`, run 16 ticks of `stepPlayer` with `Input{mx:1}`, assert `player.x == startX + 18` (sword spd 18/16 px/tick * 16 ticks = 18 px, remainder carried in subX) and `g.tick == 16`.
- added: `tst/fxdatatest/test_boot.ino` — sketch shape copied from `test_moves.ino`: `setup()` runs `fxTestSetup(); FxTest test; test_boot(test); test.report(F("test_boot"));`, `loop()` exits.
- changed: `Makefile` — replaced old `fxtest`/`fxtest-build`/`fxtest-run` (INTEGRATION_TESTS --serial-test) with CreatureGathererFX-style targets: `ARDENS ?= $(HOME)/code/Ardens/build/Ardens.app/Contents/MacOS/Ardens`, `FXTEST_MS ?= 3000`, `FXDATA_BIN ?= fxdata/fxdata.bin`, `FXTEST_BUILD_DIR ?= build/fxtest`. `fxtest` aliases `fxtest-headless`; skips (exit 0) when ARDENS unset or not executable. Preflight checks ARDENS executable, FXDATA_BIN exists, and `captureserial` in Ardens binary. Build stages under `build/fxtest/<name>` (copy src, ino, *.hpp, harness), compiles with arduino-cli FQBN `arduboy-homemade:avr:arduboy-fx`. Run boots `captureserial=$(FXTEST_MS) fxport=d1 display=ssd1306 file=<hex> file=<fxdata.bin>`, CRLF-normalizes, fails on empty output / `F` line / nonzero Ardens exit / missing P-F marker. Host `make test` untouched (188 asserts).

## `make test` output (tail)
```
---------- all weapons step 60 ticks without breaking ----------
Passed: 3
Failed: 0
========== Total Counts ==========
Total Passed: 188
Total Failed: 0
```
Exit 0.

## `make fxtest-headless` output (tail)
```
test_boot
Sketch uses 9626 bytes (32%) of program storage space. Maximum is 29696 bytes.
Global variables use 1751 bytes (68%) of dynamic memory, leaving 809 bytes for local variables. Maximum is 2560 bytes.
=== test_boot ===
test_boot PASSED=2 FAILED=0
P
test_boot: PASS
```
Exit 0.

## Deviations
- None: `FXTEST_MS=3000` captured the full suite (2 asserts + marker) on this machine; no raise needed.
- `fx_globals.hpp` drops the `FX_SAVE_PAGE` second arg to `FX::begin` (repo has no save partition), matching `monhun-ardu.ino`.
- `fxtest-headless` skip also covers a missing (non-executable) ARDENS path, not just unset — per task spec ("unset or missing").
- No `generated/` staging needed: boot suite reads no FX fixture tables yet.