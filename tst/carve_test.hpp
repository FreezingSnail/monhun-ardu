#pragma once
// Host unit tests for the carcass carve (bead monhun-ardu-prg.3):
// src/core/carve.hpp (stepCarve/applyCarve, the PS_CARVE window and the packed
// drop table) and the over-screen return gate in src/app_state.hpp
// (appHuntReturnAllowed). The generated combat_expect carve pins are the source
// of truth for the table values; tests reference the symbolic item ids, never a
// literal record index.
#include "test.hpp"
#include "../src/core/world.hpp"   // stepGame + carve.hpp + combat read layer
#include "../src/app_state.hpp"
#include "../src/generated/combat_expect.hpp"

using namespace mh;

namespace carvetest {

const Input C_IDLE = Input{0, 0, false, false};
const Input C_A = Input{0, 0, true, false};
const Input C_RIGHT = Input{1, 0, false, false};

// Win the hunt: newGame spawns the lunge beast, then we drop it where the
// hunter stands so the carcass rect is under the hunter. over=OVER_WIN is what
// the damage path sets on the killing blow (monster.hpp damageMonster).
void killLunge(Game &g, int16_t x = 200, int16_t y = 40) {
    newGame(g, W_SWORD, MODE_HUNT, MON_LUNGE);
    g.monster.x = x;
    g.monster.y = y;
    g.monster.state = MS_DEAD;
    g.over = OVER_WIN;
    g.player.x = x;
    g.player.y = y;
}

// Press A: true when the press started PS_CARVE.
bool startCarve(Game &g) {
    stepGame(g, C_A);
    return g.player.state == PS_CARVE;
}

// Run the rooted window out (idle input); leaves the state at PS_IDLE.
void finishCarve(Game &g) {
    for (int i = 0; i < CARVE_TICKS + 4 && g.player.state == PS_CARVE; i++)
        stepGame(g, C_IDLE);
}

}   // namespace carvetest

using namespace carvetest;

void CarveSuite(TestRunner &runner) {
    TestSuite suite("Carcass carve: PS_CARVE, drop table, over-screen return gate (prg.3)");

    {
        Test t("shipped carve tables decode: 4 slots, authored entries, inert empty slots");
        // lunge: scale 100%, shell 45% (2 entries, 2 padded zeros).
        CombatCarve c = combatCarveRead(combat::CREATURE_LUNGE, 0);
        t.assert(c.item, ITEM_SCALE, "lunge carve0 item scale");
        t.assert(c.count, combat_expect::CREATURE_LUNGE_CARVE0_COUNT, "lunge carve0 count pin");
        t.assert(c.chance, combat_expect::CREATURE_LUNGE_CARVE0_CHANCE, "lunge carve0 chance pin");
        c = combatCarveRead(combat::CREATURE_LUNGE, 1);
        t.assert(c.item, ITEM_SHELL, "lunge carve1 item shell");
        t.assert(c.chance, combat_expect::CREATURE_LUNGE_CARVE1_CHANCE, "lunge carve1 chance pin");
        t.assert(combatCarveRead(combat::CREATURE_LUNGE, 2).count, 0, "lunge carve2 padded empty");
        t.assert(combatCarveRead(combat::CREATURE_LUNGE, 3).count, 0, "lunge carve3 padded empty");
        // sweep: shell / fang.
        t.assert(combatCarveRead(combat::CREATURE_SWEEP, 0).item, ITEM_SHELL, "sweep carve0 shell");
        t.assert(combatCarveRead(combat::CREATURE_SWEEP, 1).item, ITEM_FANG, "sweep carve1 fang");
        // heavy: tail / scale / shell (3 entries).
        t.assert(combatCarveRead(combat::CREATURE_HEAVY, 0).item, ITEM_TAIL, "heavy carve0 tail");
        t.assert(combatCarveRead(combat::CREATURE_HEAVY, 2).chance, combat_expect::CREATURE_HEAVY_CARVE2_CHANCE, "heavy carve2 chance pin");
        // ravager: fang / tail / scale x2 (the count-2 jackpot).
        t.assert(combatCarveRead(combat::CREATURE_RAVAGER, 0).item, ITEM_FANG, "ravager carve0 fang");
        t.assert(combatCarveRead(combat::CREATURE_RAVAGER, 2).count, combat_expect::CREATURE_RAVAGER_CARVE2_COUNT, "ravager carve2 count pin");
        t.assert(combatCarveRead(combat::CREATURE_RAVAGER, 2).count, 2, "ravager carve2 is a 2-count drop");
        // Bad ids read inert.
        t.assert(combatCarveRead(99, 0).count, 0, "bad creature id inert");
        t.assert(combatCarveRead(combat::CREATURE_LUNGE, 99).count, 0, "bad slot inert");
        suite.addTest(t);
    }

    {
        Test t("3 carves then inert: inventory grows, spark fires, carcass persists");
        Game g;
        killLunge(g);
        t.assert(g.carveHold, 0, "no carve hold before the A");
        for (uint8_t i = 0; i < CARVE_MAX; i++) {
            t.assert(startCarve(g), 1, "A inside the carcass starts PS_CARVE");
            t.assert(g.carveHold, 1, "carve hold live");
            finishCarve(g);
            t.assert(g.player.state, PS_IDLE, "carve window ends at idle");
            t.assert(g.carvesDone, i + 1, "carve counted");
            t.assertGreaterThan(g.fxN, 0, "completion left a spark");   // the earlier haul's spark may have decayed over the window
            stepGame(g, C_IDLE);                                        // release A so the next press is a fresh edge
        }
        t.assert(g.carvesDone, CARVE_MAX, "three carves done");
        // The 100% first entry makes a full haul meaningful: 3 x scale minimum.
        t.assertGreaterThan(static_cast<int>(itemCount(g, ITEM_SCALE)), 2, "three scale minimum");
        // Fourth press: the carcass is inert, so no carve and no hold; the A is
        // free for the return nav.
        const uint8_t scaleBefore = itemCount(g, ITEM_SCALE);
        t.assert(startCarve(g), 0, "fourth A does not carve");
        t.assert(g.carveHold, 0, "inert carcass does not hold the return");
        t.assert(g.carvesDone, CARVE_MAX, "carve count capped");
        t.assert(itemCount(g, ITEM_SCALE), scaleBefore, "inert carcass yields nothing");
        // Carcass geometry survives the win + carves.
        t.assert(g.monster.x, 200, "carcass x persists");
        t.assert(g.monster.y, 40, "carcass y persists");
        t.assert(g.monster.w, 32, "carcass w persists");
        t.assert(g.monster.h, 24, "carcass h persists");
        suite.addTest(t);
    }

    {
        Test t("A outside the carcass rect does not carve (return nav allowed)");
        Game g;
        killLunge(g);
        g.player.x = 0;
        g.player.y = 0;
        stepGame(g, C_A);
        t.assert(g.player.state != PS_CARVE ? 1 : 0, 1, "no carve off the carcass");
        t.assert(g.carveHold, 0, "return nav allowed");
        t.assert(g.carvesDone, 0, "nothing carved");
        suite.addTest(t);
    }

    {
        Test t("movement cancels a running carve before the yield");
        Game g;
        killLunge(g);
        t.assert(startCarve(g), 1, "carve running");
        const int16_t x0 = g.player.x;
        stepGame(g, C_RIGHT);
        t.assert(g.player.state, PS_IDLE, "movement cancels");
        t.assert(g.carveHold, 0, "hold clears on cancel");
        t.assert(g.carvesDone, 0, "cancel did not count a carve");
        t.assert(itemCount(g, ITEM_SCALE), 0, "cancel yields nothing");
        t.assert(g.player.x, x0, "a cancelled carve does not also move the hunter");
        suite.addTest(t);
    }

    {
        Test t("damage cancels a running carve before the yield");
        Game g;
        killLunge(g);
        t.assert(startCarve(g), 1, "carve running");
        playerHurt(g, 5, 1, 0);
        t.assert(g.player.state, PS_IDLE, "damage cancels");
        t.assert(g.carvesDone, 0, "cancel did not count a carve");
        t.assert(itemCount(g, ITEM_SCALE), 0, "cancel yields nothing");
        stepGame(g, C_IDLE);
        t.assert(g.carveHold, 0, "hold clears after the cancel tick");
        suite.addTest(t);
    }

    {
        Test t("deterministic yields: the completion tick reproduces the table roll");
        Game g;
        killLunge(g);
        t.assert(startCarve(g), 1, "carve running");
        finishCarve(g);
        const uint16_t rollTick = static_cast<uint16_t>(g.tick);
        // Replay the exact roll applyCarve made (creature lunge, carve ordinal 0).
        uint8_t expScale = 0;
        uint8_t expShell = 0;
        for (uint8_t slot = 0; slot < CARVE_SLOTS; slot++) {
            const CombatCarve c = combatCarveRead(combat::CREATURE_LUNGE, slot);
            if (c.count == 0)
                continue;
            if (!combatChancePasses(rollTick, combat::CREATURE_LUNGE, slot, 0, c.chance))
                continue;
            if (c.item == ITEM_SCALE)
                expScale = static_cast<uint8_t>(expScale + c.count);
            else if (c.item == ITEM_SHELL)
                expShell = static_cast<uint8_t>(expShell + c.count);
        }
        t.assert(itemCount(g, ITEM_SCALE), expScale, "scale matches the replay");
        t.assert(itemCount(g, ITEM_SHELL), expShell, "shell matches the replay");
        t.assert(itemCount(g, ITEM_SCALE), 1, "the 100% scale slot always yields");
        suite.addTest(t);
    }

    {
        Test t("carve is a win-only verb: a loss and a live hunt never carve");
        Game lose;
        killLunge(lose);
        lose.over = OVER_LOSE;
        stepGame(lose, C_A);
        t.assert(lose.player.state != PS_CARVE ? 1 : 0, 1, "loss does not carve");
        t.assert(lose.carveHold, 0, "loss keeps the return nav");
        t.assert(lose.carvesDone, 0, "loss carved nothing");

        Game live;
        newGame(live, W_SWORD, MODE_HUNT, MON_LUNGE);
        live.player.x = live.monster.x;
        live.player.y = live.monster.y;
        stepGame(live, C_A);
        t.assert(live.player.state != PS_CARVE ? 1 : 0, 1, "live hunt A is combat, not carve");
        t.assert(live.carveHold, 0, "live hunt has no carve hold");
        suite.addTest(t);
    }

    {
        Test t("flow guard: carveHold suppresses the return nav, then clears");
        Game g;
        killLunge(g);
        MenuState menu;
        t.assert(appHuntReturnAllowed(g), 1, "no carve: return allowed");
        t.assert(startCarve(g), 1, "carve running");
        t.assert(appHuntReturnAllowed(g), 0, "live carve blocks the return");
        // The caller still consumes the A edge (menuReturnStep owns its flags);
        // the gate is what stops the nav.
        t.assert(menuReturnStep(menu, true, C_A), 1, "menu sees the A edge");
        t.assert(appHuntReturnAllowed(g), 0, "gate still blocks while carving");
        finishCarve(g);
        t.assert(g.carveHold, 0, "hold clears when the carve ends");
        t.assert(appHuntReturnAllowed(g), 1, "return allowed after the carve");
        // A fresh over-screen A away from the carcass exits as before.
        g.player.x = 0;
        g.player.y = 0;
        stepGame(g, C_IDLE);
        stepGame(g, C_A);
        t.assert(g.carveHold, 0, "off-carcass A does not hold");
        t.assert(appHuntReturnAllowed(g), 1, "return allowed off the carcass");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
