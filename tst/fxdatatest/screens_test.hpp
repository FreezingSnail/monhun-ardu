#pragma once
// On-device suite for the data-driven screens + EEPROM save (bead
// monhun-ardu-cgz, docs/quests-shops.md).
//
// Covers the shipped cart path that the host suite cannot: reading the
// ScreenDef/ScreenRow records out of the mhScreens blob, nav/scroll through
// screenStep, the action switch, the real EEPROM roundtrip
// (store/load/verify/corrupt fallback/write-on-change), the qs.4 hub navigation
// rows + zenny dynamic-value rendering, and framebuffer checks for the rendered
// pages.

#include "harness/fxtest.hpp"
#include "src/screens.hpp"
#include "src/app_state.hpp"

#include <stdint.h>

namespace screenfx {

using namespace mh;

// ---- EEPROM backends: the real one and a write-counting wrapper ------------
static uint16_t eepWrites = 0;

static uint8_t countRead(uint16_t addr) {
    return saveEepromRead(addr);
}
static void countWrite(uint16_t addr, uint8_t value) {
    eepWrites++;
    saveEepromWrite(addr, value);
}
static const SaveBackend REAL_BACKEND = {saveEepromRead, saveEepromWrite};
static const SaveBackend COUNT_BACKEND = {countRead, countWrite};

// ---- framebuffer helpers (hud_test.hpp style) ------------------------------
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

inline void test_screens(FxTest &test) {
    // ------------------------------------------------- generated cart rows
    test.expectEq(screens::SCREEN_COUNT, 3, F("screen count"));
    test.expectEq(screens::SCREEN_HUB, 0, F("hub index"));
    test.expectEq(screens::SCREEN_QUESTS, 1, F("quests index"));
    test.expectEq(screens::SCREEN_SMITH, 2, F("smith index"));
    test.expectEq(screenRowCount(screens::SCREEN_HUB), 4, F("hub row count"));

    // Title bytes come from the cart def (id u8, titleLen u8, title chars).
    const uint16_t hubDef = screenDefOff(screens::SCREEN_HUB);
    test.expectEq(mhFxReadU8(screenCart(hubDef)), 0, F("hub def id"));
    test.expectEq(mhFxReadU8(screenCart(hubDef + 1)), 3, F("hub title len"));
    test.expectEq(mhFxReadU8(screenCart(hubDef + 2)), 'H', F("hub title H"));
    test.expectEq(mhFxReadU8(screenCart(hubDef + 3)), 'U', F("hub title U"));
    test.expectEq(mhFxReadU8(screenCart(hubDef + 4)), 'B', F("hub title B"));

    ScreenRow r0, r1, r2, r3;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_HUB, 0), r0);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_HUB, 1), r1);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_HUB, 2), r2);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_HUB, 3), r3);
    test.expectEq(r0.cost, 0, F("row0 cost"));
    test.expectEq(r0.action, screens::ACTION_HUNT, F("row0 action hunt"));
    test.expectEq(r0.cond, screens::COND_ALWAYS, F("row0 cond"));
    test.expectEq(r0.param, 0, F("row0 param"));
    test.expectEq(r1.action, screens::ACTION_OPEN_QUESTS, F("row1 action open quests"));
    test.expectEq(r1.cond, screens::COND_ALWAYS, F("row1 cond"));
    test.expectEq(r2.action, screens::ACTION_OPEN_SMITH, F("row2 action open smith"));
    test.expectEq(r2.cond, screens::COND_ALWAYS, F("row2 cond"));
    test.expectEq(r3.action, screens::ACTION_NONE, F("row3 action none"));
    test.expectEq(r3.flags, screens::ROW_F_ZENNY, F("row3 zenny dynamic-value flag"));

    // ----------------------------------------------------- nav/scroll
    SaveBlock save;
    saveDefaults(save);

    ScreenState st;
    screenEnter(st, screens::SCREEN_HUB, save);
    test.expectEq(st.rowCount, 4, F("enter rowCount"));
    test.expectEq(st.cursor, 0, F("enter cursor"));
    test.expectEq(st.scroll, 0, F("enter scroll"));
    test.expectEq(st.active, 1, F("enter active"));

    const Input idle = {0, 0, false, false};
    const Input down = {0, 1, false, false};
    const Input up = {0, -1, false, false};
    const Input a = {0, 0, true, false};
    const Input b = {0, 0, false, true};

    test.expectEq(screenStep(st, down), SCREEN_NONE, F("nav down silent"));
    test.expectEq(st.cursor, 1, F("nav down row1"));
    screenStep(st, idle);
    test.expectEq(screenStep(st, up), SCREEN_NONE, F("nav up silent"));
    test.expectEq(st.cursor, 0, F("nav up row0"));
    screenStep(st, idle);
    screenStep(st, up);   // wrap to the last row
    screenStep(st, idle);
    test.expectEq(st.cursor, 3, F("nav up wraps"));
    test.expectEq(screenStep(st, a), SCREEN_ACCEPT, F("A accepts"));
    test.expectEq(screenStep(st, a), SCREEN_NONE, F("held A silent"));
    screenStep(st, idle);
    test.expectEq(screenStep(st, b), SCREEN_BACK, F("B backs out"));

    // Page scroll (rows 7..13 do not exist in the demo data, so drive the
    // compiled device state machine directly).
    ScreenState page;
    page.screen = screens::SCREEN_HUB;
    page.rowCount = 13;
    for (uint8_t i = 0; i < 6; i++) {
        screenStep(page, down);
        screenStep(page, idle);
    }
    test.expectEq(page.cursor, 6, F("page cursor 6"));
    test.expectEq(page.scroll, 6, F("page scroll by 6"));

    // --------------------------------------------------- action dispatch
    // Hub rows are navigation (app_state.hpp); the save actions are exercised
    // through the smith (buy) and quests (take) rows.
    ScreenRow s0, q0;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_SMITH, 0), s0);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_QUESTS, 0), q0);
    SaveBlock act;
    saveDefaults(act);
    act.zenny = 250;
    act.items[ITEM_ORE] = 2;   // SWORD T1 recipe (prg.7)
    act.items[ITEM_SCALE] = 1;
    test.expectEq(screenCondOk(act, s0), 1, F("buy affordable"));
    test.expectEq(screenApplyAction(act, s0), 1, F("buy applies"));
    test.expectEq(act.zenny, 150, F("buy debits zenny"));
    test.expectEq(act.tier[0], 1, F("buy bumps tier"));
    test.expectEq(static_cast<uint32_t>(act.items[ITEM_ORE]), 0, F("buy debits ore"));
    test.expectEq(static_cast<uint32_t>(act.items[ITEM_SCALE]), 0, F("buy debits scale"));
    test.expectEq(screenCondOk(act, q0), 1, F("take always allowed"));
    test.expectEq(screenApplyAction(act, q0), 1, F("take applies"));
    test.expectEq(saveQuestGet(act, 0, 0), 1, F("quest0 taken"));
    test.expectEq(screenApplyAction(act, r3), 0, F("none row changes nothing"));
    test.expectEq(appScreenAccept(screens::SCREEN_HUB, r0), APP_NAV_HUNT, F("hub HUNT routes to hunt"));
    test.expectEq(appScreenAccept(screens::SCREEN_HUB, r1), APP_NAV_QUESTS, F("hub QUESTS route"));
    test.expectEq(appScreenAccept(screens::SCREEN_HUB, r2), APP_NAV_SMITH, F("hub SMITH route"));
    test.expectEq(appScreenAccept(screens::SCREEN_HUB, r3), APP_NAV_NONE, F("hub zenny row is a no-op"));
    test.expectEq(appScreenBack(screens::SCREEN_QUESTS), APP_NAV_HUB, F("quests B -> hub"));
    test.expectEq(appScreenBack(screens::SCREEN_HUB), APP_NAV_MENU, F("hub B -> menu"));

    // ----------------------------------------------------- EEPROM roundtrip
    SaveBlock eep;
    saveDefaults(eep);
    eep.zenny = 1234;
    eep.tier[0] = 2;
    eep.equip[0] = 3;   // head slot (prg.5 tail)
    eep.flags = SAVE_FLAG_SMITHY_SEEN;
    eep.items[ITEM_HERB] = 6;
    eep.items[ITEM_FANG] = 2;
    saveQuestSet(eep, 4, 0);
    test.expectEq(saveStore(eep, REAL_BACKEND), 1, F("eeprom store verifies"));

    SaveBlock out;
    test.expectEq(saveLoad(out, REAL_BACKEND), 1, F("eeprom load valid"));
    test.expectEq(out.zenny, 1234, F("eeprom zenny"));
    test.expectEq(out.tier[0], 2, F("eeprom tier"));
    test.expectEq(saveQuestGet(out, 4, 0), 1, F("eeprom quest taken"));
    test.expectEq(out.equip[0], 3, F("eeprom equip head"));
    test.expectEq(out.flags, SAVE_FLAG_SMITHY_SEEN, F("eeprom flags"));
    test.expectEq(out.items[ITEM_HERB], 6, F("eeprom herb count"));
    test.expectEq(out.items[ITEM_FANG], 2, F("eeprom fang count"));

    // Write-on-change: an identical block rewrites nothing; one changed field
    // rewrites the field byte plus the checksum.
    eepWrites = 0;
    saveStore(eep, COUNT_BACKEND);
    test.expectEq(eepWrites, 0, F("identical store writes nothing"));
    eep.zenny = 1235;
    saveStore(eep, COUNT_BACKEND);
    test.expectEq(eepWrites, 2, F("zenny + checksum rewritten"));
    // A changed inventory byte rewrites exactly that byte + the checksum.
    eepWrites = 0;
    eep.items[ITEM_ORE] = 9;
    saveStore(eep, COUNT_BACKEND);
    test.expectEq(eepWrites, 2, F("inventory byte + checksum rewritten"));

    // Corrupt magic -> safe defaults.
    saveEepromWrite(SAVE_EEPROM_ADDR, 0x00);
    test.expectEq(saveLoad(out, REAL_BACKEND), 0, F("bad magic rejected"));
    test.expectEq(out.zenny, 0, F("bad magic defaults zenny"));
    test.expectEq(out.tier[0], 0, F("bad magic defaults tier"));

    // Corrupt a payload byte without fixing the checksum.
    saveStore(eep, REAL_BACKEND);
    saveEepromWrite(static_cast<uint16_t>(SAVE_EEPROM_ADDR + 5), 0xFF);
    test.expectEq(saveLoad(out, REAL_BACKEND), 0, F("bad checksum rejected"));
    test.expectEq(saveQuestGet(out, 4, 0), 0, F("bad checksum defaults quests"));

    // -------------------------------------------------------- pixel check
    arduboy.startGray();
    while (arduboy.currentPlane() != 0) {
        FX::enableOLED();
        arduboy.waitForNextPlane();
        FX::disableOLED();
    }
    clearFb();
    SaveBlock ps;
    saveDefaults(ps);
    ps.zenny = 1234;
    ScreenState draw;
    screenEnter(draw, screens::SCREEN_HUB, ps);
    drawScreen(draw, ps);

    // Selected row carries the 4x4 white chip cursor at (2, row0 y + 2 = 13).
    test.expectEq(countBits(2, 5, 13, 16), 16, F("cursor chip 4x4"));
    // Title "HUB" ink on the white lane at the top.
    test.expectEq(countBits(2, 13, 0, 7) > 0 ? 1 : 0, 1, F("title ink"));
    // Row 0 label (selected -> white) and the right-aligned cost 0.
    test.expectEq(countBits(10, 40, 11, 18) > 0 ? 1 : 0, 1, F("row0 label ink"));
    test.expectEq(countBits(112, 123, 11, 18) > 0 ? 1 : 0, 1, F("row0 cost right-aligned"));
    // Row 1 label (light gray, unselected) one row pitch below.
    test.expectEq(countBits(10, 50, 20, 27) > 0 ? 1 : 0, 1, F("row1 QUESTS label ink"));
    // The cursor sits on row 0, so row 1's cursor cell stays empty.
    test.expectEq(countBits(2, 5, 22, 25), 0, F("row1 no cursor"));
    // Row 2 label (SMITH) at y = 11 + 2*9 = 29.
    test.expectEq(countBits(10, 50, 29, 36) > 0 ? 1 : 0, 1, F("row2 SMITH label ink"));
    // Row 3 is the dynamic ZENNY row: the live balance (1234, 4 digits) is
    // drawn in the cost column at y = 11 + 3*9 = 38, not the packed cost.
    test.expectEq(countBits(10, 32, 38, 45) > 0 ? 1 : 0, 1, F("zenny row label ink"));
    test.expectEq(countBits(108, 123, 38, 45) > 0 ? 1 : 0, 1, F("zenny balance drawn"));
    // Control: an empty balance draws one digit at the right edge only.
    clearFb();
    saveDefaults(ps);
    drawScreen(draw, ps);
    test.expectEq(countBits(108, 119, 38, 45), 0, F("zenny 0 leaves the 4-digit span empty"));
    test.expectEq(countBits(120, 123, 38, 45) > 0 ? 1 : 0, 1, F("zenny 0 digit drawn"));

    // -------------------------------------------------- quests board screen
    test.expectEq(screenRowCount(screens::SCREEN_QUESTS), 8, F("quests row count"));
    ScreenRow qr0, qr1;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_QUESTS, 0), qr0);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_QUESTS, 1), qr1);
    test.expectEq(qr0.action, screens::ACTION_TAKE_QUEST, F("quest row0 action"));
    test.expectEq(qr0.cond, screens::COND_QUEST, F("quest row0 cond"));
    test.expectEq(qr0.param, 0, F("quest row0 param (quest 0)"));
    test.expectEq(qr1.action, screens::ACTION_TURN_IN_QUEST, F("quest row1 action"));
    test.expectEq(qr1.cost, 150, F("quest row1 reward cost"));
    test.expectEq(qr1.param, 48, F("quest row1 param (need 3, quest 0)"));

    // Pixel: the quests page draws through the same generic renderer.
    clearFb();
    ScreenState qs;
    screenEnter(qs, screens::SCREEN_QUESTS, ps);
    drawScreen(qs, ps);
    test.expectEq(countBits(2, 5, 13, 16), 16, F("quests cursor chip 4x4"));
    test.expectEq(countBits(2, 20, 0, 7) > 0 ? 1 : 0, 1, F("quests title ink"));
    test.expectEq(countBits(10, 60, 11, 18) > 0 ? 1 : 0, 1, F("quests row0 label ink"));
}

}   // namespace screenfx
