#pragma once
// On-device end-to-end suite for the app flow (bead monhun-ardu-mgn qs.4; hub as
// the root screen monhun-ardu-isp.1): boot -> hub -> HUNT -> fight -> win/death
// -> A -> hub -> QUESTS board, plus the hub -> quests take -> gear -> hub
// detour. Drives the same src/app_state.hpp routing and src/app_setup.hpp hunt
// start (huntStart + quest/tier/items/armor arming) as the shipping sketch.
// ui.3.1 (5co.6): the SMITH screen is gone; the armor card craft/equip E2E is
// pinned by cards_test.

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

// Apply a nav exactly like the sketch: a hunt request starts the hunt through
// the device glue (huntStart) and arms quest/tier/items. (armorApplyToGame is
// the same call the sketch makes; it is covered by the armor engine suites.)
static bool applyNav(AppNav nav, ScreenState &s, SaveBlock &save, Game &g, const Input &in) {
    if (!appNavApply(nav, s, save, g, in))
        return false;
    huntStart(g, save);
    questApplyToGame(g, save);
    upgradeApplyToGame(g, save);
    itemsApplyToGame(g, save);
    return true;
}

inline void test_hub(FxTest &test) {
    // Known save: 500 zenny, a 4-herb pantry, no quest, no tier.
    SaveBlock save;
    saveDefaults(save);
    save.zenny = 500;
    save.items[ITEM_HERB] = 4;
    save.items[ITEM_ORE] = 3;
    save.items[ITEM_SCALE] = 2;
    saveStore(save, REAL_BACKEND);

    ScreenState screen;
    Game g;

    // --------------------------------------- boot: the hub is the root (isp.1)
    screenEnter(screen, screens::SCREEN_HUB, save);
    test.expectEq(static_cast<uint32_t>(screen.active), 1, F("boot hub active"));
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("boot hub screen"));
    test.expectEq(static_cast<uint32_t>(screen.rowCount), screens::SCREEN_HUB_ROWS, F("hub row count"));
    test.expectEq(static_cast<uint32_t>(g.over), OVER_NONE, F("no hunt started yet"));

    // ----------------------------- hub HUNT (row 0) launches the picked hunt
    // No active quest -> the LUNGE fallback beast; the save weapon is used.
    test.expectEq(static_cast<uint32_t>(applyNav(pressA(screen, save), screen, save, g, H_A)), 1, F("hub HUNT starts the hunt"));
    test.expectEq(static_cast<uint32_t>(g.weapon), W_SWORD, F("hunt weapon from the save"));
    test.expectEq(static_cast<uint32_t>(g.monsterKind), MON_LUNGE, F("no quest -> fallback lunge beast"));
    test.expectEq(static_cast<uint32_t>(g.over), OVER_NONE, F("hunt starts live"));
    test.expectEq(static_cast<uint32_t>(g.projN), 0, F("fresh projectile ring"));
    test.expectEq(static_cast<uint32_t>(g.fxN), 0, F("fresh effect ring"));

    // --------------------- hub -> quests take -> gear -> hub
    appNavApply(APP_NAV_HUB, screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(screen.active), 1, F("hub active again"));
    test.expectEq(static_cast<uint32_t>(screen.rowCount), screens::SCREEN_HUB_ROWS, F("hub row count"));

    // QUESTS is hub row 1: cursor down once, A opens the board, A takes quest 0.
    tap(screen, H_DOWN);
    test.expectEq(static_cast<uint32_t>(screen.cursor), 1, F("cursor on QUESTS"));
    appNavApply(pressA(screen, save), screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_QUESTS, F("quests screen"));
    test.expectEq(static_cast<uint32_t>(screen.rowCount), screens::SCREEN_QUESTS_ROWS, F("quests row count"));
    appNavApply(pressA(screen, save), screen, save, g, H_A);   // take row 0
    test.expectEq(static_cast<uint32_t>(save.activeQuest), 0, F("quest 0 active"));
    test.expectEq(static_cast<uint32_t>(saveQuestGet(save, 0, 0)), 1, F("quest 0 taken bit"));

    // GEAR is hub row 2 (ui.3.1: the SMITH row is gone). The armor craft/equip
    // now lives on the GEAR armor card and is pinned by cards_test.
    appNavApply(pressB(screen, save), screen, save, g, H_B);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("back on hub"));
    test.expectEq(static_cast<uint32_t>(screen.cursor), 0, F("hub cursor reset to HUNT"));
    tap(screen, H_DOWN);
    tap(screen, H_DOWN);
    test.expectEq(static_cast<uint32_t>(screen.cursor), 2, F("cursor on GEAR"));
    appNavApply(pressA(screen, save), screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_GEAR, F("gear screen"));
    test.expectEq(static_cast<uint32_t>(screen.rowCount), screens::SCREEN_GEAR_ROWS, F("gear row count"));
    appNavApply(pressB(screen, save), screen, save, g, H_B);
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

    // HUNT is row 0 with the white chip cursor; QUESTS row 1; GEAR row 2; the
    // ZENNY row (row 3, y = 11 + 3*9 = 38) shows the live 500 balance.
    test.expectEq(countBits(2, 5, 13, 16), 16, F("hub cursor chip 4x4"));
    test.expectEq(countBits(2, 13, 0, 7) > 0 ? 1 : 0, 1, F("hub title ink"));
    test.expectEq(countBits(10, 30, 11, 18) > 0 ? 1 : 0, 1, F("HUNT label ink"));
    test.expectEq(countBits(10, 50, 20, 27) > 0 ? 1 : 0, 1, F("QUESTS label ink"));
    test.expectEq(countBits(10, 40, 29, 36) > 0 ? 1 : 0, 1, F("GEAR label ink"));
    test.expectEq(countBits(10, 32, 38, 45) > 0 ? 1 : 0, 1, F("ZENNY label ink"));
    test.expectEq(countBits(108, 123, 38, 45) > 0 ? 1 : 0, 1, F("live zenny 500 drawn"));

    // hub B is a root no-op: the hub stays up (isp.1 deleted the menu).
    appNavApply(pressB(screen, save), screen, save, g, H_B);
    test.expectEq(static_cast<uint32_t>(screen.active), 1, F("hub B keeps the hub active"));
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("hub B stays on the hub"));

    // ------------- hub HUNT with quest 0 active: kill target armed
    test.expectEq(static_cast<uint32_t>(applyNav(pressA(screen, save), screen, save, g, H_A)), 1, F("second hunt started"));
    test.expectEq(static_cast<uint32_t>(g.weapon), W_SWORD, F("hunt weapon"));
    test.expectEq(static_cast<uint32_t>(g.monsterKind), MON_LUNGE, F("quest kill target beast"));
    test.expectEq(static_cast<uint32_t>(g.questGoalKind), quests::GOAL_KILL, F("quest kill goal armed"));
    test.expectEq(static_cast<uint32_t>(g.questTarget), MON_LUNGE, F("quest target armed"));
    test.expectEq(static_cast<uint32_t>(g.questNeed), 3, F("quest need armed"));
    // ui.2: no purchasable weapon tiers -> save.tier stays 0 -> identity mul.
    // (The smith tier table still feeds upgradeApplyToGame; the purchase rows
    // return as ui.4 FORGE trees.)
    test.expectEq(static_cast<uint32_t>(g.dmgMul), 100, F("tier 0 identity multiplier"));
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

    bool huntLatched = false;
    test.expectEq(static_cast<uint32_t>(appHuntCommit(g.over != OVER_NONE, huntLatched, save, g)), 1, F("end commit fires"));
    saveStore(save, REAL_BACKEND);
    test.expectEq(static_cast<uint32_t>(appHuntCommit(g.over != OVER_NONE, huntLatched, save, g)), 0, F("end commit latched"));
    test.expectEq(static_cast<uint32_t>(save.progress), 1, F("progress written"));
    SaveBlock reloaded;
    test.expectEq(static_cast<uint32_t>(saveLoad(reloaded, REAL_BACKEND)), 1, F("progress reloads"));
    test.expectEq(static_cast<uint32_t>(reloaded.progress), 1, F("reloaded progress"));
    test.expectEq(reloaded.zenny, 500, F("reloaded zenny"));
    test.expectEq(static_cast<uint32_t>(reloaded.items[ITEM_HERB]), 4, F("pantry herb persisted"));
    test.expectEq(static_cast<uint32_t>(reloaded.items[ITEM_SCALE]), 2, F("carved scale persisted"));
    test.expectEq(static_cast<uint32_t>(reloaded.items[ITEM_ORE]), 3, F("gathered ore folds over the pantry (max)"));

    // Over screen: the sketch runs appOverReturnStep each tick; a fresh A
    // returns to the hub (dlp.3) so the quest can be turned in.
    bool prevA = false;
    appOverReturnStep(true, H_IDLE, prevA);
    test.expectEq(static_cast<uint32_t>(appOverReturnStep(true, H_A, prevA)), 1, F("over + A returns"));
    test.expectEq(static_cast<uint32_t>(appHuntReturn()), APP_NAV_HUB, F("hunt end routes to hub"));
    test.expectEq(static_cast<uint32_t>(appNavApply(appHuntReturn(), screen, save, g, H_A)), 0, F("return is not a hunt start"));
    test.expectEq(static_cast<uint32_t>(screen.active), 1, F("hunt end -> hub"));
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("hub after hunt"));
    test.expectEq(save.zenny, 500, F("hub zenny updated"));
    test.expectEq(static_cast<uint32_t>(save.progress), 1, F("hub progress updated"));

    // The hub QUESTS row (row 1) reaches the 8-row board from the live hub.
    tap(screen, H_DOWN);
    appNavApply(pressA(screen, save), screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_QUESTS, F("hub QUESTS row reaches the board"));
    test.expectEq(static_cast<uint32_t>(screen.rowCount), screens::SCREEN_QUESTS_ROWS, F("board row count"));
    appNavApply(pressB(screen, save), screen, save, g, H_B);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("board B -> hub"));

    // Fresh hunt from the hub resets the fight state (newGame path).
    g.projN = 2;
    g.fxN = 1;
    g.tick = 50;
    applyNav(pressA(screen, save), screen, save, g, H_A);   // hub HUNT
    test.expectEq(static_cast<uint32_t>(g.tick), 0, F("fresh tick"));
    test.expectEq(static_cast<uint32_t>(g.projN), 0, F("fresh projectiles"));
    test.expectEq(static_cast<uint32_t>(g.fxN), 0, F("fresh effects"));
    test.expectEq(static_cast<uint32_t>(g.monsterKind), MON_LUNGE, F("fresh quest beast"));

    // ---------------------- hub GEAR: equip FLAIL, next hunt uses it (hml.3)
    // Return to the hub, open GEAR (row 2), pick FLAIL (row 1), A equips and
    // persists; the next hub HUNT starts the hunt with the equipped weapon.
    appNavApply(APP_NAV_HUB, screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("hub again"));
    tap(screen, H_DOWN);
    tap(screen, H_DOWN);
    test.expectEq(static_cast<uint32_t>(screen.cursor), 2, F("cursor on GEAR"));
    appNavApply(pressA(screen, save), screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_GEAR, F("gear screen"));
    test.expectEq(static_cast<uint32_t>(screen.rowCount), screens::SCREEN_GEAR_ROWS, F("gear row count"));
    tap(screen, H_DOWN);   // SWORD -> FLAIL
    test.expectEq(static_cast<uint32_t>(screen.cursor), 1, F("cursor on FLAIL"));
    applyNav(pressA(screen, save), screen, save, g, H_A);   // equip (save action, no hunt start)
    test.expectEq(static_cast<uint32_t>(save.weapon), W_FLAIL, F("flail equipped"));
    SaveBlock geareload;
    test.expectEq(static_cast<uint32_t>(saveLoad(geareload, REAL_BACKEND)), 1, F("gear save reloads"));
    test.expectEq(static_cast<uint32_t>(geareload.weapon), W_FLAIL, F("weapon byte persisted"));
    appNavApply(pressB(screen, save), screen, save, g, H_B);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("gear B -> hub"));
    applyNav(pressA(screen, save), screen, save, g, H_A);   // hub HUNT
    test.expectEq(static_cast<uint32_t>(g.weapon), W_FLAIL, F("hunt started with the equipped flail"));
}

}   // namespace hubfx
