# Dev flow conventions (lessons from the 42n / 6zb / ljj waves)

These are workflow rules distilled from the creature-framework wave. They are
about the repo, not any particular tool or agent.

## Budget-first for flash/perf work

- **Spike before bead.** Any change that can touch the flash or perf gates gets
  a measurable spike first (stub the old code, build, report the whole-image
  delta). LTO makes per-symbol arithmetic meaningless — only `.text`/`.data`
  totals count. Use `make size`.
- **Split "implement" from "make it fit".** A data/feature change that flips a
  compile-time gate re-expands code paths; budget that flip in its own bead,
  not after tests are written. (ljj.6: parts machinery cost +3.58 KB, found
  only after the data was authored.)
- **Acceptance reports exact numbers + a justification**, not a pre-guessed
  target. "Report flash delta and explain it" beats "must be ≤ N bytes" when N
  was estimated before measurement.

## Compile-time data facts

`tools/gen-combat.py` emits `HAS_*` booleans into `src/generated/combat_meta.hpp`
derived from the packed data. Generic machinery (parts, stagger, multi-window,
wait steps, chance, complex guards) compiles in only when the data uses it, so
shipping stays small until a feature is actually authored.

- Adding data that flips a fact changes the shipping image — treat it as a
  budget event and run `make size` before/after.
- `make size` prints the facts alongside the ELF sizes so a flip is visible in
  every report.
- Static asserts that pin struct sizes must fold with the facts
  (see `CombatState`: `HAS_PARTS ? 78 : 64`).

## Generated artifacts

- `make gen` is the only generation entry; `make gen-check` re-runs it and
  fails on any byte change, plus asserts `fxdata/fxdata.h == src/fxdata.h`.
- Stage generated sets together after a regen (`git add -A`); never commit a
  half-generated set (one wave lost `src/fxdata.h` to a manual `git add`).
- Tests must reference generated symbolic constants, never literal record
  indices — an inserted creature shifted a hardcoded index and broke pack
  parity (ljj.6).

## Inner loop vs full gate

- Full device gate compiles every `test_*.ino`. For iteration:
  `make fxtest-headless FXTEST_ONLY=test_combat`.
- Host: `make test`; tooling: `make test-tools`; size: `make size`.
- Full gate before every commit: gen-check, host, all device suites, and
  `node tools/gen-parity-fixtures.js` + empty diff on
  `tst/fxdatatest/parity_fixtures.hpp`.

## Render/data review checklist

Diff review before commit (it caught five real drifts in one bead):

- state shades (windup vs attack vs hit flash) match the mock;
- hitbox sizes match the sim's `hw/hh` exactly, including branch attacks;
- frame anchors land on the same pixels the old draw used;
- shade-0/erase frames still erase on all planes;
- telegraph and hit test read the same cached window.

## Releases

- `.github/workflows/release.yml` builds and publishes on tag push
  (`git tag v0.1.0 && git push origin v0.1.0`); `workflow_dispatch` is a dry
  run that uploads the workflow artifact without creating a release.
- `tools/package-arduboy.py` assembles the `.arduboy` container (flat zip:
  `info.json` schemaVersion 3 + one hex per device + `fxdata.bin` as the
  binary's `flashdata` + `LICENSE.txt`) deterministically; tests live in
  `tools/tests/test_package_arduboy.py`. The FX hex is written before the Mini
  hex because MrBlinky's `uploader.py` flashes the first `.hex` in the archive.
- CI facts: the full `make gen-check` regen stays a local gate (PNG/zlib output
  is not guaranteed byte-identical across platforms/Pillow versions). CI runs
  the read-only `tools/fxdata_manifest.py --check`, `cmp fxdata/fxdata.h
  src/fxdata.h`, `make test`, `make test-tools`, then `make build` + `make mini`.
- Release assets: `monhun-ardu-<tag>.arduboy`, both plain hex files and the FX
  data image for manual flashing (`fxdata-upload.py`/Ardens).

## Evidence ledger

- `output.md` is the per-bead ledger: exact commands, tails, numbers, and any
  documented deviation. Spike reports live under the temp dir / repo `build/`.
- Long builds should emit phase lines (start/finish of each suite) so progress
  is observable without polling.
