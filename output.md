# monhun-ardu-ljj.6 — Content: first framework creature (scope amended)

## Bead
`monhun-ardu-ljj.6` (epic `monhun-ardu-ljj`). Original scope: first breakable-part
creature + authoring tools. **Amended (owner decision, option A):** the
breakable-part framework costs +3.58 KB and does not fit the shipping image
(measured 32894 B / 29696 with parts on vs 29314 B without; `monsterOnHit`
808->2122, `initMonster` 604->1236, `combatPartRectRot` 606 new). Parts are
deferred to `monhun-ardu-ljj.8`; this bead ships the first framework creature
patterns-only plus the authoring tools.

## Shipped
- `data/creatures/ravager.json`: 260 HP / spd 6 creature on skeleton
  `quad_32x24`; attacks `bite` (lunge, 36/8/45) and `tail_sweep` (stationary,
  48/12/60, single wide window); ordered patterns `p_sweep` (maxDist 24) and
  `p_bite` (minDist 25). No parts, `staggerMax` 0 — every generic/parts path
  stays compiled out (data facts: HAS_PARTS/HAS_STAGGER/HAS_MULTI_* false).
- Menu: ravager is a selectable target (demo menu otherwise unchanged).
- `tools/contact_sheet.py`: JSON -> PNG contact sheet of attack windows for
  authoring review (`make test-tools` renders a sample; output under `build/`).
- `tools/gen-combat.py`: new `HAS_HIT_STAGGER` data fact; ravager trimmed to
  single-window/single-step data so unused machinery folds away.
- Deferred with the parts bead: `fxtail` art sheet, tail overlay draw in
  `render.hpp` (comment placeholders left), part/stage tests.

## Gates (all re-run at this tree)
```
make gen (x2)      deterministic; fxdata manifest PASS (34 artifacts)
make test-tools    OK (49 tests)
make gen-check     PASS
make test          3117 passed / 0 failed
make build         Sketch uses 29400 bytes (99%)   [296 B headroom]
make fxtest-headless (all PASS):
  assets 254/0  audio 14/0  boot 4/0  combat 195/0  data 221/0
  hud 17/0  menu 59/0  parity 660/0  perf 5/0
  B pUs=6384 pHz=156 lHz=52 lTk=984 rMx=5040 rAv=4800 ram=408
  (gates: rMx<=7407, pHz>=135, lHz>=45, ram>=300)
node tools/gen-parity-fixtures.js && git diff tst/fxdatatest/parity_fixtures.hpp
                    -> empty diff (shipped 3 byte-identical)
```

## Notes
- `test_perf` sketch initially 29700/29696 (4 B over); `hit()`/`avg()` marked
  `noinline` in `tst/fxdatatest/perf_test.hpp` -> 29474 (222 B free), bench
  semantics unchanged.
- `CombatState` assert now folds with the data facts
  (`HAS_PARTS ? 78 : 64`).
- Parts follow-up evidence + work list recorded in `monhun-ardu-ljj.8`.
