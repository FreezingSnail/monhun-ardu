#pragma once
// On-device end-to-end suite for the weapon forge trees (bead monhun-ardu-5co.4,
// docs/ui-design.md).
//
// Covers the shipped cart path the host suite cannot: reading the mhForge node
// records through src/forge.hpp, the generated FORGE/GEAR row layout, the list
// token rule, the card -> A forge/upgrade/equip E2E (save mutation + EEPROM
// roundtrip) and the equipped-follows-upgrade rule.
#include "harness/fxtest.hpp"
#include "src/cards.hpp"
#include "src/screens.hpp"
#include "src/forge.hpp"
#include "src/fxdata.h"

#include <stdint.h>

namespace forgefx {

using namespace mh;

static const SaveBackend REAL_BACKEND = {saveEepromRead, saveEepromWrite};

inline void test_forge(FxTest &test) {
    // ------------------------------------------------------ cart node records
    ForgeNode n;
    forgeReadNode(forge::NODE_SWORD_BASE, n);
    test.expectEq(n.cls, forge::WEAPON_SWORD, F("sword root class"));
    test.expectEq(n.parent, forge::NODE_NONE, F("sword root parent"));
    test.expectEq(n.cost, 0, F("sword root upgrade cost"));
    test.expectEq(n.dmgMul, 100, F("sword root dmg"));
    test.expectEq(n.spdMul, 100, F("sword root spd"));

    forgeReadNode(forge::NODE_SWORD_T1, n);
    test.expectEq(n.cls, forge::WEAPON_SWORD, F("sword t1 class"));
    test.expectEq(n.parent, forge::NODE_SWORD_BASE, F("sword t1 parent"));
    test.expectEq(n.cost, 100, F("sword t1 upgrade cost"));
    test.expectEq(n.directCost, 180, F("sword t1 direct cost"));
    test.expectEq(n.dmgMul, 110, F("sword t1 dmg"));
    test.expectEq(n.spdMul, 105, F("sword t1 spd"));
    test.expectEq(n.mats[0].item, static_cast<uint8_t>(item::ITEM_ORE + 1), F("sword t1 ore code"));
    test.expectEq(n.mats[0].count, 2, F("sword t1 ore count"));
    test.expectEq(n.directMats[0].count, 3, F("sword t1 direct ore count"));

    forgeReadNode(forge::NODE_GUN_T2, n);
    test.expectEq(n.cls, forge::WEAPON_GUN, F("gun t2 class"));
    test.expectEq(n.parent, forge::NODE_GUN_T1, F("gun t2 parent"));
    test.expectEq(forgeNodeClass(forge::NODE_FLAIL_T1), forge::WEAPON_FLAIL, F("flail class helper"));

    // Out-of-range node reads the sentinel: index kept, parent none, flags 0
    // (forgeNodeState rejects it on the index before touching the bill).
    forgeReadNode(forge::NODE_COUNT, n);
    test.expectEq(n.index, forge::NODE_COUNT, F("bad node index kept"));
    test.expectEq(n.parent, forge::NODE_NONE, F("bad node parent none"));
    test.expectEq(n.flags, 0, F("bad node flags clear"));

    // jd1: the equipped node's sheet kind flows through forgeEquippedSheet to
    // the render sheet selector (0 = class default, 1..4 = gun variants).
    SaveBlock sheetSave;
    saveDefaults(sheetSave);
    sheetSave.equippedNode = forge::NODE_GUN_BUCKLER;
    test.expectEq(forgeEquippedSheet(sheetSave), 1, F("buckler node -> sheet 1"));
    sheetSave.equippedNode = forge::NODE_GUN_KITE;
    test.expectEq(forgeEquippedSheet(sheetSave), 2, F("kite node -> sheet 2"));
    sheetSave.equippedNode = forge::NODE_GUN_TOWER;
    test.expectEq(forgeEquippedSheet(sheetSave), 3, F("tower node -> sheet 3"));
    sheetSave.equippedNode = forge::NODE_GUN_BRACE;
    test.expectEq(forgeEquippedSheet(sheetSave), 4, F("brace node -> sheet 4"));
    sheetSave.equippedNode = forge::NODE_GUN_BASE;
    test.expectEq(forgeEquippedSheet(sheetSave), 0, F("gun base -> default sheet"));
    // 2tb: the melee beast variants ride the same table -- sword 5..8, flail
    // 9..12 -- resolved off the equipped node exactly like the gun kinds.
    sheetSave.equippedNode = forge::NODE_SWORD_SABER;
    test.expectEq(forgeEquippedSheet(sheetSave), 5, F("sword saber node -> sheet 5"));
    sheetSave.equippedNode = forge::NODE_SWORD_FANG;
    test.expectEq(forgeEquippedSheet(sheetSave), 8, F("sword fang node -> sheet 8"));
    sheetSave.equippedNode = forge::NODE_FLAIL_SLING;
    test.expectEq(forgeEquippedSheet(sheetSave), 9, F("flail sling node -> sheet 9"));
    sheetSave.equippedNode = forge::NODE_FLAIL_SPIKE;
    test.expectEq(forgeEquippedSheet(sheetSave), 12, F("flail spike node -> sheet 12"));
    sheetSave.equippedNode = forge::NODE_FLAIL_BASE;
    test.expectEq(forgeEquippedSheet(sheetSave), 0, F("flail spine -> default sheet"));
    sheetSave.equippedNode = SAVE_NODE_NONE;
    test.expectEq(forgeEquippedSheet(sheetSave), 0, F("unequipped -> default sheet"));
    sheetSave.equippedNode = forge::NODE_COUNT;
    test.expectEq(forgeEquippedSheet(sheetSave), 0, F("out-of-range -> default sheet"));

    // --------------------------------------------------- CRAFT row layout
    // hbk.10: the FORGE submenu opens the flat CRAFT list (no headers or tree
    // prefixes); each row carries forge_node + the baked direct cost.
    test.expectEq(screens::SCREEN_FORGE, 3, F("forge submenu index"));
    test.expectEq(screens::SCREEN_CRAFT, 4, F("craft screen index"));
    test.expectEq(screenRowCount(screens::SCREEN_CRAFT), 22, F("craft row count"));
    ScreenRow t1, t3, leave;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_CRAFT, 0), t1);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_CRAFT, 2), t3);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_CRAFT, 21), leave);
    test.expectEq(t1.action, screens::ACTION_FORGE_NODE, F("craft node action"));
    test.expectEq(t1.param, forge::NODE_SWORD_BASE, F("craft root param"));
    test.expectEq(t1.cost, 0, F("craft root cost"));
    test.expectEq(t1.flags, screens::ROW_F_FORGE, F("craft row flag"));
    test.expectEq(t3.param, forge::NODE_SWORD_T2, F("craft t3 param"));
    test.expectEq(t3.cost, 400, F("craft t3 baked direct cost"));
    test.expectEq(leave.action, screens::ACTION_LEAVE, F("craft leave row"));

    // ---------------------------------------------- card A forges/upgrades
    SaveBlock save;
    saveDefaults(save);
    save.zenny = 1000;
    save.items[item::ITEM_ORE] = 10;
    save.items[item::ITEM_SCALE] = 5;
    save.items[item::ITEM_FANG] = 5;
    test.expectEq(save.equippedNode, forge::NODE_SWORD_BASE, F("sword root equipped by default"));

    // CRAFT row 1 (SWD T2, node 1): the parent (root) is owned, so the default
    // card path takes the upgrade bill (100 + ore 2).
    ScreenRow upRow;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_CRAFT, 1), upRow);
    DetailState detail;
    CardItem cache;
    cardLoad(detail, cache, cardRowIndex(upRow), save, false);
    test.expectEq(detail.kind, cards::KIND_WEAPON, F("weapon card kind"));
    test.expectEq(detail.node.index, forge::NODE_SWORD_T1, F("cached forge node"));
    test.expectEq(cardHint(save, upRow, cache, detail.node), HINT_FORGE, F("affordable upgrade hint"));

    test.expectEq(cardApply(save, cache, detail.node, upRow), 1, F("card A forges"));
    test.expectEq(saveWeaponOwned(save, forge::NODE_SWORD_T1), 1, F("node owned"));
    test.expectEq(save.equippedNode, forge::NODE_SWORD_T1, F("equipped followed the upgrade"));
    test.expectEq(save.zenny, 900, F("upgrade cost debited"));
    test.expectEq(static_cast<uint32_t>(save.items[item::ITEM_ORE]), 8, F("upgrade ore debited"));
    test.expectEq(cardHint(save, upRow, cache, detail.node), HINT_NONE, F("owned node hint clears"));

    // A grandchild (node 2, CRAFT row 2): its parent (node 1) is owned now, so
    // the default bill is the upgrade (250 + ore 3 + fang 1). Own it first.
    ScreenRow grandRow;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_CRAFT, 2), grandRow);
    cardLoad(detail, cache, cardRowIndex(grandRow), save, false);
    test.expectEq(cardApply(save, cache, detail.node, grandRow), 1, F("grandchild forges"));
    test.expectEq(saveWeaponOwned(save, forge::NODE_SWORD_T2), 1, F("grandchild owned"));
    test.expectEq(save.zenny, 650, F("grandchild upgrade debited"));

    // hbk.10 direct bill: the same SWD T2 row opened from SCREEN_CRAFT (the
    // sketch sets DetailState::direct) charges the baked direct cost (180 +
    // ore 3) even though the root parent is owned.
    SaveBlock directSave;
    saveDefaults(directSave);
    directSave.zenny = 1000;
    directSave.items[item::ITEM_ORE] = 10;
    directSave.items[item::ITEM_SCALE] = 2;   // directMats = ore 3 + scale 2
    cardLoad(detail, cache, cardRowIndex(upRow), directSave, false);
    test.expectEq(cardHint(directSave, upRow, cache, detail.node, true), HINT_FORGE, F("direct craft hint"));
    test.expectEq(cardApply(directSave, cache, detail.node, upRow, true), 1, F("direct craft applies"));
    test.expectEq(directSave.zenny, 820, F("direct craft cost debited"));
    test.expectEq(static_cast<uint32_t>(directSave.items[item::ITEM_ORE]), 7, F("direct craft mats debited"));
    test.expectEq(saveWeaponOwned(directSave, forge::NODE_SWORD_T1), 1, F("direct craft owns the node"));

    // ---------------------------------------------- weapon equip/unequip + save
    // hbk.12: GEAR is a slot view (A equips in place), so the weapon-card equip
    // path is driven here with a synthetic equip row against the cached node.
    ScreenRow gearRow;
    gearRow.cost = 0;
    gearRow.action = screens::ACTION_EQUIP_WEAPON;
    gearRow.flags = screens::ROW_F_FORGE;
    gearRow.cond = screens::COND_ALWAYS;
    gearRow.param = forge::NODE_SWORD_T1;
    gearRow.unlock = 0;
    gearRow.recipe[0].item = 0;
    gearRow.recipe[0].count = 0;
    gearRow.recipe[1].item = 0;
    gearRow.recipe[1].count = 0;
    test.expectEq(cardRowIndex(gearRow), static_cast<uint8_t>(cards::WEAPON_BASE + forge::NODE_SWORD_T1), F("equip row card index"));
    cardLoad(detail, cache, cardRowIndex(gearRow), save, false);
    test.expectEq(cardHint(save, gearRow, cache, detail.node), HINT_EQUIP, F("owned -> equip hint"));
    test.expectEq(cardApply(save, cache, detail.node, gearRow), 1, F("card A equips"));
    test.expectEq(save.equippedNode, forge::NODE_SWORD_T1, F("equipped"));
    test.expectEq(cardHint(save, gearRow, cache, detail.node), HINT_UNEQUIP, F("equipped -> unequip hint"));
    test.expectEq(cardApply(save, cache, detail.node, gearRow), 1, F("card A unequips"));
    test.expectEq(save.equippedNode, SAVE_NODE_NONE, F("unequipped to none"));
    test.expectEq(cardApply(save, cache, detail.node, gearRow), 1, F("re-equip"));
    test.expectEq(save.equippedNode, forge::NODE_SWORD_T1, F("re-equipped"));

    // Persist + reload.
    test.expectEq(saveStore(save, REAL_BACKEND), 1, F("forge save stores"));
    SaveBlock back;
    test.expectEq(saveLoad(back, REAL_BACKEND), 1, F("forge save reloads"));
    test.expectEq(saveWeaponOwned(back, forge::NODE_SWORD_T1), 1, F("owned persisted"));
    test.expectEq(saveWeaponOwned(back, forge::NODE_SWORD_T2), 1, F("grandchild persisted"));
    test.expectEq(back.equippedNode, forge::NODE_SWORD_T1, F("equipped persisted"));
    test.expectEq(back.zenny, 650, F("zenny persisted"));
}

}   // namespace forgefx
