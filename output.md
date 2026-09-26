# monhun-ardu-d76 — fxtest: guard test-sketch RAM headroom (player_art trap)

Status: DONE. No worker commit (orchestrator commits).

## What landed

- `tools/fxtest_ram.py`: reads a fxtest compile log (path arg or stdin),
  regexes `Global variables use (\d+) bytes`, prints the max, exits 0 under
  budget / 1 over / 2 no measurement (a build the tool cannot audit is a hard
  error, never a silent pass). `--budget` default 2350 (2560 - 210 stack
  floor); `--label` names the suite in the message.
- `Makefile` `fxtest-build-%`: arduino-cli output now streams through
  `tee build/fxtest/<suite>/compile.log`, its exit status is preserved via a
  side `compile.status` file (POSIX sh, no PIPESTATUS), and a successful
  compile is audited by the tool with `FXTEST_RAM_BUDGET ?= 2350`. An
  over-budget sketch fails the **build** target, so it can never silently skip
  the serial run; `fxtest-run` is untouched.
- `tools/tests/test_fxtest_ram.py`: 8 cases (under/over/budget-flag/no-line/
  stdin/max-of-multiple/missing-file/label) in unittest, scratch under
  `build/tests/fxtest_ram/` (no /tmp).
- `docs/dev-flow.md`: new "Device-test RAM budget" section with the budget,
  the observed maximum, and the player_art PROGMEM trap (2bb7742).

## Numbers

- Fresh `arduino-cli cache clean` + `rm -rf build/fxtest`, then all 19 gate
  suites compiled: 19/19 `fxtest_ram: OK`. Observed maximum (default budget):
  `test_zones` 2017 B (543 B stack margin). Next: `test_wire` 1951,
  `test_perf` 1827, `test_hud` 1823, `test_player_art` 1823 B.
- Guard-fail demo (scratch, `FXTEST_RAM_BUDGET=1800`): make exited 2;
  `fxtest_ram: FAIL test_zones: globals 2017 B > budget 1800 B (543 B left for
  stack would be below the 760 B floor; move const tables to PROGMEM)`.
- Note: bead prose says player_art is 1821 B; the fresh compile reports
  **1823 B** (.data 76 + .bss 1747). Bead figure was off by 2 B; measured wins.

## Gates

- `make test-tools` — 402 tests OK, incl. the 8 new `test_fxtest_ram` cases.
- `FXTEST_ONLY="test_boot test_player_art" make fxtest-headless` —
  `test_boot: PASS` (4/0), `test_player_art: PASS` (156/0).
- `make gen-check` — PASS (216 generated artifacts unchanged).
- `make size` — flash 29502/29696 (194 free), ram 1920/2560; data facts shown.

## Wall time (scripted phases)

all-19 fresh build 9 s · guard-fail scratch ~2 s · two-suite device gate 2 s ·
test-tools 27 s · gen-check 10 s · size 3 s.

## Deviations

- No C++/core changes, so `make test` was not required by the bead's gate list
  and was not run.
