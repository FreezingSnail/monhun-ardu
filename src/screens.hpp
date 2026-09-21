#pragma once
// Generic list-screen renderer + cart readers (bead monhun-ardu-cgz,
// docs/quests-shops.md). Device-only (like render.hpp): draws through the FX
// glyph lane + the chip cursor tile, and reads the ScreenDef/ScreenRow records
// from the mhScreens cart blob during the scan/render window.
//
// Layout: title on the glyph lane at the top; up to 6 rows per page, scroll by
// 6; the selected row carries the white chip cursor and white label, the cost
// is right-aligned at x=124. Conditions gate the row action (screen_state.hpp),
// not the render, so every row is drawn.
//
// The pure state machine (nav, conditions, action switch, save) lives in
// screen_state.hpp so the host suite can exercise it without the cart.

#include "render.hpp"
#include "screen_state.hpp"
#include "smith.hpp"   // smithReadDef: cart recipe bill for COND_UPGRADE rows (prg.7)

namespace mh {

constexpr int16_t SCREEN_TITLE_Y = 0;
constexpr int16_t SCREEN_ROW_Y0 = 11;   // first of 6 rows, 9 px pitch
constexpr int16_t SCREEN_ROW_H = 9;
constexpr int16_t SCREEN_LABEL_X = 10;   // past the cursor tile
constexpr int16_t SCREEN_CURSOR_X = 2;
constexpr int16_t SCREEN_COST_RIGHT = 124;

// Fake cart pointer for a byte offset into the mhScreens raw_t section.
inline const uint8_t *screenCart(uint16_t off) {
    return reinterpret_cast<const uint8_t *>(static_cast<uint16_t>(static_cast<uint16_t>(mhScreens) + off));
}

// u16 ScreenDef offset for a screen index from the header's defOff table.
MH_NOINLINE inline uint16_t screenDefOff(uint8_t screen) {
    return mhFxReadU16(reinterpret_cast<const uint16_t *>(screenCart(static_cast<uint16_t>(screens::DEF_OFF_OFF + screen * 2))));
}

// ScreenDef: id u8, titleLen u8, title[titleLen], rowCount u8, firstRow u16.
inline uint8_t screenRowCount(uint8_t screen) {
    const uint16_t off = screenDefOff(screen);
    const uint8_t titleLen = mhFxReadU8(screenCart(static_cast<uint16_t>(off + 1)));
    return mhFxReadU8(screenCart(static_cast<uint16_t>(off + 2 + titleLen)));
}

inline uint16_t screenFirstRow(uint8_t screen) {
    const uint16_t off = screenDefOff(screen);
    const uint8_t titleLen = mhFxReadU8(screenCart(static_cast<uint16_t>(off + 1)));
    return mhFxReadU16(reinterpret_cast<const uint16_t *>(screenCart(static_cast<uint16_t>(off + 3 + titleLen))));
}

// ScreenRow: labelLen u8, label[labelLen], cost u16, actionId u8, flags u8,
// condId u8, param u8.
inline uint16_t screenRowNext(uint16_t off) {
    return static_cast<uint16_t>(off + 7 + mhFxReadU8(screenCart(off)));
}

// Recipe bill for a COND_UPGRADE row: walk the mhSmith cart records looking for
// the (weapon, tier) the row's packed param names, then copy its material
// slots. `param` is (unlockFlag << 4) | (weaponIdx << 2) | tier. A missing def
// leaves the recipe empty (zenny-only), which matches the pre-prg.7 content.
MH_NOINLINE inline void screenRowRecipe(uint8_t param, ScreenRecipe *recipe) {
    recipe[0].item = 0;
    recipe[0].count = 0;
    recipe[1].item = 0;
    recipe[1].count = 0;
    const uint8_t weapon = static_cast<uint8_t>((param >> 2) & 3);
    const uint8_t tier = static_cast<uint8_t>(param & 3);
    for (uint8_t i = 0; i < smith::UPGRADE_COUNT; i++) {
        UpgradeDef def;
        smithReadDef(i, def);
        if (def.weaponIdx != weapon || def.tier != tier)
            continue;
        for (uint8_t j = 0; j < smith::MAT_SLOTS; j++) {
            recipe[j].item = def.mat[j].item;
            recipe[j].count = def.mat[j].count;
        }
        return;
    }
}

// Recipe bill + zenny for a COND_ARMOR row: the mhSmith armor recipe record at
// the row's piece index (armor::ARMOR_<ID>) is the single source of truth, so
// the packed row cost is ignored and the row's bill always matches the data.
inline void screenRowArmorRecipe(uint8_t piece, uint16_t &cost, ScreenRecipe *recipe) {
    recipe[0].item = 0;
    recipe[0].count = 0;
    recipe[1].item = 0;
    recipe[1].count = 0;
    cost = 0;
    if (piece >= smith::ARMOR_RECIPE_COUNT)
        return;
    const uint16_t off = static_cast<uint16_t>(smith::ARMOR_RECIPES_OFF + smith::ARMOR_RECIPE_SIZE * piece);
    cost = mhFxReadU16(reinterpret_cast<const uint16_t *>(smithCart(static_cast<uint16_t>(off + smith::AREC_COST_OFF))));
    for (uint8_t j = 0; j < smith::MAT_SLOTS; j++) {
        const uint16_t m = static_cast<uint16_t>(off + smith::AREC_MAT_OFF + j * smith::AREC_MAT_STRIDE);
        recipe[j].item = mhFxReadU8(smithCart(m));
        recipe[j].count = mhFxReadU8(smithCart(static_cast<uint16_t>(m + 1)));
    }
}

inline void screenReadRow(uint16_t off, ScreenRow &row) {
    const uint8_t labelLen = mhFxReadU8(screenCart(off));
    const uint16_t fields = static_cast<uint16_t>(off + 1 + labelLen);
    row.cost = mhFxReadU16(reinterpret_cast<const uint16_t *>(screenCart(fields)));
    row.action = mhFxReadU8(screenCart(static_cast<uint16_t>(fields + 2)));
    row.flags = mhFxReadU8(screenCart(static_cast<uint16_t>(fields + 3)));
    row.cond = mhFxReadU8(screenCart(static_cast<uint16_t>(fields + 4)));
    row.param = mhFxReadU8(screenCart(static_cast<uint16_t>(fields + 5)));
    row.recipe[0].item = 0;
    row.recipe[0].count = 0;
    row.recipe[1].item = 0;
    row.recipe[1].count = 0;
    if (row.cond == screens::COND_UPGRADE)
        screenRowRecipe(row.param, row.recipe);
    else if (row.cond == screens::COND_ARMOR)
        screenRowArmorRecipe(screenArmorPiece(row.param), row.cost, row.recipe);
}

// Blob offset of row `index` (walk the variable-length records).
inline uint16_t screenRowOffsetAt(uint8_t screen, uint8_t index) {
    uint16_t off = screenFirstRow(screen);
    while (index-- > 0)
        off = screenRowNext(off);
    return off;
}

inline bool screenCursorRow(const ScreenState &s, ScreenRow &row) {
    if (s.rowCount == 0)
        return false;
    screenReadRow(screenRowOffsetAt(s.screen, s.cursor), row);
    return true;
}

// Enter a screen: reset cursor/scroll/edges and load its row count off the
// cart (the pure reset lives in screen_state.hpp screenReset()).
inline void screenEnter(ScreenState &s, uint8_t screen, const SaveBlock &save) {
    (void)save;
    screenReset(s, screen, screenRowCount(screen));
}

// One page of the generic list. Called once per plane (same discipline as
// renderScene/menu), between ArduboyG's plane blits.
inline void drawScreen(const ScreenState &s, const SaveBlock &save) {
    (void)save;
    const uint16_t defOff = screenDefOff(s.screen);
    const uint8_t titleLen = mhFxReadU8(screenCart(static_cast<uint16_t>(defOff + 1)));
    int16_t x = 2;
    for (uint8_t i = 0; i < titleLen; i++)
        x = textPut(fxfontw, x, SCREEN_TITLE_Y, static_cast<char>(mhFxReadU8(screenCart(static_cast<uint16_t>(defOff + 2 + i)))));

    const uint8_t last = static_cast<uint8_t>(s.scroll + SCREEN_ROWS);
    uint16_t rowOff = screenFirstRow(s.screen);
    for (uint8_t i = 0; i < s.rowCount; i++, rowOff = screenRowNext(rowOff)) {
        if (i < s.scroll || i >= last)
            continue;
        const int16_t y = static_cast<int16_t>(SCREEN_ROW_Y0 + (i - s.scroll) * SCREEN_ROW_H);
        const bool selected = i == s.cursor;
        if (selected)
            sprDraw(fxchip, SCREEN_CURSOR_X, static_cast<int16_t>(y + 2), FRAME(1));

        const uint8_t labelLen = mhFxReadU8(screenCart(rowOff));
        const uint24_t sheet = selected ? fxfontw : fxfontg;
        int16_t lx = SCREEN_LABEL_X;
        for (uint8_t j = 0; j < labelLen; j++)
            lx = textPut(sheet, lx, y, static_cast<char>(mhFxReadU8(screenCart(static_cast<uint16_t>(rowOff + 1 + j)))));

        const uint16_t fields = static_cast<uint16_t>(rowOff + 1 + labelLen);
        const uint8_t flags = mhFxReadU8(screenCart(static_cast<uint16_t>(fields + 3)));
        // Dynamic value token (qs.4): a ROW_F_ZENNY row draws the live save
        // balance in the cost column instead of the packed row cost.
        const int16_t value = (flags & screens::ROW_F_ZENNY) != 0 ? static_cast<int16_t>(save.zenny) : static_cast<int16_t>(mhFxReadU16(reinterpret_cast<const uint16_t *>(screenCart(fields))));
        const uint8_t digits = hudDigits(value);
        drawNumber(static_cast<int16_t>(SCREEN_COST_RIGHT - digits * 4), y, value, selected ? 3 : 2);
    }
}

}   // namespace mh
