# Quests & shops — data-driven screens (design)

Status: accepted defaults (kill-quests + zenny; armor crafted on the GEAR armor
card; weapon progression is the ui.4 FORGE trees, which replaced the removed
SMITH screen). ui.5 moved the live zenny balance into the list header.
Budget: shipping flash 29582/29696 (114 free) after ui.5; each new screen must
cost ~0 flash (cart data) once the framework lands.

## Layers

```
hub menu (title -> HUNT / QUESTS / FORGE / GEAR, header zenny)
  screen framework: one generic list renderer + input + row actions
    cart data: screen tables (title, rows: label, cost, flags, condition, action)
    state:    save block v5 in EEPROM (zenny, quest flags, owned/crafted bitsets)
    content:  quest board rows, armor craft bills (baked into the cards), forge
              trees (data/forge/*.json -> generated FORGE/GEAR rows)
```

## Screen data (cart, packed little-endian)

```
ScreenDef: id u8, titleLen u8 + chars, rowCount u8, firstRow u16
ScreenRow: labelLen u8 + chars, cost u16, actionId u8, flags u8,
           condId u8 (0 = always, else save-flag query), param u8
```

- Render: rows via the existing `textPut` glyph lane + the baked cursor/tile
  sprites; 6 rows per page, scroll by 6; cost right-aligned. The title line
  carries the live zenny (`$` + `drawNumber`, ui.5).
- Input: up/down move, A = accept (opens a card or fires the row action), B = back.
- Conditions (runtime): quest state (`COND_QUEST`) only; every other row is
  always live. The generated `zenny`/`flag`/`tier`/`upgrade` ids remain in the
  ABI but have no runtime case since ui.2. The `zenny` row *flag* is retired
  (ui.5): the header owns the live balance.
- Actions (fixed enum, one switch): TAKE_QUEST(id), TURN_IN_QUEST(id),
  EQUIP_WEAPON(weaponIdx), FORGE_NODE(nodeIdx), OPEN_GEAR, OPEN_FORGE, LEAVE.
  Armor rows (`ACTION_EQUIP_ARMOR`) open the card, whose `cardArmorApply`
  crafts/equips; weapon rows open the weapon card, whose `cardApply` forges /
  upgrades / equips. `BUY_UPGRADE` is trimmed (ui.2; FORGE trees in ui.4) and
  ui.3.1 removed `CRAFT_ARMOR` + `OPEN_SMITH` with the SMITH screen.

## Detail cards (ui.3)

```
list screen --A--> detail card --LEFT/RIGHT--> pages --A--> action --B--> list
```

- Armor rows (`ACTION_EQUIP_ARMOR`) and quest rows (`ACTION_TAKE_QUEST` /
  `ACTION_TURN_IN_QUEST`) open a prebaked 128x64 card instead of firing the
  action on the list. Quest cards run the `screenApplyAction` switch with the
  row that opened them; armor cards run `cardArmorApply`, which crafts an
  uncrafted piece from the baked bill (zenny + materials, sets the crafted bit)
  and then toggles equip (ui.3.1, 5co.6). A gated action stays inert.
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
- `mhCards` record per item: kind, page mask, the four page image addresses,
  up to two overlay slots (page + x/y + kind + args) and the armor craft bill
  (u16 zenny + up to two `{itemIdx+1, count}` pairs; zero for quests/weapons).
  The cart-free page machine is `src/card_state.hpp`; the cart reader + renderer
  is `src/cards.hpp`. Weapon cards cache the `mhForge` node record at open
  (`src/forge.hpp` `forgeReadNode`).
- ui.4 made the split whole: armor, quest, **and weapon** rows all open cards;
  a list row never fires an action directly.

## FORGE (weapon trees, ui.4)

- `data/forge/*.json` -> `fxdata/tables/forge.bin` + `src/generated/forge_meta.hpp`
  (one 17 B node record: class, parent, flags, dmg/spd multipliers, upgrade +
  direct bills). `tools/gen-forge.py` also emits the generated FORGE/GEAR row
  blocks (indented tree labels + the upgrade cost on FORGE).
- Node semantics (`src/forge_state.hpp`): an owned parent unlocks the cheaper
  **upgrade** bill (transforms the parent; if the parent was equipped the
  equipped id follows the child); with no owned parent a `direct` node uses the
  pricier **direct** bill and leaves skipped nodes unowned. `billShort`/
  `billDebit` are shared with the armor card craft.
- `huntStart` resolves the equipped node's class + multipliers
  (`forgeEquippedClass` / `forgeEquippedMul`); `SAVE_NODE_NONE` falls back to the
  sword root at 100/100.

## Save block (EEPROM, v5)

```
magic u16 "MH", version u8 (5), zenny u16, questState u8[4] (bits: taken/done),
activeQuest u8, progress u8, reserved u8[3], equip u8[3], flags u8,
items u8[ITEM_COUNT], equippedNode u8, weaponOwned u8[4], crafted u8[1],
checksum u8                                                   (44 B total)
```

- v5 (monhun-ardu-5co.4) replaced the per-class smith tier bytes + class-index
  weapon byte with the forge-tree model: a 4 B owned bitset (**32 node slots**),
  the equipped node id, and a 1 B crafted bitset (**8 armor-piece slots**).
  Caps are fixed regions; content below them is data-only.
- There is **no migration** (ui.4.1, owner decision): only a v5 record with a
  good magic + checksum decodes; anything else (blank, junk, v1..v4) falls back
  to `saveDefaults()`. Pre-release, an old save is discarded on a version change.
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
- Unlock chain (monhun-ardu-dlp.3, demo retune): `gather_ore` (id 2) is now
  `unlock 0` so the demo can show gather -> turn-in immediately (its desc points
  at the cavern mine); the kill chain stays
  `slay_lunge` (id 0, always) -> `slay_sweep` (id 1, unlock 1) ->
  `crush_heavy` (id 3, unlock 3 = gather_ore done), where `unlockFlag = N`
  means "quest index N-1 must be done". Turn one in and the next take row goes
  live; the board still draws a locked row (no graying/filtering yet).
- Live hub loop (monhun-ardu-isp.1; the hub is the root, the 5r1/opening menu is
  gone): boot --> hub --QUESTS--> take a quest --B--> hub --HUNT--> camp/area
  hunt (kill or gather goal) --win/loss + A--> hub --QUESTS--> turn-in (pays
  zenny + material, sets done) --B--> hub, where the next quest is now unlocked.
  Hub B is a root no-op; camp hold-B leaves the hunt back to the hub. The loadout
  is the save's equipped forge node (v5) and the HUNT row's beast comes from the
  active quest's `goalKind`/`target` (`huntStart`, src/app_setup.hpp).
- Gear screen (hml.3, armor rows gs.1; ui.3.1 craft; ui.4 weapons): the hub's
  GEAR row (`ACTION_OPEN_GEAR`) opens the `data/screens/gear.json` list (the
  generated weapon-tree rows + the five armor pieces + the skill rows + LEAVE).
  A on a weapon equip row (`ACTION_EQUIP_WEAPON`, `param` = node id) opens the
  weapon card; the card's A equips an owned node or unequips the wielded one
  (`forgeNodeEquipToggle`; unowned nodes refuse). The armor rows
  (`ACTION_EQUIP_ARMOR`, `param` = `(slot << 5) | pieceIdx`) are always live and
  A opens the armor card: `cardArmorApply` crafts an uncrafted piece from the
  baked bill (debit + crafted bit) then toggles it into its slot
  (`armorEquipToggle`, true only on a real slot change); a crafted piece just
  toggles. A same-piece re-press unequips. The next HUNT starts with the picked
  loadout. An items screen is still a follow-up, and the equipped weapon/armor
  has no on-screen mark yet.
- FORGE screen (ui.4): the hub's FORGE row (`ACTION_OPEN_FORGE`) opens
  `data/screens/forge.json` (generated tree rows + LEAVE). See the FORGE section.
- Scaffold limitations: the board renders every authored row even when its
  `COND_QUEST` condition is dead (locked/not-active) — status graying, progress
  display and nav filtering are a follow-up. There is no inventory screen; the
  material reward lands silently in the save and is shown on the card PARTS
  have-counts.
- Persisted on quest complete; board shows taken/progress/done and pays out on
  turn-in.
- Gather targets and material rewards are validated against `data/items.json`
  by `tools/gen-quests.py`; no second item list is hardcoded.

## Smith (removed)

The SMITH screen, the camp smithy interaction, and the smith weapon-tier
**purchase** path (`COND_UPGRADE` / `ACTION_BUY_UPGRADE` rows +
`screenRowRecipe`, trimmed in ui.2) are gone (ui.3.1, 5co.6). Weapon progression
is the ui.4 FORGE trees above: the equipped node's `dmgMul`/`spdMul` replaced the
per-class smith tiers (`upgradeApplyToGame` now calls `forgeEquippedMul`). The
`mhSmith` table and `tools/gen-smith.py` still pack armor recipes (unread by the
runtime) and host-test fixtures; nothing in the shipping UI references smith.

**Armor craft bill (monhun-ardu-arm.1; moved to the cards in ui.3.1).** The
piece's `{materials, zenny}` bill from `data/armor.json` is now baked into the
`mhCards` record by `tools/gen-cards.py` (u16 zenny + up to two
`(itemIdx+1, count)` pairs), so the armor card gates + debits its own craft
without a cart recipe read. `tools/gen-smith.py` still packs the matching armor
recipe array in `mhSmith` (blob header byte 5 = armor count), but the runtime no
longer reads it: `screenRowArmorRecipe`, `COND_ARMOR` and `ACTION_CRAFT_ARMOR`
are gone, and the host debit rule is `src/card_state.hpp cardArmorApply`
(reusing the `screenRecipeOk`/`screenRecipeDebit` pair). The armor piece index
matches `armor::ARMOR_<ID>`; the row `param` is `(slot << 5) | pieceIdx`. See
`docs/equipment-framework.md` ("Armor data" and "Armor engine") for the
piece/skill schema and the aggregation contract.

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
