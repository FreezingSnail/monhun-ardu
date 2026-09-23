#pragma once
// Host unit tests for the smith upgrade runtime (bead monhun-ardu-4ug, qs.3):
// src/upgrade_state.hpp (UpgradeDef lookup + integer-percent multiplier math)
// and the tier multipliers feeding the real player damage + move-speed paths in
// src/core/player.hpp. The cart side (src/smith.hpp) is device-only and is
// pinned by tst/fxdatatest/smith_test.hpp. The weapon-tier purchase UI was
// trimmed in ui.2 (the tier table survives as the multiplier source until the
// ui.4 FORGE trees replace it), so the COND_UPGRADE row tests are gone.
#include "test.hpp"
#include "../src/upgrade_state.hpp"
#include "../src/core/world.hpp"

using namespace mh;

namespace smithtest {

// A target rect that covers the world so the melee hitbox always overlaps.
inline int16_t meleeHitDmg(uint8_t dmgMul) {
    static int16_t last = -1;
    struct Capture {
        static void hit(Game &, uint8_t dmg, int16_t, int16_t, uint8_t, uint8_t) {
            last = static_cast<int16_t>(dmg);
        }
    };
    Game g;
    newGame(g, W_SWORD, MODE_HUNT);
    g.dmgMul = dmgMul;
    g.target.alive = true;
    g.target.rect = mh::Rect{0, 0, WORLD_W, WORLD_H};
    g.target.onHit = Capture::hit;
    last = -1;
    const Input a = {0, 0, true, false};
    const Input idle = {0, 0, false, false};
    stepPlayer(g, a);
    for (int i = 0; i < 12; i++)
        stepPlayer(g, idle);
    return last;
}

// Walk right for `ticks` ticks and report the travelled distance.
inline int16_t walkDistance(uint8_t spdMul, uint8_t ticks) {
    Game g;
    newGame(g, W_SWORD, MODE_HUNT);
    g.spdMul = spdMul;
    const int16_t x0 = g.player.x;
    const Input right = {1, 0, false, false};
    for (uint8_t i = 0; i < ticks; i++)
        stepPlayer(g, right);
    return static_cast<int16_t>(g.player.x - x0);
}

}   // namespace smithtest

using namespace smithtest;

void SmithSuite(TestRunner &runner) {
    TestSuite suite("Smith: upgrade defs, tier multipliers (qs.3)");

    // -------------------------------------------------------- multiplier math
    {
        Test t("upgradeMul is integer percent, truncating");
        t.assert(upgradeMul(9, 100), 9, "100% identity");
        t.assert(upgradeMul(9, 110), 9, "9*110/100 truncates 9.9 -> 9");
        t.assert(upgradeMul(9, 125), 11, "9*125/100 = 11.25 -> 11");
        t.assert(upgradeMul(27, 130), 35, "27*130/100 = 35.1 -> 35");
        t.assert(upgradeMul(18, 115), 20, "18*115/100 = 20.7 -> 20");
        t.assert(upgradeMul(0, 200), 0, "zero stays zero");
        t.assert(upgradeMul(8, 0), 8, "0 means unset -> identity");
        suite.addTest(t);
    }

    {
        Test t("upgradeFind/upgradeResolve pick the (weapon,tier) record");
        const UpgradeDef defs[3] = {
            {W_SWORD, 1, 100, 110, 105, 0},
            {W_SWORD, 2, 250, 125, 115, 0},
            {W_FLAIL, 1, 120, 112, 103, 0},
        };
        t.assert(upgradeFind(defs, 3, W_SWORD, 1), 0, "sword t1 index");
        t.assert(upgradeFind(defs, 3, W_FLAIL, 1), 2, "flail t1 index");
        t.assert(upgradeFind(defs, 3, W_GUN, 1), -1, "missing weapon");
        t.assert(upgradeFind(defs, 3, W_SWORD, 3), -1, "missing tier");

        uint8_t dmg = 0, spd = 0;
        upgradeResolve(defs, 3, W_SWORD, 0, dmg, spd);
        t.assert(dmg, 100, "tier 0 dmg identity");
        t.assert(spd, 100, "tier 0 spd identity");
        upgradeResolve(defs, 3, W_SWORD, 1, dmg, spd);
        t.assert(dmg, 110, "sword t1 dmg");
        t.assert(spd, 105, "sword t1 spd");
        upgradeResolve(defs, 3, W_SWORD, 2, dmg, spd);
        t.assert(dmg, 125, "sword t2 dmg");
        upgradeResolve(defs, 3, W_FLAIL, 2, dmg, spd);
        t.assert(dmg, 100, "missing record -> identity dmg");
        t.assert(spd, 100, "missing record -> identity spd");
        suite.addTest(t);
    }

    // -------------------------------------------------- stat application E2E
    {
        Test t("tier damage multiplier flows through the melee onHit chain");
        t.assert(meleeHitDmg(100), 9, "baseline sword hit 9");
        t.assert(meleeHitDmg(125), 11, "tier-2 dmg 9*125/100 = 11");
        t.assert(meleeHitDmg(200), 18, "200% doubles to 18");
        suite.addTest(t);
    }

    {
        Test t("tier speed multiplier feeds the idle move path");
        const int16_t base = walkDistance(100, 40);
        const int16_t fast = walkDistance(200, 40);
        const int16_t slow = walkDistance(50, 40);
        t.assertGreaterThan(base, 0, "baseline moves");
        t.assertGreaterThan(fast, base, "faster tier travels farther");
        t.assertLessThan(slow, base, "slower tier travels less");
        suite.addTest(t);
    }

    {
        Test t("newGame starts with identity multipliers");
        Game g;
        newGame(g, W_FLAIL, MODE_HUNT);
        t.assert(g.dmgMul, UPGRADE_MUL_BASE, "dmg mul default 100");
        t.assert(g.spdMul, UPGRADE_MUL_BASE, "spd mul default 100");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
