#pragma once
// Opening-menu render (bead monhun-ardu-6zb.2; menu v2 icons/frame by
// monhun-ardu-2u8). The layout is baked to FX sheets by tools/gen-art.py so the
// glyph pixels are provably the font glyphs and the option icons are provably
// reductions of the shipped beast/pole sheets (check_menu_identity).
//   mh_menu_bg    128x64  one frame: title/labels/footer + the DIM (light-gray)
//                         option icons and names
//   mh_menu_wsel  32x8    3 tiles (weapon 0..2): white icon+name, bright 1 px
//                         frame and the 3x5 cursor arrow
//   mh_menu_msel  64x8    5 tiles (target 0..4): same, 12 px monster icons
// FRAME(i) selects the current plane's copy, so one draw call per plane
// composites the menu exactly like renderScene(). Called once per plane while
// the menu is up, between ArduboyG's plane blits (never during the paint).
//
// Layout (screen px; mirrors the MENU_* geometry in tools/gen-art.py):
//   y=2          MONHUN DEMO            (title, white, baked in the bg)
//   y=13 WEAPON  [SWD] [FLS] [GUN]      (weapon row, 32 px tiles at x=24+34i)
//   y=22 MONSTER
//   y=29 [CHICKEN] [BULL]               (2 cols at x=0/64, rows 29/38/47)
//   y=38 [LONGTAIL] [RAVAGER]
//   y=47 [POLE]
//   y=56 A HUNT                         (footer)
// The bg carries the light-gray options; the selected weapon and target are
// each covered by their white sel tile (icon + name + bright frame + cursor),
// so unselected options stay dim and the two picks stay distinct.

#include "render.hpp"
#include "menu_state.hpp"

namespace mh {

constexpr int16_t MENU_WEAPON_X = 24;
constexpr int16_t MENU_WEAPON_STEP = 34;
constexpr int16_t MENU_WEAPON_Y = 11;
// Target grid: 2 columns (x=0/64), rows 29/38/47 (9 px pitch). Plain arithmetic
// instead of lookup arrays: a non-PROGMEM const int16 array would cost RAM.
constexpr int16_t MENU_MON_DX = 64;
constexpr int16_t MENU_MON_Y = 29;
constexpr int16_t MENU_MON_DY = 9;

// Full menu, one call per plane (mirrors renderScene's plane discipline).
static void drawMenu(const MenuState &m) {
    sprDraw(mh_menu_bg, 0, 0, FRAME(0));

    // Weapon row: one 32 px tile per weapon, 34 px apart.
    const uint8_t w = static_cast<uint8_t>(m.weapon);
    sprDraw(mh_menu_wsel, static_cast<int16_t>(MENU_WEAPON_X + w * MENU_WEAPON_STEP), MENU_WEAPON_Y, FRAME(w));

    // Target order is CHICKEN/BULL/LONGTAIL/RAVAGER/POLE: column = t & 1,
    // row = t >> 1 (0,1,2 -> y 29/38/47).
    const uint8_t t = static_cast<uint8_t>(m.target);
    const int16_t tx = (t & 1) ? MENU_MON_DX : 0;
    sprDraw(mh_menu_msel, tx, static_cast<int16_t>(MENU_MON_Y + (t >> 1) * MENU_MON_DY), FRAME(t));
}

}   // namespace mh
