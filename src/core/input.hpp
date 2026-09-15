#pragma once
// Pure, host-testable input layer: raw sample -> Input, per-tick edge flags and
// the B hold counter. Ported from mock/game.js step() (source of truth):
//
//   aP = a && !prevA;  bP = b && !prevB;  bR = !b && prevB
//
// The mock's updatePlayer() keeps bHeld/bReady/bLocked on the Player; those stay
// authoritative for the FSM (see player.hpp / game.hpp). This layer is the same
// detection stand-alone, so the device loop and host tests can drive it, and
// stepPlayer() reuses inputEdges() so the edge rule lives in exactly one place.
// No Arduino.h, no float; ints only, header-only.

#include <stdint.h>

namespace mh {

// One tick of sampled buttons + d-pad. mx/my are -1/0/1 per axis.
struct Input {
  int8_t mx, my;
  bool a, b;
};

// Input-layer state. The edge flags are recomputed every tick. bHeld counts
// held B ticks from the press and saturates at the threshold; bReady arms on
// press and bLocked latches once the hold threshold is reached, so a hold fires
// exactly once until the button is released. bHeld/bReady/bLocked here are the
// layer's own; the Player FSM keeps its own copy (no shared mutation).
struct InputState {
  bool prevA, prevB;
  uint8_t bHeld;
  bool bReady, bLocked;
  bool aP, bP, bR; // edges for the tick just consumed

  void reset() {
    prevA = prevB = false;
    bHeld = 0;
    bReady = bLocked = false;
    aP = bP = bR = false;
  }
};

// Stateless primitive: mock step() edges into caller-owned previous flags.
// stepPlayer() calls this so Game::prevA/prevB and the layer never diverge.
inline void inputEdges(const Input& in, bool& prevA, bool& prevB,
                       bool& aP, bool& bP, bool& bR) {
  aP = in.a && !prevA;
  bP = in.b && !prevB;
  bR = !in.b && prevB;
  prevA = in.a;
  prevB = in.b;
}

// One tick of edges + B hold counting. holdTicks is HOLD_TICKS (game.hpp); a
// press+release over consecutive ticks leaves bHeld < holdTicks, i.e. a tap,
// so a quick tap still registers rather than being swallowed by the hold path.
inline void stepInput(InputState& s, const Input& in, uint8_t holdTicks) {
  inputEdges(in, s.prevA, s.prevB, s.aP, s.bP, s.bR);

  if (s.bP) { s.bHeld = 0; s.bReady = true; }
  if (in.b && s.bReady) {
    if (s.bHeld < holdTicks) s.bHeld++;
    if (s.bHeld == holdTicks) s.bLocked = true; // latch: one fire per press
  }
  if (s.bR) {
    s.bReady = false;
    s.bHeld = 0;
    s.bLocked = false;
  }
}

} // namespace mh
