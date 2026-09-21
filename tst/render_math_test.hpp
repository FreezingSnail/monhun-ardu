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

void testTellWindupFrame(Test &t) {
    std::cout << "---------- windup animation-frame selector ----------" << std::endl;

    // The tell ids stay the authored order (DOT/LINE/ARC/RING/ZONE).
    t.assert(mh::TELL_DOT, 0, "dot shape id");
    t.assert(mh::TELL_LINE, 1, "line shape id");
    t.assert(mh::TELL_ARC, 2, "arc shape id");
    t.assert(mh::TELL_RING, 3, "ring shape id");
    t.assert(mh::TELL_ZONE, 4, "zone shape id");

    // prg.12 authored the per-attack windup poses on fxchickenatk/fxbullatk/
    // fxheavyatk, so tells 1..3 select an authored slot and tell 0 (generic
    // coil) stays unauthored.
    t.assert(mh::TELL_FRAMES_AUTHORED, 3, "three authored tell frames shipping");
    t.assert(mh::tellHasAuthoredFrame(mh::TELL_DOT, mh::TELL_FRAMES_AUTHORED), false, "dot has no bespoke frame");
    t.assert(mh::tellHasAuthoredFrame(mh::TELL_LINE, mh::TELL_FRAMES_AUTHORED), true, "line authored");
    t.assert(mh::tellHasAuthoredFrame(mh::TELL_ARC, mh::TELL_FRAMES_AUTHORED), true, "arc authored");
    t.assert(mh::tellHasAuthoredFrame(mh::TELL_RING, mh::TELL_FRAMES_AUTHORED), true, "ring authored");
    t.assert(mh::tellHasAuthoredFrame(mh::TELL_ZONE, mh::TELL_FRAMES_AUTHORED), false, "zone unauthored");
    t.assert(mh::tellWindupFrame(mh::TELL_LINE, mh::TELL_FRAMES_AUTHORED), mh::TELL_LINE, "line slot = tell");
    t.assert(mh::tellWindupFrame(mh::TELL_RING, mh::TELL_FRAMES_AUTHORED), mh::TELL_RING, "ring slot = tell");
    t.assert(mh::tellWindupFrame(mh::TELL_ZONE, mh::TELL_FRAMES_AUTHORED), mh::TELL_WINDUP_NONE, "zone falls back");

    // tell 0 is the generic coil, never a bespoke frame, even with art authored.
    t.assert(mh::tellHasAuthoredFrame(mh::TELL_DOT, 8), false, "dot stays generic");
    t.assert(mh::tellWindupFrame(mh::TELL_DOT, 8), mh::TELL_WINDUP_NONE, "dot never a bespoke slot");

    // With authored frames the tell selects slot 1..N; beyond N falls back.
    t.assert(mh::tellHasAuthoredFrame(mh::TELL_LINE, 3), true, "line authored at 3");
    t.assert(mh::tellWindupFrame(mh::TELL_LINE, 3), mh::TELL_LINE, "line slot = tell");
    t.assert(mh::tellWindupFrame(mh::TELL_RING, 3), mh::TELL_RING, "ring slot = tell");
    t.assert(mh::tellHasAuthoredFrame(mh::TELL_ZONE, 3), false, "zone beyond authored");
    t.assert(mh::tellWindupFrame(mh::TELL_ZONE, 3), mh::TELL_WINDUP_NONE, "zone falls back");
    t.assert(mh::tellWindupFrame(mh::TELL_LINE, 0), mh::TELL_WINDUP_NONE, "zero authored falls back");
    t.assert(mh::tellWindupFrame(200, 255), 200, "high tell within count resolves");
}

void RenderMathSuite(TestRunner &runner) {
    TestSuite suite("render math");
    {
        Test t("tail_spin sheet frame selector");
        testSpinSheetFrame(t);
        suite.addTest(t);
    }
    {
        Test t("windup animation-frame selector");
        testTellWindupFrame(t);
        suite.addTest(t);
    }
    runner.addTestSuite(suite);
}

}   // namespace rendermathtest
