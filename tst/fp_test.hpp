#pragma once
// Host unit tests for src/core/fp.hpp — permanent, co-located with repo tests.
#include "test.hpp"
#include "../src/core/fp.hpp"

using namespace fp;

// 28 ticks of sword-speed (18/16 px per tick) movement; signed % 16 must keep
// left/up symmetric with right/down.
static void moveTicks(FpBody &b, int16_t dx, int16_t dy, int16_t spd, int ticks) {
    for (int i = 0; i < ticks; i++)
        addMove(b, dx, dy, spd);
}

void FpSuite(TestRunner &runner) {
    TestSuite suite("Fixed-point math (src/core/fp.hpp)");

    {
        Test t("tdiv truncates toward zero for positive and negative");
        t.assert(tdiv(7, 16), 0, "7/16 -> 0");
        t.assert(tdiv(16, 16), 1, "16/16 -> 1");
        t.assert(tdiv(33, 16), 2, "33/16 -> 2");
        t.assert(tdiv(-7, 16), 0, "-7/16 -> 0 (toward zero, not -1)");
        t.assert(tdiv(-16, 16), -1, "-16/16 -> -1");
        t.assert(tdiv(-33, 16), -2, "-33/16 -> -2 (toward zero, not -3)");
        t.assert(tdiv(-1, 16), 0, "-1/16 -> 0");
        t.assert(tdiv(-1, 1), -1, "-1/1 -> -1");
        suite.addTest(t);
    }

    {
        Test t("addMove left distance equals right distance (within 1 px)");
        FpBody right{0, 0, 0, 0};
        FpBody left{0, 0, 0, 0};
        moveTicks(right, 16, 0, 18, 28);
        moveTicks(left, -16, 0, 18, 28);
        const int dr = right.x;
        const int dl = -left.x;   // mirror left displacement
        t.assertGreaterThan(dr, 0, "moved right");
        t.assertLessThan(dl, dr + 1, "left distance >= right distance - 1");
        t.assertGreaterThan(dl, dr - 1, "left distance <= right distance + 1");
        suite.addTest(t);
    }

    {
        Test t("addMove up distance equals down distance (within 1 px)");
        FpBody down{0, 0, 0, 0};
        FpBody up{0, 0, 0, 0};
        moveTicks(down, 0, 16, 18, 28);
        moveTicks(up, 0, -16, 18, 28);
        const int dd = down.y;
        const int du = -up.y;
        t.assertGreaterThan(dd, 0, "moved down");
        t.assertLessThan(du, dd + 1, "up distance >= down distance - 1");
        t.assertGreaterThan(du, dd - 1, "up distance <= down distance + 1");
        suite.addTest(t);
    }

    {
        Test t("DIR8 table matches prototype vectors");
        const struct {
            int16_t x, y;
        } expected[8] = {
            {16, 0}, {11, 11}, {0, 16}, {-11, 11}, {-16, 0}, {-11, -11}, {0, -16}, {11, -11},
        };
        for (int i = 0; i < 8; i++) {
            t.assert(DIR8[i].x, expected[i].x, "DIR8[" + std::to_string(i) + "].x");
            t.assert(DIR8[i].y, expected[i].y, "DIR8[" + std::to_string(i) + "].y");
        }
        suite.addTest(t);
    }

    {
        Test t("dirIndexFromInput maps all 8 directions plus idle");
        t.assert(dirIndexFromInput(1, 0), 0, "E");
        t.assert(dirIndexFromInput(1, 1), 1, "SE");
        t.assert(dirIndexFromInput(0, 1), 2, "S");
        t.assert(dirIndexFromInput(-1, 1), 3, "SW");
        t.assert(dirIndexFromInput(-1, 0), 4, "W");
        t.assert(dirIndexFromInput(-1, -1), 5, "NW");
        t.assert(dirIndexFromInput(0, -1), 6, "N");
        t.assert(dirIndexFromInput(1, -1), 7, "NE");
        t.assert(dirIndexFromInput(0, 0), -1, "idle -> -1");
        suite.addTest(t);
    }

    {
        Test t("dirIndexFromDelta maps deltas to nearest DIR8");
        t.assert(dirIndexFromDelta(16, 0), 0, "pure E");
        t.assert(dirIndexFromDelta(-16, 0), 4, "pure W");
        t.assert(dirIndexFromDelta(0, 16), 2, "pure S");
        t.assert(dirIndexFromDelta(0, -16), 6, "pure N");
        t.assert(dirIndexFromDelta(11, 11), 1, "SE");
        t.assert(dirIndexFromDelta(11, -11), 7, "NE");
        t.assert(dirIndexFromDelta(-11, 11), 3, "SW");
        t.assert(dirIndexFromDelta(-11, -11), 5, "NW");
        t.assert(dirIndexFromDelta(3, 1), 0, "x-dominant -> E");
        t.assert(dirIndexFromDelta(1, 3), 2, "y-dominant -> S");
        t.assert(dirIndexFromDelta(-3, 1), 4, "x-dominant -> W");
        t.assert(dirIndexFromDelta(1, -3), 6, "y-dominant -> N");
        t.assert(dirIndexFromDelta(2, 1), 1, "balanced (dx>=0, dy>0) -> SE");
        t.assert(dirIndexFromDelta(-2, 1), 3, "balanced (dx<0, dy>0) -> SW");
        t.assert(dirIndexFromDelta(2, -1), 7, "balanced (dx>=0, dy<0) -> NE");
        t.assert(dirIndexFromDelta(-2, -1), 5, "balanced (dx<0, dy<0) -> NW");
        suite.addTest(t);
    }

    {
        Test t("isqrt on perfect squares and non-squares");
        const int squares[7] = {0, 1, 4, 9, 16, 625, 65535};
        const int roots[7] = {0, 1, 2, 3, 4, 25, 255};
        for (int i = 0; i < 7; i++) {
            t.assert(isqrt(squares[i]), roots[i], "isqrt(" + std::to_string(squares[i]) + ")");
        }
        const int nums[6] = {2, 3, 8, 15, 24, 99};
        const int flrs[6] = {1, 1, 2, 3, 4, 9};
        for (int i = 0; i < 6; i++) {
            t.assert(isqrt(nums[i]), flrs[i], "isqrt(" + std::to_string(nums[i]) + ") floor");
        }
        suite.addTest(t);
    }

    {
        Test t("rotFp rotates unit vector with integer cos/sin");
        Dir8 r = rotFp(16, 0, 15, 6);   // ~22 deg left of E
        t.assert(r.x, 15, "rotFp(16,0,15,6).x");
        t.assert(r.y, 6, "rotFp(16,0,15,6).y");
        r = rotFp(16, 0, 15, -6);   // ~22 deg right of E
        t.assert(r.x, 15, "rotFp(16,0,15,-6).x");
        t.assert(r.y, -6, "rotFp(16,0,15,-6).y");
        r = rotFp(0, 16, 15, 6);   // ~22 deg left of S
        t.assert(r.x, -6, "rotFp(0,16,15,6).x");
        t.assert(r.y, 15, "rotFp(0,16,15,6).y");
        suite.addTest(t);
    }

    {
        Test t("drainStam decrements stamina exactly in 1/16 units");
        FpStam p{5, 0};
        t.assert(drainStam(p, 16), true, "drain 16 from full 1/16 -> still alive");
        t.assert(p.stam, 4, "drain 16 = exactly 1 stamina");
        t.assert(p.stamSub, 0, "stamSub back to 0");

        FpStam q{5, 15};
        t.assert(drainStam(q, 16), true, "drain 16 rolling over partial sub");
        t.assert(q.stam, 4, "rollover drops exactly 1 stamina");
        t.assert(q.stamSub, 15, "carry stays as 15/16");

        FpStam r{5, 0};
        t.assert(drainStam(r, 1), true, "drain 1/16 no rollover");
        t.assert(r.stam, 5, "no stamina lost below 16 units");
        t.assert(r.stamSub, 1, "sub accumulates 1/16");

        FpStam s{1, 0};
        t.assert(drainStam(s, 16), false, "drain to zero returns false");
        t.assert(s.stam, 0, "stamina hits exactly 0");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}