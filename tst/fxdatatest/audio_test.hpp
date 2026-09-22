#pragma once
// On-device audio cue test (bead monhun-ardu-6zc).
//
// Drives the edge detector in src/audio.hpp through one synthetic transition
// per event and asserts the fired-cue mask. Each case uses a fresh valid Game
// (mh::newGame) so the detector diffs real core fields the same way the device
// loop does; no sim timing is involved, so it is deterministic. Cues are played
// for real (MH_AUDIO defaults to 1): if the beeper's TIMER3_COMPA ISR collided
// with ArduboyG's TIMER1_COMPA plane ISR the sketch would not reach the final P.
//
// Pair this with the parity suite (core behaviour) — this test only proves the
// cue-to-event mapping and that the tone ISR coexists with the plane loop.

#include "harness/fxtest.hpp"
#include "src/audio.hpp"

#include <stdint.h>

namespace audio_test {

using namespace mh;

typedef void (*AudioEvent)(Game &);

// Latch the pre-event state, apply the event, tick, and return the mask the
// detector fired for it. The first audioUpdate() call latches (inited=false).
static uint16_t cueFor(AudioState &s, Game &g, AudioEvent ev) {
    audioUpdate(s, g);
    ev(g);
    g.tick++;
    audioUpdate(s, g);
    return s.firedMask;
}

static inline bool cueBit(uint16_t mask, uint8_t cue) {
    return (mask >> cue) & 1u;
}

// ---------------------------------------------------------------- events
static void evMonsterHit(Game &g) {
    g.monster.hp -= 10;
}

static void evMonsterCrit(Game &g) {
    g.monster.hp -= 14;
    Effect &e = g.fx[g.fxN++];
    e = Effect{0, 0, 1, 7, true};   // fresh crit spark
}

static void evParry(Game &g) {
    g.player.riposteT = 90;
}

static void evDeflect(Game &g) {
    g.player.state = PS_DEFLECT;
    g.monster.stun += 28;
}

static void evGuard(Game &g) {
    g.player.hp -= 1;
}

static void evShot(Game &g) {
    g.player.state = PS_SPECIAL;   // gun hitscan edge (shells retired)
}

inline void test_audio(FxTest &test) {
    {   // hunt: body hit -> CUE_HIT, not crit
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        AudioState s{};
        const uint16_t m = cueFor(s, g, evMonsterHit);
        test.expectEq(cueBit(m, CUE_HIT), 1, F("hunt hit"));
        test.expectEq(cueBit(m, CUE_CRIT), 0, F("hunt hit no crit"));
    }
    {   // hunt: head crit -> CUE_CRIT
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        AudioState s{};
        const uint16_t m = cueFor(s, g, evMonsterCrit);
        test.expectEq(cueBit(m, CUE_CRIT), 1, F("hunt crit"));
        test.expectEq(cueBit(m, CUE_HIT), 0, F("crit not plain hit"));
    }
    {   // parry riposte armed -> CUE_PARRY
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        AudioState s{};
        const uint16_t m = cueFor(s, g, evParry);
        test.expectEq(cueBit(m, CUE_PARRY), 1, F("parry"));
    }
    {   // deflect absorbed a hit -> CUE_DEFLECT
        Game g;
        newGame(g, W_FLAIL, MODE_HUNT);
        AudioState s{};
        const uint16_t m = cueFor(s, g, evDeflect);
        test.expectEq(cueBit(m, CUE_DEFLECT), 1, F("deflect"));
    }
    {   // guard block (chip hp) -> CUE_GUARD
        Game g;
        newGame(g, W_GUN, MODE_HUNT);
        g.player.stance = ST_GUARD;   // guard is held across the latch
        AudioState s{};
        const uint16_t m = cueFor(s, g, evGuard);
        test.expectEq(cueBit(m, CUE_GUARD), 1, F("guard block"));
    }
    {   // gun fired the hitscan arrowshot -> CUE_SHOT
        Game g;
        newGame(g, W_GUN, MODE_HUNT);
        AudioState s{};
        const uint16_t m = cueFor(s, g, evShot);
        test.expectEq(cueBit(m, CUE_SHOT), 1, F("shot fired"));
    }
    {   // no edge -> no cue
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        AudioState s{};
        audioUpdate(s, g);
        g.tick++;
        audioUpdate(s, g);
        test.expectEq(s.firedMask, 0, F("idle no cue"));
    }
}

}   // namespace audio_test
