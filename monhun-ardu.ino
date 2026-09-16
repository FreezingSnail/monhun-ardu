
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
#include "src/menu.hpp"   // draws through render.hpp (textPut/blk) + MenuState

decltype(arduboy) arduboy;

// Single game state. The core is header-only and shared verbatim with the host
// tests; the device loop only samples input, steps it, and reads it for draw.
mh::Game g;

// Opening menu (bead monhun-ardu-6zb.2): boot lands here. While active it owns
// every input edge; the sim and audio are not stepped. A starts the chosen
// scene, and after a win/lose the same state re-opens with the picks kept.
mh::MenuState s_menu;

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

// One input sample per logic tick, shared by the menu and the sim. The menu
// owns its own edge flags (MenuState::prevA/prevB); while the sim runs,
// menuReturnStep() keeps those same flags current, so the post-game A edge
// needs no second edge rule here.
static mh::Input sampleInput() {
    mh::Input in;
    in.mx = (arduboy.pressed(RIGHT_BUTTON) ? 1 : 0) - (arduboy.pressed(LEFT_BUTTON) ? 1 : 0);
    in.my = (arduboy.pressed(DOWN_BUTTON) ? 1 : 0) - (arduboy.pressed(UP_BUTTON) ? 1 : 0);
    in.a = arduboy.pressed(A_BUTTON);
    in.b = arduboy.pressed(B_BUTTON);
    return in;
}

// One logic tick. Called only from needsUpdate() (never mid-plane), so the
// whole core advances atomically between planes. pollButtons() already ran.
void run() {
    const mh::Input in = sampleInput();
#if DEBUG_HURTBOXES
    pollDebugToggle(in);   // observes A+B; does not consume input from stepGame
#endif
    if (s_menu.active) {
        // Menu tick: no stepGame, no audio (the new game re-latches the audio
        // snapshot on its tick 0). A starts the picked loadout and drops out.
        if (mh::menuStep(s_menu, in) == mh::MENU_START) {
            mh::menuStart(g, s_menu);
            s_menu.active = false;
        }
        return;
    }
    mh::stepGame(g, in);
    mh::audioUpdate(s_audio, g);
    if (mh::menuReturnStep(s_menu, g.over != mh::OVER_NONE, in))
        s_menu.active = true;   // picks preserved until reboot
}

// Full block-art scene (arena, target, player, shells, effects, HUD). Read-only:
// render never mutates Game; the three plane passes composite one L4 image.
// While the menu is up it replaces the scene (same per-plane call discipline).
void render() {
    if (s_menu.active) {
        mh::drawMenu(s_menu);
        return;
    }
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
