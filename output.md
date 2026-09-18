# monhun-ardu-cgz — qs.1 screen framework + EEPROM save (spike)

Status: **DONE** (all gates green; no commit per worker protocol).
Design: `docs/quests-shops.md` sections "Screen data" / "Save block" / "Actions".

The generic list-screen framework is in: cart `ScreenDef`/`ScreenRow` tables +
packer, one generic renderer (glyph-lane labels, chip cursor, right-aligned
cost, 6 rows/page, scroll by 6), debounced up/down + A/B input, the fixed
action switch, the EEPROM save block (load/verify/write-on-change/corrupt
fallback) and a hub stub screen reachable from the opening title menu (B) and
returnable from (B or a LEAVE row). Cart content is one demo screen
(`data/screens/hub.json`, 3 rows) proving the pipeline. Budget **over**: see
below.

## Files

New:
- `tools/gen-screens.py` — `data/screens/*.json` -> `src/generated/screen_meta.hpp`
  + `fxdata/tables/screens.bin` (schema-validated, deterministic, `--dump`).
- `src/core/save.hpp` — host-testable `SaveBlock` (magic/version/zenny/quest
  bits/tier/checksum), `saveLoad`/`saveStore`, AVR `EEPROM.update` backend at
  `SAVE_EEPROM_ADDR = 16` (`EEPROM_STORAGE_SPACE_START`).
- `src/screen_state.hpp` — host-testable `ScreenState`, `screenStep` (debounced
  nav + edges), `screenCondOk`, `screenApplyAction`.
- `src/screens.hpp` — device-only cart readers + `drawScreen` (like `render.hpp`).
- `data/screens/hub.json`, `tst/screens_test.hpp` (host, 86 asserts),
  `tst/fxdatatest/screens_test.hpp` + `test_screens.ino` (device, 56 asserts),
  `tools/tests/test_gen_screens.py` (20 tests) + fixture.
- `src/generated/screen_meta.hpp`, `fxdata/tables/screens.bin` (generated).

Changed:
- `src/menu_state.hpp` — added `MENU_SCREEN` (B rising edge on the title menu).
- `monhun-ardu.ino` — `s_save`/`s_screen`, boot `saveLoad`, run/render routing.
- `fxdata/fxdata.txt` — `raw_t mhScreens = "tables/screens.bin"` in the table
  section (before sprites, inside the <64 KiB fxmem window).
- `tools/gen.sh`, `tools/fxdata_manifest.py` (OUTPUT_PATHS), manifest fixture
  and its 2 count assertions, `tst/main.cpp`.
- Generated set re-baked: adding `mhScreens` shifts the FX image, so
  `equip.bin`/`equip_meta.hpp`/`fxdata.h` were regenerated (two-pass note);
  `make gen-check` confirms they are stable.

## Verification (exact)

1. `make gen` (run repeatedly) + `make gen-check`:
   `fxdata_manifest: PASS (55 generated artifacts unchanged)`; `cmp` header copy
   check passed.
2. `make test`: `Total Passed: 3230 / Total Failed: 0`.
   Baseline per bead = 3144; the new host suite adds exactly 86.
   `make test-tools`: `Ran 101 tests ... OK` (20 new gen_screens).
3. `make fxtest-headless` (full): all 11 suites PASS —
   assets 262, audio 14, boot 4, combat 184, data 221, hud 17, menu 59,
   parity 660, perf 5, player_art 111, **screens 56**; 0 failures.
   Perf identical to the baseline:
   `B pUs=6375 pHz=156 lHz=52 lTk=488 rMx=4968 rAv=4756 ram=611`
   (baseline `rMx=4968 rAv=4756 pUs=6375`; screens never render during a hunt).
4. `make build` + `make size`:
   `Sketch uses 25250 bytes (85%)` / `Global variables use 1847 bytes`.
   `size: .text=25192 .data=58 .bss=1789`
   `size: flash=25250/29696 (4446 free)  ram=1847/2560`.
5. `node tools/gen-parity-fixtures.js` -> `git diff --stat --
   tst/fxdatatest/parity_fixtures.hpp` empty (byte-identical).

## Budget (measured, whole-image)

| | flash | RAM |
|---|---|---|
| baseline (HEAD ec5d1d6) | 23342 | 1829 |
| qs.1 spike | **25250** | **1847** |
| delta | **+1908** | **+18** |

At 25250/29696 there are still 4446 B free; the hunt perf path is untouched.
Attribution (whole-image builds with a temporary gate, removed before commit):
`drawScreen` ~0.4 KB; EEPROM `save.hpp` + `eeprom_read/write_byte` ~0.45 KB;
cart readers + `screenStep`/conditions/action switch ~1.05 KB. Trimming the
initial hide-locked/mask design saved 474 B (25724 -> 25250).

Options to reach ~1 KB (or to decide the budget):
1. Split the EEPROM save into its own bead now (~-0.45 KB) and ship the screen
   framework only; save lands with qs.2/qs.3.
2. Compile the framework behind a build flag until qs.4 integration
   (zero shipping cost today, no data/ABI loss).
3. Fixed-stride row records (22 B: label[16] + 6 fields) to drop the
   variable-length walk (`screenRowNext`/`screenRowOffsetAt`), est. -0.15–0.25 KB
   at the cost of a larger cart blob.
4. Cache the visible page (title + 6 labels + costs) in a small RAM struct on
   entry; removes repeated cart walks from `drawScreen`, est. -0.2 KB flash,
   +~50 B RAM.
5. Accept +1.9 KB for the spike (still 4.4 KB headroom; no `HAS_*` data fact
   flipped, no gameplay/parity change).

## Deviations / notes

- Title->screen entry uses the menu's **B edge** (`MENU_SCREEN`); the full
  HUNT/QUESTS/SMITH hub menu is qs.4. A still starts the hunt. One demo screen
  with 3 fake rows as the bead allows.
- Conditions gate the row action (A on `zenny`/`flag`/`tier`-locked rows is a
  no-op). The packed `flags u8` is emitted/reserved; the hide-locked visibility
  mask was cut to fit better (host/device suites cover condition + action
  gating instead).
- `make test` baseline in the dispatch was 3144; actual is 3230/0 (+86 new).
- No `/tmp` test code; tests are co-located (`tst/`, `tst/fxdatatest/`,
  `tools/tests/`) and permanent. Generated set must be staged together.
