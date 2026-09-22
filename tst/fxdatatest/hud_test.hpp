#pragma once
// On-device HUD raster suite (bead monhun-ardu-7y3).
//
// Regression for the HUD-bar clamp bug: blk() clamped every rect to the arena
// band (y >= HUD_H == 8), so drawHud()'s divider (y=7), HP/stamina/monster bars
// (rows 2..5) and the gun reload bar (y=6) returned without painting. No
// headless screenshot exists (Ardens F2 is GUI-only), so this suite renders the
// real scene through renderScene() (same path the shipping loop and test_perf
// call) and pins actual arduboy.getBuffer() bytes:
//
//   plane 0 - every HUD shade (1..3) is "lit" there, so divider + bar backs +
//             bar fills + reload bar must all leave set bits in the buffer
//   plane 1 - shade 1 clears while shades 2..3 set, so the same render isolates
//             the bar fills from their dark backs (proves the fill draws)
//   negative - a world-path blk() rect anchored at y=0 must still clip at
//             y >= HUD_H (page 0 untouched, arena page painted)
//   positive - hudBlk() paints the same rect in rows 0..1
//
// Framebuffer layout (ArduboyG L4_Triplane): 128 B/page, pixel(x,y) =
// buf[(y >> 3) * 128 + x], bit y & 7. Expected masks are the same page masks
// blk() builds from MH_MASK_TOP/MH_MASK_BOT for the pinned rects.

#include "harness/fxtest.hpp"
#include "src/core/world.hpp"
#include "src/render.hpp"

#include <stdint.h>

namespace hud {

using namespace mh;

static void clearFb() {
    uint8_t *b = arduboy.getBuffer();
    for (uint16_t i = 0; i < 1024; i++)
        b[i] = 0;
}

static uint8_t bitAt(uint8_t x, uint8_t y) {
    return static_cast<uint8_t>((arduboy.getBuffer()[static_cast<uint16_t>(y >> 3) * 128 + x] >> (y & 7)) & 1);
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

// Count x in [xa, xb] with pixel (x, y) lit.
static uint16_t countRowBit(uint8_t y, uint8_t xa, uint8_t xb) {
    uint16_t n = 0;
    for (uint8_t x = xa; x <= xb; x++)
        n += bitAt(x, y);
    return n;
}

// Count lit pixels in the [y0,y1] x [x0,x1] box.
static uint16_t countBoxBits(uint8_t y0, uint8_t y1, uint8_t x0, uint8_t x1) {
    uint16_t n = 0;
    for (uint8_t y = y0; y <= y1; y++)
        n += countRowBit(y, x0, x1);
    return n;
}

inline void test_hud(FxTest &test) {
    arduboy.startGray();   // plane ISR drives waitForNextPlane (as in test_perf)

    // Deterministic scene: hunt + gun, full HP/stamina/monster HP. The gun's
    // reload lane was retired with the hitscan rework (no shells).
    static Game g;
    newGame(g, W_GUN, MODE_HUNT);
    g.player.hp = g.player.hpMax;
    g.player.stam = g.player.stamMax;
    g.monster.hp = g.monster.hpMax;

    // Land on plane 0 from a black framebuffer. Plane 0 lights every shade
    // 1..3, so each HUD shape must be visible as set bits.
    while (arduboy.currentPlane() != 0) {
        FX::enableOLED();
        arduboy.waitForNextPlane();
        FX::disableOLED();
    }
    clearFb();
    renderScene(g, false);

    // Divider: blk(0, 7, 128, 1, 1) -> row 7 lit across the whole strip.
    test.expectEq(countRowBit(7, 0, 127), 128, F("hud divider plane0"));
    // HP bar: hudBar(1, 2, 28, 4) -> back+fill rows 2..5 (0x3C) plus the
    // divider row 7 (0x80) -> 0xBC for x=1..28.
    test.expectEq(countPage(0, 1, 28, 0xBC), 28, F("hud hp bar plane0"));
    // Stamina bar: hudBar(29, 2, 16, 4) -> 0xBC for x=29..44.
    test.expectEq(countPage(0, 29, 44, 0xBC), 16, F("hud stam bar plane0"));
    // Monster bar (hunt): hudBar(82, 2, 44, 3) -> rows 2..4 (0x1C) + divider
    // (0x80) -> 0x9C for x=82..125.
    test.expectEq(countPage(0, 82, 125, 0x9C), 44, F("hud mon bar plane0"));
    // Retired gun reload lane: row 6 x=67..72 stays clear (no reload bar).
    test.expectEq(countRowBit(6, 67, 72), 0, F("no reload bar plane0"));

    // Plane 1: shade 1 clears, shades 2/3 set. The bar BACKS (shade 1) now
    // clear their area and only the FILLS light pixels, proving the fill blk()
    // ran; the divider (shade 1) and back edges stay clear.
    FX::enableOLED();
    arduboy.waitForNextPlane();
    FX::disableOLED();
    test.expectEq(arduboy.currentPlane(), 1, F("hud plane1 reached"));
    renderScene(g, false);

    // HP fill: blk(2, 3, 26, 2, 3) -> rows 3..4 only -> 0x18 for x=2..27.
    test.expectEq(countPage(0, 2, 27, 0x18), 26, F("hud hp fill plane1"));
    // Stamina fill: blk(30, 3, 14, 2, 2) -> 0x18 for x=30..43.
    test.expectEq(countPage(0, 30, 43, 0x18), 14, F("hud stam fill plane1"));
    // Monster fill (hunt): blk(83, 3, 42, 1, 3) -> 0x08 for x=83..124.
    test.expectEq(countPage(0, 83, 124, 0x08), 42, F("hud mon fill plane1"));
    // Back-only edge columns and the divider are cleared on plane 1.
    test.expectEq(bitAt(1, 3), 0, F("hud hp back edge plane1"));
    test.expectEq(bitAt(28, 3), 0, F("hud hp back edge2 plane1"));
    test.expectEq(countRowBit(7, 0, 127), 0, F("hud divider plane1"));
    // Retired reload lane: clear on plane 1 too.
    test.expectEq(countRowBit(6, 67, 72), 0, F("no reload plane1"));

    // Negative control: the world path (blk) still clips at y >= HUD_H. A
    // 20x16 rect anchored at y=0 leaves page 0 (rows 0..7) untouched and paints
    // the arena page 1 (rows 8..15) solid.
    clearFb();
    blk(10, 0, 20, 16, 3);
    test.expectEq(countPage(0, 10, 29, 0x00), 20, F("world blk no hud spill"));
    test.expectEq(countPage(1, 10, 29, 0xFF), 20, F("world blk paints arena"));
    // Positive control for the new HUD path: rows 0..1 are paintable there.
    clearFb();
    hudBlk(10, 0, 20, 2, 3);
    test.expectEq(countPage(0, 10, 29, 0x03), 20, F("hudBlk paints hud rows"));
    test.expectEq(countPage(1, 10, 29, 0x00), 20, F("hudBlk no arena paint"));

    // ---- feel.22: herb count + rooted-action progress (still on plane 1) ----
    // Herb indicator lives in the free 5 px lane x=62..66 (after the 16-wide
    // weapon marker at 46..61, before the gun text at 67). 0 herbs draws
    // nothing there; a held herb draws the 1 px plant glyph + one digit.
    g.items[ITEM_HERB] = 0;
    g.player.state = PS_IDLE;
    clearFb();
    renderScene(g, false);
    test.expectEq(countBoxBits(1, 6, 62, 66), 0, F("herb region clear at 0 herbs"));

    g.items[ITEM_HERB] = 8;
    clearFb();
    renderScene(g, false);
    test.expectEq(countBoxBits(1, 6, 62, 66) > 0 ? 1 : 0, 1, F("herb region ink at 8 herbs"));
    test.expectEq(bitAt(62, 2), 1, F("herb icon bud plane1"));
    test.expectEq(bitAt(62, 3), 1, F("herb icon stem plane1"));

    // Rooted-action bar: 32x4 at (48,12), fill x49..78 rows 13..14. On plane 1
    // the shade-1 back clears and only the shade-3/shade-2 fill lights, so the
    // 30-wide fill is an exact pin (procedural arena dots are shade 1).
    g.player.state = PS_GATHER;
    g.player.t = GATHER_TICKS;
    clearFb();
    renderScene(g, false);
    test.expectEq(countRowBit(13, 49, 78), 30, F("gather bar fill plane1"));
    test.expectEq(bitAt(48, 13), 0, F("gather bar left back edge plane1"));
    test.expectEq(bitAt(79, 13), 0, F("gather bar right back edge plane1"));

    g.player.state = PS_ITEM;
    g.player.t = ITEM_USE_TICKS;
    clearFb();
    renderScene(g, false);
    test.expectEq(countRowBit(13, 49, 78), 30, F("item bar fill plane1"));
    g.player.state = PS_IDLE;
    g.items[ITEM_HERB] = 0;

    // ---- gun rework: arrowshot nock hint in the retired shell lane ----
    // Ready draws "RDY" (rows 1..5) with no row-6 fill; mid-nock draws "LOD"
    // plus a 1 px fill bar. reload 12 -> w = (8*12 + 12)/24 = 4.
    g.player.reload = 0;
    clearFb();
    renderScene(g, false);
    test.expectEq(countBoxBits(1, 5, 67, 78) > 0 ? 1 : 0, 1, F("nock RDY text plane1"));
    test.expectEq(countRowBit(6, 67, 78), 0, F("nock ready no fill"));

    g.player.reload = ARROW_NOCK_TICKS / 2;
    clearFb();
    renderScene(g, false);
    test.expectEq(countRowBit(6, 67, 70), 4, F("nock LOD fill plane1"));
    test.expectEq(countBoxBits(1, 5, 67, 78) > 0 ? 1 : 0, 1, F("nock LOD text plane1"));
    g.player.reload = 0;
}

}   // namespace hud
