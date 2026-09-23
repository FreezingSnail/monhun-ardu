# AGENTS.md — monhun-ardu

Monster-Hunter-style duel for **Arduboy FX** (ATmega32u4, 4-shade grayscale via
ArduboyG `L4_Triplane`). Read `README.md` for architecture/status and
`docs/dev-flow.md` for the workflow conventions distilled from past waves.

## Source of truth

- The device code in `src/` is the source of truth. `mock/` (`game.js` +
  `index.html`) is a legacy prototype: do not update it, and do not maintain the
  mock/device parity suite or regenerate `tst/fxdatatest/parity_fixtures.hpp`.
  The parity image stays runnable as legacy diagnostics only — it is not a gate.
- Generated data is produced from JSON/PNG sources; never hand-edit generated
  files. `tools/gen.sh` (`make gen`) is the only generation entry.

## Commands

```sh
make test                  # host C++ suites (tst/, run from repo root)
make fxtest-headless       # Ardens device suites; FXTEST_ONLY=<name> for one
                           #   FXTEST_JOBS=N parallel compiles (default 4)
make gen-check             # regen determinism + generated header sync
make test-tools            # Python tooling unittests (tools/tests/)
make size                  # ELF size + headroom + compile-time data facts
make size-line             # just the flash/RAM line (script/checkpoint friendly)
make build | mini | debug  # shipping / Arduboy Mini / Ardens debugger
make gen | format | hooks
```

Full gate before any commit: `make gen-check`, `make test`,
`make fxtest-headless` (all suites), `make size`.
Iterate with `FXTEST_ONLY=test_<name>`; only the final run is the full gate.

## Hard rules

- **Testing**: use each language's native framework; tests are permanent,
  co-located with the code under test (`tst/`, `tst/fxdatatest/`,
  `tools/tests/`); never write test code to `/tmp`; never use Python/perl/ruby
  as a harness to drive C++/JS tests.
- **Fixed point**: no `float`/`double` in core/device code (FP=16, DIR8, integer
  math only). Damage/percent math truncates at each step.
- **FX/OLED share SPI**: cart reads happen only in `run()`/`render()` between
  plane blits, never during the ArduboyG paint.
- **Budget first**: LTO makes per-symbol size math meaningless — measure
  whole-image deltas (`make size`). Spike before any flash/perf-touching bead;
  split "implement" from "make it fit". `HAS_*` data facts in
  `src/generated/combat_meta.hpp` gate optional combat machinery — adding data
  that flips one is a budget event.
- **Generated sets**: stage them together after a regen (`git add -A`);
  `gen-check` asserts `src/fxdata.h == fxdata/fxdata.h`. Tests must reference
  generated symbolic constants, never literal record indices.
- **Single FX image**: `fxdata/fxdata.bin` is the only flashable cart image;
  table blobs are sections inside it.
- **Review**: render/data diffs get the checklist in `docs/dev-flow.md`
  (state shades, exact hitbox sizes vs sim dims, anchors, shade-0 erase,
  telegraph == hit-test window).

## Dev-cycle speed rules (retro 2026-09, ui-v2 wave)

- **Walking-skeleton spike.** Any UI/data-model wave gets one spike that
  implements the thinnest *end-to-end* slice (not one component) and reports the
  whole-image delta with `make size`. Budget the wave from that number. Component
  spikes undershoot 4–6× (the ui-v2 card blit measured +284 B; the shipped
  pipeline cost ~1030 B).
- **Small beads, checkpoints inside.** Target <= ~300 B whole-image delta per
  bead. Run `make size-line` after each layer (data -> runtime -> UI) and stop
  for a trim when the budget is blown — do not finish the feature first.
- **Headroom reserve.** Plan a wave against `free - 300 B`; never land below
  ~150 B free. The ui-v2 wave hit 16 B free mid-flight and needed two extra trim
  beads.
- **One full gate per bead.** The worker runs `make test`, `make test-tools`,
  `FXTEST_ONLY=<touched suites> make fxtest-headless`, `make size-line`; the
  orchestrator runs the full 18-suite gate once before committing. Do not run
  the full device gate twice per bead.
- **Freeze interaction details before dispatch.** Tokens, save caps, labels and
  row models are pinned in the design doc (`bd show`/`--design`); workers may
  not reinterpret them. The ui-v2 token column was built then folded (~426 B
  wasted).
- **Parallelize non-overlapping beads.** Docs/art/tooling beads can run
  alongside code beads in separate workers; only same-file work serializes.
- **Split oversized beads** before dispatch (2–3 logical layers each): smaller
  beads = fewer BLOCKED walls and cheaper resumes after a cancelled worker.
- **Log wall time** per bead (worker / gate / orchestrator) in `output.md` so
  the next retro is data.

## Worker protocol (agents spawned for bd tasks)

- One bead = one implement-verify loop; read `bd show <id>`, implement, run the
  bead's exact verification commands, write the report to `output.md`, then
  `bd close <id>`.
- **Do not commit or push** — the orchestrator commits between bead waves and
  runs the full gate. The worker's own verification is the host/tools suites,
  the *touched* device suites (`FXTEST_ONLY=...`), `make gen-check` when data
  changed, and `make size-line` (see "Dev-cycle speed rules").
- Report exact numbers and tails; if the build cannot fit or a gate fails,
  report BLOCKED with the deficit and options instead of faking a pass.
