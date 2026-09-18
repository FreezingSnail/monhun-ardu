#pragma once
// Host unit test for the pure spin-sheet frame selector (bead
// monhun-ardu-nch.3). src/render_math.hpp is Arduino-free, so this suite pins
// the exact frame mapping the device render uses for the longtail tail_spin.
#include "test.hpp"
#include "../src/render_math.hpp"

namespace rendermathtest {

void testSpinSheetFrame(Test &t) {
    std::cout << "---------- tail_spin sheet frame selector ----------" << std::endl;
    // active 20: progress8 = (tick * 8) / 20 truncated, so the first slice is
    // ticks 0..2, then every two-three ticks. At tick == active it is exactly 8
    // and wraps back to the start facing.
    t.assert(mh::spinSheetFrame(0, 0, 20), 0, "t0 frame 0");
    t.assert(mh::spinSheetFrame(0, 1, 20), 0, "t1 still 0");
    t.assert(mh::spinSheetFrame(0, 2, 20), 0, "t2 still 0");
    t.assert(mh::spinSheetFrame(0, 3, 20), 1, "t3 frame 1");
    t.assert(mh::spinSheetFrame(0, 4, 20), 1, "t4 still 1");
    t.assert(mh::spinSheetFrame(0, 5, 20), 2, "t5 frame 2");
    t.assert(mh::spinSheetFrame(0, 18, 20), 7, "t18 frame 7");
    t.assert(mh::spinSheetFrame(0, 19, 20), 7, "t19 still 7");
    t.assert(mh::spinSheetFrame(0, 20, 20), 0, "t20 full revolution wraps");

    // The locked-facing start offsets the whole sequence and wraps mod 8.
    t.assert(mh::spinSheetFrame(4, 0, 20), 4, "west start at rest");
    t.assert(mh::spinSheetFrame(4, 5, 20), 6, "west start + 2 slices");
    t.assert(mh::spinSheetFrame(6, 5, 20), 0, "north start wraps past 7");
    t.assert(mh::spinSheetFrame(7, 20, 20), 7, "ne start wraps to itself");

    // active 8: one slice per tick, tick == active wraps.
    for (int16_t tick = 0; tick < 8; tick++)
        t.assert(mh::spinSheetFrame(0, tick, 8), tick, "active 8 per-tick frame");
    t.assert(mh::spinSheetFrame(0, 8, 8), 0, "active 8 wraps at 8");

    // Degenerate inputs collapse to the start facing instead of dividing by 0.
    t.assert(mh::spinSheetFrame(3, 5, 0), 3, "zero active collapses");
    t.assert(mh::spinSheetFrame(3, 0, 20), 3, "tick 0 is the start");
    t.assert(mh::spinSheetFrame(3, -1, 20), 3, "negative tick collapses");
}

void RenderMathSuite(TestRunner &runner) {
    TestSuite suite("render math");
    {
        Test t("tail_spin sheet frame selector");
        testSpinSheetFrame(t);
        suite.addTest(t);
    }
    runner.addTestSuite(suite);
}

}   // namespace rendermathtest
