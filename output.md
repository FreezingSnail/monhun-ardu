# monhun-ardu-mn6.1 — GEAR: armor equip rows (crafted gate, toggle)

Baseline HEAD `8f26685`, clean. **DONE — all gates green, no commit/push.**

Hub GEAR now lists the five crafted-gated armor rows beside the weapon rows;
A toggles a crafted piece into its slot (`armorEquipToggle`), and the change
persists. SMITH owns craft; GEAR owns equip/unequip.

## Size (final gate)

```
Sketch uses 28970 bytes (97%) of program storage space. Maximum is 29696 bytes.
Global variables use 1705 bytes (66%) of dynamic memory, leaving 855 bytes for local variables. Maximum is 2560 bytes.
size: .text=28952 .data=18 .bss=1687
size: flash=28970/29696 (726 free)  ram=1705/2560
size: data facts: HAS_CARVE:true HAS_ENRAGE:true HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false HAS_GUARD_FACING:true HAS_GUARD_HP:true HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true HAS_HIT_STAGGER:false HAS_MULTI_STEP:true HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false HAS_STAGGER:true HAS_STEP_AFTER:true HAS_STEP_CHANCE:true HAS_TURN_RATE:true HAS_WAIT_STEPS:true HAS_ZONES:true
```

| | baseline | mn6.1 | delta |
|---|---|---|---|
| flash | 28848/29696 (848 free) | **28970/29696 (726 free)** | **+122 B** |
| RAM | 1705/2560 | 1705/2560 | **0 B** |
| FX data (`fxdata/fxdata-data.bin`) | 200360 B | 200445 B | +85 B |

The +122 B MCU flash is the new `COND_CRAFTED`/`ACTION_EQUIP_ARMOR` switch arms
plus the `bool` return of `armorEquipToggle` (the compiler no longer elides the
now-observed result). The +85 B FX data is the 5 extra `screens.bin` rows; that
blob precedes the image sheets, so every later baked offset shifts +85, which
rewrites `equip_meta.hpp`/`zone_meta.hpp`/`manifest.json` with identical deltas.
`HAS_*` facts unchanged. Fits with 726 B free.

## What changed

- `tools/gen-screens.py` — appended `equip_armor` to `ACTION_NAMES` (id 11) and
  `crafted` to `COND_NAMES` (id 7), ids append-only so existing values stay
  stable; added the `condition 'crafted'` validation (needs an `equip_armor`
  action, slot 0..2).
- `src/armor_state.hpp` — `armorEquipToggle()` now returns `bool`: `false` for
  an out-of-range piece/slot or an uncrafted piece, `true` only when the slot
  byte actually changed.
- `src/screen_state.hpp` — `COND_CRAFTED` (piece = `screenArmorPiece(param)`,
  live when `saveCrafted`); `ACTION_EQUIP_ARMOR` (packs `(slot << 5) | pieceIdx`,
  returns `armorEquipToggle`). `ACTION_CRAFT_ARMOR` now accumulates the craft +
  toggle deltas through the bool (same observable behaviour).
- `data/screens/gear.json` — 5 `equip_armor`/`crafted` rows before LEAVE:
  HUNTER HELM (0), BONE CAP (1), HUNTER MAIL (34), BONE MAIL (35),
  EVADE CHARM (68); GEAR rows 4 → 9.
- Generated set (staged together by the orchestrator): `src/generated/screen_meta.hpp`,
  `fxdata/tables/screens.bin`, plus the +85-shifted `fxdata.bin`/`fxdata-data.bin`/
  `fxdata.h`/`src/fxdata.h`/`manifest.json`/`equip.bin`/`equip_meta.hpp`/`zone_meta.hpp`.
- Tests: `tst/screens_test.hpp` (new crafted-gate/toggle/bad-id block);
  `tst/armor_engine_test.hpp` (return-value assertions on the existing toggle
  test); `tst/fxdatatest/screens_test.hpp` (9-row cart read + crafted E2E equip
  + EEPROM persist + unequip).
- Docs: `README.md` (GEAR paragraph), `docs/quests-shops.md` (actions/conditions
  + gear bullet), `docs/equipment-framework.md` (new "Gear equip UI" note).

## Command tails

```
make gen
  gen-screens: 4 screens, 34 rows, 574 B blob (magic 0x5343 version 1)
  ... fxdata/fxdata.bin regenerated

make gen-check
  fxdata_manifest: PASS (87 generated artifacts unchanged)

make test
  Total Passed: 6225   Total Failed: 0

make test-tools
  Ran 310 tests in 18.674s   OK

make fxtest-headless (full, 16 suites)
  asset_test   PASSED=264 FAILED=0
  test_audio   PASSED=9   FAILED=0
  test_boot    PASSED=4   FAILED=0
  combat_test  PASSED=237 FAILED=0
  data_test    PASSED=348 FAILED=0
  test_hub     PASSED=81  FAILED=0
  test_hud     PASSED=29  FAILED=0
  test_items   PASSED=35  FAILED=0
  test_monster_art PASSED=127 FAILED=0
  perf_test    PASSED=5   FAILED=0
  test_player_art PASSED=120 FAILED=0
  test_quests  PASSED=87  FAILED=0
  test_screens PASSED=128 FAILED=0
  test_smith   PASSED=115 FAILED=0
  test_tell    PASSED=18  FAILED=0
  zones_test   PASSED=82  FAILED=0
```

## Deviations

- The bead names `tst/armor_test.hpp` for the `armorEquipToggle` return value,
  but that suite is the generated-data pin mirror (no save/armor_state context).
  The return-value assertions were added to `tst/armor_engine_test.hpp`, where
  the toggle test already lives and `armor_state.hpp` is in scope.
- `make gen-check` needed the second `make gen` pass (documented stale-blob
  two-pass: pass 1 bakes `equip.bin`/`equip_meta.hpp`/`zone_meta.hpp` offsets
  from the previous `fxdata.h`). It passes on the converged tree.
- `output.md` overwritten with this bead's report (was the isp.2 ledger).
