# monhun-ardu-hbk.9 — trim: drop hub strip, lean quest column, fixed-stride page table

Status: **DONE.** All gates green. Shipping flash freed **364 B**
(29506 → 29142, 190 → 554 free), above the ~350 B floor this bead owed the
MH-flow wave. No commit/push (orchestrator commits).

## What changed

1. **Hub bottom strip deleted end-to-end** (the ui.5.2/gs.2 chrome):
   - `data/screens/hub.json` — dropped `"strip": true`.
   - `tools/gen-screens.py` — dropped `SKILLS_REL`, `read_skill_abbrs()`, the
     `PREBAKE_LAYOUT` `strip_y`/`strip_slot_x0`/`strip_slot_w` keys, the strip
     draw in `screen_pages()` (signature is now `screen_pages(screen)`), and the
     `"strip"` key validation/`want_skills` load in `compile_model` /
     `normalize_screen`. Blob layout doc updated.
   - `src/screens.hpp` — deleted `drawHubStrip` + its call site, `SCREEN_WCLASS`,
     `SCREEN_READY`, `SCREEN_STRIP_Y`, `SCREEN_STRIP_SLOT_X0`,
     `SCREEN_STRIP_SLOT_W`, the now-unused `screenTextN()` helper, and the
     now-unused `#include "forge.hpp"` (its only use was the strip marker).
   - `tst/fxdatatest/hub_test.hpp` / `screens_test.hpp` — hub strip pixel pins
     removed; `hub_test.hpp` gains an explicit `#include "src/forge.hpp"` (it
     used to get `forgeReadNode` transitively through `screens.hpp`), and
     `screens_test.hpp` switches `forge_state.hpp` → `forge.hpp` for the same
     reason.
   - `tools/tests/test_gen_screens.py` — `test_strip_non_bool_rejected`,
     `test_strip_requires_skills_json`, `write_skills()`,
     `test_strip_bakes_skill_labels_at_fixed_slots` removed.

2. **Hub quest column leaned** (`drawHubQuestColumn`): only the active-quest
   progress pair `p/n`, right-aligned ending at `SCREEN_COST_RIGHT` (fxfontg /
   fxfontw-on-selected unchanged). The no-quest `-` branch, the `READY` branch
   and `SCREEN_READY` are gone; a save with no active quest draws nothing.
   `screens_test.hpp` pins updated (no `-`, no READY, met-progress still `p/n`).

3. **Fixed-stride page table** (hbk.9):
   - `tools/gen-screens.py` emits one `SCREEN_PAGE_STRIDE = 13`-byte slot per
     screen (`u8 pageCount` + 4 × `u24` addresses); `PAGE_MAX` 8 → 4 and the
     prebake row cap is validated against 24 rows (`exceed the 4 baked pages`).
   - `src/generated/screen_meta.hpp` gains
     `constexpr uint8_t SCREEN_PAGE_STRIDE = 13;` and
     `SCREEN_PAGE_MAX = 4`; the per-screen `SCREEN_*_PAGE_TABLE` constants are
     now `PAGE_TABLE_OFF + screen * SCREEN_PAGE_STRIDE` (829/842/855/868).
   - `src/screens.hpp` — `screenPageCount` / `screenPageAddr` index
     `PAGE_TABLE_OFF + screen * SCREEN_PAGE_STRIDE` directly; the variable-length
     `screenPageTableOff` walk is deleted.
   - `test_gen_screens.py` blob-layout/header pins updated (blob 863 → 881 B on
     the cart — FX cart data, not program flash), plus
     `screens_test.hpp` page-table pins now assert the stride formula.

## Command tails

```
$ make gen            # data changed
gen-screens: 4 screens, 49 rows, 10 pages, 881 B blob (magic 0x5343 version 1)
...
gen.sh: FX data + src/fxdata.h regenerated

$ make gen-check
fxdata_manifest: fxdata/manifest.json up to date (115 images, 71 inputs, 31 outputs)
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (159 generated artifacts unchanged)

$ make test
Total Passed: 6316
Total Failed: 0

$ make test-tools
Ran 354 tests in 20.801s
OK

$ FXTEST_ONLY=test_hub make fxtest-headless
test_hub PASSED=81 FAILED=0
test_hub: PASS

$ FXTEST_ONLY=test_screens make fxtest-headless
test_screens PASSED=184 FAILED=0
test_screens: PASS

$ FXTEST_ONLY=test_quests make fxtest-headless
test_quests PASSED=87 FAILED=0
test_quests: PASS

# extra (screens.hpp include churn touches these suites):
$ FXTEST_ONLY=test_cards make fxtest-headless   -> test_cards PASSED=85 FAILED=0
$ FXTEST_ONLY=test_forge make fxtest-headless   -> test_forge PASSED=58 FAILED=0
```

## Size line + delta

```
before (HEAD 64ebfe2): size: flash=29506/29696 (190 free)  ram=1798/2560
after  (make size-line): size: flash=29142/29696 (554 free)  ram=1798/2560
delta: -364 B flash, RAM unchanged
```

Delta (-364 B) ≥ the required ~350 B, so no extra cuts were needed. Slightly
better than the design's ~29130/565 estimate.

## Interfaces / types

- `screens::SCREEN_PAGE_STRIDE` (new, `= 13`) — fixed page-table slot size;
  `screens::SCREEN_PAGE_MAX` now `4`.
- `screens::SCREEN_*_PAGE_TABLE` now equals `PAGE_TABLE_OFF + screen * stride`.
- `screenPageTableOff()` removed; `screenPageCount()` / `screenPageAddr()` keep
  their signatures.
- `tools/gen-screens.py` `screen_pages(screen)` (was `screen_pages(screen,
  skill_abbrs)`); `"strip"` is no longer a valid screen JSON key.

## Regen cascade (stage generated artifacts together)

The page table grew 20 → 52 B (fixed stride), so `fxdata/tables/screens.bin`
is +18 B and every later image in the single FX image shifts +18 B. `make gen`
therefore also refreshes `fxdata/tables/{cards,equip}.bin`,
`src/generated/{equip,zone}_meta.hpp`, `fxdata/fxdata-data.bin`,
`fxdata/manifest.json`, etc. — all cascading absolute-offset updates, not
unrelated edits. `make gen` is byte-stable across consecutive runs and
`make gen-check` PASSes; stage the whole generated set with `git add -A`.

## Wall time

Worker (implement + full verify loop): ~30 min. Slowest step: AVR device
compiles for the touched suites (~6 sketch builds).
