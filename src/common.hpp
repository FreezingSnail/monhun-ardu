#pragma once

#include <stdint.h>

#define ABG_TIMER1
#define ABG_SYNC_PARK_ROW

#include "external/ArduboyG.h"
extern ArduboyGBase_Config<ABG_Mode::L4_Triplane> arduboy;

#define SPRITESU_OVERWRITE
#define SPRITESU_PLUSMASK
#define SPRITESU_RECT
#define SPRITESU_FX
#include "external/SpritesU.hpp"

#define DGF __attribute__((optimize("-O0")))

#define FRAME(x) ((x) * 3 + arduboy.currentPlane())
#define FRAMESHIFT(x) ((x) + (2097152 * arduboy.currentPlane()))
