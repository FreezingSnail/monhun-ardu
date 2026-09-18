#pragma once
// On-device end-to-end suite for the qs.4 boot flow (bead monhun-ardu-mgn):
// boot menu -> hub -> quests (take; entered directly -- the hub row is hidden,
// epic monhun-ardu-nch) -> hub -> smith (buy) -> hub -> HUNT -> fight -> win ->
// hub with the committed progress + updated zenny, plus hub pixel checks.
// Drives the same src/app_state.hpp routing and src/app_setup.hpp hunt arming
// as the shipping sketch.

#include "harness/fxtest.hpp"
#include "src/screens.hpp"
#include "src/app_state.hpp"
#include "src/app_setup.hpp"
#include "src/core/world.hpp"
#include "src/core/monster.hpp"

#include <stdint.h>

namespace hubfx {

using namespace mh;

static const SaveBackend REAL_BACKEND = {saveEepromRead, saveEepromWrite};

static const Input H_IDLE = {0, 0, false, false};
static const Input H_DOWN = {0, 1, false, false};
static const Input H_A = {0, 0, true, false};
static const Input H_B = {0, 0, false, true};

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

// One d-pad tap: press then release, so the next direction is a fresh press.
static void tap(ScreenState &s, const Input &dir) {
    screenStep(s, dir);
    screenStep(s, H_IDLE);
}

// Mirror of the sketch's screen branch: nav on A/B, and on a non-routing A
// resolve + apply the cursor row's save action (committing it). Returns the
// boot-flow destination for this tick.
static AppNav screenTick(ScreenState &s, SaveBlock &save, const Input &in) {
    const ScreenEvent ev = screenStep(s, in);
    if (ev == SCREEN_BACK)
        return appScreenBack(s.screen);
    if (ev != SCREEN_ACCEPT)
        return APP_NAV_NONE;
    ScreenRow row;
    if (!screenCursorRow(s, row) || !screenCondOk(save, row))
        return APP_NAV_NONE;
    const AppNav nav = appScreenAccept(s.screen, row);
    if (nav == APP_NAV_NONE && screenApplyAction(save, row))
        saveStore(save, REAL_BACKEND);
    return nav;
}

// A/B press+release, releasing first so a held button from the previous
// transition cannot swallow the edge (the appNavApply guard).
static AppNav pressA(ScreenState &s, SaveBlock &save) {
    screenTick(s, save, H_IDLE);
    const AppNav nav = screenTick(s, save, H_A);
    screenTick(s, save, H_IDLE);
    return nav;
}

static AppNav pressB(ScreenState &s, SaveBlock &save) {
    screenTick(s, save, H_IDLE);
    const AppNav nav = screenTick(s, save, H_B);
    screenTick(s, save, H_IDLE);
    return nav;
}

inline void test_hub(FxTest &test) {
    // Known save: 500 zenny, no quest, no tier.
    SaveBlock save;
    saveDefaults(save);
    save.zenny = 500;
    saveStore(save, REAL_BACKEND);

    MenuState menu;   // boot picks SWD / LUNGE
    ScreenState screen;
    Game g;
    bool huntLatched = false;

    // --------------------------------------------------- boot: menu -> hub
    test.expectEq(static_cast<uint32_t>(menuStep(menu, H_A)), MENU_ACCEPT, F("menu A accepts"));
    appNavApply(appMenuAccept(), menu, screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(screen.active), 1, F("hub active"));
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("hub screen"));
    test.expectEq(static_cast<uint32_t>(screen.rowCount), screens::SCREEN_HUB_ROWS, F("hub row count"));

    // ------------------------------ quests flow (hub row hidden, epic nch)
    // The QUESTS row is hidden from the shipped hub, but the quest screen and
    // its save actions stay: enter it directly, take quest 0, then B back.
    appNavApply(APP_NAV_QUESTS, menu, screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_QUESTS, F("quests screen"));
    appNavApply(pressA(screen, save), menu, screen, save, g, H_A);   // take row 0
    test.expectEq(static_cast<uint32_t>(save.activeQuest), 0, F("quest 0 active"));
    test.expectEq(static_cast<uint32_t>(saveQuestGet(save, 0, 0)), 1, F("quest 0 taken bit"));

    // ----------------------------------------------- back -> smith: buy T1
    // SMITH is now hub row 1 (QUESTS hidden).
    appNavApply(pressB(screen, save), menu, screen, save, g, H_B);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("back on hub"));
    tap(screen, H_DOWN);
    test.expectEq(static_cast<uint32_t>(screen.cursor), 1, F("cursor on SMITH"));
    appNavApply(pressA(screen, save), menu, screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_SMITH, F("smith screen"));
    appNavApply(pressA(screen, save), menu, screen, save, g, H_A);   // buy SWORD T1
    test.expectEq(save.zenny, 400, F("zenny after buy"));
    test.expectEq(static_cast<uint32_t>(save.tier[W_SWORD]), 1, F("sword tier 1 stored"));

    // -------------------------------------------------- back -> HUNT start
    appNavApply(pressB(screen, save), menu, screen, save, g, H_B);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("back on hub 2"));
    test.expectEq(static_cast<uint32_t>(screen.cursor), 0, F("hub cursor reset to HUNT"));
    const AppNav hunt = pressA(screen, save);
    test.expectEq(static_cast<uint32_t>(hunt), APP_NAV_HUNT, F("HUNT routes to hunt"));
    test.expectEq(static_cast<uint32_t>(appNavApply(hunt, menu, screen, save, g, H_A)), 1, F("hunt started"));
    questApplyToGame(g, save);
    upgradeApplyToGame(g, save);
    test.expectEq(static_cast<uint32_t>(g.weapon), W_SWORD, F("hunt weapon"));
    test.expectEq(static_cast<uint32_t>(g.monsterKind), MON_LUNGE, F("hunt beast"));
    test.expectEq(static_cast<uint32_t>(g.questTarget), MON_LUNGE, F("quest target armed"));
    test.expectEq(static_cast<uint32_t>(g.questNeed), 3, F("quest need armed"));
    test.expectEq(static_cast<uint32_t>(g.dmgMul), 110, F("tier 1 damage multiplier applied"));

    // ------------------------------- fight a few ticks, then win + commit
    for (uint8_t i = 0; i < 3; i++)
        stepGame(g, H_IDLE);
    test.expectEq(static_cast<uint32_t>(g.over), OVER_NONE, F("still fighting"));
    damageMonster(g, 9999, g.monster.x, g.monster.y);
    test.expectEq(static_cast<uint32_t>(g.over), OVER_WIN, F("hunt won"));
    test.expectEq(static_cast<uint32_t>(g.questProgress), 1, F("one kill counted"));

    test.expectEq(static_cast<uint32_t>(appHuntCommit(g.over != OVER_NONE, huntLatched, save, g.questProgress)), 1, F("end commit fires"));
    saveStore(save, REAL_BACKEND);
    test.expectEq(static_cast<uint32_t>(appHuntCommit(g.over != OVER_NONE, huntLatched, save, g.questProgress)), 0, F("end commit latched"));
    test.expectEq(static_cast<uint32_t>(save.progress), 1, F("progress written"));
    SaveBlock reloaded;
    test.expectEq(static_cast<uint32_t>(saveLoad(reloaded, REAL_BACKEND)), 1, F("progress reloads"));
    test.expectEq(static_cast<uint32_t>(reloaded.progress), 1, F("reloaded progress"));
    test.expectEq(reloaded.zenny, 400, F("reloaded zenny"));

    // Over screen: the sketch runs menuReturnStep each tick; a fresh A returns.
    menuReturnStep(menu, true, H_IDLE);
    test.expectEq(static_cast<uint32_t>(menuReturnStep(menu, true, H_A)), 1, F("over + A returns"));
    appNavApply(appHuntReturn(), menu, screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("hunt end -> hub"));
    test.expectEq(save.zenny, 400, F("hub zenny updated"));
    test.expectEq(static_cast<uint32_t>(save.progress), 1, F("hub progress updated"));

    // ------------------------------------------------------ hub pixels
    arduboy.startGray();
    while (arduboy.currentPlane() != 0) {
        FX::enableOLED();
        arduboy.waitForNextPlane();
        FX::disableOLED();
    }
    clearFb();
    drawScreen(screen, save);

    // HUNT is row 0 with the white chip cursor; SMITH row 1; the ZENNY row
    // (row 2, y = 11 + 2*9 = 29) shows the live 400 balance in the cost column.
    test.expectEq(countBits(2, 5, 13, 16), 16, F("hub cursor chip 4x4"));
    test.expectEq(countBits(2, 13, 0, 7) > 0 ? 1 : 0, 1, F("hub title ink"));
    test.expectEq(countBits(10, 30, 11, 18) > 0 ? 1 : 0, 1, F("HUNT label ink"));
    test.expectEq(countBits(10, 40, 20, 27) > 0 ? 1 : 0, 1, F("SMITH label ink"));
    test.expectEq(countBits(10, 32, 29, 36) > 0 ? 1 : 0, 1, F("ZENNY label ink"));
    test.expectEq(countBits(112, 123, 29, 36) > 0 ? 1 : 0, 1, F("live zenny 400 drawn"));
}

}   // namespace hubfx
