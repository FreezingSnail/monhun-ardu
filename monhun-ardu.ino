
#define ABG_IMPLEMENTATION
#define SPRITESU_IMPLEMENTATION
#include "src/common.hpp"
#include "src/globals.hpp"
#include "src/fxdata.h"
#include "src/core/world.hpp"

decltype(arduboy) arduboy;

// Single game state. The core is header-only and shared verbatim with the host
// tests; the device loop only samples input, steps it, and reads it for draw.
mh::Game g;

void setup() {
    // Serial.begin(9600);

    arduboy.boot();
    arduboy.startGray();
    arduboy.initRandomSeed();

    FX::begin(FX_DATA_PAGE);
    FX::setCursorRange(0, 32767);

    mh::newGame(g, mh::W_SWORD, mh::MODE_HUNT);
}

// One logic tick. Called only from needsUpdate() (never mid-plane), so the
// whole core advances atomically between planes. pollButtons() already ran.
void run() {
    mh::Input in;
    in.mx = (arduboy.pressed(RIGHT_BUTTON) ? 1 : 0)
          - (arduboy.pressed(LEFT_BUTTON)  ? 1 : 0);
    in.my = (arduboy.pressed(DOWN_BUTTON)  ? 1 : 0)
          - (arduboy.pressed(UP_BUTTON)    ? 1 : 0);
    in.a  = arduboy.pressed(A_BUTTON);
    in.b  = arduboy.pressed(B_BUTTON);
    mh::stepGame(g, in);
}

// Placeholder scene for this bead (art parity is monhun-ardu-rze): one player
// block, one target block, both offset by the camera. Identical shapes are
// drawn on every plane with no per-plane state change, so the ArduboyG triplane
// pipeline turns them into gray while logic stays parked between planes.
// Read-only: render never mutates Game.
void render() {
    const int16_t camX = g.camX;
    const int16_t camY = g.camY;

    const int16_t px = static_cast<int16_t>(g.player.x - camX);
    const int16_t py = static_cast<int16_t>(g.player.y - camY + mh::HUD_H);
    arduboy.fillRect(px, py, static_cast<uint8_t>(g.player.w),
                     static_cast<uint8_t>(g.player.h));

    if (g.target.alive) {
        const int16_t tx = static_cast<int16_t>(g.target.rect.x - camX);
        const int16_t ty = static_cast<int16_t>(g.target.rect.y - camY + mh::HUD_H);
        arduboy.fillRect(tx, ty, static_cast<uint8_t>(g.target.rect.w),
                         static_cast<uint8_t>(g.target.rect.h));
    }
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
