#pragma once
// On-device menu raster suite (bead monhun-ardu-2u8).
//
// No headless screenshot exists (Ardens F2 is GUI-only), so this suite renders
// the real drawMenu() through the shipping render path and pins the actual
// arduboy.getBuffer() bytes, hud_test style:
//
//   plane 0 - the selected option's bright 1 px frame and cursor arrow must
//             light the exact tile whose pick is current, and only that tile
//   plane 2 - white (sel) sets while light gray (bg) clears, so the selected
//             weapon/target name lights and every unselected copy clears,
//             proving the bright frame + dim options composite as designed
//   names   - every option name lane carries ink (plane 0 dim / plane 2 sel)
//             while the v2 icon slot is clear on both planes (bead 4t4 made
//             the menu name-only: no icons)
//
// Framebuffer layout (ArduboyG L4_Triplane): 128 B/page, pixel(x,y) =
// buf[(y >> 3) * 128 + x], bit y & 7.

#include "harness/fxtest.hpp"
#include "src/menu.hpp"

#include <stdint.h>

namespace menuart {

using namespace mh;

// Screen geometry (mirrors tools/gen-art.py MENU_* / src/menu.hpp).
constexpr int16_t W_X = 24;
constexpr int16_t W_STEP = 34;
constexpr int16_t W_Y = 11;
constexpr int16_t M_DX = 64;
constexpr int16_t M_Y = 26;
constexpr int16_t M_DY = 8;
constexpr int16_t FRAME_X = 4;
constexpr int16_t TILE_W = 32;
constexpr int16_t MTILE_W = 64;
constexpr int16_t ICON_X = 6;
constexpr int16_t ICON_Y = 1;
constexpr int16_t M_ICON_W = 12;
constexpr int16_t ICON_H = 6;

// Cell origin for target t (same arithmetic as drawMenu; no lookup arrays).
static int16_t monX(uint8_t t) {
    return (t & 1) ? M_DX : 0;
}
static int16_t monY(uint8_t t) {
    return static_cast<int16_t>(M_Y + (t >> 1) * M_DY);
}

static void clearFb() {
    uint8_t *b = arduboy.getBuffer();
    for (uint16_t i = 0; i < 1024; i++)
        b[i] = 0;
}

// Wait until the given plane is current, parking the OLED off between blits so
// the cart reads (FX seek in drawMenu) are safe, exactly like the game loop.
static void gotoPlane(uint8_t p) {
    while (arduboy.currentPlane() != p) {
        FX::enableOLED();
        arduboy.waitForNextPlane();
        FX::disableOLED();
    }
}

static uint8_t bitAt(uint8_t x, uint8_t y) {
    return static_cast<uint8_t>((arduboy.getBuffer()[static_cast<uint16_t>(y >> 3) * 128 + x] >> (y & 7)) & 1);
}

static uint16_t countRowBit(uint8_t y, uint8_t xa, uint8_t xb) {
    uint16_t n = 0;
    for (uint8_t x = xa; x <= xb; x++)
        n += bitAt(x, y);
    return n;
}

static uint16_t countColBit(uint8_t x, uint8_t ya, uint8_t yb) {
    uint16_t n = 0;
    for (uint8_t y = ya; y <= yb; y++)
        n += bitAt(x, y);
    return n;
}

static uint16_t countRegionBit(uint8_t xa, uint8_t ya, uint8_t w, uint8_t h) {
    uint16_t n = 0;
    for (uint8_t y = ya; y < ya + h; y++)
        for (uint8_t x = xa; x < xa + w; x++)
            n += bitAt(x, y);
    return n;
}

inline void test_menu_art(FxTest &test) {
    arduboy.startGray();
    gotoPlane(0);

    // ---- selection A: SWD + CHICKEN. Plane 0 lights every shade, so the
    // white frame and cursor must appear on the picked tiles and nowhere else.
    MenuState a;
    clearFb();
    drawMenu(a);
    // Weapon frame top row (local row 0) is local x 4..31 -> screen 28..55.
    test.expectEq(countRowBit(W_Y, W_X + FRAME_X, W_X + TILE_W - 1), TILE_W - FRAME_X, F("wsel A frame0 top"));
    test.expectEq(countRowBit(W_Y, W_X + W_STEP + FRAME_X, W_X + W_STEP + TILE_W - 1), 0, F("wsel A frame1 clear"));
    test.expectEq(countRowBit(W_Y, W_X + 2 * W_STEP + FRAME_X, W_X + 2 * W_STEP + TILE_W - 1), 0, F("wsel A frame2 clear"));
    // Cursor arrow left column (local x 0) is local rows 2..6 -> screen 13..17.
    test.expectEq(countColBit(W_X, W_Y + 2, W_Y + 6), 5, F("wsel A cursor0"));
    test.expectEq(countColBit(W_X + W_STEP, W_Y + 2, W_Y + 6), 0, F("wsel A cursor1 clear"));
    // Target frame top row (local row 0) is local x 4..63 -> screen 4..63.
    test.expectEq(countRowBit(monY(0), monX(0) + FRAME_X, monX(0) + MTILE_W - 1), MTILE_W - FRAME_X, F("msel A frame0 top"));
    test.expectEq(countRowBit(monY(0), monX(1) + FRAME_X, monX(1) + MTILE_W - 1), 0, F("msel A frame1 clear"));
    test.expectEq(countColBit(monX(0), monY(0) + 2, monY(0) + 6), 5, F("msel A cursor0"));
    test.expectEq(countColBit(monX(1), monY(0) + 2, monY(0) + 6), 0, F("msel A cursor1 clear"));
    // Unselected tiles stay dim: their name cells carry ink on plane 0 (light
    // gray is lit there) but are erased on plane 2 (checked below). Every
    // option is name-only now, so the old v2 icon slot stays clear.
    for (uint8_t i = 0; i < MENU_WEAPON_COUNT; i++)
        test.expectEq(countRegionBit(W_X + i * W_STEP + 15, W_Y + 2, 12, 5) > 0 ? 1 : 0, 1, F("wsel dim name has ink"));
    test.expectEq(countRegionBit(W_X + W_STEP + ICON_X, W_Y + ICON_Y, 8, ICON_H), 0, F("wsel A icon slot clear"));
    for (uint8_t t = 0; t < MENU_TARGET_COUNT; t++) {
        test.expectEq(countRegionBit(monX(t) + 19, monY(t) + 2, 32, 5) > 0 ? 1 : 0, 1, F("msel dim name has ink"));
        test.expectEq(countRegionBit(monX(t) + ICON_X, monY(t) + ICON_Y, M_ICON_W, ICON_H), 0, F("msel icon slot clear"));
    }

    // ---- plane 2: white sel sets, light bg clears. The selected names light
    // and every unselected option's bg copy is erased.
    gotoPlane(2);
    clearFb();
    drawMenu(a);
    test.expectEq(countRegionBit(W_X + 15, W_Y + 2, 12, 5) > 0 ? 1 : 0, 1, F("plane2 sel SWD name bright"));
    test.expectEq(countRegionBit(W_X + W_STEP + 15, W_Y + 2, 12, 5), 0, F("plane2 dim FLS name cleared"));
    test.expectEq(countRegionBit(W_X + 2 * W_STEP + 15, W_Y + 2, 12, 5), 0, F("plane2 dim GUN name cleared"));
    test.expectEq(countRegionBit(monX(0) + 19, monY(0) + 2, 28, 5) > 0 ? 1 : 0, 1, F("plane2 sel CHICKEN name bright"));
    test.expectEq(countRegionBit(monX(1) + 19, monY(1) + 2, 16, 5), 0, F("plane2 dim BULL name cleared"));
    test.expectEq(countRegionBit(monX(2) + 19, monY(2) + 2, 32, 5), 0, F("plane2 dim LONGTAIL name cleared"));
    test.expectEq(countRegionBit(monX(3) + 19, monY(3) + 2, 28, 5), 0, F("plane2 dim RAVAGER name cleared"));
    // The selected frame stays bright on plane 2 (white sets there).
    test.expectEq(countRowBit(W_Y, W_X + FRAME_X, W_X + TILE_W - 1), TILE_W - FRAME_X, F("plane2 sel weapon frame"));
    test.expectEq(countRowBit(monY(0), monX(0) + FRAME_X, monX(0) + MTILE_W - 1), MTILE_W - FRAME_X, F("plane2 sel target frame"));

    // ---- selection B: GUN + RAVAGER (target 3, last grid cell). Both frames
    // and cursors must move to the last option tile.
    MenuState b;
    b.weapon = MENU_WEAPON_COUNT - 1;
    b.target = MENU_TARGET_COUNT - 1;
    gotoPlane(0);
    clearFb();
    drawMenu(b);
    test.expectEq(countRowBit(W_Y, W_X + FRAME_X, W_X + TILE_W - 1), 0, F("wsel B frame0 clear"));
    test.expectEq(countRowBit(W_Y, W_X + 2 * W_STEP + FRAME_X, W_X + 2 * W_STEP + TILE_W - 1), TILE_W - FRAME_X, F("wsel B frame2 top"));
    test.expectEq(countColBit(W_X + 2 * W_STEP, W_Y + 2, W_Y + 6), 5, F("wsel B cursor2"));
    test.expectEq(countRowBit(monY(3), monX(3) + FRAME_X, monX(3) + MTILE_W - 1), MTILE_W - FRAME_X, F("msel B ravager frame"));
    test.expectEq(countRowBit(monY(0), monX(0) + FRAME_X, monX(0) + MTILE_W - 1), 0, F("msel B row0 clear"));
    test.expectEq(countRowBit(monY(1), monX(0) + FRAME_X, monX(0) + MTILE_W - 1), 0, F("msel B row1 clear"));
    test.expectEq(countColBit(monX(3), monY(3) + 2, monY(3) + 6), 5, F("msel B ravager cursor"));

    // ---- name-only (bead 4t4): select every target in turn, prove its white
    // name lane is drawn, its bright frame is present and the old v2 icon slot
    // stays clear on both the selected plane 2 and the dim base plane 0.
    for (uint8_t t = 0; t < MENU_TARGET_COUNT; t++) {
        MenuState m;
        m.target = t;
        gotoPlane(2);
        clearFb();
        drawMenu(m);
        test.expectEq(countRegionBit(monX(t) + 19, monY(t) + 2, 32, 5) > 0 ? 1 : 0, 1, F("target sel name bright"));
        test.expectEq(countRowBit(monY(t), monX(t) + FRAME_X, monX(t) + MTILE_W - 1), MTILE_W - FRAME_X, F("target sel frame"));
        test.expectEq(countRegionBit(monX(t) + ICON_X, monY(t) + ICON_Y, M_ICON_W, ICON_H), 0, F("target icon slot clear"));
        gotoPlane(0);
        clearFb();
        drawMenu(m);
        test.expectEq(countRegionBit(monX(t) + ICON_X, monY(t) + ICON_Y, M_ICON_W, ICON_H), 0, F("target icon slot clear dim"));
    }
}

}   // namespace menuart
