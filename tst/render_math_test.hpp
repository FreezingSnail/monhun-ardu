#pragma once
// Host unit test for the pure spin-sheet frame selector (bead
// monhun-ardu-nch.3) and the per-attack telegraph geometry (bead
// monhun-ardu-feel.5). src/render_math.hpp is Arduino-free and src/render.hpp is
// device-only, so this suite pins the exact rect/scalar math the device tell
// draw consumes; the Ardens tell_test pins the resulting framebuffer bytes.
#include "test.hpp"
#include "../src/render_math.hpp"
#include "../src/core/combat.hpp"   // enum Tell shape ids

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

void testTellShapes(Test &t) {
    std::cout << "---------- per-attack telegraph geometry ----------" << std::endl;

    // Dot default negative control: tell 0 draws the legacy 2x2 core only, so
    // the render takes the no-window path.
    t.assert(mh::TELL_DOT, 0, "dot shape id");
    t.assert(mh::tellNeedsWindow(mh::TELL_DOT), false, "dot needs no window");
    t.assert(mh::tellNeedsWindow(mh::TELL_LINE), true, "line uses the window");
    t.assert(mh::tellNeedsWindow(mh::TELL_ARC), true, "arc uses the window");
    t.assert(mh::tellNeedsWindow(mh::TELL_RING), true, "ring uses the window");
    t.assert(mh::tellNeedsWindow(mh::TELL_ZONE), true, "zone uses the window");
    t.assert(mh::tellNeedsWindow(200), true, "unknown shape uses the window");

    // Line dashes: Q2 fractions of the window-centre vector, floor shifts.
    int16_t ox = 0, oy = 0;
    mh::tellLineDash(14, 0, 1, ox, oy);
    t.assert(ox, 3, "line dash 1/4 ox");
    t.assert(oy, 0, "line dash 1/4 oy");
    mh::tellLineDash(14, 0, 2, ox, oy);
    t.assert(ox, 7, "line dash 2/4 ox");
    mh::tellLineDash(14, 0, 3, ox, oy);
    t.assert(ox, 10, "line dash 3/4 ox");
    mh::tellLineDash(12, -8, 2, ox, oy);
    t.assert(ox, 6, "line diagonal ox");
    t.assert(oy, -4, "line diagonal oy");
    mh::tellLineDash(-14, 0, 3, ox, oy);
    t.assert(ox, -11, "line negative dx floors");

    // Ring expansion: ~1 px per 2 elapsed windup ticks from 2, clamped.
    t.assert(mh::tellRingHalf(6, -5), 2, "ring negative elapsed clamps to 2");
    t.assert(mh::tellRingHalf(6, 0), 2, "ring starts at 2");
    t.assert(mh::tellRingHalf(6, 1), 2, "ring t1 still 2");
    t.assert(mh::tellRingHalf(6, 2), 3, "ring t2 -> 3");
    t.assert(mh::tellRingHalf(6, 4), 4, "ring t4 -> 4");
    t.assert(mh::tellRingHalf(6, 8), 6, "ring t8 reaches half");
    t.assert(mh::tellRingHalf(6, 100), 6, "ring clamps to half");
    t.assert(mh::tellRingHalf(2, 10), 2, "ring clamps to a tiny box");

    // Arc segments: left/centre/right across the box width, centre dropped 2.
    t.assert(mh::TELL_ARC, 2, "arc shape id");
    mh::tellArcSeg(12, 10, 0, ox, oy);
    t.assert(ox, 0, "arc left ox");
    t.assert(oy, 4, "arc left oy");
    mh::tellArcSeg(12, 10, 1, ox, oy);
    t.assert(ox, 4, "arc centre ox");
    t.assert(oy, 6, "arc centre drop");
    mh::tellArcSeg(12, 10, 2, ox, oy);
    t.assert(ox, 8, "arc right ox");
    t.assert(oy, 4, "arc right oy");
    mh::tellArcSeg(18, 16, 1, ox, oy);
    t.assert(ox, 7, "arc wide centre ox");

    // Window rect origin shared by ring/zone; clamps the hit-test box maths.
    int16_t rx = 0, ry = 0;
    mh::tellRectOrigin(78, 36, 12, 10, rx, ry);
    t.assert(rx, 72, "rect origin x");
    t.assert(ry, 31, "rect origin y");
    mh::tellRectOrigin(10, 10, 1, 1, rx, ry);
    t.assert(rx, 10, "1px rect origin x");
    t.assert(ry, 10, "1px rect origin y");
}

void RenderMathSuite(TestRunner &runner) {
    TestSuite suite("render math");
    {
        Test t("tail_spin sheet frame selector");
        testSpinSheetFrame(t);
        suite.addTest(t);
    }
    {
        Test t("per-attack telegraph geometry");
        testTellShapes(t);
        suite.addTest(t);
    }
    runner.addTestSuite(suite);
}

}   // namespace rendermathtest
