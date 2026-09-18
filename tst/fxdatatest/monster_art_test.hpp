#pragma once
// On-device HEAVY long-tail overlay oracle (bead monhun-ardu-4t4).
//
// Renders the real drawMonster() through the shipping path (ArduboyG L4
// triplane, FX cart reads between plane blits) for a fixed HEAVY Game state and
// pins the tail pixels:
//
//   * east: the 24x16 fxtail_heavy overlay sits entirely LEFT of the 32x24 body
//     sprite (body at screen x=40 -> tail x=16..39), so ink at x=16 proves the
//     overlay is drawn offset at its appendage-zone anchor and x=15 stays clear
//   * the zone anchor is the same face-relative box the hit test uses
//     (combatFaceOffset), so the world rect and the hit zone cannot drift
//   * west: the mirrored frame lands on the RIGHT of the body (x=64..87) while
//     the east tail band is empty -- the facing mirror really flips
//   * broken: the east/west stub frames keep ink at the body end and clear the
//     tip, proving combatPartArtFrame() selects the stage frame
//   * LUNGE has no appendage zone, so its east band stays clear (no overlay)
//
// Framebuffer layout (ArduboyG L4_Triplane): 128 B/page, pixel(x,y) =
// buf[(y >> 3) * 128 + x], bit y & 7. Body draw origin: rndPx(m.x)+HUD_H.
//
// Plane 0 lights BLACK/DARK/LIGHT/WHITE (mask+data), so the overlay shapes are
// checked there; plane 2 lights white only (the tip cap / root highlight).

#include "harness/fxtest.hpp"
#include "src/core/world.hpp"
#include "src/render.hpp"

#include <stdint.h>

namespace monsterart {

using namespace mh;

// Monster cell draw origin for monster x=40, y=20, cam 0: (40, 20 + HUD_H=8).
constexpr int16_t BX = 40;
constexpr int16_t BY = 20 + HUD_H;   // 28
// East intact tail band (zone origin 40-24, 28) and the west mirror (40+24, 28).
constexpr int16_t E_TAIL_X = BX - 24;   // 16
constexpr int16_t W_TAIL_X = BX + 24;   // 64
constexpr int16_t TAIL_Y = BY;          // 28
constexpr int16_t TAIL_W = 24;
constexpr int16_t TAIL_H = 16;

static void clearFb() {
    uint8_t *b = arduboy.getBuffer();
    for (uint16_t i = 0; i < 1024; i++)
        b[i] = 0;
}

static uint8_t bitAt(uint8_t x, uint8_t y) {
    return static_cast<uint8_t>((arduboy.getBuffer()[static_cast<uint16_t>(y >> 3) * 128 + x] >> (y & 7)) & 1);
}

static uint16_t countRegionBit(uint8_t xa, uint8_t ya, uint8_t w, uint8_t h) {
    uint16_t n = 0;
    for (uint8_t y = ya; y < ya + h; y++)
        for (uint8_t x = xa; x < xa + w; x++)
            n += bitAt(x, y);
    return n;
}

// Park the real drawMonster() on one plane with the OLED parked off, exactly
// like the game loop (cart reads only while OLED is disabled).
static void renderMonster(const Game &g, uint8_t plane) {
    while (arduboy.currentPlane() != plane) {
        FX::enableOLED();
        arduboy.waitForNextPlane();
        FX::disableOLED();
    }
    clearFb();
    drawMonster(g, 0, 0);
}

static void setupBeast(Game &g, int8_t kind, int8_t fx, int8_t fy) {
    newGame(g, W_SWORD, MODE_HUNT, kind);
    Monster &m = g.monster;
    m.x = BX;
    m.y = 20;
    m.subX = 0;
    m.subY = 0;
    m.fx = fx;
    m.fy = fy;
    m.state = MS_IDLE;
    m.stun = 0;
    m.hitFlash = 0;
    m.atkIdx = COMBAT_NO_ATTACK;
    g.tick = 0;
    g.combat.zoneBroken = 0;
}

inline void test_monster_art(FxTest &test) {
    arduboy.startGray();

    static Game g;

    // ---- east intact: overlay offset left of the body, tip at x=16 row 37.
    setupBeast(g, MON_HEAVY, 16, 0);
    test.expectEq(g.combat.appendZone != COMBAT_NO_ZONE ? 1 : 0, 1, F("heavy has appendage zone"));
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 10, BY + 8), 1, F("body ink at cell"));
    test.expectEq(bitAt(E_TAIL_X, TAIL_Y + 9), 1, F("east tail tip plane0"));
    test.expectEq(bitAt(E_TAIL_X - 1, TAIL_Y + 9), 0, F("east tail offset clear one left"));
    test.expectEq(bitAt(E_TAIL_X + 23, TAIL_Y + 9), 1, F("east tail root plane0"));

    // ---- plane 2: the white tip cap and root highlight are drawn.
    renderMonster(g, 2);
    test.expectEq(bitAt(E_TAIL_X, TAIL_Y + 8), 1, F("east tail white cap plane2"));
    test.expectEq(bitAt(E_TAIL_X + 22, TAIL_Y + 5), 1, F("east tail root highlight plane2"));
    test.expectEq(bitAt(E_TAIL_X + 20, TAIL_Y + 9), 0, F("east tail body not white"));

    // ---- west intact: west frame on the right, east band empty.
    setupBeast(g, MON_HEAVY, -16, 0);
    renderMonster(g, 0);
    test.expectEq(bitAt(W_TAIL_X + 23, TAIL_Y + 9), 1, F("west tail tip plane0"));
    test.expectEq(bitAt(W_TAIL_X + 24, TAIL_Y + 9), 0, F("west tail offset clear one right"));
    test.expectEq(bitAt(W_TAIL_X, TAIL_Y + 9), 1, F("west tail root plane0"));
    test.expectEq(countRegionBit(static_cast<uint8_t>(E_TAIL_X), static_cast<uint8_t>(TAIL_Y), TAIL_W, TAIL_H), 0, F("west facing east band empty"));

    // ---- broken stages: stub at the body end, tip cleared.
    setupBeast(g, MON_HEAVY, 16, 0);
    g.combat.zoneBroken = COMBAT_ZONE_APPENDAGE_BIT;
    renderMonster(g, 0);
    test.expectEq(bitAt(E_TAIL_X, TAIL_Y + 9), 0, F("east broken tip cleared"));
    test.expectEq(bitAt(E_TAIL_X + 19, TAIL_Y + 8), 1, F("east broken stub plane0"));

    setupBeast(g, MON_HEAVY, -16, 0);
    g.combat.zoneBroken = COMBAT_ZONE_APPENDAGE_BIT;
    renderMonster(g, 0);
    test.expectEq(bitAt(W_TAIL_X + 23, TAIL_Y + 9), 0, F("west broken tip cleared"));
    test.expectEq(bitAt(W_TAIL_X + 3, TAIL_Y + 8), 1, F("west broken stub plane0"));

    // ---- LUNGE declares no appendage zone: no overlay, east band clear.
    setupBeast(g, MON_LUNGE, 16, 0);
    renderMonster(g, 0);
    test.expectEq(g.combat.appendZone, COMBAT_NO_ZONE, F("lunge has no appendage zone"));
    test.expectEq(countRegionBit(static_cast<uint8_t>(E_TAIL_X), static_cast<uint8_t>(TAIL_Y), TAIL_W, TAIL_H), 0, F("lunge no tail band"));
}

}   // namespace monsterart
