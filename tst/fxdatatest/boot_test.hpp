#pragma once

#include "harness/fxtest.hpp"
#include "src/core/game.hpp"
#include "src/core/player.hpp"

// First device smoke suite: initGame, hold right (mx=1) for 16 ticks, expect
// the fixed-point mover to land at exactly start + 18 (sword spd 18/16 px per
// tick * 16 ticks = 18 px; sub-pixel remainder carried in subX). Mirrors the
// host player_test.hpp semantics so host and device agree.
inline void test_boot(FxTest &test) {
    mh::Game g;
    mh::initGame(g, mh::W_SWORD);
    const int32_t startX = g.player.x;
    const mh::Input inp = { 1, 0, false, false };
    for (int i = 0; i < 16; ++i) {
        mh::stepPlayer(g, inp);
    }
    test.expectEq(static_cast<uint32_t>(g.player.x),
                  static_cast<uint32_t>(startX + 18),
                  F("boot move 16 ticks right"));
    test.expectEq(static_cast<uint32_t>(g.tick), 16, F("boot tick count"));
}