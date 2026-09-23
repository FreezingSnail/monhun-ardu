# monhun-ardu-isp.2 — hml.2 art: remove the opening-menu sheets + raster suite (reclaim)

Baseline HEAD `564e358` (isp.3), clean. **DONE — all gates green, no commit/push.**

## Size (final gate)

```
Sketch uses 28848 bytes (97%) of program storage space. Maximum is 29696 bytes.
Global variables use 1705 bytes (66%) of dynamic memory, leaving 855 bytes for local variables. Maximum is 2560 bytes.
size: .text=28830 .data=18 .bss=1687
size: flash=28848/29696 (848 free)  ram=1705/2560
size: data facts: HAS_CARVE:true HAS_ENRAGE:true HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false HAS_GUARD_FACING:true HAS_GUARD_HP:true HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true HAS_HIT_STAGGER:false HAS_MULTI_STEP:true HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false HAS_STAGGER:true HAS_STEP_AFTER:true HAS_STEP_CHANCE:true HAS_TURN_RATE:true HAS_WAIT_STEPS:true HAS_ZONES:true
```

| | baseline | isp.2 | delta |
|---|---|---|---|
| flash | 28848 (848 free) | 28848 (848 free) | **0 B** |
| RAM | 1705 (855 free) | 1705 (855 free) | **0 B** |
| FX data (`fxdata/fxdata-data.bin`) | 208622 B | **200360 B** | **−8262 B** |

The reclaim is entirely cart-side: the three `mh_menu_*` sheets lived in the FX
image, so MCU flash/RAM are unchanged. No `HAS_*` data fact flipped.

## What changed

- `tools/gen-art.py` — deleted the whole opening-menu section:
  `MENU_ELEMENTS`/`MENU_WEAPONS`/`MENU_TARGETS`, the `MENU_*` tile/geometry/
  cursor constants, `frame_blocks`, `menu_weapon_cell`, `menu_target_cell`,
  `menu_defs`, `check_menu_identity`, the `menu_*` cases in `sheet_filename`/
  `sheet_kind`, the `menu` sheet emission + `menu` return from `render_all`, the
  `images/menu` dir/`clean_stale`/`check_menu_identity` wiring in `main`, and the
  `menu` branches in the docstring/`ascii_dump`. `text_blocks` is kept (still
  used by `hud_defs`).
- `tools/gen.sh` — dropped the `fxdata/menu` mkdir/rm and the
  `convert-sprite.py ../images/menu` call.
- `tools/fxdata_manifest.py` — dropped `images/menu/*.png` and
  `fxdata/menu/Sprites.txt` from `GENERATED_GLOBS`.
- `fxdata/fxdata.txt` — removed `include "menu/Sprites.txt"`; comment updated.
- `fxdata/menu/Sprites.txt` + `images/menu/` (3 PNGs) — **deleted**.
- `tst/fxdatatest/asset_test.hpp` — removed the 3 `mh_menu_*` `blobHeader`
  asserts (−6 device asserts: assets 270 → 264).
- Docs rewritten as removed: `README.md` (status snapshot + blobs list + flash/
  RAM history), `docs/equipment-framework.md` (naming example now
  `mh_head_helm_16x16.png`), `docs/feel-design.md` (prg.8 table note).

## Regen note (stale-blob two-pass)

`make gen` needs **two consecutive runs**: pass 1 bakes `equip.bin`/
`equip_meta.hpp`/`zone_meta.hpp` sheet offsets from the *previous* `fxdata.h`,
so pass 1 differs from pass 2; pass 3 == pass 2. `make gen-check` snapshots the
converged tree and re-runs the pipeline once, so it passes only after the second
`make gen`. No stale-blob static_assert actually tripped (device tests compile
the asserts and pass).

## Command tails

```
make gen-check
  fxdata_manifest: PASS (87 generated artifacts unchanged)

make test
  Total Passed: 6207   Total Failed: 0

make test-tools
  Ran 310 tests in 18.005s   OK

make fxtest-headless
  test_assets PASSED=264 FAILED=0
  test_audio PASSED=9 FAILED=0
  test_boot PASSED=4 FAILED=0
  test_combat PASSED=237 FAILED=0
  test_data PASSED=348 FAILED=0
  test_hub PASSED=81 FAILED=0
  test_hud PASSED=29 FAILED=0
  test_items PASSED=35 FAILED=0
  test_monster_art PASSED=127 FAILED=0
  test_perf PASSED=5 FAILED=0
  test_player_art PASSED=120 FAILED=0
  test_quests PASSED=87 FAILED=0
  test_screens PASSED=110 FAILED=0
  test_smith PASSED=115 FAILED=0
  test_tell PASSED=18 FAILED=0
  test_zones PASSED=82 FAILED=0
  (16 suites / 1671 asserts, all PASS)
```

## Deviations

- The bead references `tools/tests/test_gen_art.py` for menu-identity cases;
  that file does not exist in the tree (no `test_gen_art.py`; the only
  `gen-art` test references are in `test_gen_equipment.py`, which never touched
  the menu). Nothing to drop there.
- The "raster suite" (`test_menu_art.hpp`/`test_menu.ino`) was already deleted
  by `isp.1`; only the `mh_menu_*` asset asserts + sheets remained, and are
  removed here.
- `output.md` overwritten with this bead's report (was the isp.3 ledger).
