#pragma once
// On-device framebuffer suite for the per-attack telegraph shapes (bead
// monhun-ardu-feel.5).
//
// src/render.hpp is device-only (ArduboyG/SpritesU), so the host suite
// (tst/render_math_test.hpp) pins the pure tell geometry and this suite pins the
// actual arduboy.getBuffer() bytes drawMonsterTell() leaves. The render already
// owns the plane wait (hud_test pattern): select plane 0, clear the buffer, draw
// the tell, assert exact page bytes. No cart read happens in the draw.
//
// Scene is chosen so every rect lands on a known page: the monster body centre
// is screen (64,36), the window is 12x10 at facing offset (14,0), so the window
// centre is (78,36). Framebuffer layout is 128 B/page, pixel(x,y) =
// buf[(y >> 3) * 128 + x], bit y & 7; plane 0 lights every shade > 0.

#include "harness/fxtest.hpp"
#include "src/core/world.hpp"
#include "src/render.hpp"

#include <stdint.h>

namespace tell {

using namespace mh;

static void clearFb() {
    uint8_t *b = arduboy.getBuffer();
    for (uint16_t i = 0; i < 1024; i++)
        b[i] = 0;
}

// Count x in [xa, xb] whose page byte equals `want` (exact-byte pin).
static uint16_t countPage(uint8_t page, uint8_t xa, uint8_t xb, uint8_t want) {
    const uint8_t *b = arduboy.getBuffer() + static_cast<uint16_t>(page) * 128;
    uint16_t n = 0;
    for (uint8_t x = xa; x <= xb; x++) {
        if (b[x] == want)
            n++;
    }
    return n;
}

// Deterministic monster + cached window, body centre at screen (64,36).
static void primeTell(Game &g, uint8_t tell, uint8_t state, uint8_t windupMax, uint8_t t) {
    g.monster.w = 32;
    g.monster.h = 24;
    g.monster.fx = 16;
    g.monster.fy = 0;
    g.monster.atkIdx = 0;   // != COMBAT_NO_ATTACK
    g.monster.state = state;
    g.monster.windupMax = windupMax;
    g.monster.t = t;
    g.combat.attack.tell = tell;
    g.combat.attack.win.box.ox = 14;
    g.combat.attack.win.box.oy = 0;
    g.combat.attack.win.box.w = 12;
    g.combat.attack.win.box.h = 10;
}

static const int16_t kX = 48;   // monster top-left -> body centre (64,36)
static const int16_t kY = 24;

inline void test_tell(FxTest &test) {
    arduboy.startGray();
    while (arduboy.currentPlane() != 0) {
        FX::enableOLED();
        arduboy.waitForNextPlane();
        FX::disableOLED();
    }

    // DOT (tell 0): the legacy single 2x2 shade-2 core at the window centre
    // (78,36). One page, mask 0x18 at x=77..78.
    static Game g;
    primeTell(g, TELL_DOT, MS_WINDUP, 10, 10);
    clearFb();
    drawMonsterTell(g, kX, kY);
    test.expectEq(countPage(4, 77, 78, 0x18), 2, F("dot core bytes"));

    // LINE: 3 dashed 2x2 blocks at the Q2 fractions of the centre vector
    // (14,0) -> x 66, 70, 73 on rows 35..36. Six columns of 0x18, three empty.
    primeTell(g, TELL_LINE, MS_WINDUP, 10, 10);
    clearFb();
    drawMonsterTell(g, kX, kY);
    test.expectEq(countPage(4, 66, 74, 0x18), 6, F("line dash bytes"));
    test.expectEq(countPage(4, 66, 74, 0x00), 3, F("line dash gaps"));

    // ZONE: static outline of the 12x10 window at (72,31). Top/bottom rows are
    // single-bit pages; the side columns are solid through the middle page.
    primeTell(g, TELL_ZONE, MS_WINDUP, 10, 10);
    clearFb();
    drawMonsterTell(g, kX, kY);
    test.expectEq(countPage(3, 72, 83, 0x80), 12, F("zone top row"));
    test.expectEq(countPage(4, 72, 83, 0xFF), 2, F("zone side columns"));
    test.expectEq(countPage(4, 72, 83, 0x00), 10, F("zone hollow middle"));
    test.expectEq(countPage(5, 72, 83, 0x01), 12, F("zone bottom row"));

    // RING: expanding outline at elapsed 0 clamps to a 4x4 box at (76,34):
    // corners 0x3C, edges 0x24.
    primeTell(g, TELL_RING, MS_WINDUP, 10, 10);
    clearFb();
    drawMonsterTell(g, kX, kY);
    test.expectEq(countPage(4, 76, 79, 0x3C), 2, F("ring corners"));
    test.expectEq(countPage(4, 76, 79, 0x24), 2, F("ring edges"));

    // RING full windup: half-extents clamp to the window half (6,5) -> 12x10 at
    // (72,31), the same bounds ZONE draws.
    primeTell(g, TELL_RING, MS_WINDUP, 10, 0);
    clearFb();
    drawMonsterTell(g, kX, kY);
    test.expectEq(countPage(3, 72, 83, 0x80), 12, F("ring expanded top"));
    test.expectEq(countPage(4, 72, 83, 0xFF), 2, F("ring expanded sides"));
    test.expectEq(countPage(5, 72, 83, 0x01), 12, F("ring expanded bottom"));

    // ARC: 3x 4x2 segments across the box width: left/right rows 35..36 (0x18),
    // centre dropped to rows 37..38 (0x60) at x=77..79.
    primeTell(g, TELL_ARC, MS_WINDUP, 10, 10);
    clearFb();
    drawMonsterTell(g, kX, kY);
    test.expectEq(countPage(4, 72, 75, 0x18), 4, F("arc left segment"));
    test.expectEq(countPage(4, 80, 83, 0x18), 4, F("arc right segment"));
    test.expectEq(countPage(4, 76, 79, 0x60), 4, F("arc centre drop"));

    // Attack phase: the tell is replaced by the 4x4 shade-3 marker at the
    // window centre (76,34) regardless of tell; rows 34..37 -> 0x3C for x76..79.
    primeTell(g, TELL_RING, MS_ATTACK, 10, 0);
    clearFb();
    drawMonsterTell(g, kX, kY);
    test.expectEq(countPage(4, 76, 79, 0x3C), 4, F("attack 4x4 marker"));
    test.expectEq(countPage(4, 66, 74, 0x00), 9, F("attack phase skips tell"));
}

}   // namespace tell
