#pragma once
// On-device framebuffer suite for the windup animation tells (bead
// monhun-ardu-feel.5; reworked for the prg.11 tell->animation carve).
//
// src/render.hpp is device-only (ArduboyG/SpritesU), so the host suite
// (tst/render_math_test.hpp) pins the pure frame selector (mh::tellWindupFrame)
// and this suite pins the actual arduboy.getBuffer() bytes drawAttackMarker()
// leaves. The render already owns the plane wait (hud_test pattern): select
// plane 0, clear the buffer, draw the marker, assert exact page bytes. No cart
// read happens in the draw.
//
// prg.11 replaced the procedural tell shapes with a frame selector: tells 1..3
// now select an authored windup pose on the beast sheets (prg.12) and draw no
// core marker, while tell 0 (DOT generic coil) and tell 4 (ZONE, beyond the
// authored set) fall back to the legacy 2x2 shade-2 core marker. MS_ATTACK keeps
// the 4x4 shade-3 marker.
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

    // Selector pin: prg.12 authored tells 1..3 (LINE/ARC/RING); DOT (0) is the
    // generic coil and ZONE (4) is beyond the authored set, so both fall back.
    test.expectEq(tellHasAuthoredFrame(TELL_DOT, TELL_FRAMES_AUTHORED), 0, F("dot no bespoke frame"));
    test.expectEq(tellHasAuthoredFrame(TELL_LINE, TELL_FRAMES_AUTHORED), 1, F("line authored"));
    test.expectEq(tellHasAuthoredFrame(TELL_ARC, TELL_FRAMES_AUTHORED), 1, F("arc authored"));
    test.expectEq(tellHasAuthoredFrame(TELL_RING, TELL_FRAMES_AUTHORED), 1, F("ring authored"));
    test.expectEq(tellHasAuthoredFrame(TELL_ZONE, TELL_FRAMES_AUTHORED), 0, F("zone unauthored"));
    test.expectEq(tellWindupFrame(TELL_RING, TELL_FRAMES_AUTHORED), TELL_RING, F("ring authored slot"));
    test.expectEq(tellWindupFrame(TELL_ZONE, TELL_FRAMES_AUTHORED), TELL_WINDUP_NONE, F("zone falls back"));

    // DOT (tell 0): the legacy single 2x2 shade-2 core at the window centre
    // (78,36). One page, mask 0x18 at x=77..78.
    static Game g;
    primeTell(g, TELL_DOT, MS_WINDUP, 10, 10);
    clearFb();
    drawAttackMarker(g, kX, kY);
    test.expectEq(countPage(4, 77, 78, 0x18), 2, F("dot core bytes"));

    // LINE (tell 1): authored -> the pose carries the read, no core marker.
    primeTell(g, TELL_LINE, MS_WINDUP, 10, 10);
    clearFb();
    drawAttackMarker(g, kX, kY);
    test.expectEq(countPage(4, 77, 78, 0x18), 0, F("line no core marker"));
    test.expectEq(countPage(4, 66, 83, 0x00), 18, F("line draws no marker"));

    // ARC (tell 2): authored -> no core marker.
    primeTell(g, TELL_ARC, MS_WINDUP, 10, 10);
    clearFb();
    drawAttackMarker(g, kX, kY);
    test.expectEq(countPage(4, 77, 78, 0x18), 0, F("arc no core marker"));

    // RING (tell 3): authored -> no core marker.
    primeTell(g, TELL_RING, MS_WINDUP, 10, 10);
    clearFb();
    drawAttackMarker(g, kX, kY);
    test.expectEq(countPage(4, 77, 78, 0x18), 0, F("ring no core marker"));
    test.expectEq(countPage(4, 66, 83, 0x00), 18, F("ring draws no outline"));

    // ZONE (tell 4): beyond the authored set -> the same core marker fallback.
    primeTell(g, TELL_ZONE, MS_WINDUP, 10, 10);
    clearFb();
    drawAttackMarker(g, kX, kY);
    test.expectEq(countPage(4, 77, 78, 0x18), 2, F("zone falls back to core"));
    test.expectEq(countPage(4, 72, 83, 0x00), 10, F("zone draws no outline"));

    // Attack phase: the 4x4 shade-3 marker at the window centre (76,34)
    // regardless of tell; rows 34..37 -> 0x3C for x76..79.
    primeTell(g, TELL_RING, MS_ATTACK, 10, 0);
    clearFb();
    drawAttackMarker(g, kX, kY);
    test.expectEq(countPage(4, 76, 79, 0x3C), 4, F("attack 4x4 marker"));
    test.expectEq(countPage(4, 66, 74, 0x00), 9, F("attack phase skips tell"));

    // No cached attack: nothing draws.
    primeTell(g, TELL_LINE, MS_WINDUP, 10, 10);
    g.monster.atkIdx = COMBAT_NO_ATTACK;
    clearFb();
    drawAttackMarker(g, kX, kY);
    test.expectEq(countPage(4, 66, 83, 0x00), 18, F("no attack draws nothing"));
}

}   // namespace tell
