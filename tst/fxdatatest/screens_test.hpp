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
#include "src/armor.hpp"
#include "src/forge.hpp"   // forgeNodeApply / forgeReadNode + forge::NODE_* (hbk.3 markers)
#include "src/cards.hpp"   // cardLoad/cardHint/cardApply (hbk.10 craft direct bill)
#include "src/fxdata.h"    // mh_screen_forge_* page addresses

#include <stdint.h>

namespace screenfx {

using namespace mh;

// gs.2: one file-scope Game for the armor cache fill (a second ~750 B stack
// frame does not fit the sim's tight stack; smith_test.hpp takes the same
// approach).
static Game g_gear;

// hbk.10: the smithy suite runs in its own frame, so it parks the same kind of
// file-scope Game (no extra stack).
static Game g_smithy;

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

// Park on a specific triplane pass (mirrors cards_test.hpp).
static void waitPlane(uint8_t plane) {
    while (arduboy.currentPlane() != plane) {
        FX::enableOLED();
        arduboy.waitForNextPlane();
        FX::disableOLED();
    }
}

inline void test_screens(FxTest &test) {
    // ------------------------------------------------- generated cart rows
    // ui.4 (5co.4) added the hub FORGE row; hbk.10 splits FORGE into a smithy
    // submenu + the CRAFT/ARMOR FORGE screens (dense indices 0..5).
    test.expectEq(screens::SCREEN_COUNT, 6, F("screen count"));
    test.expectEq(screens::SCREEN_HUB, 0, F("hub index"));
    test.expectEq(screens::SCREEN_QUESTS, 1, F("quests index"));
    test.expectEq(screens::SCREEN_GEAR, 2, F("gear index"));
    test.expectEq(screens::SCREEN_FORGE, 3, F("forge index"));
    test.expectEq(screens::SCREEN_CRAFT, 4, F("craft index"));
    test.expectEq(screens::SCREEN_ARMOR_FORGE, 5, F("armor forge index"));
    test.expectEq(screenRowCount(screens::SCREEN_HUB), 4, F("hub row count"));

    // Title bytes come from the cart def (id u8, titleLen u8, title chars).
    const uint16_t hubDef = screenDefOff(screens::SCREEN_HUB);
    test.expectEq(mhFxReadU8(screenCart(hubDef)), 0, F("hub def id"));
    test.expectEq(mhFxReadU8(screenCart(hubDef + 1)), 3, F("hub title len"));
    test.expectEq(mhFxReadU8(screenCart(hubDef + 2)), 'H', F("hub title H"));
    test.expectEq(mhFxReadU8(screenCart(hubDef + 3)), 'U', F("hub title U"));
    test.expectEq(mhFxReadU8(screenCart(hubDef + 4)), 'B', F("hub title B"));

    // ui.5: the ZENNY row is retired; the hub is HUNT/QUESTS/FORGE/GEAR.
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
    test.expectEq(r2.action, screens::ACTION_OPEN_FORGE, F("row2 action open forge"));
    test.expectEq(r2.cond, screens::COND_ALWAYS, F("row2 cond"));
    test.expectEq(r3.action, screens::ACTION_OPEN_GEAR, F("row3 action open gear"));
    test.expectEq(r3.cond, screens::COND_ALWAYS, F("row3 cond"));

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
    // through the quests (take) rows. Weapon equip/forge and armor craft/equip
    // are card actions now (ui.4, ui.3.1), pinned by cards_test/forge_test.
    ScreenRow q0, gw1;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_QUESTS, 0), q0);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 1), gw1);
    SaveBlock act;
    saveDefaults(act);
    test.expectEq(screenCondOk(act, q0), 1, F("take always allowed"));
    test.expectEq(screenApplyAction(act, q0), 1, F("take applies"));
    test.expectEq(saveQuestGet(act, 0, 0), 1, F("quest0 taken"));
    test.expectEq(gw1.action, screens::ACTION_EQUIP_WEAPON, F("gear row1 is a weapon equip row"));
    test.expectEq(gw1.flags, screens::ROW_F_FORGE, F("gear weapon row carries ROW_F_FORGE"));
    test.expectEq(screenApplyAction(act, gw1), 0, F("gear weapon equip is a card action"));
    test.expectEq(appScreenAccept(screens::SCREEN_HUB, r0), APP_NAV_HUNT, F("hub HUNT routes to hunt"));
    test.expectEq(appScreenAccept(screens::SCREEN_HUB, r1), APP_NAV_QUESTS, F("hub QUESTS route"));
    test.expectEq(appScreenAccept(screens::SCREEN_HUB, r2), APP_NAV_FORGE, F("hub FORGE route"));
    test.expectEq(appScreenAccept(screens::SCREEN_HUB, r3), APP_NAV_GEAR, F("hub GEAR route"));
    test.expectEq(appScreenBack(screens::SCREEN_QUESTS), APP_NAV_HUB, F("quests B -> hub"));
    test.expectEq(appScreenBack(screens::SCREEN_HUB), APP_NAV_NONE, F("hub B is a root no-op"));

    // ----------------------------------------------------- EEPROM roundtrip
    SaveBlock eep;
    saveDefaults(eep);
    eep.zenny = 1234;
    saveSetWeaponOwned(eep, forge::NODE_GUN_T2);   // save v5 owned bitset
    saveSetCrafted(eep, 2);
    eep.equip[0] = 3;   // head slot (prg.5 tail)
    eep.flags = SAVE_FLAG_SMITHY_SEEN;
    eep.items[ITEM_HERB] = 6;
    eep.items[ITEM_FANG] = 2;
    eep.equippedNode = forge::NODE_GUN_T2;   // save v5 equipped node
    saveQuestSet(eep, 4, 0);
    test.expectEq(saveStore(eep, REAL_BACKEND), 1, F("eeprom store verifies"));

    SaveBlock out;
    test.expectEq(saveLoad(out, REAL_BACKEND), 1, F("eeprom load valid"));
    test.expectEq(out.zenny, 1234, F("eeprom zenny"));
    test.expectEq(saveWeaponOwned(out, forge::NODE_GUN_T2), 1, F("eeprom owned bit"));
    test.expectEq(saveCrafted(out, 2), 1, F("eeprom crafted bit"));
    test.expectEq(saveQuestGet(out, 4, 0), 1, F("eeprom quest taken"));
    test.expectEq(out.equip[0], 3, F("eeprom equip head"));
    test.expectEq(out.flags, SAVE_FLAG_SMITHY_SEEN, F("eeprom flags"));
    test.expectEq(out.items[ITEM_HERB], 6, F("eeprom herb count"));
    test.expectEq(out.items[ITEM_FANG], 2, F("eeprom fang count"));
    test.expectEq(out.equippedNode, forge::NODE_GUN_T2, F("eeprom equipped node"));

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
    test.expectEq(saveWeaponOwned(out, forge::NODE_SWORD_BASE), 1, F("bad magic defaults to the sword root owned"));

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
    drawScreen(draw, ps, g_gear);

    // Selected row carries the 4x4 white chip cursor at (2, row0 y + 2 = 13).
    test.expectEq(countBits(2, 5, 13, 16), 16, F("cursor chip 4x4"));
    // Baked title band (shade 1) + title text; header zenny (ui.5) is
    // right-aligned on the same title line: `$` + 1234 at x 104..123.
    test.expectEq(countBits(2, 13, 0, 7) > 0 ? 1 : 0, 1, F("title band ink"));
    test.expectEq(countBits(104, 123, 0, 7) > 0 ? 1 : 0, 1, F("header zenny drawn"));
    // Row 0 label (baked; selected -> white redraw). Row 0 is HUNT: with no
    // active quest the live quest column draws nothing.
    test.expectEq(countBits(10, 40, 11, 18) > 0 ? 1 : 0, 1, F("row0 label ink"));
    test.expectEq(countBits(104, 123, 11, 18), 0, F("hub quest column empty without a quest"));
    // The hub bakes labels only (costs are 0): rows 1..3 have no cost ink.
    test.expectEq(countBits(104, 123, 20, 27), 0, F("hub row1 no baked cost"));
    test.expectEq(countBits(104, 123, 29, 36), 0, F("hub row2 no baked cost"));
    test.expectEq(countBits(104, 123, 38, 45), 0, F("hub row3 no baked cost"));
    // Row 1 label (baked light gray, unselected) one row pitch below.
    test.expectEq(countBits(10, 50, 20, 27) > 0 ? 1 : 0, 1, F("row1 QUESTS label ink"));
    // The cursor sits on row 0, so row 1's cursor cell stays empty.
    test.expectEq(countBits(2, 5, 22, 25), 0, F("row1 no cursor"));
    // Row 2 label (FORGE) at y = 11 + 2*9 = 29; row 3 (GEAR) at y = 38.
    test.expectEq(countBits(10, 50, 29, 36) > 0 ? 1 : 0, 1, F("row2 FORGE label ink"));
    test.expectEq(countBits(10, 50, 38, 45) > 0 ? 1 : 0, 1, F("row3 GEAR label ink"));
    // Control: an empty balance draws `$` + one digit at the right edge only.
    // Plane 1 clears the shade-1 band, so only the white zenny shows there.
    waitPlane(1);
    clearFb();
    saveDefaults(ps);
    drawScreen(draw, ps, g_gear);
    test.expectEq(countBits(104, 115, 0, 7), 0, F("zenny 0 leaves the 4-digit span empty"));
    test.expectEq(countBits(116, 123, 0, 7) > 0 ? 1 : 0, 1, F("zenny 0 `$`+digit drawn"));
    waitPlane(0);

    // -------------------------------------------- ui.5.2 hub chrome pixels
    // HUNT right column (row 0, y=11): active quest progress `p/n` only
    // (hbk.9 dropped the no-quest `-` and the READY branch).
    clearFb();
    SaveBlock qs2;
    saveDefaults(qs2);
    qs2.activeQuest = quests::QUEST_SLAY_LUNGE;   // need 3
    qs2.progress = 2;
    ScreenState hud;
    screenEnter(hud, screens::SCREEN_HUB, qs2);
    drawScreen(hud, qs2, g_gear);
    // "2/3": digits x 112..115 / 120..123, slash 116..119.
    test.expectEq(countBits(112, 123, 11, 18) > 0 ? 1 : 0, 1, F("hunt progress p/n ink"));
    test.expectEq(countBits(104, 111, 11, 18), 0, F("hunt progress leaves the 4-digit span empty"));

    // Progress at/over the need still draws `p/n` (READY retired in hbk.9).
    clearFb();
    qs2.progress = 3;
    drawScreen(hud, qs2, g_gear);
    test.expectEq(countBits(112, 123, 11, 18) > 0 ? 1 : 0, 1, F("hunt met progress p/n ink"));
    test.expectEq(countBits(104, 111, 11, 18), 0, F("hunt met progress leaves the 4-digit span empty"));

    // No active quest -> the column stays empty (`-` retired in hbk.9).
    clearFb();
    qs2.activeQuest = SAVE_QUEST_NONE;
    drawScreen(hud, qs2, g_gear);
    test.expectEq(countBits(104, 123, 11, 18), 0, F("hunt none leaves the column empty"));

    // Page indicator `n/m` bakes into each page at x = 2 + title width + 4
    // (QUESTS title 6 chars -> x=30, shade 2). Plane 1 clears the shade-1 band,
    // so the shade-2 indicator text is the only ink there.
    waitPlane(1);
    clearFb();
    ScreenState pgs;
    screenEnter(pgs, screens::SCREEN_QUESTS, ps);
    drawScreen(pgs, ps, g_gear);
    test.expectEq(countBits(30, 41, 0, 7) > 0 ? 1 : 0, 1, F("page indicator 1/2 ink"));
    clearFb();
    pgs.cursor = 6;
    pgs.scroll = 6;
    drawScreen(pgs, ps, g_gear);
    test.expectEq(countBits(30, 41, 0, 7) > 0 ? 1 : 0, 1, F("page indicator 2/2 ink"));
    // A <=6-row list (HUB) draws no indicator.
    clearFb();
    ScreenState hubPg;
    screenEnter(hubPg, screens::SCREEN_HUB, ps);
    drawScreen(hubPg, ps, g_gear);
    test.expectEq(countBits(30, 41, 0, 7), 0, F("hub has no page indicator"));
    waitPlane(0);

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

    // Pixel: the quests page draws through the same generic renderer. Page 0
    // bakes the turn-in costs (shade 3, right-aligned ending at x=112); the take
    // rows bake no cost.
    clearFb();
    ScreenState qs;
    screenEnter(qs, screens::SCREEN_QUESTS, ps);
    drawScreen(qs, ps, g_gear);
    test.expectEq(countBits(2, 5, 13, 16), 16, F("quests cursor chip 4x4"));
    test.expectEq(countBits(2, 20, 0, 7) > 0 ? 1 : 0, 1, F("quests title ink"));
    test.expectEq(countBits(10, 60, 11, 18) > 0 ? 1 : 0, 1, F("quests row0 label ink"));
    // Row 1 "TURN IN 150" cost at x 100..111; take rows 0/2 have none.
    test.expectEq(countBits(100, 111, 20, 27) > 0 ? 1 : 0, 1, F("quests row1 baked cost 150"));
    test.expectEq(countBits(104, 112, 11, 18), 0, F("quests row0 take no cost"));
    test.expectEq(countBits(104, 112, 29, 36), 0, F("quests row2 take no cost"));
    // Row 3 "TURN IN 250" and row 5 "TURN IN 200" (page 0).
    test.expectEq(countBits(100, 111, 38, 45) > 0 ? 1 : 0, 1, F("quests row3 baked cost 250"));
    test.expectEq(countBits(100, 111, 56, 63) > 0 ? 1 : 0, 1, F("quests row5 baked cost 200"));

    // ------------------------------------------------------- gear screen
    // ui.4: the weapon section is the whole tree (class headers + one equip row
    // per node), then the armor header/rows, then the five skill rows + LEAVE.
    test.expectEq(screenRowCount(screens::SCREEN_GEAR), 24, F("gear row count"));
    ScreenRow g0, g1, g5, g9, g13, g17, g18, g23;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 0), g0);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 1), g1);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 5), g5);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 9), g9);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 13), g13);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 17), g17);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 18), g18);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 23), g23);
    test.expectEq(g0.action, screens::ACTION_NONE, F("gear row0 class header"));
    test.expectEq(g1.action, screens::ACTION_EQUIP_WEAPON, F("gear row1 action equip"));
    test.expectEq(g1.param, forge::NODE_SWORD_BASE, F("gear row1 param sword root"));
    test.expectEq(g1.flags, screens::ROW_F_FORGE, F("gear row1 forge flag"));
    test.expectEq(g5.action, screens::ACTION_EQUIP_WEAPON, F("gear row5 action equip"));
    test.expectEq(g5.param, forge::NODE_FLAIL_BASE, F("gear row5 param flail root"));
    test.expectEq(g9.action, screens::ACTION_EQUIP_WEAPON, F("gear row9 action equip"));
    test.expectEq(g9.param, forge::NODE_GUN_BASE, F("gear row9 param gun root"));
    // Armor rows pack (slot << 5) | pieceIdx; ui.3.1: the card gates craft/equip,
    // so the row itself is always live.
    test.expectEq(g13.action, screens::ACTION_EQUIP_ARMOR, F("gear row13 action equip armor"));
    test.expectEq(g13.cond, screens::COND_ALWAYS, F("gear row13 cond always"));
    test.expectEq(g13.param, armor::ARMOR_HUNTER_HELM, F("gear row13 param helm"));
    test.expectEq(g17.param, static_cast<uint8_t>((armor::SLOT_CHARM << 5) | armor::ARMOR_EVADE_CHARM), F("gear row17 param charm"));
    // gs.2 skill rows: inert (action none, always live) with param = skill idx.
    test.expectEq(g18.action, screens::ACTION_NONE, F("gear row18 skill action none"));
    test.expectEq(g18.cond, screens::COND_ALWAYS, F("gear row18 skill cond always"));
    test.expectEq(g18.flags, screens::ROW_F_SKILL, F("gear row18 skill flag"));
    test.expectEq(g18.param, armor::SKILL_ATTACK_UP, F("gear row18 attack up"));
    test.expectEq(g23.action, screens::ACTION_LEAVE, F("gear leave row"));
    test.expectEq(appScreenAccept(screens::SCREEN_GEAR, g23), APP_NAV_HUB, F("gear leave backs to hub"));
    test.expectEq(appScreenAccept(screens::SCREEN_GEAR, g18), APP_NAV_NONE, F("gear skill row is inert"));
    test.expectEq(appScreenAccept(screens::SCREEN_GEAR, g1), APP_NAV_NONE, F("gear equip is a card action"));
    // Weapon equip/armor craft are card actions now; screenApplyAction stays
    // inert for them (cards_test/forge_test drive the card path).
    SaveBlock gear;
    saveDefaults(gear);
    test.expectEq(screenApplyAction(gear, g1), 0, F("gear weapon equip is a card action"));
    test.expectEq(screenCondOk(gear, g13), 1, F("armor row is always live"));
    test.expectEq(screenApplyAction(gear, g13), 0, F("armor is a card action, not a screen action"));

    // -------------------------------------- gear skill readout cache (gs.2)
    // Cart armor records -> ScreenState cache: helm (attack_up 6, defense_up 4)
    // + mail (attack_up 6, health_up 4) -> attack 12/S, health 4/inert.
    SaveBlock skillSave;
    saveDefaults(skillSave);
    saveSetCrafted(skillSave, armor::ARMOR_HUNTER_HELM);
    saveSetCrafted(skillSave, armor::ARMOR_HUNTER_MAIL);
    skillSave.equip[armor::SLOT_HEAD] = armor::ARMOR_HUNTER_HELM + 1;
    skillSave.equip[armor::SLOT_BODY] = armor::ARMOR_HUNTER_MAIL + 1;
    armorApplyToGame(g_gear, skillSave);
    ScreenState readout;
    screenEnter(readout, screens::SCREEN_GEAR, skillSave);
    screenGearCache(readout, g_gear.armor);
    test.expectEq(readout.skillPoints[armor::SKILL_ATTACK_UP], 12, F("attack points from cart"));
    test.expectEq(readout.skillTier[armor::SKILL_ATTACK_UP], 1, F("12 -> S tier"));
    test.expectEq(readout.skillPoints[armor::SKILL_HEALTH_UP], 4, F("health points from cart"));
    test.expectEq(readout.skillTier[armor::SKILL_HEALTH_UP], 0, F("health 4 inert"));
    test.expectEq(readout.skillPoints[armor::SKILL_EVADE_WINDOW], 0, F("no charm -> evade 0"));

    // Equipping a piece through the cart gear row moves the readout: mail added
    // to the helm -> attack 6 -> 12 (crosses S), health 0 -> 4.
    SaveBlock moveSave;
    saveDefaults(moveSave);
    saveSetCrafted(moveSave, armor::ARMOR_HUNTER_HELM);
    saveSetCrafted(moveSave, armor::ARMOR_HUNTER_MAIL);
    moveSave.equip[armor::SLOT_HEAD] = armor::ARMOR_HUNTER_HELM + 1;
    armorApplyToGame(g_gear, moveSave);
    ScreenState moveRead;
    screenEnter(moveRead, screens::SCREEN_GEAR, moveSave);
    screenGearCache(moveRead, g_gear.armor);
    test.expectEq(moveRead.skillPoints[armor::SKILL_ATTACK_UP], 6, F("helm-only attack 6"));
    test.expectEq(moveRead.skillTier[armor::SKILL_ATTACK_UP], 0, F("helm-only attack inert"));
    test.expectEq(armorEquipToggle(moveSave, armor::ARMOR_HUNTER_MAIL, armor::SLOT_BODY), 1, F("equip mail toggles"));
    armorApplyToGame(g_gear, moveSave);
    screenGearCache(moveRead, g_gear.armor);
    test.expectEq(moveRead.skillPoints[armor::SKILL_ATTACK_UP], 12, F("equip raises attack to 12"));
    test.expectEq(moveRead.skillTier[armor::SKILL_ATTACK_UP], 1, F("equip crosses S"));
    test.expectEq(moveRead.skillPoints[armor::SKILL_HEALTH_UP], 4, F("equip adds health points"));

    // M clamp: helm + mail + charm -> attack 16 -> clamped 15/M.
    saveSetCrafted(moveSave, armor::ARMOR_EVADE_CHARM);
    moveSave.equip[armor::SLOT_CHARM] = armor::ARMOR_EVADE_CHARM + 1;
    armorApplyToGame(g_gear, moveSave);
    screenGearCache(moveRead, g_gear.armor);
    test.expectEq(moveRead.skillPoints[armor::SKILL_ATTACK_UP], 15, F("attack clamps to 15"));
    test.expectEq(moveRead.skillTier[armor::SKILL_ATTACK_UP], 2, F("clamped attack is M"));

    // Pixel: a skill row draws its points + tier letter at the baked cost column
    // (right-aligned ending at x=112). ATTACK UP (row 18) is the first row of
    // page 3 (rows 18..23) at y = 11; 15 points -> 2 digits at x 104..111, the M
    // letter just left at 96..99.
    clearFb();
    ScreenState smoke;
    screenEnter(smoke, screens::SCREEN_GEAR, moveSave);
    smoke.cursor = 18;
    smoke.scroll = 18;
    screenGearCache(smoke, g_gear.armor);   // attack 15/M
    drawScreen(smoke, moveSave, g_gear);
    test.expectEq(countBits(96, 99, 11, 18) > 0 ? 1 : 0, 1, F("skill M letter ink"));
    test.expectEq(countBits(104, 111, 11, 18) > 0 ? 1 : 0, 1, F("skill points ink"));

    // An inert skill (DEFENSE UP row 19: defense_up 4) draws the points only
    // (1 digit at 108..111, no tier letter).
    clearFb();
    screenEnter(smoke, screens::SCREEN_GEAR, moveSave);
    smoke.cursor = 19;
    smoke.scroll = 18;
    screenGearCache(smoke, g_gear.armor);
    drawScreen(smoke, moveSave, g_gear);
    test.expectEq(countBits(108, 111, 20, 27) > 0 ? 1 : 0, 1, F("inert skill points ink"));
    test.expectEq(countBits(100, 107, 20, 27), 0, F("inert skill no prefix"));

    // Live skill number straight from the cache: skill 0 (attack_up) = 12 with
    // no tier -> 2 digits at the baked cost column (104..111), no S/M letter.
    clearFb();
    screenEnter(smoke, screens::SCREEN_GEAR, moveSave);
    smoke.cursor = 18;
    smoke.scroll = 18;
    smoke.skillPoints[armor::SKILL_ATTACK_UP] = 12;
    smoke.skillTier[armor::SKILL_ATTACK_UP] = 0;
    drawScreen(smoke, moveSave, g_gear);
    test.expectEq(countBits(104, 111, 11, 18) > 0 ? 1 : 0, 1, F("gear skill 12 points ink"));
    test.expectEq(countBits(96, 103, 11, 18), 0, F("gear skill 12 no tier letter"));

    // Pixel: the gear page draws through the same generic renderer.
    clearFb();
    ScreenState gs;
    screenEnter(gs, screens::SCREEN_GEAR, ps);
    drawScreen(gs, ps, g_gear);
    test.expectEq(countBits(2, 5, 13, 16), 16, F("gear cursor chip 4x4"));
    test.expectEq(countBits(2, 20, 0, 7) > 0 ? 1 : 0, 1, F("gear title ink"));
    test.expectEq(countBits(10, 60, 11, 18) > 0 ? 1 : 0, 1, F("gear row0 label ink"));

    // Section header rows bake a full-width dark band (shade 1) behind centered
    // white text: the SWD header (row 0, band y=10..17) and the ARMOR header
    // (row 12, page 2, band y=10..17). The legacy path never drew a band.
    test.expectEq(countBits(110, 127, 10, 17) > 0 ? 1 : 0, 1, F("gear SWD section band ink"));
    // Page 2 (scroll 12) opens on the ARMOR header at row 12.
    clearFb();
    screenEnter(gs, screens::SCREEN_GEAR, ps);
    gs.scroll = 12;
    drawScreen(gs, ps, g_gear);
    test.expectEq(countBits(110, 127, 10, 17) > 0 ? 1 : 0, 1, F("gear ARMOR section band ink"));
    // Armor rows draw no marker (deferred): the marker column stays empty.
    test.expectEq(countBits(116, 123, 20, 27), 0, F("gear armor no marker"));

    // -------------------------------------------- hbk.3 prebaked pages
    // Page table (docs/ui-design.md, fixed stride hbk.9): one 13-byte slot per
    // screen -- a u8 page count then 4 x u24 absolute FX addresses of the baked
    // mh_screen_<name>_<page> layers. Every shipped screen is prebaked.
    test.expectEq(screens::SCREEN_PAGE_STRIDE, 13, F("page table stride"));
    test.expectEq(screenPageCount(screens::SCREEN_HUB), 1, F("hub page count"));
    test.expectEq(screenPageCount(screens::SCREEN_QUESTS), 2, F("quests page count"));
    test.expectEq(screenPageCount(screens::SCREEN_GEAR), 4, F("gear page count"));
    test.expectEq(screenPageCount(screens::SCREEN_FORGE), 1, F("forge submenu page count"));
    test.expectEq(screenPageCount(screens::SCREEN_CRAFT), 2, F("craft page count"));
    test.expectEq(screenPageCount(screens::SCREEN_ARMOR_FORGE), 1, F("armor forge page count"));
    // The generated per-screen offsets are PAGE_TABLE_OFF + screen * stride.
    test.expectEq(screens::SCREEN_HUB_PAGE_TABLE, screens::PAGE_TABLE_OFF, F("hub page table off"));
    test.expectEq(screens::SCREEN_QUESTS_PAGE_TABLE, static_cast<uint16_t>(screens::PAGE_TABLE_OFF + screens::SCREEN_PAGE_STRIDE), F("quests page table off"));
    test.expectEq(screens::SCREEN_GEAR_PAGE_TABLE, static_cast<uint16_t>(screens::PAGE_TABLE_OFF + 2 * screens::SCREEN_PAGE_STRIDE), F("gear page table off"));
    test.expectEq(screens::SCREEN_FORGE_PAGE_TABLE, static_cast<uint16_t>(screens::PAGE_TABLE_OFF + 3 * screens::SCREEN_PAGE_STRIDE), F("forge page table off"));
    test.expectEq(screens::SCREEN_CRAFT_PAGE_TABLE, static_cast<uint16_t>(screens::PAGE_TABLE_OFF + 4 * screens::SCREEN_PAGE_STRIDE), F("craft page table off"));
    test.expectEq(screens::SCREEN_ARMOR_FORGE_PAGE_TABLE, static_cast<uint16_t>(screens::PAGE_TABLE_OFF + 5 * screens::SCREEN_PAGE_STRIDE), F("armor page table off"));
    test.expectEq(screenPageAddr(screens::SCREEN_HUB, 0), mh_screen_hub_0, F("hub page0 addr"));
    test.expectEq(screenPageAddr(screens::SCREEN_QUESTS, 0), mh_screen_quests_0, F("quests page0 addr"));
    test.expectEq(screenPageAddr(screens::SCREEN_QUESTS, 1), mh_screen_quests_1, F("quests page1 addr"));
    test.expectEq(screenPageAddr(screens::SCREEN_GEAR, 0), mh_screen_gear_0, F("gear page0 addr"));
    test.expectEq(screenPageAddr(screens::SCREEN_GEAR, 3), mh_screen_gear_3, F("gear page3 addr"));
    test.expectEq(screenPageAddr(screens::SCREEN_FORGE, 0), mh_screen_forge_0, F("forge page0 addr"));
    test.expectEq(screenPageAddr(screens::SCREEN_CRAFT, 0), mh_screen_craft_0, F("craft page0 addr"));
    test.expectEq(screenPageAddr(screens::SCREEN_CRAFT, 1), mh_screen_craft_1, F("craft page1 addr"));
    test.expectEq(screenPageAddr(screens::SCREEN_ARMOR_FORGE, 0), mh_screen_armor_forge_0, F("armor page0 addr"));
}

// Smithy split (hbk.10): the FORGE submenu + CRAFT + ARMOR FORGE checks live in
// their own suite function so their stack frame does not overlap test_screens'
// (the AVR test stack is tight; test_screens' frame is already ~780 B, and a
// nested callee overflows it).
inline void test_screens_smithy(FxTest &test) {
    // --------------------------------------------- hbk.10 FORGE submenu
    // The FORGE screen is now the smithy submenu: WEAPON CRAFT / WEAPON UPGRADE
    // / ARMOR FORGE / LEAVE; each open row routes to its screen (UPGRADE lands
    // in hbk.11, so its route is defined but the screen is inert for now).
    // One reused ScreenRow keeps the tight suite stack small.
    ScreenRow srow;
    test.expectEq(screenRowCount(screens::SCREEN_FORGE), 4, F("submenu row count"));
    screenReadRow(screenRowOffsetAt(screens::SCREEN_FORGE, 0), srow);
    test.expectEq(srow.action, screens::ACTION_OPEN_CRAFT, F("submenu row0 open craft"));
    test.expectEq(appScreenAccept(screens::SCREEN_FORGE, srow), APP_NAV_CRAFT, F("craft row route"));
    screenReadRow(screenRowOffsetAt(screens::SCREEN_FORGE, 1), srow);
    test.expectEq(srow.action, screens::ACTION_OPEN_UPGRADE, F("submenu row1 open upgrade"));
    test.expectEq(appScreenAccept(screens::SCREEN_FORGE, srow), APP_NAV_UPGRADE, F("upgrade row route"));
    screenReadRow(screenRowOffsetAt(screens::SCREEN_FORGE, 2), srow);
    test.expectEq(srow.action, screens::ACTION_OPEN_ARMOR_FORGE, F("submenu row2 open armor forge"));
    test.expectEq(appScreenAccept(screens::SCREEN_FORGE, srow), APP_NAV_ARMOR_FORGE, F("armor row route"));
    screenReadRow(screenRowOffsetAt(screens::SCREEN_FORGE, 3), srow);
    test.expectEq(srow.action, screens::ACTION_LEAVE, F("submenu row3 leave"));
    test.expectEq(appScreenAccept(screens::SCREEN_FORGE, srow), APP_NAV_HUB, F("submenu leave -> hub"));

    // Baked submenu page 0: y=8 rule, full-width title band, unselected baked
    // label (row 2 "ARMOR FORGE" at y=29..36), live cursor chip on row 0.
    // Plane ISR drives waitForNextPlane: start the gray mode once (same
    // discipline as test_screens), else waitPlane() spins forever.
    arduboy.startGray();
    SaveBlock fsave;
    saveDefaults(fsave);
    ScreenState forge;
    screenEnter(forge, screens::SCREEN_FORGE, fsave);
    waitPlane(0);
    clearFb();
    drawScreen(forge, fsave, g_smithy);
    test.expectEq(countBits(0, 127, 8, 8) > 0 ? 1 : 0, 1, F("forge baked rule ink"));
    test.expectEq(countBits(0, 127, 0, 7) > 0 ? 1 : 0, 1, F("forge baked title band ink"));
    test.expectEq(countBits(10, 60, 29, 36) > 0 ? 1 : 0, 1, F("forge baked row2 label ink"));
    test.expectEq(countBits(2, 5, 13, 16), 16, F("forge cursor chip 4x4"));

    // Plane 2 lights shade 3 (white) only: the baked white title band and the
    // live white selected-row redraw are ink there, the shade-2 rule/labels are
    // not. Select row 1 (WEAPON UPGRADE) to prove the redraw.
    waitPlane(2);
    clearFb();
    screenEnter(forge, screens::SCREEN_FORGE, fsave);
    forge.cursor = 1;
    drawScreen(forge, fsave, g_smithy);
    test.expectEq(countBits(0, 127, 0, 7) > 0 ? 1 : 0, 1, F("forge white title band plane2"));
    test.expectEq(countBits(0, 127, 8, 8), 0, F("forge rule skips plane2"));
    test.expectEq(countBits(10, 60, 20, 27) > 0 ? 1 : 0, 1, F("forge selected row1 label white plane2"));
    test.expectEq(countBits(10, 60, 29, 36), 0, F("forge unselected baked label skips plane2"));

    // --------------------------------------------------- hbk.10 CRAFT list
    // Flat rows in forge-node order (9 nodes + LEAVE = 10, 2 pages): no class
    // headers/prefixes, cost = the node's directCost (the baked craft price).
    test.expectEq(screenRowCount(screens::SCREEN_CRAFT), 10, F("craft row count"));
    screenReadRow(screenRowOffsetAt(screens::SCREEN_CRAFT, 0), srow);
    test.expectEq(srow.action, screens::ACTION_FORGE_NODE, F("craft row0 action forge"));
    test.expectEq(srow.flags, screens::ROW_F_FORGE, F("craft row0 forge flag"));
    test.expectEq(srow.param, forge::NODE_SWORD_BASE, F("craft row0 param sword root"));
    test.expectEq(srow.cost, 0, F("craft root cost 0"));
    screenReadRow(screenRowOffsetAt(screens::SCREEN_CRAFT, 1), srow);
    test.expectEq(srow.param, forge::NODE_SWORD_T1, F("craft row1 param sword t2"));
    test.expectEq(srow.cost, 180, F("craft row1 baked direct cost"));
    screenReadRow(screenRowOffsetAt(screens::SCREEN_CRAFT, 2), srow);
    test.expectEq(srow.cost, 400, F("craft row2 baked direct cost"));
    screenReadRow(screenRowOffsetAt(screens::SCREEN_CRAFT, 3), srow);
    test.expectEq(srow.param, forge::NODE_FLAIL_BASE, F("craft row3 param flail root"));
    test.expectEq(srow.cost, 0, F("craft flail root cost 0"));
    screenReadRow(screenRowOffsetAt(screens::SCREEN_CRAFT, 8), srow);
    test.expectEq(srow.param, forge::NODE_GUN_T2, F("craft row8 param gun t3"));
    test.expectEq(srow.cost, 360, F("craft row8 baked direct cost"));
    screenReadRow(screenRowOffsetAt(screens::SCREEN_CRAFT, 9), srow);
    test.expectEq(srow.action, screens::ACTION_LEAVE, F("craft leave row"));

    // Pixel: page 0 bakes the flat labels (no tree prefix) and the direct
    // costs; row 1 (SWD T2, y=20) bakes cost 180 ending at x=112, row 0 (root,
    // cost 0) bakes none. The cursor chip stays live on row 0. Back to plane 0
    // so the shade-2 baked labels/markers are ink again.
    waitPlane(0);
    clearFb();
    screenEnter(forge, screens::SCREEN_CRAFT, fsave);
    drawScreen(forge, fsave, g_smithy);
    test.expectEq(countBits(2, 5, 13, 16), 16, F("craft cursor chip 4x4"));
    test.expectEq(countBits(0, 127, 0, 7) > 0 ? 1 : 0, 1, F("craft baked title band ink"));
    test.expectEq(countBits(10, 60, 11, 18) > 0 ? 1 : 0, 1, F("craft row0 label ink"));
    test.expectEq(countBits(100, 111, 20, 27) > 0 ? 1 : 0, 1, F("craft row1 baked cost 180"));
    test.expectEq(countBits(100, 111, 11, 18), 0, F("craft row0 root has no cost"));
    // Live node markers on the craft rows (ROW_F_FORGE): the default save owns
    // and equips the sword root (row 0 -> white) and owns the flail/gun roots;
    // an unforged child (row 1) leaves the marker column empty.
    test.expectEq(countBits(118, 121, 14, 17) > 0 ? 1 : 0, 1, F("craft equipped root marker ink"));
    test.expectEq(countBits(118, 121, 23, 26), 0, F("craft unforged child marker empty"));
    test.expectEq(countBits(118, 121, 41, 44) > 0 ? 1 : 0, 1, F("craft owned flail root marker ink"));
    // Plane 2 lights shade 3 only: the equipped (white) root marker is ink, the
    // owned (shade 2) flail root marker is not.
    waitPlane(2);
    clearFb();
    screenEnter(forge, screens::SCREEN_CRAFT, fsave);
    drawScreen(forge, fsave, g_smithy);
    test.expectEq(countBits(118, 121, 14, 17) > 0 ? 1 : 0, 1, F("craft equipped marker lights plane2"));
    test.expectEq(countBits(118, 121, 41, 44), 0, F("craft owned marker skips plane2"));
    waitPlane(0);

    // Craft card (hbk.10): a card opened from SCREEN_CRAFT charges the direct
    // bill baked into the row (cost == the row cost) even though the root
    // parent is owned.
    SaveBlock craf;
    saveDefaults(craf);
    craf.zenny = 1000;
    craf.items[ITEM_ORE] = 10;
    craf.items[ITEM_SCALE] = 2;
    DetailState cdet;
    CardItem ccard;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_CRAFT, 1), srow);   // SWD T2
    cardLoad(cdet, ccard, cardRowIndex(srow), craf, false);
    cdet.direct = true;
    cardSetHint(cdet, craf, ccard, srow);
    test.expectEq(cdet.hint, HINT_FORGE, F("craft card direct hint"));
    test.expectEq(cardApply(craf, ccard, cdet.node, srow, cdet.direct), 1, F("craft card applies"));
    test.expectEq(craf.zenny, 820, F("craft card charges the direct cost"));
    test.expectEq(static_cast<uint32_t>(craf.items[ITEM_ORE]), 7, F("craft card charges the direct mats"));

    // ---------------------------------------------- hbk.10 ARMOR FORGE list
    // Rows come from data/armor.json: label + recipe zenny + equip_armor with
    // param = (slot << 5) | piece; the armor card gates the craft.
    test.expectEq(screenRowCount(screens::SCREEN_ARMOR_FORGE), 6, F("armor row count"));
    screenReadRow(screenRowOffsetAt(screens::SCREEN_ARMOR_FORGE, 0), srow);
    test.expectEq(srow.action, screens::ACTION_EQUIP_ARMOR, F("armor row0 action equip armor"));
    test.expectEq(srow.param, armor::ARMOR_HUNTER_HELM, F("armor row0 helm param"));
    test.expectEq(srow.cost, 300, F("armor row0 helm zenny"));
    test.expectEq(cardRowOpens(srow), 1, F("armor row opens its card"));
    test.expectEq(cardRowIndex(srow), cards::CARD_ARMOR_HUNTER_HELM, F("armor row0 card index"));
    screenReadRow(screenRowOffsetAt(screens::SCREEN_ARMOR_FORGE, 1), srow);
    test.expectEq(srow.param, armor::ARMOR_BONE_CAP, F("armor row1 bone cap param"));
    test.expectEq(srow.cost, 200, F("armor row1 bone cap zenny"));
    screenReadRow(screenRowOffsetAt(screens::SCREEN_ARMOR_FORGE, 2), srow);
    test.expectEq(srow.param, static_cast<uint8_t>((armor::SLOT_BODY << 5) | armor::ARMOR_HUNTER_MAIL), F("armor row2 mail param"));
    test.expectEq(srow.cost, 400, F("armor row2 mail zenny"));
    screenReadRow(screenRowOffsetAt(screens::SCREEN_ARMOR_FORGE, 3), srow);
    test.expectEq(srow.cost, 250, F("armor row3 bone mail zenny"));
    screenReadRow(screenRowOffsetAt(screens::SCREEN_ARMOR_FORGE, 4), srow);
    test.expectEq(srow.param, static_cast<uint8_t>((armor::SLOT_CHARM << 5) | armor::ARMOR_EVADE_CHARM), F("armor row4 charm param"));
    test.expectEq(srow.cost, 600, F("armor row4 charm zenny"));
    screenReadRow(screenRowOffsetAt(screens::SCREEN_ARMOR_FORGE, 5), srow);
    test.expectEq(srow.action, screens::ACTION_LEAVE, F("armor leave row"));

    // Pixel: the armor page bakes the labels + zenny costs (row 0 helm 300 at
    // y=11, row 4 charm 600 at y=47), cursor chip live on row 0.
    screenEnter(forge, screens::SCREEN_ARMOR_FORGE, fsave);
    clearFb();
    drawScreen(forge, fsave, g_smithy);
    test.expectEq(countBits(2, 5, 13, 16), 16, F("armor cursor chip 4x4"));
    test.expectEq(countBits(0, 127, 0, 7) > 0 ? 1 : 0, 1, F("armor baked title band ink"));
    test.expectEq(countBits(10, 60, 11, 18) > 0 ? 1 : 0, 1, F("armor row0 label ink"));
    test.expectEq(countBits(100, 111, 11, 18) > 0 ? 1 : 0, 1, F("armor row0 baked cost 300"));
    test.expectEq(countBits(100, 111, 47, 54) > 0 ? 1 : 0, 1, F("armor row4 baked cost 600"));
}

}   // namespace screenfx
