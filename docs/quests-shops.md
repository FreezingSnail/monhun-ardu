# Quests & shops — data-driven screens (design)

Status: accepted defaults (kill-quests + zenny; smith = weapon upgrade tiers).
Budget: shipping flash 23342/29696 (6354 free) after the trim waves; each new
screen must cost ~0 flash (cart data) once the framework lands.

## Layers

```
hub menu (title -> HUNT / QUESTS / SMITH)
  screen framework: one generic list renderer + input + row actions
    cart data: screen tables (title, rows: label, cost, flags, condition, action)
    state:    save block in EEPROM (zenny, quest flags, upgrade tiers)
    content:  quest board rows, smith tiers, future shop stock
```

## Screen data (cart, packed little-endian)

```
ScreenDef: id u8, titleLen u8 + chars, rowCount u8, firstRow u16
ScreenRow: labelLen u8 + chars, cost u16, actionId u8, flags u8,
           condId u8 (0 = always, else save-flag query), param u8
```

- Render: rows via the existing `textPut` glyph lane + the baked cursor/tile
  sprites; 6 rows per page, scroll by 6; cost right-aligned.
- Input: up/down move, A = accept (fires action), B = back.
- Conditions: `zenny >= cost`, `flag set`, `tier < max` — one switch, no VM.
- Actions (fixed enum, one switch): BUY_UPGRADE(tier), TAKE_QUEST(id),
  TURN_IN_QUEST(id), LEAVE.

## Save block (EEPROM)

```
magic u16 "MH", version u8, zenny u16, questState u8[4] (bits: taken/done),
upgradeTier u8[N_WEAPONS], checksum u8
```

- Load on boot; write-on-change with a verify read (Arduboy2 EEPROM helper);
  first boot / bad magic / bad checksum = defaults.
- No save during a hunt (only on screen actions) to keep write cycles low.

## Quests (content model)

```
QuestDef (v2): id, goalKind u8 (0 kill / 1 gather), target u8
               (MonsterKind for kill / item index for gather), need u8,
               rewardZenny u16, rewardItem u8 (itemIdx+1, 0 = none),
               rewardCount u8, unlockFlag u8
```
- Progress counted in `Game` during a hunt (kill target kind, or gathered item
  count for a gather quest), persisted on quest complete; board shows
  taken/progress/done and pays out on turn-in (zenny + optional material).
- Gather targets and material rewards are validated against `data/items.json`
  by `tools/gen-quests.py`; no second item list is hardcoded.

## Smith (content model)

```
UpgradeDef: weaponIdx u8, tier u8, cost u16, dmgMul u8, spdMul u8, unlockFlag u8
```
- Applied as a multiplier in the player damage/velocity path; tier persisted.

**Armor recipes (monhun-ardu-arm.1).** The same recipe path also crafts armor.
`tools/gen-smith.py` derives one armor recipe record per `data/armor.json`
piece from that piece's `{materials, zenny}` bill and appends it after the
weapon records in `mhSmith` (blob header byte 5 = armor count, then
`armorIdx u8, cost u16, unlockFlag u8, mat[2] x (itemIdx+1 u8, count u8)`).
The packed material slots use the identical `(itemIdx+1, count)` convention and
the host debit is the same `screenRecipeOk` / `screenRecipeDebit` pair, so
armor spends zenny and materials exactly like a weapon tier. The armor piece
index matches `armor::ARMOR_<ID>`. `arm.2` wires the smith action/condition:
the smith screen gains one `COND_ARMOR` / `ACTION_CRAFT_ARMOR` row per piece
(`param = (slot << 5) | pieceIdx`), the row's cost/bill come from the recipe
record, and A crafts (debits + sets the crafted bit + equips) or toggles
equip/unequip. See `docs/equipment-framework.md` ("Armor data" and "Armor
engine") for the piece/skill schema and the aggregation contract.

## Beads

1. `qs.1` framework spike: screen tables on cart, generic list render, row
   actions, EEPROM save block, hub stub screen; host nav tests + device save
   roundtrip + pixel test; budget <= ~1 KB.
2. `qs.2` quest content: board screen data, kill accounting, payout, guards.
3. `qs.3` smith content: tier table, stat application, purchase flow.
4. `qs.4` integration: hub entry from the opening menu, return paths, art.
   (Superseded on the demo path by `monhun-ardu-5r1`: the opening menu A now
   launches the hunt directly and win/loss + A returns to the menu. The hub
   graph stays compiled and tested but is not reachable from the sketch.)

## Acceptance

- New screens cost data only (adding a row/screen touches JSON + `make gen`).
- EEPROM survives power cycle; corrupt/first-boot falls back safely.
- Existing gameplay/parity untouched; device suites green.
