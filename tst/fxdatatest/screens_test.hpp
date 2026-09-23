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

#include <stdint.h>

namespace screenfx {

using namespace mh;

// gs.2: one file-scope Game for the armor cache fill (a second ~750 B stack
// frame does not fit the sim's tight stack; smith_test.hpp takes the same
// approach).
static Game g_gear;

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
    test.expectEq(screens::SCREEN_COUNT, 4, F("screen count"));
    test.expectEq(screens::SCREEN_HUB, 0, F("hub index"));
    test.expectEq(screens::SCREEN_QUESTS, 1, F("quests index"));
    test.expectEq(screens::SCREEN_SMITH, 2, F("smith index"));
    test.expectEq(screens::SCREEN_GEAR, 3, F("gear index"));
    test.expectEq(screenRowCount(screens::SCREEN_HUB), 5, F("hub row count"));

    // Title bytes come from the cart def (id u8, titleLen u8, title chars).
    const uint16_t hubDef = screenDefOff(screens::SCREEN_HUB);
    test.expectEq(mhFxReadU8(screenCart(hubDef)), 0, F("hub def id"));
    test.expectEq(mhFxReadU8(screenCart(hubDef + 1)), 3, F("hub title len"));
    test.expectEq(mhFxReadU8(screenCart(hubDef + 2)), 'H', F("hub title H"));
    test.expectEq(mhFxReadU8(screenCart(hubDef + 3)), 'U', F("hub title U"));
    test.expectEq(mhFxReadU8(screenCart(hubDef + 4)), 'B', F("hub title B"));

    ScreenRow r0, r1, r2, r3, r4;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_HUB, 0), r0);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_HUB, 1), r1);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_HUB, 2), r2);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_HUB, 3), r3);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_HUB, 4), r4);
    test.expectEq(r0.cost, 0, F("row0 cost"));
    test.expectEq(r0.action, screens::ACTION_HUNT, F("row0 action hunt"));
    test.expectEq(r0.cond, screens::COND_ALWAYS, F("row0 cond"));
    test.expectEq(r0.param, 0, F("row0 param"));
    test.expectEq(r1.action, screens::ACTION_OPEN_QUESTS, F("row1 action open quests"));
    test.expectEq(r1.cond, screens::COND_ALWAYS, F("row1 cond"));
    test.expectEq(r2.action, screens::ACTION_OPEN_SMITH, F("row2 action open smith"));
    test.expectEq(r2.cond, screens::COND_ALWAYS, F("row2 cond"));
    test.expectEq(r3.action, screens::ACTION_OPEN_GEAR, F("row3 action open gear"));
    test.expectEq(r3.cond, screens::COND_ALWAYS, F("row3 cond"));
    test.expectEq(r4.action, screens::ACTION_NONE, F("row4 action none"));
    test.expectEq(r4.flags, screens::ROW_F_ZENNY, F("row4 zenny dynamic-value flag"));

    // ----------------------------------------------------- nav/scroll
    SaveBlock save;
    saveDefaults(save);

    ScreenState st;
    screenEnter(st, screens::SCREEN_HUB, save);
    test.expectEq(st.rowCount, 5, F("enter rowCount"));
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
    test.expectEq(st.cursor, 4, F("nav up wraps"));
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
    // through the smith (armor craft) and quests (take) rows.
    ScreenRow s0, q0;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_SMITH, 0), s0);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_QUESTS, 0), q0);
    SaveBlock act;
    saveDefaults(act);
    act.zenny = 300;
    act.items[ITEM_ORE] = 3;   // HUNTER HELM recipe (arm.2)
    act.items[ITEM_SCALE] = 2;
    test.expectEq(screenCondOk(act, s0), 1, F("helm craftable"));
    test.expectEq(screenApplyAction(act, s0), 1, F("craft applies"));
    test.expectEq(act.zenny, 0, F("craft debits zenny"));
    test.expectEq(saveCrafted(act, armor::ARMOR_HUNTER_HELM), 1, F("craft sets the crafted bit"));
    test.expectEq(act.equip[armor::SLOT_HEAD], armor::ARMOR_HUNTER_HELM + 1, F("craft equips the helm"));
    test.expectEq(static_cast<uint32_t>(act.items[ITEM_ORE]), 0, F("craft debits ore"));
    test.expectEq(static_cast<uint32_t>(act.items[ITEM_SCALE]), 0, F("craft debits scale"));
    test.expectEq(screenCondOk(act, q0), 1, F("take always allowed"));
    test.expectEq(screenApplyAction(act, q0), 1, F("take applies"));
    test.expectEq(saveQuestGet(act, 0, 0), 1, F("quest0 taken"));
    test.expectEq(screenApplyAction(act, r3), 0, F("none row changes nothing"));
    test.expectEq(appScreenAccept(screens::SCREEN_HUB, r0), APP_NAV_HUNT, F("hub HUNT routes to hunt"));
    test.expectEq(appScreenAccept(screens::SCREEN_HUB, r1), APP_NAV_QUESTS, F("hub QUESTS route"));
    test.expectEq(appScreenAccept(screens::SCREEN_HUB, r2), APP_NAV_SMITH, F("hub SMITH route"));
    test.expectEq(appScreenAccept(screens::SCREEN_HUB, r3), APP_NAV_GEAR, F("hub GEAR route"));
    test.expectEq(appScreenAccept(screens::SCREEN_HUB, r4), APP_NAV_NONE, F("hub zenny row is a no-op"));
    test.expectEq(appScreenBack(screens::SCREEN_QUESTS), APP_NAV_HUB, F("quests B -> hub"));
    test.expectEq(appScreenBack(screens::SCREEN_HUB), APP_NAV_NONE, F("hub B is a root no-op"));

    // ----------------------------------------------------- EEPROM roundtrip
    SaveBlock eep;
    saveDefaults(eep);
    eep.zenny = 1234;
    eep.tier[0] = 2;
    eep.equip[0] = 3;   // head slot (prg.5 tail)
    eep.flags = SAVE_FLAG_SMITHY_SEEN;
    eep.items[ITEM_HERB] = 6;
    eep.items[ITEM_FANG] = 2;
    eep.weapon = W_GUN;   // save v4 weapon (hml.1)
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
    test.expectEq(out.weapon, W_GUN, F("eeprom weapon"));

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
    // Row 3 label (GEAR) at y = 11 + 3*9 = 38.
    test.expectEq(countBits(10, 50, 38, 45) > 0 ? 1 : 0, 1, F("row3 GEAR label ink"));
    // Row 4 is the dynamic ZENNY row: the live balance (1234, 4 digits) is
    // drawn in the cost column at y = 11 + 4*9 = 47, not the packed cost.
    test.expectEq(countBits(10, 32, 47, 54) > 0 ? 1 : 0, 1, F("zenny row label ink"));
    test.expectEq(countBits(108, 123, 47, 54) > 0 ? 1 : 0, 1, F("zenny balance drawn"));
    // Control: an empty balance draws one digit at the right edge only.
    clearFb();
    saveDefaults(ps);
    drawScreen(draw, ps);
    test.expectEq(countBits(108, 119, 47, 54), 0, F("zenny 0 leaves the 4-digit span empty"));
    test.expectEq(countBits(120, 123, 47, 54) > 0 ? 1 : 0, 1, F("zenny 0 digit drawn"));

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

    // ------------------------------------------------------- gear screen
    // gs.2: five skill readout rows sit between the armor rows and LEAVE.
    test.expectEq(screenRowCount(screens::SCREEN_GEAR), 14, F("gear row count"));
    ScreenRow g0, g1, g2, g3, g4, g5, g6, g7, g8, g9, g10, g11, g12, g13;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 0), g0);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 1), g1);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 2), g2);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 3), g3);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 4), g4);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 5), g5);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 6), g6);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 7), g7);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 8), g8);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 9), g9);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 10), g10);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 11), g11);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 12), g12);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_GEAR, 13), g13);
    test.expectEq(g0.action, screens::ACTION_EQUIP_WEAPON, F("gear row0 action equip"));
    test.expectEq(g0.param, W_SWORD, F("gear row0 param sword"));
    test.expectEq(g1.action, screens::ACTION_EQUIP_WEAPON, F("gear row1 action equip"));
    test.expectEq(g1.param, W_FLAIL, F("gear row1 param flail"));
    test.expectEq(g2.action, screens::ACTION_EQUIP_WEAPON, F("gear row2 action equip"));
    test.expectEq(g2.param, W_GUN, F("gear row2 param gun"));
    // Armor rows pack (slot << 5) | pieceIdx like the smith rows (gs.1).
    test.expectEq(g3.action, screens::ACTION_EQUIP_ARMOR, F("gear row3 action equip armor"));
    test.expectEq(g3.cond, screens::COND_CRAFTED, F("gear row3 cond crafted"));
    test.expectEq(g3.param, armor::ARMOR_HUNTER_HELM, F("gear row3 param helm"));
    test.expectEq(g4.param, armor::ARMOR_BONE_CAP, F("gear row4 param cap"));
    test.expectEq(g5.param, static_cast<uint8_t>((armor::SLOT_BODY << 5) | armor::ARMOR_HUNTER_MAIL), F("gear row5 param mail"));
    test.expectEq(g6.param, static_cast<uint8_t>((armor::SLOT_BODY << 5) | armor::ARMOR_BONE_MAIL), F("gear row6 param bone mail"));
    test.expectEq(g7.param, static_cast<uint8_t>((armor::SLOT_CHARM << 5) | armor::ARMOR_EVADE_CHARM), F("gear row7 param charm"));
    // gs.2 skill rows: inert (action none, always live) with param = skill idx.
    test.expectEq(g8.action, screens::ACTION_NONE, F("gear row8 skill action none"));
    test.expectEq(g8.cond, screens::COND_ALWAYS, F("gear row8 skill cond always"));
    test.expectEq(g8.flags, screens::ROW_F_SKILL, F("gear row8 skill flag"));
    test.expectEq(g8.param, armor::SKILL_ATTACK_UP, F("gear row8 attack up"));
    test.expectEq(g9.param, armor::SKILL_DEFENSE_UP, F("gear row9 defense up"));
    test.expectEq(g10.param, armor::SKILL_HEALTH_UP, F("gear row10 health up"));
    test.expectEq(g11.param, armor::SKILL_STAMINA_UP, F("gear row11 stamina up"));
    test.expectEq(g12.param, armor::SKILL_EVADE_WINDOW, F("gear row12 evade"));
    test.expectEq(g13.action, screens::ACTION_LEAVE, F("gear leave row"));
    test.expectEq(appScreenAccept(screens::SCREEN_GEAR, g13), APP_NAV_HUB, F("gear leave backs to hub"));
    test.expectEq(appScreenAccept(screens::SCREEN_GEAR, g8), APP_NAV_NONE, F("gear skill row is inert"));
    test.expectEq(appScreenAccept(screens::SCREEN_GEAR, g1), APP_NAV_NONE, F("gear equip is a save action"));
    // The cart's equip row writes the v4 weapon byte.
    SaveBlock gear;
    saveDefaults(gear);
    test.expectEq(screenApplyAction(gear, g2), 1, F("cart gear row equips"));
    test.expectEq(gear.weapon, W_GUN, F("cart gear row wrote the weapon byte"));
    test.expectEq(screenApplyAction(gear, g2), 0, F("re-equip same weapon is a no-op"));
    // E2E armor equip off the cart: uncrafted is dead + inert, crafted toggles
    // the piece's slot and the change persists.
    test.expectEq(screenCondOk(gear, g3), 0, F("uncrafted helm row dead"));
    test.expectEq(screenApplyAction(gear, g3), 0, F("uncrafted equip no-op"));
    test.expectEq(gear.equip[armor::SLOT_HEAD], SAVE_EQUIP_NONE, F("head slot empty"));
    saveSetCrafted(gear, armor::ARMOR_HUNTER_HELM);
    test.expectEq(screenCondOk(gear, g3), 1, F("crafted helm row live"));
    test.expectEq(screenApplyAction(gear, g3), 1, F("crafted equip applies"));
    test.expectEq(gear.equip[armor::SLOT_HEAD], armor::ARMOR_HUNTER_HELM + 1, F("helm equipped"));
    saveStore(gear, REAL_BACKEND);
    SaveBlock geararmor;
    test.expectEq(saveLoad(geararmor, REAL_BACKEND), 1, F("armor save reloads"));
    test.expectEq(geararmor.equip[armor::SLOT_HEAD], armor::ARMOR_HUNTER_HELM + 1, F("helm equip persisted"));
    test.expectEq(saveCrafted(geararmor, armor::ARMOR_HUNTER_HELM), 1, F("crafted bit persisted"));
    test.expectEq(screenApplyAction(gear, g3), 1, F("second A unequips"));
    test.expectEq(gear.equip[armor::SLOT_HEAD], SAVE_EQUIP_NONE, F("helm unequipped"));

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
    test.expectEq(screenApplyAction(moveSave, g5), 1, F("equip mail via gear row"));
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

    // Pixel: a skill row draws its points + tier letter in the cost column.
    // ATTACK UP (idx 8) sits on page 1 (rows 6..11) at y = 11 + (8-6)*9 = 29;
    // 15 points -> 2 digits at x 116..123, the M letter just left at 108..111.
    clearFb();
    ScreenState smoke;
    screenEnter(smoke, screens::SCREEN_GEAR, moveSave);
    smoke.cursor = 8;
    smoke.scroll = 6;
    screenGearCache(smoke, g_gear.armor);   // attack 15/M
    drawScreen(smoke, moveSave);
    test.expectEq(countBits(108, 111, 29, 36) > 0 ? 1 : 0, 1, F("skill M letter ink"));
    test.expectEq(countBits(116, 123, 29, 36) > 0 ? 1 : 0, 1, F("skill points ink"));

    // An inert skill (DEFENSE UP idx 9: defense_up 4) draws the points only.
    clearFb();
    screenEnter(smoke, screens::SCREEN_GEAR, moveSave);
    smoke.cursor = 9;
    smoke.scroll = 6;
    screenGearCache(smoke, g_gear.armor);
    drawScreen(smoke, moveSave);
    test.expectEq(countBits(120, 123, 38, 45) > 0 ? 1 : 0, 1, F("inert skill points ink"));
    test.expectEq(countBits(108, 115, 38, 45), 0, F("inert skill no letter"));

    // Pixel: the gear page draws through the same generic renderer.
    clearFb();
    ScreenState gs;
    screenEnter(gs, screens::SCREEN_GEAR, ps);
    drawScreen(gs, ps);
    test.expectEq(countBits(2, 5, 13, 16), 16, F("gear cursor chip 4x4"));
    test.expectEq(countBits(2, 20, 0, 7) > 0 ? 1 : 0, 1, F("gear title ink"));
    test.expectEq(countBits(10, 60, 11, 18) > 0 ? 1 : 0, 1, F("gear row0 label ink"));
}

}   // namespace screenfx
