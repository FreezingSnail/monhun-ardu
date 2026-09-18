#pragma once
// Host unit tests for src/menu_state.hpp — opening-menu debounced nav (tap vs
// hold), A-edge START exactly once, pick -> mode/kind mapping and the post-game
// return edge. The menu is device glue (mock/game.js has no menu); the sim
// mapping it applies is core newGame(), which world_test/monster_test already
// pin separately.
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

// Tap helper: press then release, so every call is a fresh press (immediate
// step) rather than a continuation of a held direction.
inline void menuTap(MenuState &m, const Input &dir) {
    menuStep(m, dir);
    menuStep(m, MT_IDLE);
}

}   // namespace menutest

using namespace menutest;

void MenuSuite(TestRunner &runner) {
    TestSuite suite("Opening menu: debounced nav, start edge, pick mapping (src/menu_state.hpp)");

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
        Test t("taps cycle weapon 0..2 and wrap both ways (one step per press)");
        MenuState m;
        menuTap(m, MT_RIGHT);
        t.assert(m.weapon, 1, "right: SWD -> FLS");
        menuTap(m, MT_RIGHT);
        t.assert(m.weapon, 2, "right: FLS -> GUN");
        menuTap(m, MT_RIGHT);
        t.assert(m.weapon, 0, "right wraps GUN -> SWD");
        menuTap(m, MT_LEFT);
        t.assert(m.weapon, 2, "left wraps SWD -> GUN");
        menuTap(m, MT_LEFT);
        t.assert(m.weapon, 1, "left: GUN -> FLS");
        t.assert(m.target, 0, "weapon nav leaves target alone");
        suite.addTest(t);
    }

    {
        Test t("taps cycle target 0..4 (beasts, ravager, pole) and wrap both ways");
        MenuState m;
        menuTap(m, MT_DOWN);
        t.assert(m.target, 1, "down: LUNGE -> SWEEP");
        menuTap(m, MT_DOWN);
        t.assert(m.target, 2, "down: SWEEP -> HEAVY");
        menuTap(m, MT_DOWN);
        t.assert(m.target, 3, "down: HEAVY -> RAVAGER");
        menuTap(m, MT_DOWN);
        t.assert(m.target, 4, "down: RAVAGER -> POLE");
        menuTap(m, MT_DOWN);
        t.assert(m.target, 0, "down wraps POLE -> LUNGE");
        menuTap(m, MT_UP);
        t.assert(m.target, 4, "up wraps LUNGE -> POLE");
        menuTap(m, MT_UP);
        t.assert(m.target, 3, "up: POLE -> RAVAGER");
        t.assert(m.weapon, 0, "target nav leaves weapon alone");
        suite.addTest(t);
    }

    {
        Test t("hold waits MENU_NAV_DELAY then repeats every MENU_NAV_REPEAT");
        MenuState m;
        // Fresh press: immediate step, then DELAY-1 continuation ticks hold.
        menuStep(m, MT_RIGHT);
        t.assert(m.weapon, 1, "press steps immediately");
        for (uint8_t i = 0; i < MENU_NAV_DELAY - 1; i++) {
            menuStep(m, MT_RIGHT);
            t.assert(m.weapon, 1, "held before delay: no repeat");
        }
        menuStep(m, MT_RIGHT);
        t.assert(m.weapon, 2, "delay tick fires second step");
        // Then one step every REPEAT ticks.
        for (uint8_t i = 0; i < MENU_NAV_REPEAT - 1; i++) {
            menuStep(m, MT_RIGHT);
            t.assert(m.weapon, 2, "between repeats: no step");
        }
        menuStep(m, MT_RIGHT);
        t.assert(m.weapon, 0, "first repeat after REPEAT ticks");
        for (uint8_t i = 0; i < MENU_NAV_REPEAT - 1; i++)
            menuStep(m, MT_RIGHT);
        menuStep(m, MT_RIGHT);
        t.assert(m.weapon, 1, "second repeat after REPEAT ticks");
        // Release resets: the next press steps immediately, not on a stale timer.
        menuStep(m, MT_IDLE);
        t.assert(m.weapon, 1, "release keeps the pick");
        t.assert(m.navX, 0, "release clears last direction");
        t.assert(m.navXTimer, 0, "release clears hold timer");
        menuStep(m, MT_RIGHT);
        t.assert(m.weapon, 2, "fresh press after release steps immediately");
        suite.addTest(t);
    }

    {
        Test t("direction reversal steps immediately and re-arms the delay");
        MenuState m;
        menuStep(m, MT_RIGHT);   // immediate: FLS
        for (uint8_t i = 0; i < 5; i++)
            menuStep(m, MT_RIGHT);   // still held, well before DELAY
        menuStep(m, MT_LEFT);
        t.assert(m.weapon, 0, "reversal steps immediately");
        for (uint8_t i = 0; i < MENU_NAV_DELAY - 1; i++)
            menuStep(m, MT_LEFT);
        t.assert(m.weapon, 0, "reversed hold still waits DELAY");
        menuStep(m, MT_LEFT);
        t.assert(m.weapon, 2, "reversed hold repeats after DELAY");
        suite.addTest(t);
    }

    {
        Test t("axes debounce independently (held X repeats while Y taps)");
        MenuState m;
        menuStep(m, MT_RIGHT);   // x armed for DELAY, immediate step
        for (uint8_t i = 0; i < MENU_NAV_DELAY - 1; i++)
            menuStep(m, MT_RIGHT);
        const Input diagonal = Input{1, 1, false, false};
        menuStep(m, diagonal);   // x delay expires the same tick y taps
        t.assert(m.weapon, 2, "held x repeats on schedule");
        t.assert(m.target, 1, "fresh y tap steps same tick");
        for (uint8_t i = 0; i < MENU_NAV_DELAY - 1; i++)
            menuStep(m, diagonal);
        t.assert(m.target, 1, "y hold waits its own DELAY");
        menuStep(m, diagonal);
        t.assert(m.target, 2, "y repeats after its DELAY");
        t.assert(m.navX > 0 && m.navY > 0, true, "both axes still armed");
        suite.addTest(t);
    }

    {
        Test t("A rising edge fires START once per press, hold stays silent");
        MenuState m;
        t.assert(menuStep(m, MT_A), MENU_ACCEPT, "press tick: START");
        for (int i = 0; i < 10; i++)
            t.assert(menuStep(m, MT_A), MENU_NONE, "held A: no repeat");
        t.assert(menuStep(m, MT_IDLE), MENU_NONE, "release: no action");
        t.assert(menuStep(m, MT_A), MENU_ACCEPT, "re-press fires again");
        suite.addTest(t);
    }

    {
        Test t("same-tick nav + A applies the nav and still fires START");
        MenuState m;
        const Input rightA = Input{1, 0, true, false};
        t.assert(menuStep(m, rightA), MENU_ACCEPT, "diagonal press starts");
        t.assert(m.weapon, 1, "diagonal press nav applied");
        suite.addTest(t);
    }

    {
        Test t("pick -> mode/kind mapping (targets 0..3 hunt, 4 pole)");
        MenuState m;
        for (int8_t target = 0; target < 5; target++) {
            m.target = target;
            t.assert(menuMode(m), target == 4 ? MODE_TRAIN : MODE_HUNT, "mode by target");
            t.assert(menuMonsterKind(m), target < 4 ? target : 0, "kind by target");
        }
        suite.addTest(t);
    }

    {
        Test t("menuStart applies weapon + mode + kind to Game");
        for (int8_t weapon = 0; weapon < 3; weapon++) {
            for (int8_t target = 0; target < 5; target++) {
                MenuState m;
                m.weapon = weapon;
                m.target = target;
                Game g;
                menuStart(g, m);
                t.assert(g.weapon, weapon, "start weapon");
                t.assert(g.mode, target == 4 ? MODE_TRAIN : MODE_HUNT, "start mode");
                t.assert(g.monsterKind, target < 4 ? target : 0, "start kind");
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
        menuTap(m, MT_RIGHT);   // FLS
        menuTap(m, MT_DOWN);
        menuTap(m, MT_DOWN);   // HEAVY
        t.assert(menuStep(m, MT_A), MENU_ACCEPT, "start picked loadout");
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
        t.assert(menuStep(m, MT_A), MENU_ACCEPT, "fresh A press starts again");
        suite.addTest(t);
    }

    {
        Test t("return-to-menu resets nav timers (held dpad cannot skip)");
        MenuState m;
        // Hold RIGHT through the sim and the over screen: ax timer is mid-hold.
        menuStep(m, MT_RIGHT);
        for (uint8_t i = 0; i < MENU_NAV_DELAY - 1; i++)
            menuStep(m, MT_RIGHT);
        t.assert(m.navXTimer, 1, "x timer nearly expired before the return");
        t.assert(menuReturnStep(m, true, MT_A), true, "over + A returns");
        t.assert(m.navX, 0, "return clears last direction");
        t.assert(m.navXTimer, 0, "return clears hold timer");
        // Re-entry with RIGHT still held: exactly one immediate step, then the
        // fresh DELAY applies. Without the reset the old timer would have fired
        // a repeat on the very first menu tick.
        menuStep(m, MT_RIGHT);
        t.assert(m.weapon, 2, "re-entry with held dpad steps once");
        for (uint8_t i = 0; i < MENU_NAV_DELAY - 1; i++) {
            menuStep(m, MT_RIGHT);
            t.assert(m.weapon, 2, "fresh hold waits DELAY after re-entry");
        }
        menuStep(m, MT_RIGHT);
        t.assert(m.weapon, 0, "fresh hold repeats after DELAY");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
