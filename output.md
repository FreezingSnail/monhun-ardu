# monhun-ardu-arm.2 — armor engine: slots, aggregation, equip UI, paper-doll

Status: DONE. Shipping flash +896 B (29170/29696, 526 free), RAM +21 B
(1631/2560). Repo left dirty on purpose (orchestrator commits); no commit/push.

## What changed

- `src/armor_state.hpp` (new, host-testable): `ArmorPiece` view,
  `ArmorAgg { defense u16, resist[4] i16, points[5], tier[5] }`, crafted/equip
  helpers, `armorAggregate()` (sums defense/resist/skill points, clamps to
  `THRESHOLD_M`, resolves tier 0/1/2), and `armorEquipToggle()` (refuses an
  uncrafted piece, replaces within its own slot).
- `src/armor.hpp` (new, device-only): reads the packed `mhArmor` records via
  `core/fxmem.hpp` into `ArmorPiece` and caches the equipped set into
  `Game::armor` + `Game::armorHead` (`armorApplyToGame`), ignoring out-of-range
  or wrong-slot ids.
- `src/core/save.hpp`: crafted bitmask reuses the existing `flags` byte (bits
  1..7 = pieces 0..6; bit 0 stays `SAVE_FLAG_SMITHY_SEEN`) — no layout/version
  change, so an older record migrates with no crafted bits. Added
  `saveCrafted`/`saveSetCrafted` + `SAVE_CRAFTED_BIT_BASE/MAX`.
- `src/core/game.hpp` / `src/core/player.hpp`: `Game::armor`/`Game::armorHead`
  appended last; `initGame` zeroes them (default Game keeps base head/body).
- `src/screen_state.hpp`: `COND_ARMOR` + `ACTION_CRAFT_ARMOR`. `param` packs
  `(slot << 5) | pieceIdx`. A is one verb: uncrafted -> debit bill + zenny, set
  crafted bit, auto-equip; crafted -> toggle equip/unequip. One return = one
  `saveStore`.
- `src/screens.hpp`: `screenRowArmorRecipe()` resolves the row's cost + material
  bill from the `mhSmith` armor recipe record (single source of truth); used by
  `screenReadRow` for `COND_ARMOR` rows.
- `data/screens/smith.json`: 5 armor rows (`HUNTER HELM`, `BONE CAP`,
  `HUNTER MAIL`, `BONE MAIL`, `EVADE CHARM`; params 0/1/34/35/68) before LEAVE.
  Smith rows 7 -> 12.
- `tools/gen-screens.py`: added the `craft_armor` action + `armor` condition
  names and the `(slot << 5) | piece` param validation.
- `src/render.hpp`: `armorHeadPart()` maps the equipped head piece to the
  closest existing layered head sheet (hunter helm -> `mh_head_helm`, bone cap ->
  `mh_head_bandana`; base otherwise). Body/charm fall back to the base sheets —
  documented placeholder until the 05x per-piece art lands.
- `monhun-ardu.ino`: `armorApplyToGame` at hunt start (boot, menu A, screen nav)
  and when a camp smithy closes back into the camp (equip change).
- Tests (permanent, native): `tst/armor_engine_test.hpp` (new host suite:
  equip/unequip + uncrafted refusal, crafted round-trip, aggregation incl.
  thresholds/empty/wrong-slot, craft gate/debit/toggle), `tst/main.cpp` wiring,
  `tst/fxdatatest/smith_test.hpp` (12 rows + armor row pins + craft/equip EEPROM
  round-trip + cart aggregation), `tst/fxdatatest/player_art_test.hpp`
  (armor head pixel cases 37..39, goldens captured; cases 0..36 byte-identical).
- Docs: `docs/equipment-framework.md` ("Armor engine" contract + render
  placeholder + arm.2 landed), `docs/quests-shops.md`, `README.md` (file map,
  smith flow, counts/size).

## Interfaces

- `mh::ArmorPiece { slot, defense, resist[4], skillCount, skill[2], points[2] }`
- `mh::ArmorAgg { defense, resist[4], points[armor::SKILL_COUNT], tier[...] }`
- `mh::armorClear/armorAdd/armorFinalize/armorAggregate/armorEquipToggle`
- `mh::armorReadPiece/armorApplyToGame` (device), `Game::armor`/`Game::armorHead`
- `saveCrafted/saveSetCrafted`, `SAVE_CRAFTED_BIT_BASE/MAX`
- `screens::COND_ARMOR=6`, `screens::ACTION_CRAFT_ARMOR=8`,
  `screenArmorPiece/screenArmorSlot`, `screenRowArmorRecipe`

## Verification (tails)

`make gen` x2 (the first pass re-bakes the equip/zone sheet offsets after the
screens table grew; second converges), then `make gen-check`:
```
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (90 generated artifacts unchanged)
```

`make test`:
```
Total Passed: 6190
Total Failed: 0
```

`make test-tools`:
```
Ran 300 tests in 16.982s
OK
```

`make fxtest-headless` (full, test_parity excluded):
```
B pUs=6347 pHz=157 lHz=52 lTk=480 rMx=3348 rAv=3075 ram=689
perf_test PASSED=5 FAILED=0
test_perf: PASS
... all suites PASS (assets 270, audio 10, boot 4, combat 237, data 343, hub 63,
hud 25, items 35, menu_art 53, menu 60, monster_art 111, perf 5, player_art 120,
quests 50, screens 85, smith 105, tell 14, zones 80)
```

`make size`:
```
size: .text=29150 .data=20 .bss=1611
size: flash=29170/29696 (526 free)  ram=1631/2560
size: data facts: HAS_CARVE:true HAS_ENRAGE:true ... HAS_ZONES:true
```
Flash delta vs HEAD (28274): **+896 B** (29170). RAM delta: **+21 B** (1631).
Under the 900 B target. Cart grew 85 B (5 smith rows), shifting the sprite
section (equip/zone meta regenerated); cart size is not a flash constraint.

## Deviations / notes

- Crafted state reuses the save `flags` byte instead of a new byte/version: the
  bead allowed "a crafted bitmask/flags byte", and this avoids a save v4
  migration entirely. `armor::PIECE_COUNT <= SAVE_CRAFTED_MAX` is static-asserted
  (5 <= 7).
- Per-piece armor sheets are not in the equip blob (the 05x art epic owns them),
  so the paper-doll uses the closest existing head layer as a documented
  placeholder; body/charm draw the base sheets. This is the bead's explicit
  "otherwise base sheets + report" fallback.
- `armorAggregate` trusts the equip slots; the crafted gate is enforced at the
  lowest write (`armorEquipToggle` refuses uncrafted ids), so the slots can only
  hold crafted pieces in normal flow.
- `make size` headroom is now 526 B; arm.3 (combat effects) will need its own
  budget check.
