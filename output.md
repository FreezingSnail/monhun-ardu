# monhun-ardu-5co.7 — ui.4.1 trim: no save migration, fold list tokens into cards, unify card paths

Status: **DONE.** The shipping image fits with headroom; ui.4's features are
intact end to end and the full gate is green.

```
size: flash=29506/29696 (190 free)  ram=1801/2560
```

| | flash |
|---|---|
| ui.4 working tree (BLOCKED) | 30880 (1184 over) |
| ui.4.1 (this bead) | **29506 (190 free)** |
| reclaimed from the ui.4 tree | **1374 B** |
| HEAD 30ef641 (ui.3.1) | 29172 (524 free) |
| ui.4 net delta now | +334 B |

Target was ≥ ~150 B free; **190 B free** clears it.

## Owner decisions, implemented

### 1. No save migration (≈ −460 B)

`src/core/save.hpp` now decodes **only** version 5 + magic + checksum; anything
else (blank, junk, v1..v4) falls back to `saveDefaults()`.
- Removed `saveDecodePrefix`, `saveDecodeV4`, `saveDecodeV3`, `saveMigrateWeapon`,
  `saveZeroTail`, `saveDecodeCommon`, `saveChecksumN`.
- Removed every legacy constant: `SAVE_VERSION_V1..V4`, `SAVE_TIER_*`,
  `SAVE_LEGACY_CRAFTED_*`, `SAVE_V4/V3_CHECKSUM_OFF`.
- Removed the v4 spine map `forge::TIER_NODE` from `tools/gen-forge.py` and the
  generated `src/generated/forge_meta.hpp`; `saveDefaults()` now owns every
  depth-0 node via `forge::NODE_DEPTH` and equips `forge::NODE_SWORD_BASE`.
- Kept v5 encode/decode/defaults + checksum. Pre-release: an old save is
  discarded on a version change.

### 2. Fold the live list token column into the cards (≈ −430 B)

- `src/screens.hpp`: deleted the `MH_FORGE_TOKENS` block, the `FORGE_TOKENS`
  string/offset tables and the per-row classifier + render branch; deleted the
  `src/forge.hpp` include. FORGE/GEAR rows now draw label + packed cost through
  the generic path.
- `src/forge_state.hpp`: deleted the `ForgeToken` enum, `forgeToken()` and
  `forgeActionable()` (list-only); their coverage is gone (justified below).
- `tst/fxdatatest/test_hub.ino` no longer defines `MH_FORGE_TOKENS 0`.

### 3. Unify the weapon and armor card paths (≈ −300 B incl. sweeps)

- New shared bill machinery in `src/forge_state.hpp`: `billShort()` (gate with a
  reason code) + `billDebit()`, operating on packed `{itemCode,count}` byte pairs.
  The armor card craft bill (`CardItem.craft`) and a forge node's
  `mats`/`directMats` share the exact layout, so both card kinds run **one**
  gate loop and **one** debit loop.
- `forgeActiveMats()` selects the upgrade/direct bill as a pointer (no
  `ForgeBill` copy); `forgeActiveBill()`/`forgeAffordable()` remain only for the
  host suite (LTO drops them from shipping).
- One card action entry: `cardApply(save, it, node, row)` replaces the
  ino ternary chain and `cardWeaponApply`. `forgeHint()` is the weapon half of
  `cardHint()`; the hint is classified once per card state change
  (`cardSetHint`) and cached in `DetailState`, so `drawCard` no longer
  re-classifies per plane.
- `cardRowIndex()` now returns `CARD_NONE` for non-opening rows; the sketch tests
  one value instead of `cardRowOpens()` + `cardRowIndex()`; `cardRowNode()` is
  gone.
- `forgeReadNode()` bulk-reads the 17-byte record straight into the `ForgeNode`
  tail (static_asserts pin the layout) instead of staging + field-decoding;
  `forgeEquippedMul()` reads only the two multiplier bytes.
- `forgeNodeState()` does one `billShort()` pass (no duplicate zenny compare).

### 4. Safe sweeps (each measured)

| Sweep | Δ |
|---|---|
| save bitsets narrowed to the data: owned 8 B/64 → 4 B/32, crafted 8 B/64 → 1 B/8 (record 44 → 33 B); generic `saveBitGet/Set` replaced by sized accessors | **≈ −290** |
| `forgeReadNode` bulk read + `forgeEquippedMul` 2-byte read | −90 |
| `billShort`/`billDebit` `MH_NOINLINE` (one shared copy) + `billShort` reason code | −18 |
| dead `screen_meta.hpp` `TIER_COUNT` (stale `SAVE_TIER_COUNT` reference) removed from `gen-screens.py` | 0 flash (dead const) |

`MH_NOINLINE` on `forgeNodeState`/`forgeNodeApply`/`forgeNodeEquipToggle` was
tried and **reverted** (+46 B: it added call overhead without dedup).

## What still works (ui.4, verified)

- Forge tree data + `mhForge` cart records; `FORGE`/`GEAR` generated rows.
- Save v5: weapon owned bitset, equipped node, armor crafted bitset; round-trip
  + checksum.
- `forge_state.hpp`: direct-vs-upgrade bills, affordability, debit,
  equipped-follows-upgrade, equip/unequip toggle.
- Weapon cards: `DESC/PARTS/STATS`, cached node, `A FORGE`/`A EQUIP`/
  `A UNEQUIP`/`NEED PARTS`/`NEED ZENNY` hint, forge/upgrade/equip on A.
- Armor cards: craft bill gate/debit/equip, crafted PARTS trim.
- Quest cards; hub FORGE routing; hunt reads the equipped node's class +
  dmg/spd multipliers.

## Tests

Permanent, co-located, native frameworks, no `/tmp`.

- Updated: `tst/screens_test.hpp` — v5 offsets/bitset caps; the four migration
  tests replaced by one "v1..v4 fall back to defaults" test.
- Updated: `tst/forge_state_test.hpp` — token test removed (machinery gone);
  bitset caps. `tst/card_state_test.hpp` — weapon path now `cardHint`/`cardApply`.
- Updated: `tst/armor_engine_test.hpp` (crafted cap), `tst/fxdatatest/forge_test.hpp`
  (`cardApply`, sentinel fields), `tst/fxdatatest/cards_test.hpp`
  (`drawCard` signature + `cardSetHint`), `tst/fxdatatest/hub_test.hpp` comment,
  `tst/fxdatatest/test_hub.ino`.
- Tools: `tools/tests/test_gen_forge.py` asserts `TIER_NODE` is gone;
  `tools/tests/test_gen_screens.py` drops the `TIER_COUNT` expectation.
- Removed tests pin removed machinery: v1..v4 decoders + spine map, list
  E/OK/UP/DIR tokens, `cardWeaponHint`/`cardWeaponApply`/`cardRowNode`,
  `MH_FORGE_TOKENS`. Coverage of what remains (tree walk, upgrade/direct,
  equipped follow, card forge/equip/craft, v5 round-trip) is kept.

## Gate tails

`make gen`:
```
gen-forge: 9 nodes, 161 B blob (magic 0x4647 version 1)
gen-screens: 4 screens, 50 rows, 841 B blob (magic 0x5343 version 1)
gen-cards: 18 items, 56 pages, 602 B blob (magic 0x4341 version 1)
```
`make gen-check`:
```
fxdata_manifest: PASS (148 generated artifacts unchanged)
```
`make test`:
```
Total Passed: 6306
Total Failed: 0
```
`make test-tools`:
```
Ran 344 tests in 18.2s
OK
```
`make fxtest-headless` (full, 18/18):
```
asset_test PASSED=264  test_audio PASSED=9    test_boot PASSED=4
test_cards PASSED=85   combat_test PASSED=237 data_test PASSED=348
test_forge PASSED=58   test_hub PASSED=81     test_hud PASSED=29
test_items PASSED=35   test_monster_art PASSED=127  test_perf PASSED=5
test_player_art PASSED=120  test_quests PASSED=87  test_screens PASSED=136
test_smith PASSED=51   test_tell PASSED=18     zones_test PASSED=82
```
`make size`:
```
size: .text=29474 .data=32 .bss=1769
size: flash=29506/29696 (190 free)  ram=1801/2560
```

## Smith retirement note

Unchanged from the ui.4 report: `src/smith.hpp` / `UpgradeDef` / `upgradeFind` /
`upgradeResolve` are shipping-dead and `mhSmith` is read only by
`tst/fxdatatest/smith_test.hpp`; retiring them reclaims FX-cart bytes, not MCU
flash, so it is left in place (the trim did not need it).

No commit/push (orchestrator commits between bead waves). ui.4 is complete on
this same tree.
