#pragma once
// Host unit tests for the app-level routing (bead monhun-ardu-mgn qs.4, demo
// flow rework monhun-ardu-5r1): src/app_state.hpp. Pins the shipped demo loop
// (menu -> hunt -> menu, fresh newGame per hunt) plus the shelf hub graph
// (menu/hub/quests/smith) that stays compiled and tested but is off the demo
// path, the held-button guards, and the once-per-hunt progress commit. The
// device E2E counterpart (tst/fxdatatest/hub_test.hpp) drives the same
// app_state.hpp functions against the real cart + EEPROM.
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
        Test t("menu A launches the picked hunt directly, no hub (demo flow)");
        MenuState menu;
        menu.weapon = W_FLAIL;
        menu.target = MON_HEAVY;
        ScreenState screen;
        Game g;
        SaveBlock save;
        saveDefaults(save);
        t.assert(appMenuAccept(), APP_NAV_HUNT, "menu A routes to hunt");
        t.assert(menuStep(menu, AT_A), MENU_ACCEPT, "A edge accepts");
        t.assert(appNavApply(appMenuAccept(), menu, screen, save, g, AT_A), true, "hunt started");
        t.assert(g.weapon, W_FLAIL, "started flail");
        t.assert(g.monsterKind, MON_HEAVY, "started heavy beast");
        t.assert(g.roomId, zone::ROOM_CAMP, "menu A starts in the camp");
        t.assert(g.roomMonsterKind, zone::MONSTER_NONE, "camp room is safe");
        t.assert(roomIsSafe(g), true, "camp reads safe");
        t.assert(menu.active, false, "menu closed while hunting");
        t.assert(screen.active, false, "no hub on the demo path");
        t.assert(menu.weapon, W_FLAIL, "weapon pick kept");
        t.assert(menu.target, MON_HEAVY, "target pick kept");
        suite.addTest(t);
    }

    {
        Test t("menu A with a pole pick starts the pole room in train mode");
        MenuState menu;
        menu.weapon = W_GUN;
        menu.target = MENU_POLE_TARGET + POLE_CRACK;
        ScreenState screen;
        Game g;
        SaveBlock save;
        saveDefaults(save);
        t.assert(appNavApply(appMenuAccept(), menu, screen, save, g, AT_A), true, "train started");
        t.assert(g.mode, MODE_TRAIN, "train mode");
        t.assert(g.roomId, zone::ROOM_POLE_ROOM, "starts in the pole room");
        t.assert(g.roomMonsterKind, zone::MONSTER_NONE, "pole room is safe");
        t.assert(g.pole.kind, POLE_CRACK, "variant installed after the room load");
        t.assert(g.target.alive, true, "pole target armed");
        suite.addTest(t);
    }

    {
        Test t("menu request is consumed once and routes to the menu");
        Game g;
        g.menuRequest = false;
        t.assert(appMenuRequest(g), APP_NAV_NONE, "no request -> none");
        g.menuRequest = true;
        t.assert(appMenuRequest(g), APP_NAV_MENU, "request -> menu");
        t.assert(g.menuRequest, false, "request consumed");
        t.assert(appMenuRequest(g), APP_NAV_NONE, "no re-fire while held");
        suite.addTest(t);
    }

    {
        Test t("held A at a screen transition cannot re-fire inside it (shelf)");
        MenuState menu;
        menu.active = false;   // as after the demo menu A
        ScreenState screen;
        Game g;
        SaveBlock save;
        saveDefaults(save);
        appNavApply(APP_NAV_HUB, menu, screen, save, g, AT_A);   // A still down
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
        appNavApply(APP_NAV_HUB, menu, screen, save, g, AT_A);
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
        t.assert(menu.active, false, "menu closed while hunting");
        suite.addTest(t);
    }

    {
        Test t("hunt end / hub B re-open the menu and clear the held-dpad state");
        MenuState menu;
        ScreenState screen;
        screenReset(screen, screens::SCREEN_HUB, screens::SCREEN_HUB_ROWS);
        Game g;
        SaveBlock save;
        saveDefaults(save);
        menu.weapon = 2;
        menu.navX = 1;
        menu.navXTimer = 4;
        t.assert(appHuntReturn(), APP_NAV_MENU, "hunt end routes to the menu");
        t.assert(appScreenBack(screens::SCREEN_HUB), APP_NAV_MENU, "hub B routes to the menu (shelf)");
        t.assert(appNavApply(appHuntReturn(), menu, screen, save, g, AT_B), false, "return is not a hunt start");
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
        Test t("demo round trip: menu -> hunt -> death -> menu, picks kept, fresh hunt resets");
        MenuState menu;
        menu.weapon = W_GUN;
        menu.target = MON_SWEEP;
        ScreenState screen;
        Game g;
        SaveBlock save;
        saveDefaults(save);
        appNavApply(appMenuAccept(), menu, screen, save, g, AT_A);
        t.assert(g.weapon, W_GUN, "picks survived to the hunt");
        t.assert(g.monsterKind, MON_SWEEP, "target survived to the hunt");
        t.assert(menu.active, false, "no menu while hunting");
        t.assert(screen.active, false, "no hub on the demo path");
        g.projN = 3;
        g.fxN = 2;
        g.tick = 77;
        g.questProgress = 5;
        g.over = OVER_LOSE;
        t.assert(menuReturnStep(menu, true, AT_IDLE), false, "over tick without A");
        t.assert(menuReturnStep(menu, true, AT_A), true, "death + A returns");
        t.assert(appNavApply(appHuntReturn(), menu, screen, save, g, AT_A), false, "return is not a hunt start");
        t.assert(menu.active, true, "death returns to the menu");
        t.assert(screen.active, false, "no hub on the return");
        t.assert(menu.weapon, W_GUN, "weapon pick kept across the death");
        t.assert(menu.target, MON_SWEEP, "target pick kept across the death");
        appNavApply(appMenuAccept(), menu, screen, save, g, AT_A);   // fresh hunt
        t.assert(g.tick, 0, "fresh tick");
        t.assert(g.projN, 0, "projectiles cleared");
        t.assert(g.fxN, 0, "effects cleared");
        t.assert(g.questProgress, 0, "quest progress cleared");
        t.assert(g.over, OVER_NONE, "over cleared");
        t.assert(g.weapon, W_GUN, "fresh hunt keeps the weapon pick");
        t.assert(g.monsterKind, MON_SWEEP, "fresh hunt keeps the target pick");
        t.assert(g.roomId, zone::ROOM_CAMP, "fresh hunt restarts in the camp");
        suite.addTest(t);
    }

    {
        Test t("shelf hub graph: smith round trip still starts the picked hunt");
        MenuState menu;
        menu.weapon = W_GUN;
        menu.target = MON_SWEEP;
        ScreenState screen;
        Game g;
        SaveBlock save;
        saveDefaults(save);
        appNavApply(APP_NAV_HUB, menu, screen, save, g, AT_A);
        appNavApply(APP_NAV_SMITH, menu, screen, save, g, AT_A);
        t.assert(screen.screen, screens::SCREEN_SMITH, "on smith");
        appNavApply(appScreenBack(screen.screen), menu, screen, save, g, AT_B);
        t.assert(screen.screen, screens::SCREEN_HUB, "back on hub");
        t.assert(appNavApply(appScreenAccept(screen.screen, arow(screens::ACTION_HUNT)), menu, screen, save, g, AT_A), true, "hunt started");
        t.assert(g.weapon, W_GUN, "picks survived the shelf detour");
        t.assert(g.monsterKind, MON_SWEEP, "target survived the shelf detour");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
