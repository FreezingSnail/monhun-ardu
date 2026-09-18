#pragma once
// Host unit tests for the boot-flow routing (bead monhun-ardu-mgn, qs.4):
// src/app_state.hpp. Pins the menu -> hub -> quests/smith/hunt -> back graph,
// the held-button guards on every transition, and the once-per-hunt progress
// commit. The device E2E counterpart (tst/fxdatatest/hub_test.hpp) drives the
// same app_state.hpp functions against the real cart + EEPROM.
#include "test.hpp"
#include "../src/app_state.hpp"

using namespace mh;

namespace apptest {

const Input AT_IDLE = Input{0, 0, false, false};
const Input AT_A = Input{0, 0, true, false};
const Input AT_B = Input{0, 0, false, true};

inline ScreenRow arow(uint8_t action, uint8_t cond = screens::COND_ALWAYS, uint8_t param = 0) {
    ScreenRow r;
    r.cost = 0;
    r.action = action;
    r.cond = cond;
    r.param = param;
    r.flags = 0;
    return r;
}

}   // namespace apptest

using namespace apptest;

void AppSuite(TestRunner &runner) {
    TestSuite suite("Boot-flow routing: menu/hub/screens/hunt (src/app_state.hpp, qs.4)");

    {
        Test t("menu A opens the hub with the generated row count and keeps the picks");
        MenuState menu;
        menu.weapon = W_FLAIL;
        menu.target = MON_HEAVY;
        ScreenState screen;
        Game g;
        SaveBlock save;
        saveDefaults(save);
        t.assert(menuStep(menu, AT_A), MENU_ACCEPT, "A edge accepts");
        t.assert(appNavApply(appMenuAccept(), menu, screen, save, g, AT_A), false, "no hunt yet");
        t.assert(screen.active, true, "hub active");
        t.assert(screen.screen, screens::SCREEN_HUB, "hub screen id");
        t.assert(screen.rowCount, screens::SCREEN_HUB_ROWS, "hub row count from meta");
        t.assert(screen.cursor, 0, "hub cursor at HUNT");
        t.assert(menu.active, false, "menu closed");
        t.assert(menu.weapon, W_FLAIL, "weapon pick kept");
        t.assert(menu.target, MON_HEAVY, "target pick kept");
        suite.addTest(t);
    }

    {
        Test t("held A at the transition cannot re-fire inside the hub");
        MenuState menu;
        ScreenState screen;
        Game g;
        SaveBlock save;
        saveDefaults(save);
        appNavApply(appMenuAccept(), menu, screen, save, g, AT_A);   // A still down
        t.assert(screenStep(screen, AT_A), SCREEN_NONE, "held A silent on entry tick");
        t.assert(screenStep(screen, AT_IDLE), SCREEN_NONE, "release silent");
        t.assert(screenStep(screen, AT_A), SCREEN_ACCEPT, "fresh A accepts");
        suite.addTest(t);
    }

    {
        Test t("held B at the back transition cannot re-fire inside the destination");
        MenuState menu;
        ScreenState screen;
        Game g;
        SaveBlock save;
        saveDefaults(save);
        appNavApply(appMenuAccept(), menu, screen, save, g, AT_A);
        appNavApply(APP_NAV_QUESTS, menu, screen, save, g, AT_A);
        appNavApply(appScreenBack(screen.screen), menu, screen, save, g, AT_B);   // B still down
        t.assert(screen.screen, screens::SCREEN_HUB, "back landed on hub");
        t.assert(screenStep(screen, AT_B), SCREEN_NONE, "held B silent on entry tick");
        t.assert(screenStep(screen, AT_IDLE), SCREEN_NONE, "release silent");
        t.assert(screenStep(screen, AT_B), SCREEN_BACK, "fresh B backs out");
        suite.addTest(t);
    }

    {
        Test t("hub A routing: HUNT / QUESTS / SMITH / LEAVE; other rows are no-ops");
        t.assert(appScreenAccept(screens::SCREEN_HUB, arow(screens::ACTION_HUNT)), APP_NAV_HUNT, "HUNT row");
        t.assert(appScreenAccept(screens::SCREEN_HUB, arow(screens::ACTION_OPEN_QUESTS)), APP_NAV_QUESTS, "QUESTS row");
        t.assert(appScreenAccept(screens::SCREEN_HUB, arow(screens::ACTION_OPEN_SMITH)), APP_NAV_SMITH, "SMITH row");
        t.assert(appScreenAccept(screens::SCREEN_HUB, arow(screens::ACTION_LEAVE)), APP_NAV_MENU, "LEAVE row");
        t.assert(appScreenAccept(screens::SCREEN_HUB, arow(screens::ACTION_NONE, screens::COND_ALWAYS)), APP_NAV_NONE, "status row");
        t.assert(appScreenAccept(screens::SCREEN_HUB, arow(screens::ACTION_TAKE_QUEST, screens::COND_QUEST)), APP_NAV_NONE, "quest row not a hub dest");
        suite.addTest(t);
    }

    {
        Test t("sub-screen A: LEAVE returns to the hub, save actions stay with the caller");
        t.assert(appScreenAccept(screens::SCREEN_QUESTS, arow(screens::ACTION_LEAVE)), APP_NAV_HUB, "quests LEAVE");
        t.assert(appScreenAccept(screens::SCREEN_SMITH, arow(screens::ACTION_LEAVE)), APP_NAV_HUB, "smith LEAVE");
        t.assert(appScreenAccept(screens::SCREEN_QUESTS, arow(screens::ACTION_TAKE_QUEST, screens::COND_QUEST)), APP_NAV_NONE, "take is a save action");
        t.assert(appScreenAccept(screens::SCREEN_SMITH, arow(screens::ACTION_BUY_UPGRADE, screens::COND_UPGRADE, 1)), APP_NAV_NONE, "buy is a save action");
        suite.addTest(t);
    }

    {
        Test t("B backs out one level: hub -> menu, quests/smith -> hub");
        t.assert(appScreenBack(screens::SCREEN_HUB), APP_NAV_MENU, "hub backs to menu");
        t.assert(appScreenBack(screens::SCREEN_QUESTS), APP_NAV_HUB, "quests backs to hub");
        t.assert(appScreenBack(screens::SCREEN_SMITH), APP_NAV_HUB, "smith backs to hub");
        suite.addTest(t);
    }

    {
        Test t("apply HUNT starts the picked loadout and reports the transition");
        MenuState menu;
        menu.weapon = W_FLAIL;
        menu.target = MON_HEAVY;
        ScreenState screen;
        screenReset(screen, screens::SCREEN_HUB, screens::SCREEN_HUB_ROWS);
        Game g;
        SaveBlock save;
        saveDefaults(save);
        t.assert(appNavApply(APP_NAV_HUNT, menu, screen, save, g, AT_A), true, "hunt started");
        t.assert(g.weapon, W_FLAIL, "started flail");
        t.assert(g.monsterKind, MON_HEAVY, "started heavy beast");
        t.assert(screen.active, false, "screens closed while hunting");
        suite.addTest(t);
    }

    {
        Test t("apply MENU re-opens the menu and clears the held-dpad state");
        MenuState menu;
        ScreenState screen;
        screenReset(screen, screens::SCREEN_HUB, screens::SCREEN_HUB_ROWS);
        Game g;
        SaveBlock save;
        saveDefaults(save);
        menu.weapon = 2;
        menu.navX = 1;
        menu.navXTimer = 4;
        t.assert(appNavApply(appScreenBack(screens::SCREEN_HUB), menu, screen, save, g, AT_B), false, "no hunt");
        t.assert(menu.active, true, "menu active");
        t.assert(screen.active, false, "hub closed");
        t.assert(menu.navX, 0, "nav direction reset");
        t.assert(menu.navXTimer, 0, "nav timer reset");
        t.assert(menu.weapon, 2, "pick kept");
        suite.addTest(t);
    }

    {
        Test t("hunt-end commit writes progress once per hunt, then latches");
        SaveBlock save;
        saveDefaults(save);
        save.activeQuest = 2;
        bool latched = false;
        t.assert(appHuntCommit(true, latched, save, 37), true, "first over tick commits");
        t.assert(save.progress, 37, "progress written");
        t.assert(appHuntCommit(true, latched, save, 37), false, "later over ticks silent");
        t.assert(appHuntCommit(true, latched, save, 37), false, "still silent");
        t.assert(appHuntCommit(false, latched, save, 37), false, "over cleared: no commit");
        t.assert(latched, false, "latch cleared with the hunt");
        t.assert(appHuntCommit(true, latched, save, 41), true, "next hunt commits again");
        t.assert(save.progress, 41, "new progress written");
        suite.addTest(t);
    }

    {
        Test t("hunt-end commit with no active quest arms the latch without writing");
        SaveBlock save;
        saveDefaults(save);
        save.progress = 9;
        bool latched = false;
        t.assert(appHuntCommit(true, latched, save, 5), false, "nothing to persist");
        t.assert(latched, true, "latch armed");
        t.assert(save.progress, 9, "progress untouched");
        t.assert(appHuntCommit(true, latched, save, 5), false, "no second attempt");
        t.assert(appHuntCommit(false, latched, save, 5), false, "clear");
        suite.addTest(t);
    }

    {
        Test t("round trip keeps the loadout picks and ends back on the hub");
        MenuState menu;
        menu.weapon = W_GUN;
        menu.target = MON_SWEEP;
        ScreenState screen;
        Game g;
        SaveBlock save;
        saveDefaults(save);
        appNavApply(appMenuAccept(), menu, screen, save, g, AT_A);
        appNavApply(APP_NAV_SMITH, menu, screen, save, g, AT_A);
        t.assert(screen.screen, screens::SCREEN_SMITH, "on smith");
        appNavApply(appScreenBack(screen.screen), menu, screen, save, g, AT_B);
        t.assert(screen.screen, screens::SCREEN_HUB, "back on hub");
        appNavApply(appScreenAccept(screen.screen, arow(screens::ACTION_HUNT)), menu, screen, save, g, AT_A);
        t.assert(g.weapon, W_GUN, "picks survived to the hunt");
        t.assert(g.monsterKind, MON_SWEEP, "target survived to the hunt");
        appNavApply(appHuntReturn(), menu, screen, save, g, AT_A);
        t.assert(screen.screen, screens::SCREEN_HUB, "hunt end returns to hub");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
