# Quests & shops — data-driven screens (design)

Status: accepted defaults (kill-quests + zenny; smith = armor crafting; weapon
progression returns as FORGE trees in ui.4).
Budget: shipping flash 23342/29696 (6354 free) after the trim waves; each new
screen must cost ~0 flash (cart data) once the framework lands.

## Layers

```
hub menu (title -> HUNT / QUESTS / SMITH / GEAR)
  screen framework: one generic list renderer + input + row actions
    cart data: screen tables (title, rows: label, cost, flags, condition, action)
    state:    save block in EEPROM (zenny, quest flags, upgrade tiers)
    content:  quest board rows, smith armor recipes, future shop stock
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
- Conditions (runtime): quest state, armor craftability (`COND_ARMOR`),
  `crafted` (gs.1). The generated `zenny`/`flag`/`tier`/`upgrade` ids remain in
  the ABI (append-only enum) but have no runtime case since ui.2.
- Actions (fixed enum, one switch): TAKE_QUEST(id), TURN_IN_QUEST(id),
  CRAFT_ARMOR(slot|piece), EQUIP_WEAPON(weaponIdx), EQUIP_ARMOR(slot|piece),
  OPEN_GEAR, LEAVE. `BUY_UPGRADE` is trimmed (ui.2; FORGE trees in ui.4).

## Detail cards (ui.3)

```
list screen --A--> detail card --LEFT/RIGHT--> pages --A--> action --B--> list
```

- Armor rows (`ACTION_CRAFT_ARMOR` / `ACTION_EQUIP_ARMOR`) and quest rows
  (`ACTION_TAKE_QUEST` / `ACTION_TURN_IN_QUEST`) open a prebaked 128x64 card
  instead of firing the action on the list. The card A runs the same
  `screenApplyAction` switch with the row that opened it, so the list and the
  card cannot diverge (a gated row stays inert on both).
- Pages are per-item data: armor `DESC / PARTS (uncrafted only) / STATS /
  SKILL`; quests `GOAL / PROG / REWARD`. A page with no data is not generated.
  LEFT/RIGHT cycles only pages present in the mask; B backs to the list.
- Everything is baked (`tools/gen-cards.py` -> `images/cards/` ->
  `fxdata/cards/Sprites.txt` + `fxdata/tables/cards.bin` +
  `src/generated/card_meta.hpp`). The only dynamic pixels are the hint line
  (`A CRAFT` / `A EQUIP` / `A UNEQUIP` / `A ACCEPT` / `A TURN IN` /
  `NEED PARTS` / `NEED ZENNY`) and the meta overlay slots: live PARTS
  have-counts and the quest PROG bar. After an action the card refreshes, so a
  crafted piece loses its PARTS page and cannot be crafted twice.
- `mhCards` record per item: kind, page mask, the four page image addresses and
  up to two overlay slots (page + x/y + kind + args). The cart-free page machine
  is `src/card_state.hpp`; the cart reader + renderer is `src/cards.hpp`.
- Temporary scope split (ui.4 owns the rest): GEAR **weapon** rows keep their
  direct-equip action because weapon cards land with the FORGE trees. Every
  armor row (smith craft + gear equip) and every quest row opens a card.

## Save block (EEPROM)

```
magic u16 "MH", version u8 (4), zenny u16, questState u8[4] (bits: taken/done),
activeQuest u8, progress u8, upgradeTier u8[N_WEAPONS], equip u8[3], flags u8,
items u8[ITEM_COUNT], weapon u8, checksum u8
```

- v4 (monhun-ardu-isp.1/hml.1) appended the `weapon` byte at 26 and moved the
  checksum to 27 (`SAVE_BYTES` 28). A v3 record migrates with its full tail and
  `weapon = 0` (checksum still at 26); v1/v2 migrate their shared prefix.

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
- Progress counted in `Game` during a hunt (`questGoalKind` selects the hook):
  a `GOAL_KILL` quest counts one per target-kind death (`core/monster.hpp`); a
  `GOAL_GATHER` quest counts the node's `gatherYield` when the gathered item
  matches its target (`core/items.hpp` `applyGather`; carves go through
  `carve.hpp`, so they never count). Progress saturates at 255, and a kill goal
  never counts a gather (and vice versa).
- The board row is decoded from the quest def by `screens.hpp screenReadRow`:
  a take row fills `ScreenRow::unlock` (`0` = always, else the 1-based prior
  quest whose done bit gates it, enforced in the `COND_QUEST` condition and
  re-checked in `ACTION_TAKE_QUEST`), and a turn-in row fills `recipe[0] =
  (rewardItem, rewardCount)` plus `cost = rewardZenny` (the def is the source of
  truth, like the armor recipe rows). Payout is zenny + the optional material.
- Unlock chain (monhun-ardu-dlp.3): the four shipped quests are
  `slay_lunge` (id 0, always) -> `slay_sweep` (id 1, unlock 1) -> `gather_ore`
  (id 2, unlock 2) -> `crush_heavy` (id 3, unlock 3), where `unlockFlag = N`
  means "quest index N-1 must be done". Turn one in and the next take row goes
  live; the board still draws a locked row (no graying/filtering yet).
- Live hub loop (monhun-ardu-isp.1; the hub is the root, the 5r1/opening menu is
  gone): boot --> hub --QUESTS--> take a quest --B--> hub --HUNT--> camp/area
  hunt (kill or gather goal) --win/loss + A--> hub --QUESTS--> turn-in (pays
  zenny + material, sets done) --B--> hub, where the next quest is now unlocked.
  Hub B is a root no-op; camp hold-B leaves the hunt back to the hub. The loadout
  is the save's v4 `weapon` byte (hml.1) and the HUNT row's beast comes from the
  active quest's `goalKind`/`target` (`huntStart`, src/app_setup.hpp).
- Gear screen (hml.3, armor rows gs.1): the hub's GEAR row (`ACTION_OPEN_GEAR`)
  opens the `data/screens/gear.json` list (SWORD / FLAIL / GUN + the five armor
  pieces + LEAVE). A on a weapon equip row (`ACTION_EQUIP_WEAPON`, `param` =
  weapon index) writes `save.weapon` when it changes (same-weapon press and
  out-of-range params are no-ops). The armor rows
  (`COND_CRAFTED`/`ACTION_EQUIP_ARMOR`, `param` = `(slot << 5) | pieceIdx`) are
  live only once the piece's crafted bit is set on SMITH, and A toggles it into
  its slot (`armorEquipToggle`, true only on a real slot change; an uncrafted or
  out-of-range row is inert). A same-piece re-press unequips. The next HUNT
  starts with the picked weapon + armor, so crafting lives on SMITH and
  equip/unequip lives on GEAR. An items screen is still a follow-up, and the
  equipped weapon/armor has no on-screen mark yet.
- Scaffold limitations: the board renders every authored row even when its
  `COND_QUEST` condition is dead (locked/not-active) — status graying, progress
  display and nav filtering are a follow-up. There is no inventory screen; the
  material reward lands silently in the save and is shown only in the smith
  recipe debits.
- Persisted on quest complete; board shows taken/progress/done and pays out on
  turn-in.
- Gather targets and material rewards are validated against `data/items.json`
  by `tools/gen-quests.py`; no second item list is hardcoded.

## Smith (content model)

```
UpgradeDef: weaponIdx u8, tier u8, cost u16, dmgMul u8, spdMul u8, unlockFlag u8
```
- Applied as a multiplier in the player damage/velocity path; tier persisted.
- ui.2 trimmed the smith weapon-tier **purchase** path (`COND_UPGRADE` /
  `ACTION_BUY_UPGRADE` rows + `screenRowRecipe`, and the dead
  `zenny`/`flag`/`tier` condition cases) for headroom. The `mhSmith` upgrade
  table itself is unchanged and still feeds `upgradeApplyToGame` /
  `smithResolve`; the smith screen now lists armor recipes only. Weapon
  progression returns as ui.4 FORGE trees, which will own the upgrade UI.

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
4. `qs.4` integration: hub entry, return paths, art. (`monhun-ardu-isp.1` made
   the hub the root screen and deleted the opening menu: boot lands on the hub,
   its HUNT row starts the save's quest hunt via `huntStart`, camp hold-B returns
   to the hub, and a finished hunt returns to the hub for turn-ins.)

## Acceptance

- New screens cost data only (adding a row/screen touches JSON + `make gen`).
- EEPROM survives power cycle; corrupt/first-boot falls back safely.
- Existing gameplay/parity untouched; device suites green.
