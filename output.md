# monhun-ardu-6zb.7 — poles: BREAK/CRACK visual indicators (emblems, damage stage, detached-piece broken art)

Status: DONE. No commit/push. Parity fixture regen empty diff; no float; no /tmp.

## What changed

### Art (`tools/gen-art.py`)
- Variant sheets are now **6 frames in stage*2 + flash order**: intact, intact-flash,
  damaged, damaged-flash, broken, broken-flash. `pole_variant_frames()` iterates
  `(stage, flash)`; PLAIN keeps the legacy 2-frame `pole_frame`.
- Bold BLACK weapon emblems on the LIGHT head block (~8x8):
  - SEVER: 3px-wide diagonal blade + crossguard.
  - BREAK: spiked mace ball + short handle.
  - CRACK: gun target (concentric ring + centre dot + top tick).
- Damaged stage: emblem chipped (blade gap / spike+ball chip / ring corner chip)
  plus 1–3 crack lines on the breakable part.
- Broken stage: severed piece on the ground in the 4 padding rows below the
  36-tall art (rows 36..39):
  - SEVER: stepped slanted stump + top block on the ground.
  - BREAK: jagged sheared arm stub + arm (shaft + hammer head) on the ground.
  - CRACK: split band with a DARK jagged gap + band chunk on the ground.
- `make gen` moved the FX cart data 159977 -> 164057 B (+4080 B, cart only).

### Engine
- `CombatZoneCache` gains `uint8_t hpMax` (`src/core/game.hpp`); `combatZoneSeed`
  fills `c.hpMax = z.hp` (`src/core/combat.hpp`), so both zone slots cache the
  pool at load. No per-tick cart reads added. AVR asserts updated:
  `sizeof(CombatZoneCache) == 11`, `sizeof(CombatState) == 82`.
- Pure stage math in `src/core/projectiles.hpp`:
  `poleDamageStage(broken, hp, hpMax)` = 2 broken / 1 damaged (`hp*2 <= hpMax`) / 0,
  and `poleStageFrame(...)` = `stage*2 + flash`.
- `src/render.hpp` `poleSheetFrame(sheet, broken, hp, hpMax, flash)`; `drawPole`
  picks the breakable zone (appendage if `hpMax != 0`, else head) and reads the
  cached `hp`/`hpMax` + `zoneBroken` bit. PLAIN (`SHEET_POLE`) keeps normal/flash.
  New `POLE_DAMAGED`(2)/`POLE_DAMAGED_FLASH`(3)/`POLE_BROKEN`(4)/`POLE_BROKEN_FLASH`(5).

### Mock (`mock/game.js`)
- `poleStage(pole, def)` mirrors the stage math; `drawPole` mirrors the shapes,
  damaged chips/cracks and the ground pieces; PLAIN keeps the legacy draw.
- Exported `poleStage`.

### Tests (permanent)
- `tst/art_dims_test.hpp`: variant sheets 6 frames; every stage/flash frame
  distinct; emblem ink on the head block (frames 0/2); ground-piece ink only in
  frames 4/5; SEVER broken head gone, BREAK/CRACK head kept; BREAK arm ink.
- `tst/shells_test.hpp`: `hpMax` cached for every pole zone; new render-frame
  test (stage 0/1/2 from hp/hpMax + broken, flash adds 1; PLAIN hpMax 0).
- `tst/combat_test.hpp`: loader caches `hpMax` for both lunge zones.
- `tst/fxdatatest/asset_test.hpp`: pole variant frame count pinned device-side
  from the packed blob span (sever/break/crack -> 6 frames).
- `mock/game.test.js`: damage threshold + broken stage after a real break hit.

## Verification (exact tails)

1. `make gen` x2 then `make gen-check` -> exit 0, empty regen diff:
```
fxdata_manifest: PASS (67 generated artifacts unchanged)
gen-check exit=0
```

2. `make test` -> 0 failed:
```
Total Passed: 4629
Total Failed: 0
```

3. `make test-tools` -> 0 failed:
```
Ran 141 tests in 7.442s
OK
```

4. `node --test mock/game.test.js` -> 0 fail:
```
tests 35
pass 35
fail 0
```

5. Targeted device suites (`make fxtest-headless FXTEST_ONLY=...`):
```
test_parity   parity_test PASSED=660 FAILED=0
test_assets   asset_test PASSED=259 FAILED=0
test_menu_art test_menu_art PASSED=81 FAILED=0
test_combat   combat_test PASSED=233 FAILED=0
test_data     data_test PASSED=221 FAILED=0
```

6. Parity fixture regen: `node tools/gen-parity-fixtures.js` -> 0 diff lines in
   `tst/fxdatatest/parity_fixtures.hpp`.

7. `make size`:
```
before (HEAD df5cd75): flash=25234/29696 (4462 free)
after:                 .text=25250 .data=40 .bss=1704
                       flash=25290/29696 (4406 free)  ram=1744/2560
```
Flash delta +56 B (< 60 B investigate threshold). RAM delta +2 B (the two u8
`hpMax` fields), matching design.

## Generated set (staged together by make gen)
`fxdata/blocks/Sprites.txt`, `fxdata/fxdata.bin`, `fxdata/fxdata-data.bin`,
`fxdata/fxdata.h`, `fxdata/manifest.json`, `fxdata/tables/equip.bin`,
`src/fxdata.h`, `src/generated/equip_meta.hpp`, and the three
`images/blocks/fxpole_{sever,break,crack}_*.png`.
