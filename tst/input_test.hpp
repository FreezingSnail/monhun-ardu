#pragma once
// Host unit tests for src/core/input.hpp — edge detector + B hold counter.
// mock/game.js step() is the source of truth: aP/bP/bR edges and the
// HOLD_TICKS=11 release-speed tap-vs-stance rule. Permanent, co-located tests.
#include "test.hpp"
#include "../src/core/game.hpp" // HOLD_TICKS + Input (via input.hpp)

using namespace mh;

namespace inputtest {

const Input I_IDLE = Input{ 0, 0, false, false };
const Input I_A    = Input{ 0, 0, true, false };
const Input I_B    = Input{ 0, 0, false, true };
const Input I_AB   = Input{ 0, 0, true, true };

// Counts hold-threshold firings: a fire is the tick bLocked turns true.
int holdFires(InputState& s, const Input& in, int ticks, uint8_t holdTicks) {
  int fires = 0;
  bool prevLocked = s.bLocked;
  for (int i = 0; i < ticks; i++) {
    stepInput(s, in, holdTicks);
    if (s.bLocked && !prevLocked) fires++;
    prevLocked = s.bLocked;
  }
  return fires;
}

} // namespace inputtest

using namespace inputtest;

void InputSuite(TestRunner& runner) {
  TestSuite suite("Input layer: edges + B hold detection (src/core/input.hpp)");

  {
    Test t("A edge fires on press only, never double-fires while held");
    InputState s; s.reset();
    stepInput(s, I_IDLE, HOLD_TICKS);
    t.assert(s.aP, false, "idle: no A press");
    stepInput(s, I_A, HOLD_TICKS);
    t.assert(s.aP, true, "press tick: A edge");
    stepInput(s, I_A, HOLD_TICKS);
    t.assert(s.aP, false, "held tick: A edge does not repeat");
    stepInput(s, I_IDLE, HOLD_TICKS);
    t.assert(s.aP, false, "release: no A edge");
    stepInput(s, I_A, HOLD_TICKS);
    t.assert(s.aP, true, "second press fires again");
    suite.addTest(t);
  }

  {
    Test t("B edge + release edge (bR) fire once each");
    InputState s; s.reset();
    stepInput(s, I_B, HOLD_TICKS);
    t.assert(s.bP, true, "press tick: B edge");
    t.assert(s.bR, false, "press tick: no release edge");
    stepInput(s, I_B, HOLD_TICKS);
    t.assert(s.bP, false, "held: no repeat");
    stepInput(s, I_IDLE, HOLD_TICKS);
    t.assert(s.bR, true, "release tick: bR");
    t.assert(s.bP, false, "release tick: no bP");
    suite.addTest(t);
  }

  {
    Test t("press+release over one tick boundary still taps (bHeld sub-threshold)");
    InputState s; s.reset();
    stepInput(s, I_B, HOLD_TICKS);    // press
    stepInput(s, I_IDLE, HOLD_TICKS); // release next poll
    t.assert(s.bHeld, 0, "counter reset on release");
    t.assert(s.bLocked, false, "never reached hold threshold");
    t.assert(s.bR, true, "release edge visible to tap path");
    suite.addTest(t);
  }

  {
    Test t("hold counter reaches HOLD_TICKS once, then latches");
    InputState s; s.reset();
    for (int i = 1; i < HOLD_TICKS; i++) {
      stepInput(s, I_B, HOLD_TICKS);
      t.assert(s.bHeld, i, "held tick counts up");
      t.assert(s.bLocked, false, "not locked before threshold");
    }
    stepInput(s, I_B, HOLD_TICKS);
    t.assert(s.bHeld, HOLD_TICKS, "11th tick reaches threshold");
    t.assert(s.bLocked, true, "hold latches at threshold");

    int fires = 0;
    for (int i = 0; i < 20; i++) {
      const bool was = s.bLocked;
      stepInput(s, I_B, HOLD_TICKS);
      if (s.bLocked && !was) fires++;
    }
    t.assert(fires, 0, "holding past threshold does not re-fire");
    t.assert(s.bHeld, HOLD_TICKS, "counter saturates at threshold");
    suite.addTest(t);
  }

  {
    Test t("release then re-press counts and fires again");
    InputState s; s.reset();
    t.assert(holdFires(s, I_B, HOLD_TICKS + 4, HOLD_TICKS), 1, "first hold fires once");
    t.assert(s.bLocked, true, "locked while held");
    holdFires(s, I_IDLE, 1, HOLD_TICKS); // release
    t.assert(s.bLocked, false, "release clears latch");
    t.assert(s.bHeld, 0, "release clears counter");
    t.assert(holdFires(s, I_B, HOLD_TICKS + 4, HOLD_TICKS), 1, "re-press fires once");
    t.assert(s.bLocked, true, "locked again");
    suite.addTest(t);
  }

  {
    Test t("short taps never latch; d-pad leaves A/B untouched");
    InputState s; s.reset();
    for (int i = 0; i < HOLD_TICKS - 1; i++) stepInput(s, I_B, HOLD_TICKS);
    t.assert(s.bLocked, false, "10 held ticks is not a hold");
    holdFires(s, I_IDLE, 1, HOLD_TICKS);
    t.assert(s.bHeld, 0, "tap reset clean");
    stepInput(s, Input{ -1, 1, false, false }, HOLD_TICKS);
    t.assert(s.prevA, false, "A untouched by d-pad");
    t.assert(s.prevB, false, "B untouched by d-pad");
    suite.addTest(t);
  }

  {
    Test t("A and B edges are independent (A+B combo not consumed)");
    InputState s; s.reset();
    stepInput(s, I_AB, HOLD_TICKS);
    t.assert(s.aP, true, "A press while B press");
    t.assert(s.bP, true, "B press while A press");
    stepInput(s, I_AB, HOLD_TICKS);
    t.assert(s.aP, false, "A held no repeat");
    t.assert(s.bP, false, "B held no repeat");
    suite.addTest(t);
  }

  runner.addTestSuite(suite);
}
