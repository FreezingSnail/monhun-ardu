#pragma once
// On-device pin for the DEBUG_HURTBOXES 1 px wireframe overlay (bead
// monhun-ardu-rie). The overlay (src/render.hpp drawDebug) is compiled in only
// when DEBUG_HURTBOXES=1, so this suite defines it before the render include
// and pins wireBox() geometry plus the renderScene() routing that ties the
// overlay to the scene. Both styles are frozen: hurt boxes are solid 1 px open
// squares, hit boxes are dotted (every other pixel) borders; shade 3; the
// interior is never written.
//
//   (a) solid wireBox(10,12,7,5,false): 20 perimeter pixels (2*7+2*5-4) lit,
//       every strictly-interior pixel clear, nothing outside the 7x5 bbox.
//   (b) dotted wireBox(20,12,7,5,true): x=20,22,24,26 lit on the top/bottom
//       edges, x=21,23,25 clear; y=12,14,16 lit on the left/right edges,
//       y=13,15 clear; interior clear.
//   (c) routing: renderScene(g,true) == renderScene(g,false) | drawDebug(g,0,0)
//       for all 1024 buffer bytes, and the overlay changes the scene (B1 !=
//       B2). The camera is pinned at (0,0) so renderScene's ecX/ecY match the
//       direct drawDebug call. current_plane only advances inside
//       waitForNextPlane(), so all three passes render on plane 0 (shade 3
//       sets bits there; OR is commutative, so order is irrelevant).
//
// Framebuffer layout (ArduboyG L4_Triplane): 128 B/page, pixel(x,y) =
// buf[(y>>3)*128 + x], bit y&7.

#define DEBUG_HURTBOXES 1

#include "harness/fxtest.hpp"
#include "src/core/world.hpp"
#include "src/render.hpp"

#include <stdint.h>

namespace wire {

using namespace mh;

static void clearFb() {
    uint8_t *b = arduboy.getBuffer();
    for (uint16_t i = 0; i < 1024; i++)
        b[i] = 0;
}

static uint8_t bitAt(uint8_t x, uint8_t y) {
    return static_cast<uint8_t>((arduboy.getBuffer()[static_cast<uint16_t>(y >> 3) * 128 + x] >> (y & 7)) & 1);
}

// Lit pixels over the whole 128x64 framebuffer.
static uint16_t countLit() {
    uint16_t n = 0;
    for (uint8_t y = 0; y < 64; y++)
        for (uint8_t x = 0; x < 128; x++)
            n += bitAt(x, y);
    return n;
}

// Lit pixels in the inclusive box [x0,x1] x [y0,y1].
static uint16_t countBox(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
    uint16_t n = 0;
    for (uint8_t y = y0; y <= y1; y++)
        for (uint8_t x = x0; x <= x1; x++)
            n += bitAt(x, y);
    return n;
}

inline void test_wire(FxTest &test) {
    arduboy.startGray();
    while (arduboy.currentPlane() != 0) {
        FX::enableOLED();
        arduboy.waitForNextPlane();
        FX::disableOLED();
    }

    // (a) solid border: all 20 perimeter pixels, interior and the rest clear.
    clearFb();
    wireBox(10, 12, 7, 5, false);
    test.expectEq(countBox(10, 12, 16, 16), 20, F("wire solid perimeter"));
    test.expectEq(countBox(11, 13, 15, 15), 0, F("wire solid interior clear"));
    test.expectEq(countLit(), 20, F("wire solid nothing outside bbox"));
    test.expectEq(bitAt(10, 12), 1, F("wire solid corner tl"));
    test.expectEq(bitAt(16, 16), 1, F("wire solid corner br"));
    test.expectEq(bitAt(13, 12), 1, F("wire solid top mid"));
    test.expectEq(bitAt(10, 14), 1, F("wire solid left mid"));

    // (b) dotted border: every other pixel on each edge, interior clear.
    clearFb();
    wireBox(20, 12, 7, 5, true);
    test.expectEq(countBox(20, 12, 26, 16), 10, F("wire dotted lit count"));
    test.expectEq(countBox(21, 13, 25, 15), 0, F("wire dotted interior clear"));
    test.expectEq(countLit(), 10, F("wire dotted nothing outside bbox"));
    // top/bottom edges (x=20+2i): even offsets lit, odd clear.
    test.expectEq(bitAt(20, 12), 1, F("wire dotted top x20 lit"));
    test.expectEq(bitAt(22, 12), 1, F("wire dotted top x22 lit"));
    test.expectEq(bitAt(24, 12), 1, F("wire dotted top x24 lit"));
    test.expectEq(bitAt(26, 12), 1, F("wire dotted top x26 lit"));
    test.expectEq(bitAt(21, 12), 0, F("wire dotted top x21 clear"));
    test.expectEq(bitAt(23, 12), 0, F("wire dotted top x23 clear"));
    test.expectEq(bitAt(25, 12), 0, F("wire dotted top x25 clear"));
    test.expectEq(bitAt(20, 16), 1, F("wire dotted bottom x20 lit"));
    test.expectEq(bitAt(22, 16), 1, F("wire dotted bottom x22 lit"));
    test.expectEq(bitAt(24, 16), 1, F("wire dotted bottom x24 lit"));
    test.expectEq(bitAt(26, 16), 1, F("wire dotted bottom x26 lit"));
    test.expectEq(bitAt(21, 16), 0, F("wire dotted bottom x21 clear"));
    // left/right edges (y=12+2j): even offsets lit, odd clear.
    test.expectEq(bitAt(20, 14), 1, F("wire dotted left y14 lit"));
    test.expectEq(bitAt(26, 14), 1, F("wire dotted right y14 lit"));
    test.expectEq(bitAt(20, 13), 0, F("wire dotted left y13 clear"));
    test.expectEq(bitAt(20, 15), 0, F("wire dotted left y15 clear"));
    test.expectEq(bitAt(26, 13), 0, F("wire dotted right y13 clear"));
    test.expectEq(bitAt(26, 15), 0, F("wire dotted right y15 clear"));

    // (c) routing. The test image is RAM-tight (a full 1024 B snapshot per
    // buffer overflows the 2560 B AVR RAM), so the equality is verified one
    // 128 B page at a time: B2, then B3 OR'd in (expected B1), then B1, all
    // re-rendered from a cleared framebuffer with current_plane pinned at 0.
    // B3 is plane-independent (drawDebug writes shade 3, which sets bits on
    // every plane), so a page renders identically regardless of pass order.
    static Game g;
    newGame(g, W_SWORD, MODE_HUNT);   // deterministic hunt: combat caches live, no fx
    g.camX = 0;                       // match renderScene's ecX/ecY
    g.camY = 0;
    g.fxN = 0;
    // The default spawn sits at y=60, whose debug box (y + HUD_H = 68) is off
    // the 64 px screen, so pin the hunter + beast on-screen for a non-empty
    // overlay; the equality is independent of where the boxes land.
    g.player.x = 40;
    g.player.y = 40;
    g.monster.x = 80;
    g.monster.y = 40;
    g.target.alive = true;

    static uint8_t expect[128];
    bool b3Ink = false;
    bool overlayAdds = false;
    uint16_t mismatch = 0;
    for (uint8_t page = 0; page < 8; page++) {
        const uint16_t base = static_cast<uint16_t>(page) * 128;

        // B2: scene only.
        clearFb();
        renderScene(g, false);
        for (uint16_t i = 0; i < 128; i++)
            expect[i] = arduboy.getBuffer()[base + i];

        // B3: overlay only; fold in -> expected B1, note overlay-added bits.
        clearFb();
        drawDebug(g, 0, 0);
        for (uint16_t i = 0; i < 128; i++) {
            const uint8_t b3 = arduboy.getBuffer()[base + i];
            if (b3 != 0)
                b3Ink = true;
            if (b3 & static_cast<uint8_t>(~expect[i]))
                overlayAdds = true;
            expect[i] |= b3;
        }

        // B1: scene + overlay; must equal B2|B3 exactly and differ from B2.
        clearFb();
        renderScene(g, true);
        for (uint16_t i = 0; i < 128; i++)
            if (arduboy.getBuffer()[base + i] != expect[i])
                mismatch++;
    }
    test.expectEq(b3Ink ? 1 : 0, 1, F("wire overlay draws"));
    test.expectEq(mismatch, 0, F("wire routing B1 == B2|B3"));
    test.expectEq(overlayAdds ? 1 : 0, 1, F("wire routing B1 != B2"));
}

}   // namespace wire
