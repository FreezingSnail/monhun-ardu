# monhun-ardu-5co.2 — ui.2 trim: adopt the reclaim set

Status: DONE. HEAD 8ee7a37 + working tree (no commit/push, per orchestrator
flow). Adopted the ui.1 reclaim set minus the ARMOR craft path.

## Adopted (measured)

| Candidate | delta | notes |
|---|---|---|
| smith weapon **UPGRADE** path: `COND_UPGRADE` case + `ACTION_BUY_UPGRADE` case + `screenRowRecipe` + the `screenUpgrade*` decoders + `SCREEN_MAX_TIER` | −462 (spike) | weapon-tier rows removed from `data/screens/smith.json` |
| dead conditions `COND_ZENNY` / `COND_FLAG` / `COND_TIER` cases in `screenCondOk` | −62 (spike) | verified unused by data/JSON + shipping path |
| `drawScreen` int-width narrowing (`x`/`y`/`lx`/`costX` int16→uint8) | −14 (spike) | stayed clean (explicit casts on the `textPut` return) |

**Kept**: the smith ARMOR craft path (`COND_ARMOR` / `ACTION_CRAFT_ARMOR` /
`screenRowArmorRecipe`) — ui.3 reuses it for armor card crafting; its trim moves
to ui.4. `screenRecipeOk` / `screenRecipeDebit`, `ScreenRecipe`, and the
`smith.hpp` cart reader stay (armor recipes + the tier multiplier path).

The `mhSmith` upgrade table and `save.tier` are untouched: `upgradeApplyToGame`
/ `smithResolve` still resolve the saved tier into `Game::dmgMul`/`spdMul` at
hunt start. Only the *purchase UI* is gone.

## Expected temporary gap

The smith screen no longer lists the six weapon-tier rows
(`SWORD T1/T2`, `FLAIL T1/T2`, `GUN T1/T2`) — `data/screens/smith.json` now
holds the five armor rows + `LEAVE` (6 rows). With no purchasable tiers,
`save.tier` stays 0 and `g.dmgMul` is identity until ui.4. **Weapon upgrades
return in ui.4 as FORGE trees**, which will own the upgrade UI and the tier
writes. `README.md` and `docs/quests-shops.md` were updated to say so.

The generated `ACTION_BUY_UPGRADE` / `COND_ZENNY` / `COND_FLAG` / `COND_TIER` /
`COND_UPGRADE` enum ids remain in `src/generated/screen_meta.hpp`: the generator
is append-only so renumbering would break the packed ABI. They are now
unreferenced constexprs (no emitted code, no flash cost).

## Files

Production:
- `src/screen_state.hpp` — dropped the `COND_ZENNY`/`COND_FLAG`/`COND_TIER`/
  `COND_UPGRADE` cases, the `ACTION_BUY_UPGRADE` case, `screenUpgradeWeapon`/
  `screenUpgradeTier`/`screenUpgradeUnlock`, `SCREEN_MAX_TIER`; comments updated.
- `src/screens.hpp` — dropped `screenRowRecipe` and the `COND_UPGRADE` branch in
  `screenReadRow`; `drawScreen` int widths narrowed; include comment updated
  (`smith.hpp` still needed for `smithCart`).
- `data/screens/smith.json` — weapon-tier rows removed, armor rows + LEAVE kept.

Generated (staged together): `fxdata/tables/screens.bin` (652 → 566 B, −86),
`src/generated/screen_meta.hpp` (smith rows 12→6), `fxdata/fxdata.h`,
`src/fxdata.h`, `fxdata/fxdata.bin`, `fxdata/fxdata-data.bin`,
`fxdata/manifest.json`. The smaller screens blob shifts every later FX symbol
by −86 B, so the downstream baked offsets regenerate too:
`fxdata/tables/equip.bin`, `src/generated/equip_meta.hpp` (SHEET_OFF_* −86),
`src/generated/zone_meta.hpp` (SHEET_*_OFF / ROOM_*_IMAGE_OFF −86). These are
part of the same regen set (`make gen-check` PASS after staging them in the
tree).

Docs: `README.md`, `docs/quests-shops.md`.

## Tests (permanent; updates/removals justified)

Removed — each pinned only the trimmed behavior:
- `tst/screens_test.hpp`: the `COND_ZENNY`/`COND_FLAG`/`COND_TIER` condition
  asserts (replaced by an `COND_ALWAYS`-only check), the `ACTION_BUY_UPGRADE`
  incremental-buy test, and the `COND_UPGRADE` next-tier/lock/funds test.
- `tst/smith_test.hpp`: `upgradeParam`/`upgradeRow`/`plainBuyRow` helpers, the
  `COND_UPGRADE` purchase-gating test and the legacy `BUY_UPGRADE` test. Kept
  `upgradeMul`/`upgradeFind`/`upgradeResolve` + the damage/speed E2E (the tier
  multiplier path still ships).
- `tst/fxdatatest/smith_test.hpp`: the weapon purchase E2E; row indices/count
  moved to the 6 armor rows; kept the `UpgradeDef` cart reads, `smithResolve`,
  and the full armor craft/equip + EEPROM flow.
- `tst/fxdatatest/screens_test.hpp`: smith action-dispatch now crafts HUNTER
  HELM instead of buying SWORD T1.
- `tst/fxdatatest/hub_test.hpp`: the smith detour crafts+equips HUNTER HELM;
  `g.dmgMul` assertion is now the tier-0 identity (100), zenny reload checks
  follow the 300-zenny craft.
- `tst/app_state_test.hpp`: the sub-screen save-action row is now
  `ACTION_CRAFT_ARMOR`/`COND_ARMOR` (same "not a hub destination" intent).

Kept coverage of everything that remains: armor crafting host
(`tst/armor_engine_test.hpp` `COND_ARMOR`/`ACTION_CRAFT_ARMOR`), GEAR
equip/skill readout, quests, tier-multiplier E2E, and the surviving smith cart
reads.

## Size (before → after)

Before (HEAD 8ee7a37):
```
size: flash=29204/29696 (492 free)  ram=1715/2560
```
After:
```
size: flash=28646/29696 (1050 free)  ram=1715/2560
```
**Total reclaimed: 558 B flash; free 492 → 1050 (target ≥ ~1000 met).** RAM
unchanged.

## Gate tails

`make gen`:
```
gen-screens: 4 screens, 33 rows, 566 B blob (magic 0x5343 version 1)
gen-screens: fxdata/tables/screens.bin
gen-screens: src/generated/screen_meta.hpp
gen.sh: FX data + src/fxdata.h regenerated
```
`make gen-check`:
```
fxdata_manifest: PASS (87 generated artifacts unchanged)
```
`make test`:
```
Total Passed: 6183
Total Failed: 0
```
`make test-tools`:
```
Ran 310 tests in 17.166s
OK
```
`make fxtest-headless` (full, 16/16):
```
test_assets: PASS      test_boot: PASS      test_combat: PASS
test_data: PASS        test_hub: PASS       test_hud: PASS
test_items: PASS       test_monster_art: PASS test_perf: PASS
test_player_art: PASS  test_quests: PASS    test_screens: PASS
test_smith: PASS       test_tell: PASS      test_zones: PASS
```
`make size`:
```
size: flash=28646/29696 (1050 free)  ram=1715/2560
```

## Blockers

None.
