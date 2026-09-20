#pragma once
// On-device end-to-end suite for the app flow (bead monhun-ardu-mgn qs.4; demo
// loop rework monhun-ardu-5r1): boot menu -> A -> hunt directly -> fight ->
// win/death -> A -> menu with a fully reset world, plus the shelf hub graph
// (hub -> quests take -> smith buy -> hub) that stays in the tree but is off the
// demo path. Drives the same src/app_state.hpp routing and src/app_setup.hpp
// hunt arming as the shipping sketch (the menu branch arms quest/tier + clears
// the hunt latch exactly like the screen branch).

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
// routing destination for this tick.
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
    // Known save: 500 zenny, a 4-herb pantry, no quest, no tier.
    SaveBlock save;
    saveDefaults(save);
    save.zenny = 500;
    save.items[ITEM_HERB] = 4;
    saveStore(save, REAL_BACKEND);

    MenuState menu;   // boot picks SWD / LUNGE
    ScreenState screen;
    Game g;
    bool huntLatched = false;

    // --------------------------------------- boot: menu A -> hunt directly
    test.expectEq(static_cast<uint32_t>(appMenuAccept()), APP_NAV_HUNT, F("menu A routes to hunt"));
    test.expectEq(static_cast<uint32_t>(menuStep(menu, H_A)), MENU_ACCEPT, F("menu A accepts"));
    test.expectEq(static_cast<uint32_t>(appNavApply(appMenuAccept(), menu, screen, save, g, H_A)), 1, F("hunt started"));
    test.expectEq(static_cast<uint32_t>(menu.active), 0, F("menu closed in hunt"));
    test.expectEq(static_cast<uint32_t>(screen.active), 0, F("no hub on the demo path"));
    test.expectEq(static_cast<uint32_t>(g.weapon), W_SWORD, F("hunt weapon from menu"));
    test.expectEq(static_cast<uint32_t>(g.monsterKind), MON_LUNGE, F("hunt beast from menu"));
    test.expectEq(static_cast<uint32_t>(g.over), OVER_NONE, F("hunt starts live"));
    test.expectEq(static_cast<uint32_t>(g.projN), 0, F("fresh projectile ring"));
    test.expectEq(static_cast<uint32_t>(g.fxN), 0, F("fresh effect ring"));

    // --------------------- shelf: hub -> quests take -> smith buy -> hub
    // The hub is off the demo path (monhun-ardu-5r1) but stays in the tree;
    // enter it directly to keep the quests/smith/EEPROM coverage live.
    appNavApply(APP_NAV_HUB, menu, screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(screen.active), 1, F("shelf hub active"));
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("hub screen"));
    test.expectEq(static_cast<uint32_t>(screen.rowCount), screens::SCREEN_HUB_ROWS, F("hub row count"));

    // The QUESTS row is hidden from the shipped hub, but the quest screen and
    // its save actions stay: enter it directly, take quest 0, then B back.
    appNavApply(APP_NAV_QUESTS, menu, screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_QUESTS, F("quests screen"));
    appNavApply(pressA(screen, save), menu, screen, save, g, H_A);   // take row 0
    test.expectEq(static_cast<uint32_t>(save.activeQuest), 0, F("quest 0 active"));
    test.expectEq(static_cast<uint32_t>(saveQuestGet(save, 0, 0)), 1, F("quest 0 taken bit"));

    // SMITH is hub row 1 (QUESTS hidden).
    appNavApply(pressB(screen, save), menu, screen, save, g, H_B);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("back on hub"));
    tap(screen, H_DOWN);
    test.expectEq(static_cast<uint32_t>(screen.cursor), 1, F("cursor on SMITH"));
    appNavApply(pressA(screen, save), menu, screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_SMITH, F("smith screen"));
    appNavApply(pressA(screen, save), menu, screen, save, g, H_A);   // buy SWORD T1
    test.expectEq(save.zenny, 400, F("zenny after buy"));
    test.expectEq(static_cast<uint32_t>(save.tier[W_SWORD]), 1, F("sword tier 1 stored"));

    // back to the hub, cursor reset to HUNT, then the hub pixel check.
    appNavApply(pressB(screen, save), menu, screen, save, g, H_B);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("back on hub 2"));
    test.expectEq(static_cast<uint32_t>(screen.cursor), 0, F("hub cursor reset to HUNT"));

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

    // hub B -> menu (shelf exit; nothing forces a hub on the demo path).
    appNavApply(pressB(screen, save), menu, screen, save, g, H_B);
    test.expectEq(static_cast<uint32_t>(menu.active), 1, F("hub B -> menu"));
    test.expectEq(static_cast<uint32_t>(screen.active), 0, F("hub closed on exit"));

    // ------------- menu A -> hunt with the shelf quest/tier armed, then win
    test.expectEq(static_cast<uint32_t>(appNavApply(appMenuAccept(), menu, screen, save, g, H_A)), 1, F("second hunt started"));
    questApplyToGame(g, save);
    upgradeApplyToGame(g, save);
    itemsApplyToGame(g, save);
    test.expectEq(static_cast<uint32_t>(g.weapon), W_SWORD, F("hunt weapon"));
    test.expectEq(static_cast<uint32_t>(g.monsterKind), MON_LUNGE, F("hunt beast"));
    test.expectEq(static_cast<uint32_t>(g.questTarget), MON_LUNGE, F("quest target armed"));
    test.expectEq(static_cast<uint32_t>(g.questNeed), 3, F("quest need armed"));
    test.expectEq(static_cast<uint32_t>(g.dmgMul), 110, F("tier 1 damage multiplier applied"));
    test.expectEq(static_cast<uint32_t>(g.items[ITEM_HERB]), 4, F("inventory restored from the save"));

    // ------------------------------- fight a few ticks, then win + commit
    for (uint8_t i = 0; i < 3; i++)
        stepGame(g, H_IDLE);
    test.expectEq(static_cast<uint32_t>(g.over), OVER_NONE, F("still fighting"));
    damageMonster(g, 2000, g.monster.x, g.monster.y);
    test.expectEq(static_cast<uint32_t>(g.over), OVER_WIN, F("hunt won"));
    test.expectEq(static_cast<uint32_t>(g.questProgress), 1, F("one kill counted"));
    // A carved scale + a gathered ore land in RAM only; the hunt-end commit
    // folds them into the save (coalesced, not per item).
    g.items[ITEM_SCALE] = 2;
    g.items[ITEM_ORE] = 1;

    test.expectEq(static_cast<uint32_t>(appHuntCommit(g.over != OVER_NONE, huntLatched, save, g)), 1, F("end commit fires"));
    saveStore(save, REAL_BACKEND);
    test.expectEq(static_cast<uint32_t>(appHuntCommit(g.over != OVER_NONE, huntLatched, save, g)), 0, F("end commit latched"));
    test.expectEq(static_cast<uint32_t>(save.progress), 1, F("progress written"));
    SaveBlock reloaded;
    test.expectEq(static_cast<uint32_t>(saveLoad(reloaded, REAL_BACKEND)), 1, F("progress reloads"));
    test.expectEq(static_cast<uint32_t>(reloaded.progress), 1, F("reloaded progress"));
    test.expectEq(reloaded.zenny, 400, F("reloaded zenny"));
    test.expectEq(static_cast<uint32_t>(reloaded.items[ITEM_HERB]), 4, F("pantry herb persisted"));
    test.expectEq(static_cast<uint32_t>(reloaded.items[ITEM_SCALE]), 2, F("carved scale persisted"));
    test.expectEq(static_cast<uint32_t>(reloaded.items[ITEM_ORE]), 1, F("gathered ore persisted"));

    // Over screen: the sketch runs menuReturnStep each tick; a fresh A returns
    // to the opening menu (demo flow), not the hub.
    menuReturnStep(menu, true, H_IDLE);
    test.expectEq(static_cast<uint32_t>(menuReturnStep(menu, true, H_A)), 1, F("over + A returns"));
    test.expectEq(static_cast<uint32_t>(appHuntReturn()), APP_NAV_MENU, F("hunt end routes to menu"));
    test.expectEq(static_cast<uint32_t>(appNavApply(appHuntReturn(), menu, screen, save, g, H_A)), 0, F("return is not a hunt start"));
    test.expectEq(static_cast<uint32_t>(menu.active), 1, F("hunt end -> menu"));
    test.expectEq(static_cast<uint32_t>(screen.active), 0, F("hunt end keeps the hub off"));
    test.expectEq(save.zenny, 400, F("menu zenny updated"));
    test.expectEq(static_cast<uint32_t>(save.progress), 1, F("menu progress updated"));

    // Fresh hunt from the menu resets the fight state (newGame path).
    g.projN = 2;
    g.fxN = 1;
    g.tick = 50;
    appNavApply(appMenuAccept(), menu, screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(g.tick), 0, F("fresh tick"));
    test.expectEq(static_cast<uint32_t>(g.projN), 0, F("fresh projectiles"));
    test.expectEq(static_cast<uint32_t>(g.fxN), 0, F("fresh effects"));
    test.expectEq(static_cast<uint32_t>(g.monsterKind), MON_LUNGE, F("fresh beast from menu"));
}

}   // namespace hubfx
