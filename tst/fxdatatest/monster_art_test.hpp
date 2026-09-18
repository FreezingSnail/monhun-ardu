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

// nch.3: park HEAVY in the locked tail_spin ACTIVE phase so drawMonster uses
// the rotating 8-frame fxtailspin body sheet (frame = dir8(lock facing) +
// t*8/active). The cached window box is shrunk to 1x1 at the body centre (ox/oy
// zeroed) so the 4x4 telegraph core sits inside the centre band the quadrant
// checks exclude; `active` is pinned to 20 so t maps to known frames.
static void setupSpinAttack(Game &g, int8_t fx, int16_t t) {
    setupBeast(g, MON_HEAVY, fx, 0);
    Monster &m = g.monster;
    m.state = MS_ATTACK;
    m.t = t;
    m.atkIdx = combat::ATTACK_HEAVY_TAIL_SPIN;
    g.combat.attack.facing = COMBAT_FACING_LOCK_AWAY;
    g.combat.attack.active = 20;
    g.combat.attack.win = combatWindowRead(combat::WINDOW_HEAVY_TAIL_SPIN_0);
    g.combat.attack.win.box.ox = 0;
    g.combat.attack.win.box.oy = 0;
    g.combat.attack.win.box.w = 1;
    g.combat.attack.win.box.h = 1;
}

// nch.2: park HEAVY in the locked tail_spin WINDUP with the FULL cached window
// (no shrink). The spin tell now draws in windup too, and the window box fill is
// gone, so a full-size box must not erase the plane-2 tip cap or paint any fill.
static void setupSpinWindup(Game &g, uint8_t window, int8_t fx) {
    setupBeast(g, MON_HEAVY, fx, 0);
    Monster &m = g.monster;
    m.state = MS_WINDUP;
    m.atkIdx = combat::ATTACK_HEAVY_TAIL_SPIN;
    m.windupMax = 42;
    m.t = 38;   // (windupMax - t) / 4 == 1 -> odd -> no windup flash
    g.combat.attack.facing = COMBAT_FACING_LOCK_AWAY;
    g.combat.attack.win = combatWindowRead(window);   // full box, no shrink
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

    // ---- LUNGE declares a legs appendage zone (76y) but ships no overlay art
    // (render only draws HEAVY's tail): east band stays clear.
    setupBeast(g, MON_LUNGE, 16, 0);
    renderMonster(g, 0);
    test.expectEq(g.combat.appendZone != COMBAT_NO_ZONE ? 1 : 0, 1, F("lunge has a legs appendage zone"));
    test.expectEq(countRegionBit(static_cast<uint8_t>(E_TAIL_X), static_cast<uint8_t>(TAIL_Y), TAIL_W, TAIL_H), 0, F("lunge no overlay band"));

    // ---- nch.3 rotating spin body: during the locked tail_spin ACTIVE phase
    // drawMonster swaps the 32x24 E/W beast sheet for the 8-frame 40x40
    // fxtailspin sheet, centred on the body centre (60,42) with cell origin
    // (40,22). Frame = dir8(lock facing) + t*8/active, so east-facing (start8 0)
    // with active 20 maps t=2 -> frame0, t=5 -> frame2 (head bottom), t=10 ->
    // frame4 (head left), t=15 -> frame6 (head top). The WHITE head orbited
    // through the quadrants is the rotation signature (host suite pins the
    // sheet; these checks prove the device frame pick + centring).
    setupSpinAttack(g, 16, 2);   // frame0: head right
    renderMonster(g, 2);
    test.expectEq(countRegionBit(64, 22, 16, 40) > 0 ? 1 : 0, 1, F("spin t2 head right"));
    test.expectEq(countRegionBit(40, 22, 16, 40), 0, F("spin t2 head not left"));

    setupSpinAttack(g, 16, 5);   // frame2: head bottom
    renderMonster(g, 2);
    test.expectEq(countRegionBit(40, 46, 40, 16) > 0 ? 1 : 0, 1, F("spin t5 head bottom"));
    test.expectEq(countRegionBit(40, 22, 40, 16), 0, F("spin t5 head not top"));

    setupSpinAttack(g, 16, 10);   // frame4: head left
    renderMonster(g, 2);
    test.expectEq(countRegionBit(40, 22, 16, 40) > 0 ? 1 : 0, 1, F("spin t10 head left"));
    test.expectEq(countRegionBit(64, 22, 16, 40), 0, F("spin t10 head not right"));

    setupSpinAttack(g, 16, 15);   // frame6: head top
    renderMonster(g, 2);
    test.expectEq(countRegionBit(40, 22, 40, 16) > 0 ? 1 : 0, 1, F("spin t15 head top"));
    test.expectEq(countRegionBit(40, 46, 40, 16), 0, F("spin t15 head not bottom"));

    // The old 4-frame tail overlay is skipped in the attack phase and the
    // resting tail_heavy cap stays clear too (the rotating sheet carries it).
    test.expectEq(bitAt(48, 41), 0, F("spin attack old overlay cap clear"));
    test.expectEq(bitAt(E_TAIL_X, TAIL_Y + 8), 0, F("spin attack skips resting tail cap"));

    // ---- nch.2 windup tell: the spin overlay draws during MS_WINDUP too, and
    // the window box fill is gone. Window 1 (oy -22, north frame) is full size:
    // its box spans x52..67, y8..31, so the old shade-1 windup fill would ink
    // the x52..67 y10..17 region on at least one plane. It must stay clear on
    // every plane while the white spin cap survives at the body centre.
    setupSpinWindup(g, combat::WINDOW_HEAVY_TAIL_SPIN_1, 16);   // north frame
    uint16_t fillBits = 0;
    for (uint8_t plane = 0; plane < 3; plane++) {
        renderMonster(g, plane);
        fillBits = static_cast<uint16_t>(fillBits + countRegionBit(52, 10, 16, 8));
    }
    test.expectEq(fillBits, 0, F("windup no window box fill"));
    renderMonster(g, 2);
    test.expectEq(bitAt(59, 30), 1, F("windup spin north cap plane2"));
    test.expectEq(bitAt(59, 52), 0, F("windup north south cap clear"));
    // The resting tail_heavy overlay is skipped during the windup spin too.
    test.expectEq(bitAt(E_TAIL_X, TAIL_Y + 8), 0, F("windup spin skips resting tail cap"));
}

}   // namespace monsterart
