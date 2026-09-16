
#define ABG_IMPLEMENTATION
#define SPRITESU_IMPLEMENTATION
#include "src/common.hpp"
#include "src/globals.hpp"
#include "src/fxdata.h"
#include "src/core/world.hpp"
#include "src/audio.hpp"

// Compile-time gate for the 1-bit wireframe debug overlay (hurt/hit boxes).
// 0 = release: every debug symbol below is preprocessed out (zero flash/RAM).
// 1 = debug: world-space wire boxes drawn after the scene, before the HUD.
#ifndef DEBUG_HURTBOXES
#define DEBUG_HURTBOXES 0
#endif

// Shared block-art renderer (arena, target, player, shells, effects, HUD). The
// exact same header is compiled into the on-device perf bench, so the numbers
// there describe this loop's real render path.
#include "src/render.hpp"

decltype(arduboy) arduboy;

// Single game state. The core is header-only and shared verbatim with the host
// tests; the device loop only samples input, steps it, and reads it for draw.
mh::Game g;

// Audio cue edge detector. Driven from run() after stepGame(); reads Game only
// (no core changes). Muted at compile time with -DMH_AUDIO=0.
mh::AudioState s_audio;

#if DEBUG_HURTBOXES
// Runtime toggle inside the debug build: hold A+B for 30 ticks to flip. The
// buttons still reach the sim unchanged (run() never consumes them); A+B is
// only *observed* here, so normal input cannot be eaten by the overlay.
static bool s_wire = true;
static uint8_t s_wireHold = 0;

static void pollDebugToggle(const mh::Input &in) {
    if (in.a && in.b) {
        if (s_wireHold < 30)
            s_wireHold++;
        if (s_wireHold == 30)
            s_wire = !s_wire;
    } else {
        s_wireHold = 0;
    }
}
#endif   // DEBUG_HURTBOXES

void setup() {
    // Serial.begin(9600);

    arduboy.boot();
    arduboy.startGray();
    // initRandomSeed() dropped: the core is fully deterministic and never calls
    // random(), so seeding only pulled the AVR random/random_r code into flash.

    FX::begin(FX_DATA_PAGE);
    FX::setCursorRange(0, 32767);

    mh::newGame(g, mh::W_SWORD, mh::MODE_HUNT);
}

// One logic tick. Called only from needsUpdate() (never mid-plane), so the
// whole core advances atomically between planes. pollButtons() already ran.
void run() {
    mh::Input in;
    in.mx = (arduboy.pressed(RIGHT_BUTTON) ? 1 : 0) - (arduboy.pressed(LEFT_BUTTON) ? 1 : 0);
    in.my = (arduboy.pressed(DOWN_BUTTON) ? 1 : 0) - (arduboy.pressed(UP_BUTTON) ? 1 : 0);
    in.a = arduboy.pressed(A_BUTTON);
    in.b = arduboy.pressed(B_BUTTON);
#if DEBUG_HURTBOXES
    pollDebugToggle(in);   // observes A+B; does not consume input from stepGame
#endif
    mh::stepGame(g, in);
    mh::audioUpdate(s_audio, g);
}

// Full block-art scene (arena, target, player, shells, effects, HUD). Read-only:
// render never mutates Game; the three plane passes composite one L4 image.
void render() {
#if DEBUG_HURTBOXES
    mh::renderScene(g, s_wire);
#else
    mh::renderScene(g, false);
#endif
}

void loop() {
    FX::enableOLED();
    arduboy.waitForNextPlane();
    FX::disableOLED();
    if (arduboy.needsUpdate()) {
        arduboy.pollButtons();
        run();
    }
    render();
}
