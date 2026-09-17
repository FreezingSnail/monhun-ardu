# monhun-ardu-ljj.7 — Gate: creature framework v1 (verified)

Independent re-run at HEAD `bff9c1b` (epic `monhun-ardu-ljj`). Every command
re-executed fresh; no reliance on child reports.

```
git status / log        clean at bff9c1b
make gen (x2)           deterministic (tree clean after regen)
make gen-check          PASS (34 generated artifacts unchanged)
make test-tools         OK (49 tests)
make test               3117 passed / 0 failed
make build              Sketch uses 29400 bytes (99%)
                        Global variables use 2018 bytes (78%), 542 free
make fxtest-headless    boot 4/0  assets 254/0  audio 14/0  combat 195/0
                        data 221/0  hud 17/0  menu 59/0  parity 660/0
                        perf 5/0 — all P
                        B pUs=6384 pHz=156 lHz=52 lTk=984 rMx=5040 rAv=4800 ram=408
                        (gates: rMx<=7407, pHz>=135, lHz>=45, ram>=300)
node gen-parity-fixtures + git diff parity_fixtures.hpp -> empty
```

Single-image invariant: `fxdata/fxdata.bin` is the only FX image; the combat
blob is a section inside it (`mhCombat = 0x00528E`, `FX_DATA_BYTES = 21768`);
`dist/` holds only the sketch build outputs.

Interpreter evidence: `mh::patternStepsSingle` present; shipped 3 creatures run
the data-driven profile/pattern path with parity fixtures byte-identical; the
ravager ships patterns-only (HAS_PARTS/HAS_STAGGER/HAS_MULTI_* false), so the
generic machinery is compiled out.

Deviations (documented, non-failing):
- ljj.6 scope amendment (owner option A): breakable parts deferred to
  `monhun-ardu-ljj.8` (+3.58 KB measured, headroom was 382 B). Tail art,
  part/stage tests removed with it.
- test_perf sketch headroom restored via noinline in the bench helpers
  (29474 B, 222 free).
- Read-count budget from spike 1b holds: steady-state 0 cart reads/tick;
  device combat_test asserts the cache model.

Open follow-ups: `ljj.8` (parts budget), `7y3` closed, `vx2` (human art),
`1to` (human tuning), `qyb` (save). README status rows updated to the numbers
above.
