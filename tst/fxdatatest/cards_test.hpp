#pragma once
// On-device suite for the prebaked detail cards (bead monhun-ardu-5co.3,
// docs/ui-design.md).
//
// Covers the shipped cart path the host suite cannot: reading the mhCards
// record through src/cards.hpp (kind/mask/overlays/page offsets + the baked
// armor craft bill), the list-row -> card mapping against the real gear/quests
// cart rows, the card -> A action E2E (armor craft/equip / take / turn-in
// change the save, EEPROM roundtrip), the crafted-armor PARTS trim on refresh,
// and framebuffer checks
// for the blitted page + the two dynamic overlay kinds (PARTS have-count,
// quest PROG bar) across the triplane passes.
#include "harness/fxtest.hpp"
#include "src/cards.hpp"
#include "src/screens.hpp"
#include "src/fxdata.h"

#include <stdint.h>

namespace cardsfx {

using namespace mh;

static const SaveBackend REAL_BACKEND = {saveEepromRead, saveEepromWrite};

// ---- framebuffer helpers (screens_test.hpp style) --------------------------
static void clearFb() {
    uint8_t *b = arduboy.getBuffer();
    for (uint16_t i = 0; i < 1024; i++)
        b[i] = 0;
}

static uint8_t bitAt(uint8_t x, uint8_t y) {
    return static_cast<uint8_t>((arduboy.getBuffer()[static_cast<uint16_t>(y >> 3) * 128 + x] >> (y & 7)) & 1);
}

static uint16_t countBits(uint8_t xa, uint8_t xb, uint8_t ya, uint8_t yb) {
    uint16_t n = 0;
    for (uint8_t y = ya; y <= yb; y++)
        for (uint8_t x = xa; x <= xb; x++)
            n += bitAt(x, y);
    return n;
}

// Copy a 128 px page row (n bytes from x0 on framebuffer page `page`) so a
// dynamic digit can be compared across two save states.
static void rowSnapshot(uint8_t *dst, uint8_t page, uint8_t x0, uint8_t n) {
    const uint8_t *b = arduboy.getBuffer() + static_cast<uint16_t>(page) * 128;
    for (uint8_t i = 0; i < n; i++)
        dst[i] = b[x0 + i];
}

// Park on a specific triplane pass (mirrors the main loop's bracket).
static void waitPlane(uint8_t plane) {
    while (arduboy.currentPlane() != plane) {
        FX::enableOLED();
        arduboy.waitForNextPlane();
        FX::disableOLED();
    }
}

inline void test_cards(FxTest &test) {
    // ------------------------------------------------- generated cart records
    test.expectEq(cards::ITEM_COUNT, 31, F("card item count"));
    test.expectEq(cards::QUEST_BASE, armor::PIECE_COUNT, F("quest base follows the armor prefix"));
    test.expectEq(cards::CARD_ARMOR_HUNTER_HELM, 0, F("helm card index"));
    test.expectEq(cards::CARD_QUEST_SLAY_LUNGE, cards::QUEST_BASE + quests::QUEST_SLAY_LUNGE, F("lunge card index"));
    // ui.4: weapon cards follow the quests; the last node is the last card.
    test.expectEq(cards::WEAPON_BASE, cards::QUEST_BASE + quests::QUEST_COUNT, F("weapon base follows the quests"));
    test.expectEq(cards::CARD_WEAPON_SWORD_BASE, cards::WEAPON_BASE + forge::NODE_SWORD_BASE, F("sword root card index"));
    test.expectEq(cards::CARD_WEAPON_GUN_BRACE, cards::ITEM_COUNT - 1, F("last weapon node is the last card"));

    CardItem it;
    cardReadItem(cards::CARD_ARMOR_HUNTER_HELM, it);
    test.expectEq(it.kind, cards::KIND_ARMOR, F("helm kind"));
    test.expectEq(it.pageMask, 0x0F, F("helm mask"));
    test.expectEq(it.overlayCount, 2, F("helm overlay count"));
    // Baked armor craft bill (ui.3.1, 5co.6): 300z, ore x3 + scale x2 (idx+1).
    test.expectEq(armorCardCost(it), 300, F("helm craft cost"));
    test.expectEq(it.craft[2], static_cast<uint8_t>(item::ITEM_ORE + 1), F("helm ore code"));
    test.expectEq(it.craft[3], 3, F("helm ore count"));
    test.expectEq(it.craft[4], static_cast<uint8_t>(item::ITEM_SCALE + 1), F("helm scale code"));
    test.expectEq(it.craft[5], 2, F("helm scale count"));
    test.expectEq(it.overlays[0].kind, cards::OVERLAY_HAVE, F("helm overlay kind"));
    test.expectEq(it.overlays[0].page, cards::PAGE_PARTS, F("helm overlay page"));
    test.expectEq(it.overlays[0].x, 112, F("helm overlay x"));
    test.expectEq(it.overlays[0].y, 13, F("helm overlay y"));
    test.expectEq(it.overlays[0].arg0, item::ITEM_ORE, F("helm overlay ore"));
    test.expectEq(it.overlays[1].arg0, item::ITEM_SCALE, F("helm overlay scale"));
    test.expectEq(it.overlays[1].y, 21, F("helm overlay y2"));

    cardReadItem(cards::CARD_QUEST_SLAY_LUNGE, it);
    test.expectEq(it.kind, cards::KIND_QUEST, F("lunge kind"));
    test.expectEq(it.pageMask, 0x07, F("lunge mask"));
    test.expectEq(it.overlayCount, 1, F("lunge overlay count"));
    test.expectEq(it.overlays[0].kind, cards::OVERLAY_PROG, F("lunge prog kind"));
    test.expectEq(it.overlays[0].page, cards::PAGE_PROG, F("lunge prog page"));
    test.expectEq(it.overlays[0].arg0, 3, F("lunge need"));
    test.expectEq(it.overlays[0].arg1, 112, F("lunge bar width"));

    // Out-of-range index decodes empty (no pages/overlays).
    cardReadItem(static_cast<uint8_t>(cards::ITEM_COUNT), it);
    test.expectEq(it.pageMask, 0, F("bad index empty mask"));
    test.expectEq(it.overlayCount, 0, F("bad index no overlays"));

    // Baked page image addresses (the generated FX symbols) from the cached
    // record, plus a mismatched item to prove the cache is per item.
    cardReadItem(cards::CARD_ARMOR_HUNTER_HELM, it);
    test.expectEq(cardPageOffset(it, cards::PAGE_DESC), mh_card_armor_hunter_helm_0, F("helm desc addr"));
    test.expectEq(cardPageOffset(it, cards::PAGE_PARTS), mh_card_armor_hunter_helm_1, F("helm parts addr"));
    test.expectEq(cardPageOffset(it, cards::PAGE_SKILL), mh_card_armor_hunter_helm_3, F("helm skill addr"));
    cardReadItem(cards::CARD_QUEST_SLAY_LUNGE, it);
    test.expectEq(cardPageOffset(it, cards::PAGE_PROG), mh_card_quest_slay_lunge_1, F("lunge prog addr"));

    // ------------------------------ ARMOR FORGE list A -> card -> A crafts
    // ui.3.1 (5co.6): the ARMOR FORGE armor row opens the card; the card A
    // crafts from the baked bill (gate + debit + crafted bit) then equips. GEAR
    // is a slot view now (hbk.12), so the smithy list carries the armor rows.
    SaveBlock save;
    saveDefaults(save);
    save.zenny = 300;
    save.items[item::ITEM_ORE] = 3;
    save.items[item::ITEM_SCALE] = 2;

    ScreenRow helm;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_ARMOR_FORGE, 0), helm);
    test.expectEq(helm.action, screens::ACTION_EQUIP_ARMOR, F("armor row action"));
    test.expectEq(cardRowOpens(helm), 1, F("armor row opens a card"));
    test.expectEq(cardRowKind(helm), cards::KIND_ARMOR, F("armor row card kind"));
    test.expectEq(cardRowIndex(helm), cards::CARD_ARMOR_HUNTER_HELM, F("armor row card index"));

    DetailState detail;
    CardItem cache;
    cardLoad(detail, cache, cardRowIndex(helm), save, false);
    test.expectEq(detail.active, 1, F("card open"));
    test.expectEq(detail.page, cards::PAGE_DESC, F("card first page"));
    test.expectEq(detail.pageCount, 4, F("card page count"));
    test.expectEq(cardHint(save, helm, cache, detail.node), HINT_CRAFT, F("craft hint"));

    const Input idle = {0, 0, false, false};
    const Input a = {0, 0, true, false};
    const Input b = {0, 0, false, true};
    test.expectEq(detailStep(detail, a), DETAIL_ACTION, F("card A is an action"));
    test.expectEq(cardArmorApply(save, cache, helm), 1, F("card craft applies"));
    test.expectEq(saveCrafted(save, armor::ARMOR_HUNTER_HELM), 1, F("crafted bit set"));
    test.expectEq(save.equip[armor::SLOT_HEAD], armor::ARMOR_HUNTER_HELM + 1, F("craft auto-equips"));
    test.expectEq(save.zenny, 0, F("craft debits the bill zenny"));
    test.expectEq(static_cast<uint32_t>(save.items[item::ITEM_ORE]), 0, F("craft debits ore"));
    test.expectEq(static_cast<uint32_t>(save.items[item::ITEM_SCALE]), 0, F("craft debits scale"));

    cardLoad(detail, cache, cardRowIndex(helm), save, true);
    test.expectEq(cardMaskHas(detail.pageMask, cards::PAGE_PARTS), 0, F("crafted drops the PARTS page"));
    test.expectEq(detail.pageCount, 3, F("page count after craft"));
    test.expectEq(cardHint(save, helm, cache, detail.node), HINT_UNEQUIP, F("crafted hint is unequip"));
    test.expectEq(detailStep(detail, b), DETAIL_BACK, F("card B backs out"));

    test.expectEq(saveStore(save, REAL_BACKEND), 1, F("card action stores"));
    SaveBlock back;
    test.expectEq(saveLoad(back, REAL_BACKEND), 1, F("card action reloads"));
    test.expectEq(saveCrafted(back, armor::ARMOR_HUNTER_HELM), 1, F("crafted persisted"));
    test.expectEq(back.equip[armor::SLOT_HEAD], armor::ARMOR_HUNTER_HELM + 1, F("equip persisted"));

    // --------------------------------------- quests list A -> card -> A takes
    SaveBlock qsave;
    saveDefaults(qsave);
    ScreenRow take, turnIn;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_QUESTS, 0), take);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_QUESTS, 1), turnIn);
    test.expectEq(take.action, screens::ACTION_TAKE_QUEST, F("quests row0 action"));
    test.expectEq(cardRowIndex(take), cards::CARD_QUEST_SLAY_LUNGE, F("take row card index"));
    test.expectEq(turnIn.action, screens::ACTION_TURN_IN_QUEST, F("quests row1 action"));
    test.expectEq(cardRowIndex(turnIn), cards::CARD_QUEST_SLAY_LUNGE, F("turn-in row card index"));

    cardLoad(detail, cache, cardRowIndex(take), qsave, false);
    test.expectEq(detail.kind, cards::KIND_QUEST, F("quest card kind"));
    test.expectEq(detail.page, cards::PAGE_GOAL, F("quest first page"));
    test.expectEq(cardHint(qsave, take, cache, detail.node), HINT_ACCEPT, F("accept hint"));
    test.expectEq(detailStep(detail, a), DETAIL_ACTION, F("quest card A"));
    test.expectEq(screenApplyAction(qsave, take), 1, F("take applies"));
    test.expectEq(saveQuestGet(qsave, quests::QUEST_SLAY_LUNGE, 0), 1, F("taken bit set"));
    test.expectEq(cardHint(qsave, take, cache, detail.node), HINT_NONE, F("taken row hint clears"));

    qsave.progress = 3;
    cardLoad(detail, cache, cardRowIndex(turnIn), qsave, false);
    test.expectEq(cardHint(qsave, turnIn, cache, detail.node), HINT_TURN_IN, F("turn-in hint"));
    test.expectEq(screenApplyAction(qsave, turnIn), 1, F("turn-in applies"));
    test.expectEq(qsave.zenny, 150, F("turn-in pays the row cost"));
    test.expectEq(saveQuestGet(qsave, quests::QUEST_SLAY_LUNGE, 1), 1, F("done bit set"));
    test.expectEq(cardHint(qsave, turnIn, cache, detail.node), HINT_NONE, F("turned-in row hint clears"));

    // ------------------------------------- pixels: blit + dynamic overlays
    arduboy.startGray();
    waitPlane(0);

    SaveBlock ps;
    saveDefaults(ps);
    ps.items[item::ITEM_ORE] = 7;
    ps.items[item::ITEM_SCALE] = 0;

    DetailState ds;
    CardItem dc;
    cardLoad(ds, dc, cards::CARD_ARMOR_HUNTER_HELM, ps, false);
    cardSetHint(ds, ps, dc, helm);
    ds.page = cards::PAGE_PARTS;
    drawCard(ds, dc, ps);
    // Baked white title + light rule + the live HAVE digit + the hint line.
    test.expectEq(countBits(2, 45, 0, 7) > 0 ? 1 : 0, 1, F("title ink plane0"));
    test.expectEq(countBits(0, 127, 9, 9) > 0 ? 1 : 0, 1, F("rule ink plane0"));
    test.expectEq(countBits(112, 123, 13, 20) > 0 ? 1 : 0, 1, F("have count ink"));
    test.expectEq(countBits(2, 30, 56, 63) > 0 ? 1 : 0, 1, F("hint ink"));
    uint8_t have7[12];
    rowSnapshot(have7, 1, 112, 12);

    // The overlay only exists on the PARTS page: DESC leaves the slot empty.
    ds.page = cards::PAGE_DESC;
    drawCard(ds, dc, ps);
    test.expectEq(countBits(112, 123, 13, 20), 0, F("no have overlay on DESC"));

    // The drawn count tracks save.items: 7 vs 3 must differ.
    SaveBlock ps3 = ps;
    ps3.items[item::ITEM_ORE] = 3;
    ds.page = cards::PAGE_PARTS;
    drawCard(ds, dc, ps3);
    uint8_t have3[12];
    rowSnapshot(have3, 1, 112, 12);
    bool same = true;
    for (uint8_t i = 0; i < 12; i++)
        if (have7[i] != have3[i])
            same = false;
    test.expectEq(same, 0, F("have digit tracks the inventory"));

    // PROG page: shade-2 bar fill lights planes 0/1 only, empty at 0 progress.
    waitPlane(1);
    cardLoad(ds, dc, cards::CARD_QUEST_SLAY_LUNGE, ps, false);
    cardSetHint(ds, ps, dc, turnIn);
    ds.page = cards::PAGE_PROG;
    ps.activeQuest = quests::QUEST_SLAY_LUNGE;
    ps.progress = 3;
    drawCard(ds, dc, ps);
    test.expectEq(countBits(9, 118, 28, 31) > 0 ? 1 : 0, 1, F("bar fill plane1"));
    SaveBlock zero = ps;
    zero.progress = 0;
    drawCard(ds, dc, zero);
    test.expectEq(countBits(9, 118, 28, 31), 0, F("empty bar at 0 progress"));

    waitPlane(2);
    drawCard(ds, dc, ps);
    test.expectEq(countBits(9, 118, 28, 31), 0, F("shade-2 fill skips plane2"));
    test.expectEq(countBits(2, 45, 0, 7) > 0 ? 1 : 0, 1, F("white title lights plane2"));
    test.expectEq(countBits(0, 127, 9, 9), 0, F("light rule skips plane2"));

    (void)idle;
}

}   // namespace cardsfx
