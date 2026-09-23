#pragma once
// Host unit tests for the prebaked detail-card state machine (bead
// monhun-ardu-5co.3, docs/ui-design.md): src/card_state.hpp.
//
// Covers the cart-free half of the card path: the page mask helpers, the
// DetailState page machine (open/cycle/back, absent-page skipping), the
// row -> card mapping (kind + global card index, including the quest
// QUEST_BASE offset), the crafted-armor PARTS trim, and the dynamic hint rule.
// The cart read + blit half (src/cards.hpp) is device-only and is pinned by
// tst/fxdatatest/cards_test.hpp; the action itself is screenApplyAction and is
// covered by tst/armor_engine_test.hpp / tst/screens_test.hpp.
#include "test.hpp"
#include "../src/card_state.hpp"
#include "../src/core/save.hpp"
#include "../src/generated/armor_meta.hpp"
#include "../src/generated/card_meta.hpp"
#include "../src/generated/items_meta.hpp"
#include "../src/generated/quest_meta.hpp"

using namespace mh;

namespace cardstatetest {

// COND_ARMOR / ACTION_CRAFT_ARMOR row: param = (slot << 5) | piece. The
// HUNTER HELM recipe (ore 3 + scale 2 + 300z) mirrors data/armor.json so the
// hint/action tests share the shipped bill.
inline ScreenRow armorRow(uint8_t action, uint8_t piece, uint8_t slot, uint16_t cost = 300) {
    ScreenRow r;
    r.cost = cost;
    r.action = action;
    r.flags = 0;
    r.cond = (action == screens::ACTION_CRAFT_ARMOR) ? screens::COND_ARMOR : screens::COND_CRAFTED;
    r.param = static_cast<uint8_t>((slot << 5) | piece);
    r.unlock = 0;
    r.recipe[0].item = ITEM_ORE + 1;
    r.recipe[0].count = 3;
    r.recipe[1].item = ITEM_SCALE + 1;
    r.recipe[1].count = 2;
    return r;
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
        const ScreenRow craft = armorRow(screens::ACTION_CRAFT_ARMOR, armor::ARMOR_HUNTER_HELM, armor::SLOT_HEAD);
        t.assert(cardRowOpens(craft), true, "craft row opens");
        t.assert(cardRowKind(craft), cards::KIND_ARMOR, "craft row is armor");
        t.assert(cardRowIndex(craft), cards::CARD_ARMOR_HUNTER_HELM, "craft index is the piece");

        const ScreenRow equip = armorRow(screens::ACTION_EQUIP_ARMOR, armor::ARMOR_BONE_MAIL, armor::SLOT_BODY);
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
        t.assert(cards::CARD_QUEST_CRUSH_HEAVY, cards::ITEM_COUNT - 1, "quests follow the armor prefix");

        // GEAR weapon rows keep their direct-equip action (ui.3 scope split).
        ScreenRow weapon;
        weapon.cost = 0;
        weapon.action = screens::ACTION_EQUIP_WEAPON;
        weapon.flags = 0;
        weapon.cond = screens::COND_ALWAYS;
        weapon.param = 1;
        weapon.unlock = 0;
        weapon.recipe[0].item = 0;
        weapon.recipe[1].item = 0;
        t.assert(cardRowOpens(weapon), false, "weapon row keeps the direct action");
        t.assert(cardRowKind(weapon), cards::KIND_QUEST, "weapon row maps to the quest fallback (unused)");
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

    // ---------------------------------------------------------- hint rule
    {
        Test t("cardHint: craft/equip/unequip + blocked reasons + quests");
        const ScreenRow craft = armorRow(screens::ACTION_CRAFT_ARMOR, armor::ARMOR_HUNTER_HELM, armor::SLOT_HEAD);
        const ScreenRow gear = armorRow(screens::ACTION_EQUIP_ARMOR, armor::ARMOR_HUNTER_HELM, armor::SLOT_HEAD);

        SaveBlock s;
        saveDefaults(s);
        s.zenny = 500;
        s.items[ITEM_ORE] = 3;
        s.items[ITEM_SCALE] = 2;
        t.assert(cardHint(s, craft), HINT_CRAFT, "affordable uncrafted -> A CRAFT");
        t.assert(cardHint(s, gear), HINT_NONE, "uncrafted GEAR row has no action");

        SaveBlock poor = s;
        poor.zenny = 299;
        t.assert(cardHint(poor, craft), HINT_NEED_ZENNY, "short zenny -> NEED ZENNY");
        SaveBlock nomat = s;
        nomat.items[ITEM_ORE] = 2;
        t.assert(cardHint(nomat, craft), HINT_NEED_PARTS, "missing parts -> NEED PARTS");

        saveSetCrafted(s, armor::ARMOR_HUNTER_HELM);
        t.assert(cardHint(s, craft), HINT_EQUIP, "crafted, unequipped -> A EQUIP");
        t.assert(cardHint(s, gear), HINT_EQUIP, "crafted GEAR row -> A EQUIP");
        s.equip[armor::SLOT_HEAD] = armor::ARMOR_HUNTER_HELM + 1;
        t.assert(cardHint(s, craft), HINT_UNEQUIP, "crafted + equipped -> A UNEQUIP");
        t.assert(cardHint(s, gear), HINT_UNEQUIP, "equipped GEAR row -> A UNEQUIP");
        // A crafted piece ignores the (spent) materials/zenny bill.
        t.assert(cardHint(poor, gear), HINT_NONE, "dead crafted state is GEAR-dead");
        saveSetCrafted(poor, armor::ARMOR_HUNTER_HELM);
        t.assert(cardHint(poor, craft), HINT_EQUIP, "crafted row live despite empty wallet");

        // Quests: takeable -> ACCEPT; active+ready -> TURN IN; else silent.
        const ScreenRow take = questRow(screens::ACTION_TAKE_QUEST, quests::QUEST_SLAY_LUNGE, 3);
        const ScreenRow turnIn = questRow(screens::ACTION_TURN_IN_QUEST, quests::QUEST_SLAY_LUNGE, 3);
        SaveBlock q;
        saveDefaults(q);
        t.assert(cardHint(q, take), HINT_ACCEPT, "fresh take row -> A ACCEPT");
        t.assert(cardHint(q, turnIn), HINT_NONE, "inactive turn-in row silent");
        saveQuestSet(q, quests::QUEST_SLAY_LUNGE, 0);
        q.activeQuest = quests::QUEST_SLAY_LUNGE;
        q.progress = 2;
        t.assert(cardHint(q, turnIn), HINT_NONE, "progress short -> silent");
        t.assert(cardHint(q, take), HINT_NONE, "already taken -> silent");
        q.progress = 3;
        t.assert(cardHint(q, turnIn), HINT_TURN_IN, "ready -> A TURN IN");

        // Locked chain row: silent until the prior quest is done.
        ScreenRow locked = take;
        locked.unlock = 2;
        SaveBlock chain;
        saveDefaults(chain);
        t.assert(cardHint(chain, locked), HINT_NONE, "locked chain row silent");
        saveQuestSet(chain, 1, 1);
        t.assert(cardHint(chain, locked), HINT_ACCEPT, "unlocked chain row -> A ACCEPT");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}

}   // namespace cardstatetest
