#pragma once
// Opening-menu render (bead monhun-ardu-6zb.2). Device-only, like render.hpp:
// text comes from the FX glyph sheets through textPut() (4 px advance), the
// selection underline is the same masked blk() rect fill the HUD bars use.
//
// Called once per plane exactly like renderScene(), so the three L4 passes
// composite the menu. ArduboyG wipes the plane framebuffer black before each
// pass, so the menu never has to clear its rows: each pass only lights its own
// shade (selected options white, labels/other options light gray).

#include "render.hpp"
#include "menu_state.hpp"

namespace mh {

// Layout (glyph lane = 4 px advance; "LUNGE" is the widest option at 20 px):
//   y=10         MONHUN DEMO            (title, white, centred)
//   y=22  WEAPON SWD  FLS  GUN          (selected white + white underline)
//   y=34  TARGET LUNGE SWEEP HEAVY      (targets wrap onto a second row so the
//   y=44         RAVAGER POLE           4th beast + pole fit 128 px)
//   y=56  A START                       (footer hint)
constexpr int16_t MENU_TITLE_X = 42;   // (128 - 11*4) / 2
constexpr int16_t MENU_TITLE_Y = 10;
constexpr int16_t MENU_LABEL_X = 4;
constexpr int16_t MENU_OPT_X = 36;   // past the 6-glyph label lane
constexpr int16_t MENU_WEAPON_Y = 22;
constexpr int16_t MENU_TARGET_Y = 34;
constexpr int16_t MENU_TARGET_Y2 = 44;
constexpr int16_t MENU_FOOTER_Y = 56;
constexpr int8_t MENU_UNDERLINE_DY = 9;   // 1 px gap under the 8 px glyph tile

// All strings live in MCU flash (PROGMEM): no menu string/table copy in RAM.
static const char MH_PROGMEM MENU_TITLE[] = "MONHUN DEMO";
static const char MH_PROGMEM MENU_LBL_WEAPON[] = "WEAPON";
static const char MH_PROGMEM MENU_LBL_TARGET[] = "TARGET";
static const char MH_PROGMEM MENU_FOOTER[] = "A START";
static const char MH_PROGMEM MENU_WEAPON_NAMES[] = "SWD\0FLS\0GUN";
static const char MH_PROGMEM MENU_TARGET_NAMES[] = "LUNGE\0SWEEP\0HEAVY\0RAVAGER\0POLE";
static const uint8_t MH_PROGMEM MENU_WEAPON_LEN[3] = {3, 3, 3};
static const uint8_t MH_PROGMEM MENU_WEAPON_OFF[3] = {0, 4, 8};
static const uint8_t MH_PROGMEM MENU_TARGET_LEN[5] = {5, 5, 5, 7, 4};
static const uint8_t MH_PROGMEM MENU_TARGET_OFF[5] = {0, 6, 12, 18, 26};

// Draw a PROGMEM string on the given glyph sheet; returns the x after it.
static int16_t menuText(int16_t x, int16_t y, uint24_t sheet, const char MH_PROGMEM *s, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        const uint8_t code = mhPgmReadU8(reinterpret_cast<const uint8_t *>(s) + i);
        x = textPut(sheet, x, y, static_cast<char>(code));
    }
    return x;
}

// One pick row: light-gray label lane, then the options. The selected option is
// white with a white underline; the rest stay light gray. Offsets/lengths are
// fixed PROGMEM tables, so the row layout costs no RAM. `first`/`count` select
// the slice of the option list drawn on this row (the target list wraps onto a
// second row). This is device UI text, not FX content data (42n.1 policy
// covers sim content tables only).
static void menuOptionRow(const char MH_PROGMEM *names, const uint8_t MH_PROGMEM *offs, const uint8_t MH_PROGMEM *lens, uint8_t first, uint8_t count, int8_t sel, int16_t y) {
    int16_t x = MENU_OPT_X;
    for (uint8_t i = first; i < first + count; i++) {
        const uint8_t len = mhPgmReadU8(lens + i);
        const uint8_t off = mhPgmReadU8(offs + i);
        menuText(x, y, i == sel ? fxfontw : fxfontg, names + off, len);
        if (i == sel)
            blk(x, y + MENU_UNDERLINE_DY, static_cast<int32_t>(len) * 4 - 1, 1, 3);
        x = static_cast<int16_t>(x + len * 4 + 4);   // 1 glyph gap between options
    }
}

// Full menu, one call per plane (mirrors renderScene's plane discipline).
static void drawMenu(const MenuState &m) {
    menuText(MENU_TITLE_X, MENU_TITLE_Y, fxfontw, MENU_TITLE, 11);

    menuText(MENU_LABEL_X, MENU_WEAPON_Y, fxfontg, MENU_LBL_WEAPON, 6);
    menuOptionRow(MENU_WEAPON_NAMES, MENU_WEAPON_OFF, MENU_WEAPON_LEN, 0, 3, m.weapon, MENU_WEAPON_Y);

    menuText(MENU_LABEL_X, MENU_TARGET_Y, fxfontg, MENU_LBL_TARGET, 6);
    menuOptionRow(MENU_TARGET_NAMES, MENU_TARGET_OFF, MENU_TARGET_LEN, 0, 3, m.target, MENU_TARGET_Y);
    menuOptionRow(MENU_TARGET_NAMES, MENU_TARGET_OFF, MENU_TARGET_LEN, 3, 2, m.target, MENU_TARGET_Y2);

    menuText(MENU_LABEL_X, MENU_FOOTER_Y, fxfontw, MENU_FOOTER, 7);
}

}   // namespace mh
