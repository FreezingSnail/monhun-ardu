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

    // --------------------------------------------------- FORGE row layout
    test.expectEq(screens::SCREEN_FORGE, 3, F("forge screen index"));
    test.expectEq(screenRowCount(screens::SCREEN_FORGE), 13, F("forge row count"));
    ScreenRow hdr, t1, t3, leave;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_FORGE, 0), hdr);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_FORGE, 1), t1);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_FORGE, 3), t3);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_FORGE, 12), leave);
    test.expectEq(hdr.action, screens::ACTION_NONE, F("forge header row"));
    test.expectEq(t1.action, screens::ACTION_FORGE_NODE, F("forge node action"));
    test.expectEq(t1.param, forge::NODE_SWORD_BASE, F("forge root param"));
    test.expectEq(t1.cost, 0, F("forge root cost"));
    test.expectEq(t1.flags, screens::ROW_F_FORGE, F("forge row flag"));
    test.expectEq(t3.param, forge::NODE_SWORD_T2, F("forge t3 param"));
    test.expectEq(t3.cost, 250, F("forge t3 cost"));
    test.expectEq(leave.action, screens::ACTION_LEAVE, F("forge leave row"));

    // ---------------------------------------------- card A forges/upgrades
    SaveBlock save;
    saveDefaults(save);
    save.zenny = 500;
    save.items[item::ITEM_ORE] = 5;
    save.items[item::ITEM_SCALE] = 5;
    save.items[item::ITEM_FANG] = 5;
    test.expectEq(save.equippedNode, forge::NODE_SWORD_BASE, F("sword root equipped by default"));

    // FORGE row 2 is "+- SWD T2" (node 1): the parent (root) is owned -> UP.
    ScreenRow upRow;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_FORGE, 2), upRow);
    DetailState detail;
    CardItem cache;
    cardLoad(detail, cache, cardRowIndex(upRow), save, false);
    test.expectEq(detail.kind, cards::KIND_WEAPON, F("weapon card kind"));
    test.expectEq(detail.node.index, forge::NODE_SWORD_T1, F("cached forge node"));
    test.expectEq(cardHint(save, upRow, cache, detail.node), HINT_FORGE, F("affordable upgrade hint"));

    test.expectEq(cardApply(save, cache, detail.node, upRow), 1, F("card A forges"));
    test.expectEq(saveWeaponOwned(save, forge::NODE_SWORD_T1), 1, F("node owned"));
    test.expectEq(save.equippedNode, forge::NODE_SWORD_T1, F("equipped followed the upgrade"));
    test.expectEq(save.zenny, 400, F("upgrade cost debited"));
    test.expectEq(static_cast<uint32_t>(save.items[item::ITEM_ORE]), 3, F("upgrade ore debited"));
    test.expectEq(cardHint(save, upRow, cache, detail.node), HINT_NONE, F("owned node hint clears"));

    // A direct forge of the grandchild (node 2): its parent (node 1) is owned
    // now, so this is an upgrade, not a direct bill. Own it first.
    ScreenRow grandRow;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_FORGE, 3), grandRow);
    cardLoad(detail, cache, cardRowIndex(grandRow), save, false);
    test.expectEq(cardApply(save, cache, detail.node, grandRow), 1, F("grandchild forges"));
    test.expectEq(saveWeaponOwned(save, forge::NODE_SWORD_T2), 1, F("grandchild owned"));

    // ---------------------------------------------- GEAR equip/unequip + save
    ScreenRow gearRow;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 2), gearRow);   // "+- SWD T2" node 1
    test.expectEq(gearRow.action, screens::ACTION_EQUIP_WEAPON, F("gear weapon row"));
    test.expectEq(gearRow.param, forge::NODE_SWORD_T1, F("gear row param node"));
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
    test.expectEq(back.zenny, 150, F("zenny persisted"));
}

}   // namespace forgefx
