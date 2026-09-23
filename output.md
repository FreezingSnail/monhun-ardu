# monhun-ardu-mn6.2 — gs.2 GEAR: active skill readout (points + S/M tier)

Status: DONE (build + all gates green). No commit/push (orchestrator commits).

## What changed

- `tools/gen-screens.py` — added `ROW_FLAGS["skill"] = 0x04` (existing values
  stable); comment documents the readout semantics.
- `data/screens/gear.json` — five `flags: ["skill"]` rows before LEAVE:
  ATTACK UP / DEFENSE UP / HEALTH UP / STAMINA UP / EVADE, `action: none`,
  `condition: always`, `param` 0..4 (armor::SKILL_* index). GEAR is now 14 rows
  (3 pages).
- `src/screen_state.hpp` — `ScreenState` gains
  `uint8_t skillPoints[armor::SKILL_COUNT]` + `uint8_t skillTier[armor::SKILL_COUNT]`;
  `screenReset()` zeroes them. Explicit `generated/armor_meta.hpp` include.
- `src/screens.hpp` — `drawScreen()` decodes `ROW_F_SKILL` rows: cached points
  right-aligned in the cost column, plus an `S` (tier 1) / `M` (tier 2) letter
  8 px left of the number via `textPut` on the selected/unselected font sheet.
  A bad `param` clamps to skill 0 (no cache overrun). ROW_F_ZENNY path and the
  `(const ScreenState&, const SaveBlock&)` signature are unchanged. New device
  helper `screenGearCache(ScreenState&, const ArmorAgg&)`.
- `monhun-ardu.ino` — `refreshGearReadout()` (`armorApplyToGame` then
  `screenGearCache`) called when a nav lands on GEAR and after every GEAR
  screen action, so equip/unequip moves the numbers on the next frame.
- Tests (permanent, co-located, native):
  - host `tst/screens_test.hpp` — new case "screenReset zeroes the gear skill
    cache; ROW_F_SKILL decodes" (flag values 0x01/0x02/0x04, cache zeroed,
    skill param decode).
  - device `tst/fxdatatest/screens_test.hpp` — GEAR row count 9 -> 14 + g8..g13
    decode; cart armor -> cache fill (attack 12/S, health 4/inert); equip via
    the cart gear row moves attack 6 -> 12 (crosses S); helm+mail+charm clamps
    attack to 15/M; pixel smoke for a skill row (M letter at x108..111, points
    at x112..123) and an inert skill (points only, no letter).
- Docs: `README.md` + `docs/equipment-framework.md` (readout: points clamp at
  15, S=10 / M=15 letters, no equipped marker yet).

## Gate evidence

```
make gen (two passes; stale-blob convergence) + make gen-check
  fxdata_manifest: PASS (87 generated artifacts unchanged)
  gen-check: OK (fxdata/fxdata.h == src/fxdata.h)

make test
  Total Passed: 6240   Total Failed: 0

make test-tools
  Ran 310 tests in 18.406s   OK

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
  test_screens PASSED=154 FAILED=0
  test_smith   PASSED=115 FAILED=0
  test_tell    PASSED=18  FAILED=0
  zones_test   PASSED=82  FAILED=0

make size
  size: .text=29186 .data=18 .bss=1697
  size: flash=29204/29696 (492 free)  ram=1715/2560
```

## Size delta

- Shipping flash: **29204 / 29696 (492 free)** — baseline 28970 (726 free) ->
  **+234 bytes**.
- RAM: 1715 / 2560 — baseline 1705 -> **+10 bytes** (the two 5-byte caches).
- Note: the binding constraint was the `test_hub` device sketch, not shipping.
  The first `drawScreen` cut pushed `test_hub` to 29704 (over by 8); the shared
  digits/drawNumber refactor dropped it to 29638 (58 free) and shipping to 492
  free.

## Deviations

- `make gen` is a documented two-pass tool: pass 1 bakes `equip_meta.hpp` /
  `fxdata.h` offsets before the cart is repacked, so the equip sheet
  static_asserts (and thus the fxtest build) only settle after a second `make
  gen`; `gen-check` then reports 87 artifacts unchanged on the converged tree.
- `output.md` was the previous bead's ledger; overwritten with this report.
