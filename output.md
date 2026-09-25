# monhun-ardu-4dm — progmem read-helper shim → macros

## Status: DONE

## Change
`src/core/progmem.hpp` only (+16 / -32, 1 file changed).

- Replaced the five `mhPgmRead*` inline functions with macros.
  - AVR: direct `pgm_read_byte/word/dword` with the same casts as before
    (`I8` → `reinterpret_cast<const uint8_t*>` then `int8_t`; `I16` →
    `reinterpret_cast<const uint16_t*>` then `int16_t`).
  - Host: `(*(p))` identity deref for all five.
- Dropped the stale `MH_NOINLINE` on `mhPgmReadU8` and its "-4 B whole-image"
  comment; rewrote the readers block comment to explain the macro form and the
  single-evaluation safety (call sites pass a plain `&table[i].field` lvalue).
- `MH_PROGMEM`, `MH_NOINLINE` (still used by ~42 other helpers) and the file's
  purpose untouched. No other file modified.

## Numbers (make size)
```
size: .text=29376 .data=50 .bss=1764
size: flash=29426/29696 (270 free)  ram=1814/2560
```
Baseline HEAD (spike): `.text=29420 .data=50 .bss=1764` /
`flash=29470/29696 (226 free) ram=1814/2560`.
Delta: **flash −44 B** (29470 → 29426), RAM unchanged 1814. Matches the spiked
29426 exactly. `make size-line` reports `flash=29426/29696 (270 free)  ram=1814/2560`.

## Gates
| command | result |
|---|---|
| `make size` | flash 29426 ≤ 29430 ✔, ram 1814 (no growth) ✔ |
| `make size-line` | `flash=29426/29696 (270 free)  ram=1814/2560` ✔ |
| `make test` | `Total Passed: 6815  Total Failed: 0` ✔ (host deref macros) |
| `make test-tools` | `Ran 373 tests ... OK` ✔ |
| `FXTEST_ONLY=test_data make fxtest-headless` | `data_test PASSED=354 FAILED=0` / `test_data: PASS` ✔ |
| `FXTEST_ONLY=test_hud make fxtest-headless` | `test_hud PASSED=29 FAILED=0` / `test_hud: PASS` ✔ |
| `FXTEST_ONLY=test_items make fxtest-headless` | `test_items PASSED=35 FAILED=0` / `test_items: PASS` ✔ |
| `FXTEST_ONLY=test_combat make fxtest-headless` | `combat_test PASSED=252 FAILED=0` / `test_combat: PASS` ✔ |
| `FXTEST_ONLY=test_zones make fxtest-headless` | `zones_test PASSED=82 FAILED=0` / `test_zones: PASS` ✔ |
| `FXTEST_ONLY=test_screens make fxtest-headless` | `test_screens PASSED=212 FAILED=0` / `test_screens: PASS` ✔ |
| `make gen-check` | not run — no fxdata/ or src/generated/ files dirty |

### Verification-command discrepancy (non-blocking)
The bead's command list names `test_sin` and `test_save` (and the acceptance
names `test_carve`); **no such suites exist** under `tst/fxdatatest/`
(`test_*.ino` list has no `test_sin`/`test_save`/`test_carve`). Those
`FXTEST_ONLY=` runs filtered to an empty suite and no-op'd
(`make[2]: Nothing to be done for 'fxtest-build'`, exit 0 — not a real pass).
Substituted the suites that actually exist and exercise the changed readers:
`test_combat` (sin256/DIR8 movement math), `test_items`, `test_zones`,
`test_screens`, plus `test_data`/`test_hud` from the bead. `dir8X/Y`
(`mhPgmReadI16`) and render masks (`mhPgmReadU8`) are exercised across these.

## Danger / correctness notes
- Macros consume the argument exactly once; every call site in `src/` passes a
  single `&table[i].field` lvalue (some wrap it in `reinterpret_cast<...>`, one
  uses `off + i` in the index) — no comma-expressions, no side-effecting
  arguments, so no multiple-evaluation hazard.
- `reinterpret_cast` inside a macro argument is fine at all call sites.

## Wall time
~5 min worker (reads/edit + `make size`/`size-line`/`test`/`test-tools` + 6
device suites).

## Commit
Not committed — orchestrator owns the commit.
