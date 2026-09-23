#pragma once
// Host unit tests for the app-level routing (bead monhun-ardu-mgn qs.4; hub as
// the root screen monhun-ardu-isp.1, which deleted the opening menu):
// src/app_state.hpp. Pins the shipped loop (boot -> hub -> hunt -> hub) plus the
// hub graph (hub/quests/smith), the held-button guards, the hunt-end return
// edge (appOverReturnStep) and the once-per-hunt progress commit. The device E2E
// counterpart (tst/fxdatatest/hub_test.hpp) drives the same app_state.hpp
// functions against the real cart + EEPROM and covers huntStart().
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
    r.unlock = 0;
    r.recipe[0].item = 0;
    r.recipe[0].count = 0;
    r.recipe[1].item = 0;
    r.recipe[1].count = 0;
    return r;
}

}   // namespace apptest

using namespace apptest;

void AppSuite(TestRunner &runner) {
    TestSuite suite("Boot-flow routing: hub/screens/hunt (src/app_state.hpp, isp.1)");

    {
        Test t("hub HUNT requests the hunt and closes the screen");
        ScreenState screen;
        screenReset(screen, screens::SCREEN_HUB, screens::SCREEN_HUB_ROWS);
        Game g;
        SaveBlock save;
        saveDefaults(save);
        t.assert(appScreenAccept(screens::SCREEN_HUB, arow(screens::ACTION_HUNT)), APP_NAV_HUNT, "HUNT row routes to hunt");
        t.assert(appNavApply(APP_NAV_HUNT, screen, save, g, AT_A), true, "HUNT reports the hunt request");
        t.assert(screen.active, false, "screen closed for the hunt");
        suite.addTest(t);
    }

    {
        Test t("hub A routing: HUNT / QUESTS / SMITH; LEAVE and status rows are no-ops");
        t.assert(appScreenAccept(screens::SCREEN_HUB, arow(screens::ACTION_HUNT)), APP_NAV_HUNT, "HUNT row");
        t.assert(appScreenAccept(screens::SCREEN_HUB, arow(screens::ACTION_OPEN_QUESTS)), APP_NAV_QUESTS, "QUESTS row");
        t.assert(appScreenAccept(screens::SCREEN_HUB, arow(screens::ACTION_OPEN_SMITH)), APP_NAV_SMITH, "SMITH row");
        t.assert(appScreenAccept(screens::SCREEN_HUB, arow(screens::ACTION_OPEN_GEAR)), APP_NAV_GEAR, "GEAR row");
        t.assert(appScreenAccept(screens::SCREEN_HUB, arow(screens::ACTION_LEAVE)), APP_NAV_NONE, "LEAVE row is root no-op");
        t.assert(appScreenAccept(screens::SCREEN_HUB, arow(screens::ACTION_NONE, screens::COND_ALWAYS)), APP_NAV_NONE, "status row");
        t.assert(appScreenAccept(screens::SCREEN_HUB, arow(screens::ACTION_TAKE_QUEST, screens::COND_QUEST)), APP_NAV_NONE, "quest row not a hub dest");
        suite.addTest(t);
    }

    {
        Test t("sub-screen A: LEAVE returns to the hub, save actions stay with the caller");
        t.assert(appScreenAccept(screens::SCREEN_QUESTS, arow(screens::ACTION_LEAVE)), APP_NAV_HUB, "quests LEAVE");
        t.assert(appScreenAccept(screens::SCREEN_SMITH, arow(screens::ACTION_LEAVE)), APP_NAV_HUB, "smith LEAVE");
        t.assert(appScreenAccept(screens::SCREEN_GEAR, arow(screens::ACTION_LEAVE)), APP_NAV_HUB, "gear LEAVE");
        t.assert(appScreenAccept(screens::SCREEN_QUESTS, arow(screens::ACTION_TAKE_QUEST, screens::COND_QUEST)), APP_NAV_NONE, "take is a save action");
        t.assert(appScreenAccept(screens::SCREEN_SMITH, arow(screens::ACTION_CRAFT_ARMOR, screens::COND_ARMOR, 0)), APP_NAV_NONE, "craft is a save action");
        t.assert(appScreenAccept(screens::SCREEN_GEAR, arow(screens::ACTION_EQUIP_WEAPON, screens::COND_ALWAYS, 1)), APP_NAV_NONE, "equip is a save action");
        suite.addTest(t);
    }

    {
        Test t("hub GEAR row opens the gear screen; B backs out; LEAVE returns");
        ScreenState screen;
        Game g;
        SaveBlock save;
        saveDefaults(save);
        t.assert(appNavApply(APP_NAV_GEAR, screen, save, g, AT_A), false, "gear nav is not a hunt start");
        t.assert(screen.active, true, "gear screen active");
        t.assert(screen.screen, screens::SCREEN_GEAR, "on the gear screen");
        t.assert(screen.rowCount, screens::SCREEN_GEAR_ROWS, "gear row count from the meta");
        t.assert(appScreenBack(screens::SCREEN_GEAR), APP_NAV_HUB, "gear B backs to the hub");
        appNavApply(appScreenBack(screen.screen), screen, save, g, AT_B);
        t.assert(screen.screen, screens::SCREEN_HUB, "back on the hub");
        suite.addTest(t);
    }

    {
        Test t("B backs out one level: hub is the root (none), quests/smith -> hub");
        t.assert(appScreenBack(screens::SCREEN_HUB), APP_NAV_NONE, "hub B is a root no-op");
        t.assert(appScreenBack(screens::SCREEN_QUESTS), APP_NAV_HUB, "quests backs to hub");
        t.assert(appScreenBack(screens::SCREEN_SMITH), APP_NAV_HUB, "smith backs to hub");
        suite.addTest(t);
    }

    {
        Test t("camp hold-B request is consumed once and routes to the hub");
        Game g;
        g.menuRequest = false;
        t.assert(appHubRequest(g), APP_NAV_NONE, "no request -> none");
        g.menuRequest = true;
        t.assert(appHubRequest(g), APP_NAV_HUB, "request -> hub");
        t.assert(g.menuRequest, false, "request consumed");
        t.assert(appHubRequest(g), APP_NAV_NONE, "no re-fire while held");
        suite.addTest(t);
    }

    {
        Test t("camp smithy request opens the smith and closes back into the camp (prg.7)");
        Game g;
        g.smithyRequest = false;
        t.assert(appSmithyRequest(g), APP_NAV_NONE, "no request -> none");
        g.smithyRequest = true;
        t.assert(appSmithyRequest(g), APP_NAV_SMITH, "request -> smith");
        t.assert(g.smithyRequest, false, "request consumed");
        t.assert(appSmithyRequest(g), APP_NAV_NONE, "no re-fire while held");

        ScreenState screen;
        SaveBlock save{};
        screenReset(screen, screens::SCREEN_SMITH, screens::SCREEN_SMITH_ROWS);
        const bool hunted = appNavApply(APP_NAV_CAMP, screen, save, g, AT_IDLE);
        t.assert(hunted, false, "camp nav is not a hunt start");
        t.assert(screen.active, false, "smith screen closed");
        suite.addTest(t);
    }

    {
        Test t("held A at a screen transition cannot re-fire inside it");
        ScreenState screen;
        Game g;
        SaveBlock save;
        saveDefaults(save);
        appNavApply(APP_NAV_HUB, screen, save, g, AT_A);   // A still down
        t.assert(screenStep(screen, AT_A), SCREEN_NONE, "held A silent on entry tick");
        t.assert(screenStep(screen, AT_IDLE), SCREEN_NONE, "release silent");
        t.assert(screenStep(screen, AT_A), SCREEN_ACCEPT, "fresh A accepts");
        suite.addTest(t);
    }

    {
        Test t("held B at the back transition cannot re-fire inside the destination");
        ScreenState screen;
        Game g;
        SaveBlock save;
        saveDefaults(save);
        appNavApply(APP_NAV_HUB, screen, save, g, AT_A);
        appNavApply(APP_NAV_QUESTS, screen, save, g, AT_A);
        appNavApply(appScreenBack(screen.screen), screen, save, g, AT_B);   // B still down
        t.assert(screen.screen, screens::SCREEN_HUB, "back landed on hub");
        t.assert(screenStep(screen, AT_B), SCREEN_NONE, "held B silent on entry tick");
        t.assert(screenStep(screen, AT_IDLE), SCREEN_NONE, "release silent");
        t.assert(screenStep(screen, AT_B), SCREEN_BACK, "fresh B backs out");
        suite.addTest(t);
    }

    {
        Test t("appOverReturnStep: A rising edge only while over, once per press");
        bool prevA = false;
        t.assert(appOverReturnStep(false, AT_A, prevA), false, "A edge pre-over: no return");
        t.assert(appOverReturnStep(true, AT_A, prevA), false, "held A post-over: no new edge");
        t.assert(appOverReturnStep(true, AT_IDLE, prevA), false, "release: no return");
        t.assert(appOverReturnStep(true, AT_A, prevA), true, "A edge post-over: return");
        for (uint8_t i = 0; i < 3; i++)
            t.assert(appOverReturnStep(true, AT_A, prevA), false, "held A: no repeat");
        t.assert(appOverReturnStep(true, AT_IDLE, prevA), false, "release again");
        t.assert(appOverReturnStep(true, AT_A, prevA), true, "fresh A returns again");
        suite.addTest(t);
    }

    {
        Test t("hub graph: quests/smith round trip, then a fresh HUNT request");
        ScreenState screen;
        Game g;
        SaveBlock save;
        saveDefaults(save);
        appNavApply(APP_NAV_HUB, screen, save, g, AT_A);
        t.assert(screen.screen, screens::SCREEN_HUB, "on the hub");
        appNavApply(APP_NAV_SMITH, screen, save, g, AT_A);
        t.assert(screen.screen, screens::SCREEN_SMITH, "on smith");
        appNavApply(appScreenBack(screen.screen), screen, save, g, AT_B);
        t.assert(screen.screen, screens::SCREEN_HUB, "back on hub");
        t.assert(appNavApply(appScreenAccept(screen.screen, arow(screens::ACTION_HUNT)), screen, save, g, AT_A), true, "hunt requested from the hub");
        t.assert(screen.active, false, "hub closed for the hunt");
        suite.addTest(t);
    }

    {
        Test t("hunt end routes to the hub; hub B is a root no-op");
        ScreenState screen;
        Game g;
        SaveBlock save;
        saveDefaults(save);
        t.assert(appHuntReturn(), APP_NAV_HUB, "hunt end routes to the hub");
        t.assert(appNavApply(appHuntReturn(), screen, save, g, AT_B), false, "return is not a hunt start");
        t.assert(screen.active, true, "hub active after the hunt");
        t.assert(screen.screen, screens::SCREEN_HUB, "return lands on the hub");
        t.assert(appNavApply(appScreenBack(screens::SCREEN_HUB), screen, save, g, AT_B), false, "hub B is a no-op");
        t.assert(screen.active, true, "hub stays active");
        t.assert(screen.screen, screens::SCREEN_HUB, "still on the hub");
        suite.addTest(t);
    }

    {
        Test t("hunt-end commit writes progress once per hunt, then latches");
        SaveBlock save;
        saveDefaults(save);
        save.activeQuest = 2;
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        g.questProgress = 37;
        bool latched = false;
        t.assert(appHuntCommit(true, latched, save, g), true, "first over tick commits");
        t.assert(save.progress, 37, "progress written");
        t.assert(appHuntCommit(true, latched, save, g), false, "later over ticks silent");
        t.assert(appHuntCommit(true, latched, save, g), false, "still silent");
        t.assert(appHuntCommit(false, latched, save, g), false, "over cleared: no commit");
        t.assert(latched, false, "latch cleared with the hunt");
        g.questProgress = 41;
        t.assert(appHuntCommit(true, latched, save, g), true, "next hunt commits again");
        t.assert(save.progress, 41, "new progress written");
        suite.addTest(t);
    }

    {
        Test t("hunt-end commit folds the hunt inventory (max of live vs saved)");
        SaveBlock save;
        saveDefaults(save);
        save.items[ITEM_HERB] = 5;   // pantry stock
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);   // items reset to 0
        bool latched = false;
        g.items[ITEM_HERB] = 2;    // used 3 herbs: live lower, must not erase stock
        g.items[ITEM_SCALE] = 4;   // carved scales: new, folds in
        g.items[ITEM_ORE] = 6;     // gathered ore: new, folds in
        t.assert(appHuntCommit(true, latched, save, g), true, "item gains commit");
        t.assert(save.items[ITEM_HERB], 5, "consumed herb keeps the pantry stock");
        t.assert(save.items[ITEM_SCALE], 4, "carved scales persisted");
        t.assert(save.items[ITEM_ORE], 6, "gathered ore persisted");
        suite.addTest(t);
    }

    {
        Test t("hunt-end commit with no active quest and no gains arms the latch without writing");
        SaveBlock save;
        saveDefaults(save);
        save.progress = 9;
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        g.questProgress = 5;
        bool latched = false;
        t.assert(appHuntCommit(true, latched, save, g), false, "nothing to persist");
        t.assert(latched, true, "latch armed");
        t.assert(save.progress, 9, "progress untouched");
        t.assert(appHuntCommit(true, latched, save, g), false, "no second attempt");
        t.assert(appHuntCommit(false, latched, save, g), false, "clear");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
