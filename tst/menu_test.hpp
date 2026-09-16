#pragma once
// Host unit tests for src/menu_state.hpp — opening-menu nav wrap, A-edge START
// exactly once, pick -> mode/kind mapping and the post-game return edge. The
// menu is device glue (mock/game.js has no menu); the sim mapping it applies is
// core newGame(), which world_test/monster_test already pin separately.
#include "test.hpp"
#include "../src/menu_state.hpp"

using namespace mh;

namespace menutest {

const Input MT_IDLE = Input{0, 0, false, false};
const Input MT_LEFT = Input{-1, 0, false, false};
const Input MT_RIGHT = Input{1, 0, false, false};
const Input MT_UP = Input{0, -1, false, false};
const Input MT_DOWN = Input{0, 1, false, false};
const Input MT_A = Input{0, 0, true, false};

}   // namespace menutest

using namespace menutest;

void MenuSuite(TestRunner &runner) {
    TestSuite suite("Opening menu: nav, start edge, pick mapping (src/menu_state.hpp)");

    {
        Test t("boot defaults: SWD/LUNGE, active, idle tick is silent");
        MenuState m;
        t.assert(m.weapon, 0, "default weapon SWD");
        t.assert(m.target, 0, "default target LUNGE");
        t.assert(m.active, true, "boot into menu");
        t.assert(menuStep(m, MT_IDLE), MENU_NONE, "idle tick: no action");
        t.assert(m.prevA, false, "idle tick leaves edge flags clear");
        suite.addTest(t);
    }

    {
        Test t("LEFT/RIGHT cycle weapon 0..2 and wrap both ways");
        MenuState m;
        menuStep(m, MT_RIGHT);
        t.assert(m.weapon, 1, "right: SWD -> FLS");
        menuStep(m, MT_RIGHT);
        t.assert(m.weapon, 2, "right: FLS -> GUN");
        menuStep(m, MT_RIGHT);
        t.assert(m.weapon, 0, "right wraps GUN -> SWD");
        menuStep(m, MT_LEFT);
        t.assert(m.weapon, 2, "left wraps SWD -> GUN");
        menuStep(m, MT_LEFT);
        t.assert(m.weapon, 1, "left: GUN -> FLS");
        t.assert(m.target, 0, "weapon nav leaves target alone");
        suite.addTest(t);
    }

    {
        Test t("UP/DOWN cycle target 0..3 (beasts then pole) and wrap both ways");
        MenuState m;
        menuStep(m, MT_DOWN);
        t.assert(m.target, 1, "down: LUNGE -> SWEEP");
        menuStep(m, MT_DOWN);
        t.assert(m.target, 2, "down: SWEEP -> HEAVY");
        menuStep(m, MT_DOWN);
        t.assert(m.target, 3, "down: HEAVY -> POLE");
        menuStep(m, MT_DOWN);
        t.assert(m.target, 0, "down wraps POLE -> LUNGE");
        menuStep(m, MT_UP);
        t.assert(m.target, 3, "up wraps LUNGE -> POLE");
        menuStep(m, MT_UP);
        t.assert(m.target, 2, "up: POLE -> HEAVY");
        t.assert(m.weapon, 0, "target nav leaves weapon alone");
        suite.addTest(t);
    }

    {
        Test t("A rising edge fires START once per press, hold stays silent");
        MenuState m;
        t.assert(menuStep(m, MT_A), MENU_START, "press tick: START");
        for (int i = 0; i < 10; i++)
            t.assert(menuStep(m, MT_A), MENU_NONE, "held A: no repeat");
        t.assert(menuStep(m, MT_IDLE), MENU_NONE, "release: no action");
        t.assert(menuStep(m, MT_A), MENU_START, "re-press fires again");
        suite.addTest(t);
    }

    {
        Test t("same-tick nav + A applies the nav and still fires START");
        MenuState m;
        const Input rightA = Input{1, 0, true, false};
        t.assert(menuStep(m, rightA), MENU_START, "diagonal press starts");
        t.assert(m.weapon, 1, "diagonal press nav applied");
        suite.addTest(t);
    }

    {
        Test t("pick -> mode/kind mapping (targets 0..2 hunt, 3 pole)");
        MenuState m;
        for (int8_t target = 0; target < 4; target++) {
            m.target = target;
            t.assert(menuMode(m), target == 3 ? MODE_TRAIN : MODE_HUNT, "mode by target");
            t.assert(menuMonsterKind(m), target <= 2 ? target : 0, "kind by target");
        }
        suite.addTest(t);
    }

    {
        Test t("menuStart applies weapon + mode + kind to Game");
        for (int8_t weapon = 0; weapon < 3; weapon++) {
            for (int8_t target = 0; target < 4; target++) {
                MenuState m;
                m.weapon = weapon;
                m.target = target;
                Game g;
                menuStart(g, m);
                t.assert(g.weapon, weapon, "start weapon");
                t.assert(g.mode, target == 3 ? MODE_TRAIN : MODE_HUNT, "start mode");
                t.assert(g.monsterKind, target <= 2 ? target : 0, "start kind");
            }
        }
        suite.addTest(t);
    }

    {
        Test t("return edge only after over, exactly once per press");
        MenuState m;
        t.assert(menuReturnStep(m, false, MT_A), false, "A edge pre-over: no return");
        t.assert(menuReturnStep(m, true, MT_A), false, "held A post-over: no new edge");
        t.assert(menuReturnStep(m, true, MT_IDLE), false, "release: no return");
        t.assert(menuReturnStep(m, true, MT_A), true, "A edge post-over: return");
        for (int i = 0; i < 5; i++)
            t.assert(menuReturnStep(m, true, MT_A), false, "held A: no repeat");
        suite.addTest(t);
    }

    {
        Test t("picks survive the return round trip; the return A does not restart");
        MenuState m;
        menuStep(m, MT_RIGHT);   // FLS
        menuStep(m, MT_DOWN);
        menuStep(m, MT_DOWN);   // HEAVY
        t.assert(menuStep(m, MT_A), MENU_START, "start picked loadout");
        Game g;
        menuStart(g, m);
        t.assert(g.weapon, W_FLAIL, "started flail");
        t.assert(g.monsterKind, MON_HEAVY, "started heavy beast");
        // Over screen: play-state ticks keep the menu flags current (idle), then
        // the player presses A to return.
        t.assert(menuReturnStep(m, true, MT_IDLE), false, "over tick without A");
        t.assert(menuReturnStep(m, true, MT_A), true, "over + A returns");
        t.assert(m.weapon, 1, "weapon pick kept");
        t.assert(m.target, 2, "target pick kept");
        t.assert(menuStep(m, MT_A), MENU_NONE, "held return A does not restart");
        t.assert(menuStep(m, MT_IDLE), MENU_NONE, "release after return");
        t.assert(menuStep(m, MT_A), MENU_START, "fresh A press starts again");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
