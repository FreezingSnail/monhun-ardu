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
QuestDef: id, targetKind u8, need u8, reward u16, unlockFlag
```
- Progress counted in `Game` during a hunt (kill target kind), persisted on
  quest complete; board shows taken/progress/done and pays out on turn-in.

## Smith (content model)

```
UpgradeDef: weaponIdx u8, tier u8, cost u16, dmgMul u8, spdMul u8, unlockFlag
```
- Applied as a multiplier in the player damage/velocity path; tier persisted.

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
