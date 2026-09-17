#pragma once

#include "harness/fxtest.hpp"
#include "src/core/game.hpp"
#include "src/core/player.hpp"
#include "src/core/world.hpp"

// First device smoke suite: initGame, hold right (mx=1) for 16 ticks, expect
// the fixed-point mover to land at exactly start + 18 (sword spd 18/16 px per
// tick * 16 ticks = 18 px; sub-pixel remainder carried in subX). Mirrors the
// host player_test.hpp semantics so host and device agree. The world check then
// compiles world.hpp for AVR and pins the start camera (player centre 104,68 ->
// cam 40,40) to the host world_test.hpp value.
//
// The two Game objects are scoped apart on purpose: each is ~753 B and the
// migration-C interpreter inlining grew this function's AVR frame, so keeping
// both live at once overran the ~1.37 KB free RAM below the stack (silent
// globals corruption, no serial). The first Game is dead before the second is
// declared, so the scopes keep the live frame under the limit on real hardware.
inline void test_boot(FxTest &test) {
    {
        mh::Game g;
        mh::initGame(g, mh::W_SWORD);
        const int32_t startX = g.player.x;
        const mh::Input inp = {1, 0, false, false};
        for (int i = 0; i < 16; ++i) {
            mh::stepPlayer(g, inp);
        }
        test.expectEq(static_cast<uint32_t>(g.player.x), static_cast<uint32_t>(startX + 18), F("boot move 16 ticks right"));
        test.expectEq(static_cast<uint32_t>(g.tick), 16, F("boot tick count"));
    }

    mh::Game w;
    mh::newGame(w, mh::W_SWORD, mh::MODE_HUNT);
    mh::updateCamera(w);
    test.expectEq(static_cast<uint32_t>(w.camX), 40u, F("boot camera x"));
    test.expectEq(static_cast<uint32_t>(w.camY), 40u, F("boot camera y"));
}