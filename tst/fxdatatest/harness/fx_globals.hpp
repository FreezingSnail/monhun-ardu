#pragma once

// Device test global instance + setup, adapted from
// ~/code/CreatureGathererFX/tst/fxdatatest/harness/fx_globals.hpp.
// Mirrors monhun-ardu.ino setup: single-arg FX::begin(FX_DATA_PAGE) because
// this repo has no FX_SAVE_PAGE partition.

#define ABG_IMPLEMENTATION
#define SPRITESU_IMPLEMENTATION
#include "../src/common.hpp"
#include "../src/fxdata.h"
#include "../src/core/game.hpp"
#include "../src/core/player.hpp"

decltype(arduboy) arduboy;

inline void fxTestSetup() {
    Serial.begin(9600);
    arduboy.begin();
    FX::begin(FX_DATA_PAGE);
    FX::setCursorRange(0, 32767);
}