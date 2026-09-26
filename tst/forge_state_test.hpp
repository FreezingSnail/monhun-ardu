#pragma once
// Host unit tests for the weapon forge-tree logic (bead monhun-ardu-5co.4,
// docs/ui-design.md "Weapon tree model"): the direct-vs-upgrade state machine,
// the bill selection/debit, the equipped-follows-upgrade rule, the equip
// toggle, the list token rule and the save v5 owned bitset.
//
// src/forge_state.hpp is cart-free: these tests build plain ForgeNode structs
// (indices from the generated forge_meta.hpp) and exercise the same code the
// device card path runs.
#include "test.hpp"
#include "../src/forge_state.hpp"

using namespace mh;

namespace forgetest {

// One node with a two-slot bill on the given item code (item index + 1).
inline ForgeNode node(uint8_t index, uint8_t parent, bool direct, uint16_t cost, uint16_t directCost, uint8_t dmgMul, uint8_t spdMul, uint8_t matItem, uint8_t matCount, uint8_t dmatItem,
                      uint8_t dmatCount) {
    ForgeNode n{};
    n.index = index;
    n.cls = forge::WEAPON_SWORD;
    n.parent = parent;
    n.flags = direct ? forge::FLAG_DIRECT : 0;
    n.dmgMul = dmgMul;
    n.spdMul = spdMul;
    n.cost = cost;
    n.directCost = directCost;
    n.mats[0].item = matItem;
    n.mats[0].count = matCount;
    n.directMats[0].item = dmatItem;
    n.directMats[0].count = dmatCount;
    return n;
}

}   // namespace forgetest

using namespace forgetest;

void ForgeSuite(TestRunner &runner) {
    TestSuite suite("Forge trees: node states, bills, tokens, save v5 bitset (ui.4)");

    const uint8_t ROOT = forge::NODE_SWORD_BASE;
    const uint8_t CHILD = forge::NODE_SWORD_T1;
    const uint8_t GRAND = forge::NODE_SWORD_T2;
    const uint8_t ITEM = static_cast<uint8_t>(item::ITEM_ORE + 1);

    {
        Test t("node state: roots owned, child upgrade, grandchild direct, locked node dead");
        SaveBlock s;
        saveDefaults(s);
        s.zenny = 1000;
        s.items[item::ITEM_ORE] = 10;   // fund the bills so only the path differs
        // The default save owns every class root and equips the sword root.
        const ForgeNode root = node(ROOT, forge::NODE_NONE, true, 0, 0, 100, 100, 0, 0, 0, 0);
        t.assert(forgeNodeState(s, root, forge::NODE_COUNT), FORGE_EQUIPPED, "sword root is equipped");
        // child: parent (root) owned -> upgrade path.
        const ForgeNode child = node(CHILD, ROOT, true, 100, 180, 110, 105, ITEM, 2, ITEM, 3);
        t.assert(forgeNodeState(s, child, forge::NODE_COUNT), FORGE_UPGRADE, "owned parent -> upgrade");
        // grandchild: parent not owned -> direct.
        const ForgeNode grand = node(GRAND, CHILD, true, 250, 400, 125, 115, ITEM, 3, ITEM, 5);
        t.assert(forgeNodeState(s, grand, forge::NODE_COUNT), FORGE_DIRECT, "unowned parent -> direct");
        // direct=false with an unowned parent -> locked.
        const ForgeNode locked = node(GRAND, CHILD, false, 250, 250, 125, 115, ITEM, 3, ITEM, 3);
        t.assert(forgeNodeState(s, locked, forge::NODE_COUNT), FORGE_DEAD, "locked node dead");
        // bad node id -> dead.
        const ForgeNode bad = node(200, forge::NODE_NONE, true, 0, 0, 100, 100, 0, 0, 0, 0);
        t.assert(forgeNodeState(s, bad, forge::NODE_COUNT), FORGE_DEAD, "out-of-range node dead");
        suite.addTest(t);
    }

    {
        Test t("active bill picks upgrade vs direct; affordability gates on zenny + parts");
        SaveBlock s;
        saveDefaults(s);
        const ForgeNode child = node(CHILD, ROOT, true, 100, 180, 110, 105, ITEM, 2, ITEM, 3);
        ForgeBill up = forgeActiveBill(s, child);
        t.assert(up.cost, 100, "owned parent -> upgrade cost");
        t.assert(up.mats[0].count, 2, "upgrade mat count");
        s.zenny = 500;
        s.items[item::ITEM_ORE] = 5;
        t.assert(forgeAffordable(s, child), true, "affordable with parts + zenny");
        s.items[item::ITEM_ORE] = 1;
        t.assert(forgeNodeState(s, child, forge::NODE_COUNT), FORGE_NEED_PARTS, "short parts");
        s.items[item::ITEM_ORE] = 5;
        s.zenny = 50;
        t.assert(forgeNodeState(s, child, forge::NODE_COUNT), FORGE_NEED_ZENNY, "short zenny");
        // Grandchild direct bill is pricier.
        const ForgeNode grand = node(GRAND, CHILD, true, 250, 400, 125, 115, ITEM, 3, ITEM, 5);
        ForgeBill dir = forgeActiveBill(s, grand);
        t.assert(dir.cost, 400, "unowned parent -> direct cost");
        t.assert(dir.mats[0].count, 5, "direct mat count");
        suite.addTest(t);
    }

    {
        Test t("direct flag (hbk.10): forces the direct bill + path even when the parent is owned");
        SaveBlock s;
        saveDefaults(s);
        s.zenny = 1000;
        s.items[item::ITEM_ORE] = 10;
        const ForgeNode child = node(CHILD, ROOT, true, 100, 180, 110, 105, ITEM, 2, ITEM, 3);
        // Default: the root parent is owned, so the upgrade bill applies.
        t.assert(forgeNodeState(s, child, forge::NODE_COUNT), FORGE_UPGRADE, "default -> upgrade path");
        ForgeBill up = forgeActiveBill(s, child);
        t.assert(up.cost, 100, "default -> upgrade cost");
        // direct=true: the direct path + bill, parent owned or not.
        t.assert(forgeNodeState(s, child, forge::NODE_COUNT, true), FORGE_DIRECT, "direct flag -> direct path");
        ForgeBill directBill = forgeActiveBill(s, child, true);
        t.assert(directBill.cost, 180, "direct flag -> direct cost");
        t.assert(directBill.mats[0].count, 3, "direct flag -> direct mats");
        t.assert(forgeAffordable(s, child, true), true, "direct bill affordable");
        t.assert(forgeNodeApply(s, child, forge::NODE_COUNT, true), true, "direct forge applies");
        t.assert(s.zenny, 820, "direct cost debited (1000-180)");
        t.assert(s.items[item::ITEM_ORE], 7, "direct mats debited (10-3)");
        t.assert(saveWeaponOwned(s, CHILD), true, "node owned after direct forge");
        // An owned node still refuses with the direct flag (no re-debit).
        t.assert(forgeNodeApply(s, child, forge::NODE_COUNT, true), false, "owned node refuses even direct");
        t.assert(s.zenny, 820, "refused direct forge does not debit");
        suite.addTest(t);
    }

    {
        Test t("forgeNodeApply: upgrade debits the parent bill and sets the owned bit");
        SaveBlock s;
        saveDefaults(s);
        s.zenny = 500;
        s.items[item::ITEM_ORE] = 5;
        const ForgeNode child = node(CHILD, ROOT, true, 100, 180, 110, 105, ITEM, 2, ITEM, 3);
        t.assert(forgeNodeApply(s, child, forge::NODE_COUNT), true, "upgrade changes save");
        t.assert(saveWeaponOwned(s, CHILD), true, "child owned");
        t.assert(s.zenny, 400, "upgrade cost debited");
        t.assert(s.items[item::ITEM_ORE], 3, "upgrade mats debited");
        t.assert(forgeNodeApply(s, child, forge::NODE_COUNT), false, "already owned is a no-op");
        suite.addTest(t);
    }

    {
        Test t("forgeNodeApply: direct forge debits the pricier direct bill");
        SaveBlock s;
        saveDefaults(s);
        s.zenny = 1000;
        s.items[item::ITEM_ORE] = 10;
        const ForgeNode grand = node(GRAND, CHILD, true, 250, 400, 125, 115, ITEM, 3, ITEM, 5);
        t.assert(forgeNodeApply(s, grand, forge::NODE_COUNT), true, "direct forge changes save");
        t.assert(saveWeaponOwned(s, GRAND), true, "grandchild owned");
        t.assert(s.zenny, 600, "direct cost debited");
        t.assert(s.items[item::ITEM_ORE], 5, "direct mats debited");
        // A locked node refuses.
        const ForgeNode locked = node(GRAND, CHILD, false, 250, 250, 125, 115, ITEM, 3, ITEM, 3);
        t.assert(forgeNodeApply(s, locked, forge::NODE_COUNT), false, "locked node refuses");
        suite.addTest(t);
    }

    {
        Test t("equipped follows an upgrade of the equipped parent");
        SaveBlock s;
        saveDefaults(s);
        s.zenny = 500;
        s.items[item::ITEM_ORE] = 5;
        t.assert(s.equippedNode, ROOT, "root equipped by default");
        const ForgeNode child = node(CHILD, ROOT, true, 100, 180, 110, 105, ITEM, 2, ITEM, 3);
        t.assert(forgeNodeApply(s, child, forge::NODE_COUNT), true, "upgrade");
        t.assert(s.equippedNode, CHILD, "equipped id followed the upgrade");
        // Forging a non-equipped-parent node leaves the equipped id alone.
        const ForgeNode other = node(forge::NODE_FLAIL_T1, forge::NODE_FLAIL_BASE, true, 120, 200, 112, 103, ITEM, 2, ITEM, 3);
        s.zenny = 500;
        t.assert(forgeNodeApply(s, other, forge::NODE_COUNT), true, "forge flail child");
        t.assert(s.equippedNode, CHILD, "sword still equipped");
        suite.addTest(t);
    }

    {
        Test t("equip toggle: owned equips, equipped unequips to NONE, unowned refuses");
        SaveBlock s;
        saveDefaults(s);
        const ForgeNode root = node(ROOT, forge::NODE_NONE, true, 0, 0, 100, 100, 0, 0, 0, 0);
        const ForgeNode child = node(CHILD, ROOT, true, 100, 180, 110, 105, ITEM, 2, ITEM, 3);
        t.assert(forgeNodeEquipToggle(s, child), false, "unowned refuses");
        saveSetWeaponOwned(s, CHILD);
        t.assert(forgeNodeEquipToggle(s, child), true, "owned equips");
        t.assert(s.equippedNode, CHILD, "child equipped");
        t.assert(forgeNodeEquipToggle(s, child), true, "equipped unequips");
        t.assert(s.equippedNode, SAVE_NODE_NONE, "equipped cleared");
        t.assert(forgeNodeEquipToggle(s, root), true, "re-equip root");
        t.assert(s.equippedNode, ROOT, "root equipped");
        suite.addTest(t);
    }

    // The list E/OK/UP/DIR token was folded into the card hint (ui.4.1, 5co.7),
    // so the token classifier and its test are gone; the hint rule is pinned by
    // tst/card_state_test.hpp and the device suite.

    {
        Test t("save v5 owned bitset: set/get + bounds; armor crafted slot cap");
        SaveBlock s;
        saveDefaults(s);
        t.assert(saveWeaponOwned(s, SAVE_OWNED_SLOTS - 1), false, "top owned slot clear");
        saveSetWeaponOwned(s, SAVE_OWNED_SLOTS - 1);
        t.assert(saveWeaponOwned(s, SAVE_OWNED_SLOTS - 1), true, "top owned slot set");
        saveSetWeaponOwned(s, SAVE_OWNED_SLOTS);
        t.assert(saveWeaponOwned(s, SAVE_OWNED_SLOTS), false, "owned slot past the cap inert");
        t.assert(saveCrafted(s, SAVE_CRAFTED_SLOTS - 1), false, "top crafted slot clear");
        saveSetCrafted(s, SAVE_CRAFTED_SLOTS - 1);
        t.assert(saveCrafted(s, SAVE_CRAFTED_SLOTS - 1), true, "top crafted slot set");
        saveSetCrafted(s, SAVE_CRAFTED_SLOTS);
        t.assert(saveCrafted(s, SAVE_CRAFTED_SLOTS), false, "crafted slot past the cap inert");
        suite.addTest(t);
    }

    // jd1: the equipped node's sheet kind. The generated NODE_SHEET table is
    // cart-free (plain constants), so the host suite pins the mapping directly;
    // the device suite pins forgeEquippedSheet's save flow.
    {
        Test t("node sheet kinds: gun variants 1..4, class defaults 0 (jd1)");
        t.assert(forge::NODE_SHEET[forge::NODE_SWORD_BASE], 0, "sword default sheet");
        t.assert(forge::NODE_SHEET[forge::NODE_FLAIL_BASE], 0, "flail default sheet");
        t.assert(forge::NODE_SHEET[forge::NODE_GUN_BASE], 0, "gun base default sheet");
        t.assert(forge::NODE_SHEET[forge::NODE_GUN_BUCKLER], 1, "buckler kind");
        t.assert(forge::NODE_SHEET[forge::NODE_GUN_KITE], 2, "kite kind");
        t.assert(forge::NODE_SHEET[forge::NODE_GUN_TOWER], 3, "tower kind");
        t.assert(forge::NODE_SHEET[forge::NODE_GUN_BRACE], 4, "brace kind");
        t.assert(sizeof(forge::NODE_SHEET), sizeof(uint8_t) * forge::NODE_COUNT, "one kind byte per node");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
