#pragma once
// Opening-menu render (bead monhun-ardu-6zb.2; baked to FX art by
// monhun-ardu-zza). The old MCU-side layout (textPut on fxfontw/fxfontg + the
// blk() underline) is baked into two FX sheets authored by tools/gen-art.py
// from the same GLYPHS table as the font sheets, so the pixels are identical:
//   mh_menu_bg  128x64  one frame: static content (title/labels/options/footer)
//   mh_menu_sel 28x16   8 tiles (weapons 0..2, targets 0..4): white glyphs at
//                       local (0,0) + the white len*4-1 underline on row 9
// FRAME(i) selects the current plane's copy, so one draw call per plane
// composites the menu exactly like renderScene(). Called once per plane while
// the menu is up, between ArduboyG's plane blits (never during the paint).
//
// Layout (glyph lane = 4 px advance; "LUNGE" is the widest option at 20 px):
//   y=10         MONHUN DEMO            (title, white, baked in the bg)
//   y=22  WEAPON SWD  FLS  GUN          (selected white + white underline)
//   y=34  TARGET LUNGE SWEEP HEAVY      (targets wrap onto a second row so the
//   y=44         RAVAGER POLE           4th beast + pole fit 128 px)
//   y=56  A HUB                        (footer hint; A opens the hub, qs.4)
// The bg carries the light-gray option glyphs; the selected option is covered
// by its white sel tile, so the composite matches the old per-glyph draw.

#include "render.hpp"
#include "menu_state.hpp"

namespace mh {

constexpr int16_t MENU_OPT_X = 36;   // past the 6-glyph label lane
constexpr int16_t MENU_WEAPON_Y = 22;
constexpr int16_t MENU_TARGET_Y = 34;
constexpr int16_t MENU_TARGET_Y2 = 44;

// Full menu, one call per plane (mirrors renderScene's plane discipline).
static void drawMenu(const MenuState &m) {
    sprDraw(mh_menu_bg, 0, 0, FRAME(0));

    // Weapon row: one sel tile per weapon, 16 px apart (3-glyph lane + gap).
    const uint8_t w = static_cast<uint8_t>(m.weapon);
    sprDraw(mh_menu_sel, static_cast<int16_t>(MENU_OPT_X + w * 16), MENU_WEAPON_Y, FRAME(w));

    // Target row: 24 px apart on the first row (5-glyph lane + gap); RAVAGER
    // and POLE share the second row at their fixed x offsets.
    const uint8_t t = static_cast<uint8_t>(m.target);
    int16_t tx, ty;
    if (t < 3) {
        tx = static_cast<int16_t>(MENU_OPT_X + t * 24);
        ty = MENU_TARGET_Y;
    } else {
        tx = (t == 3) ? MENU_OPT_X : 68;
        ty = MENU_TARGET_Y2;
    }
    sprDraw(mh_menu_sel, tx, ty, FRAME(static_cast<uint8_t>(t + 3)));
}

}   // namespace mh
