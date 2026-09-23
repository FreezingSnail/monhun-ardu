#pragma once
// Host unit tests for the prebaked detail-card state machine (bead
// monhun-ardu-5co.3, docs/ui-design.md): src/card_state.hpp.
//
// Covers the cart-free half of the card path: the page mask helpers, the
// DetailState page machine (open/cycle/back, absent-page skipping), the
// row -> card mapping (kind + global card index, including the quest
// QUEST_BASE offset), the crafted-armor PARTS trim, the armor craft/equip
// action (ui.3.1, 5co.6: the bill bakes into the card record) and the dynamic
// hint rule. The cart read + blit half (src/cards.hpp) is device-only and is
// pinned by tst/fxdatatest/cards_test.hpp.
#include "test.hpp"
#include "../src/card_state.hpp"
#include "../src/core/save.hpp"
#include "../src/generated/armor_meta.hpp"
#include "../src/generated/card_meta.hpp"
#include "../src/generated/items_meta.hpp"
#include "../src/generated/quest_meta.hpp"

using namespace mh;

namespace cardstatetest {

// GEAR armor row: param = (slot << 5) | piece. ui.3.1: the craft bill lives on
// the card, not the row.
inline ScreenRow armorRow(uint8_t piece, uint8_t slot) {
    ScreenRow r;
    r.cost = 0;
    r.action = screens::ACTION_EQUIP_ARMOR;
    r.flags = 0;
    r.cond = screens::COND_ALWAYS;
    r.param = static_cast<uint8_t>((slot << 5) | piece);
    r.unlock = 0;
    r.recipe[0].item = 0;
    r.recipe[0].count = 0;
    r.recipe[1].item = 0;
    r.recipe[1].count = 0;
    return r;
}

// A decoded armor card record with the given craft bill. The HUNTER HELM bill
// (ore 3 + scale 2 + 300z) mirrors data/armor.json so the tests share the
// shipped numbers.
inline CardItem armorCard(uint16_t cost, uint8_t mat0 = 0, uint8_t cnt0 = 0, uint8_t mat1 = 0, uint8_t cnt1 = 0) {
    CardItem it;
    for (uint8_t i = 0; i < sizeof(CardItem); i++)
        reinterpret_cast<uint8_t *>(&it)[i] = 0;
    it.kind = cards::KIND_ARMOR;
    it.craft[0] = static_cast<uint8_t>(cost & 0xFF);
    it.craft[1] = static_cast<uint8_t>(cost >> 8);
    it.craft[2] = mat0;
    it.craft[3] = cnt0;
    it.craft[4] = mat1;
    it.craft[5] = cnt1;
    return it;
}

inline CardItem helmCard() {
    return armorCard(300, static_cast<uint8_t>(ITEM_ORE + 1), 3, static_cast<uint8_t>(ITEM_SCALE + 1), 2);
}

// COND_QUEST row: param = (need << 4) | quest id.
inline ScreenRow questRow(uint8_t action, uint8_t quest, uint8_t need) {
    ScreenRow r;
    r.cost = 0;
    r.action = action;
    r.flags = 0;
    r.cond = screens::COND_QUEST;
    r.param = static_cast<uint8_t>((need << 4) | quest);
    r.unlock = 0;
    r.recipe[0].item = 0;
    r.recipe[0].count = 0;
    r.recipe[1].item = 0;
    r.recipe[1].count = 0;
    return r;
}

inline void CardStateSuite(TestRunner &runner) {
    TestSuite suite("Card state (5co.3): page mask, nav, row mapping, hint");

    // ------------------------------------------------------- mask helpers
    {
        Test t("mask helpers: bit / has / popcount / first page");
        t.assert(cardPageBit(cards::PAGE_DESC), 1, "DESC bit");
        t.assert(cardPageBit(cards::PAGE_PARTS), 2, "PARTS bit");
        t.assert(cardPageBit(cards::PAGE_STATS), 4, "STATS bit");
        t.assert(cardPageBit(cards::PAGE_SKILL), 8, "SKILL bit");
        t.assert(cardMaskHas(0x05, cards::PAGE_DESC), true, "0x05 has DESC");
        t.assert(cardMaskHas(0x05, cards::PAGE_PARTS), false, "0x05 has no PARTS");
        t.assert(cardPopcount(0x00), 0, "empty popcount");
        t.assert(cardPopcount(0x0F), 4, "full popcount");
        t.assert(cardPopcount(0x06), 2, "0x06 popcount");
        t.assert(cardFirstPage(0x00), 0, "empty first page");
        t.assert(cardFirstPage(0x06), cards::PAGE_PARTS, "first present page");
        suite.addTest(t);
    }

    // --------------------------------------------------- open / close / nav
    {
        Test t("DetailState: open lands on the first present page, nav skips absent");
        DetailState s;
        t.assert(s.active, false, "starts closed");
        cardOpen(s, cards::KIND_QUEST, cards::CARD_QUEST_GATHER_ORE, 0x05);
        t.assert(s.active, true, "opened");
        t.assert(s.kind, cards::KIND_QUEST, "kind");
        t.assert(s.index, cards::CARD_QUEST_GATHER_ORE, "index");
        t.assert(s.pageMask, 0x05, "mask");
        t.assert(s.pageCount, 2, "page count is the popcount");
        t.assert(s.page, cards::PAGE_DESC, "first present page");
        t.assert(s.navX, 0, "nav released");

        // 0x05 = DESC + STATS: RIGHT skips the absent PARTS page, wraps back.
        cardNav(s, 1);
        t.assert(s.page, cards::PAGE_STATS, "RIGHT skips PARTS");
        cardNav(s, 1);
        t.assert(s.page, cards::PAGE_DESC, "RIGHT wraps to DESC");
        cardNav(s, -1);
        t.assert(s.page, cards::PAGE_STATS, "LEFT skips PARTS backwards");

        // Empty mask: nav is a no-op.
        DetailState empty;
        cardOpen(empty, cards::KIND_ARMOR, 0, 0x00);
        t.assert(empty.pageCount, 0, "empty page count");
        cardNav(empty, 1);
        t.assert(empty.page, 0, "empty mask nav no-op");

        cardClose(s);
        t.assert(s.active, false, "closed");
        suite.addTest(t);
    }

    // ------------------------------------------------------------ input tick
    {
        Test t("detailStep: LEFT/RIGHT edges nav once, A/B rising edges fire");
        DetailState s;
        cardOpen(s, cards::KIND_ARMOR, cards::CARD_ARMOR_HUNTER_HELM, 0x0F);
        const Input idle = {0, 0, false, false};
        const Input right = {1, 0, false, false};
        const Input left = {-1, 0, false, false};
        const Input a = {0, 0, true, false};
        const Input b = {0, 0, false, true};

        t.assert(detailStep(s, right), DETAIL_NONE, "RIGHT is silent");
        t.assert(s.page, cards::PAGE_PARTS, "RIGHT pages");
        t.assert(detailStep(s, right), DETAIL_NONE, "held RIGHT does not repeat");
        t.assert(s.page, cards::PAGE_PARTS, "page held");
        t.assert(detailStep(s, idle), DETAIL_NONE, "release silent");
        t.assert(detailStep(s, right), DETAIL_NONE, "fresh RIGHT pages again");
        t.assert(s.page, cards::PAGE_STATS, "fresh RIGHT moved");
        t.assert(detailStep(s, left), DETAIL_NONE, "LEFT silent");
        t.assert(s.page, cards::PAGE_PARTS, "LEFT back");
        detailStep(s, idle);

        t.assert(detailStep(s, a), DETAIL_ACTION, "A rising edge");
        t.assert(detailStep(s, a), DETAIL_NONE, "held A silent");
        detailStep(s, idle);
        t.assert(detailStep(s, b), DETAIL_BACK, "B rising edge");
        t.assert(detailStep(s, b), DETAIL_NONE, "held B silent");
        suite.addTest(t);
    }

    // ------------------------------------------------------- row -> card map
    {
        Test t("row mapping: kind + global card index (armor prefix, quest base)");
        const ScreenRow equip = armorRow(armor::ARMOR_BONE_MAIL, armor::SLOT_BODY);
        t.assert(cardRowOpens(equip), true, "equip row opens");
        t.assert(cardRowKind(equip), cards::KIND_ARMOR, "equip row is armor");
        t.assert(cardRowIndex(equip), cards::CARD_ARMOR_BONE_MAIL, "equip index is the piece");

        const ScreenRow take = questRow(screens::ACTION_TAKE_QUEST, quests::QUEST_SLAY_LUNGE, 3);
        t.assert(cardRowOpens(take), true, "take row opens");
        t.assert(cardRowKind(take), cards::KIND_QUEST, "take row is a quest");
        t.assert(cardRowIndex(take), cards::CARD_QUEST_SLAY_LUNGE, "take index is QUEST_BASE + id");

        const ScreenRow turnIn = questRow(screens::ACTION_TURN_IN_QUEST, quests::QUEST_CRUSH_HEAVY, 1);
        t.assert(cardRowOpens(turnIn), true, "turn-in row opens");
        t.assert(cardRowIndex(turnIn), cards::CARD_QUEST_CRUSH_HEAVY, "turn-in index is QUEST_BASE + id");

        // Every shipped quest maps onto its generated card constant.
        t.assert(cards::CARD_QUEST_SLAY_LUNGE, cards::QUEST_BASE + quests::QUEST_SLAY_LUNGE, "lunge index");
        t.assert(cards::CARD_QUEST_SLAY_SWEEP, cards::QUEST_BASE + quests::QUEST_SLAY_SWEEP, "sweep index");
        t.assert(cards::CARD_QUEST_GATHER_ORE, cards::QUEST_BASE + quests::QUEST_GATHER_ORE, "gather index");
        t.assert(cards::CARD_QUEST_CRUSH_HEAVY, cards::QUEST_BASE + quests::QUEST_CRUSH_HEAVY, "crush index");
        t.assert(cards::CARD_QUEST_CRUSH_HEAVY, cards::QUEST_BASE + quests::QUEST_CRUSH_HEAVY, "quests follow the armor prefix");

        // ui.4: weapon rows open a KIND_WEAPON card; the card index is
        // WEAPON_BASE + node id (the forge node table order).
        ScreenRow weapon;
        weapon.cost = 0;
        weapon.action = screens::ACTION_EQUIP_WEAPON;
        weapon.flags = screens::ROW_F_FORGE;
        weapon.cond = screens::COND_ALWAYS;
        weapon.param = forge::NODE_SWORD_T1;
        weapon.unlock = 0;
        weapon.recipe[0].item = 0;
        weapon.recipe[1].item = 0;
        t.assert(cardRowOpens(weapon), true, "weapon row opens");
        t.assert(cardRowKind(weapon), cards::KIND_WEAPON, "weapon row is a weapon card");
        t.assert(cardRowIndex(weapon), cards::WEAPON_BASE + forge::NODE_SWORD_T1, "weapon index is WEAPON_BASE + node");
        t.assert(cards::CARD_WEAPON_SWORD_T1, cards::WEAPON_BASE + forge::NODE_SWORD_T1, "weapon constant matches");
        // A FORGE row maps the same way.
        weapon.action = screens::ACTION_FORGE_NODE;
        t.assert(cardRowOpens(weapon), true, "forge row opens");
        t.assert(cardRowKind(weapon), cards::KIND_WEAPON, "forge row is a weapon card");
        t.assert(cardRowIndex(weapon), cards::WEAPON_BASE + forge::NODE_SWORD_T1, "forge index is WEAPON_BASE + node");
        suite.addTest(t);
    }

    // --------------------------------------------------- weapon card (ui.4)
    {
        Test t("weapon card hint/apply: FORGE forges, GEAR equips/unequips");
        SaveBlock s;
        saveDefaults(s);
        s.zenny = 1000;
        s.items[ITEM_ORE] = 10;
        ForgeNode child{};
        child.index = forge::NODE_SWORD_T1;
        child.parent = forge::NODE_SWORD_BASE;
        child.flags = forge::FLAG_DIRECT;
        child.dmgMul = 110;
        child.spdMul = 105;
        child.cost = 100;
        child.directCost = 180;
        child.mats[0].item = static_cast<uint8_t>(ITEM_ORE + 1);
        child.mats[0].count = 2;
        child.directMats[0].item = static_cast<uint8_t>(ITEM_ORE + 1);
        child.directMats[0].count = 3;
        ScreenRow forge;
        forge.cost = 100;
        forge.action = screens::ACTION_FORGE_NODE;
        forge.flags = screens::ROW_F_FORGE;
        forge.cond = screens::COND_ALWAYS;
        forge.param = forge::NODE_SWORD_T1;
        forge.unlock = 0;
        forge.recipe[0].item = 0;
        forge.recipe[0].count = 0;
        forge.recipe[1].item = 0;
        forge.recipe[1].count = 0;
        const CardItem none{};
        t.assert(cardHint(s, forge, none, child), HINT_FORGE, "affordable upgrade -> A FORGE");
        t.assert(cardApply(s, none, child, forge), true, "forge applies");
        t.assert(saveWeaponOwned(s, forge::NODE_SWORD_T1), true, "node owned");
        t.assert(s.equippedNode, forge::NODE_SWORD_T1, "equipped parent followed the upgrade");
        t.assert(cardHint(s, forge, none, child), HINT_NONE, "owned node has no forge hint");
        t.assert(cardApply(s, none, child, forge), false, "owned node re-forge no-op");
        // GEAR equip/unequip.
        ScreenRow gear = forge;
        gear.action = screens::ACTION_EQUIP_WEAPON;
        t.assert(cardHint(s, gear, none, child), HINT_UNEQUIP, "equipped -> A UNEQUIP");
        t.assert(cardApply(s, none, child, gear), true, "unequip applies");
        t.assert(s.equippedNode, SAVE_NODE_NONE, "unequipped to none");
        t.assert(cardHint(s, gear, none, child), HINT_EQUIP, "owned -> A EQUIP");
        // An unowned node on GEAR is inert.
        ForgeNode grand{};
        grand.index = forge::NODE_SWORD_T2;
        grand.parent = forge::NODE_SWORD_T1;
        grand.flags = forge::FLAG_DIRECT;
        t.assert(cardHint(s, gear, none, grand), HINT_NONE, "unowned -> silent");
        t.assert(cardApply(s, none, grand, gear), false, "unowned equip no-op");
        suite.addTest(t);
    }

    // ------------------------------------------------ crafted PARTS trim
    {
        Test t("cardArmorMask / cardSetMask: craft drops PARTS, page clamps");
        const uint8_t full = 0x0F;
        t.assert(cardArmorMask(full, false), 0x0F, "uncrafted keeps all pages");
        t.assert(cardArmorMask(full, true), 0x0F & ~cardPageBit(cards::PAGE_PARTS), "crafted drops PARTS");

        DetailState s;
        cardOpen(s, cards::KIND_ARMOR, cards::CARD_ARMOR_HUNTER_HELM, full);
        s.page = cards::PAGE_PARTS;
        cardSetMask(s, cardArmorMask(full, true));
        t.assert(s.pageCount, 3, "page count follows the trimmed mask");
        t.assert(cardMaskHas(s.pageMask, cards::PAGE_PARTS), false, "PARTS gone from the mask");
        t.assert(s.page, cards::PAGE_DESC, "current page clamped to the first present");

        // An armor mask without a PARTS page (e.g. a zenny-only piece) is
        // unchanged; cardLoad only applies the trim for KIND_ARMOR (quest page
        // ids share the bit space, so the caller gates on kind -- device test).
        t.assert(cardArmorMask(0x0D, true), 0x0D, "no PARTS bit -> trim is a no-op");
        suite.addTest(t);
    }

    // ------------------------------------------------- armor card action
    {
        Test t("cardArmorApply: craft gate/debit/equip, crafted toggle, bad ids");
        const ScreenRow helm = armorRow(armor::ARMOR_HUNTER_HELM, armor::SLOT_HEAD);
        const CardItem card = helmCard();

        SaveBlock s;
        saveDefaults(s);
        s.zenny = 500;
        s.items[ITEM_ORE] = 3;
        s.items[ITEM_SCALE] = 2;
        t.assert(armorCardState(s, card, helm), ARMOR_CRAFT, "affordable uncrafted -> craft");
        t.assert(cardArmorApply(s, card, helm), true, "craft+equip applies");
        t.assert(saveCrafted(s, armor::ARMOR_HUNTER_HELM), true, "crafted bit set");
        t.assert(s.zenny, 200, "zenny debited");
        t.assert(s.items[ITEM_ORE], 0, "ore debited");
        t.assert(s.items[ITEM_SCALE], 0, "scale debited");
        t.assert(s.equip[armor::SLOT_HEAD], armor::ARMOR_HUNTER_HELM + 1, "craft auto-equips");
        t.assert(cardArmorApply(s, card, helm), true, "second A unequips");
        t.assert(s.equip[armor::SLOT_HEAD], SAVE_EQUIP_NONE, "unequipped");
        t.assert(s.zenny, 200, "no second debit");
        t.assert(cardArmorApply(s, card, helm), true, "third A re-equips");
        t.assert(s.equip[armor::SLOT_HEAD], armor::ARMOR_HUNTER_HELM + 1, "re-equipped");

        // Blocked: short zenny and a missing material are inert (no debit).
        SaveBlock poor;
        saveDefaults(poor);
        poor.zenny = 299;
        poor.items[ITEM_ORE] = 3;
        poor.items[ITEM_SCALE] = 2;
        t.assert(cardArmorApply(poor, card, helm), false, "short zenny rejected");
        t.assert(saveCrafted(poor, armor::ARMOR_HUNTER_HELM), false, "short zenny: not crafted");
        SaveBlock nomat;
        saveDefaults(nomat);
        nomat.zenny = 500;
        nomat.items[ITEM_ORE] = 2;   // need 3
        t.assert(cardArmorApply(nomat, card, helm), false, "missing material rejected");
        t.assert(nomat.items[ITEM_ORE], 2, "missing material: no debit");

        // Out-of-range piece/slot rows are inert.
        t.assert(cardArmorApply(s, card, armorRow(armor::PIECE_COUNT, armor::SLOT_HEAD)), false, "bad piece rejected");
        t.assert(cardArmorApply(s, card, armorRow(armor::ARMOR_HUNTER_HELM, 3)), false, "bad slot rejected");
        suite.addTest(t);
    }

    // ---------------------------------------------------------- hint rule
    {
        Test t("cardHint: craft/equip/unequip + blocked reasons + quests");
        const ScreenRow gear = armorRow(armor::ARMOR_HUNTER_HELM, armor::SLOT_HEAD);
        const CardItem card = helmCard();

        SaveBlock s;
        saveDefaults(s);
        s.zenny = 500;
        s.items[ITEM_ORE] = 3;
        s.items[ITEM_SCALE] = 2;
        t.assert(cardHint(s, gear, card, ForgeNode{}), HINT_CRAFT, "affordable uncrafted -> A CRAFT");

        SaveBlock poor = s;
        poor.zenny = 299;
        t.assert(cardHint(poor, gear, card, ForgeNode{}), HINT_NEED_ZENNY, "short zenny -> NEED ZENNY");
        SaveBlock nomat = s;
        nomat.items[ITEM_ORE] = 2;
        t.assert(cardHint(nomat, gear, card, ForgeNode{}), HINT_NEED_PARTS, "missing parts -> NEED PARTS");

        saveSetCrafted(s, armor::ARMOR_HUNTER_HELM);
        t.assert(cardHint(s, gear, card, ForgeNode{}), HINT_EQUIP, "crafted, unequipped -> A EQUIP");
        s.equip[armor::SLOT_HEAD] = armor::ARMOR_HUNTER_HELM + 1;
        t.assert(cardHint(s, gear, card, ForgeNode{}), HINT_UNEQUIP, "crafted + equipped -> A UNEQUIP");
        // A crafted piece ignores the (spent) materials/zenny bill.
        saveSetCrafted(poor, armor::ARMOR_HUNTER_HELM);
        t.assert(cardHint(poor, gear, card, ForgeNode{}), HINT_EQUIP, "crafted row live despite empty wallet");

        // Quests: takeable -> ACCEPT; active+ready -> TURN IN; else silent.
        // The armor bill is ignored for quest rows.
        const ScreenRow take = questRow(screens::ACTION_TAKE_QUEST, quests::QUEST_SLAY_LUNGE, 3);
        const ScreenRow turnIn = questRow(screens::ACTION_TURN_IN_QUEST, quests::QUEST_SLAY_LUNGE, 3);
        SaveBlock q;
        saveDefaults(q);
        t.assert(cardHint(q, take, card, ForgeNode{}), HINT_ACCEPT, "fresh take row -> A ACCEPT");
        t.assert(cardHint(q, turnIn, card, ForgeNode{}), HINT_NONE, "inactive turn-in row silent");
        saveQuestSet(q, quests::QUEST_SLAY_LUNGE, 0);
        q.activeQuest = quests::QUEST_SLAY_LUNGE;
        q.progress = 2;
        t.assert(cardHint(q, turnIn, card, ForgeNode{}), HINT_NONE, "progress short -> silent");
        t.assert(cardHint(q, take, card, ForgeNode{}), HINT_NONE, "already taken -> silent");
        q.progress = 3;
        t.assert(cardHint(q, turnIn, card, ForgeNode{}), HINT_TURN_IN, "ready -> A TURN IN");

        // Locked chain row: silent until the prior quest is done.
        ScreenRow locked = take;
        locked.unlock = 2;
        SaveBlock chain;
        saveDefaults(chain);
        t.assert(cardHint(chain, locked, card, ForgeNode{}), HINT_NONE, "locked chain row silent");
        saveQuestSet(chain, 1, 1);
        t.assert(cardHint(chain, locked, card, ForgeNode{}), HINT_ACCEPT, "unlocked chain row -> A ACCEPT");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}

}   // namespace cardstatetest
