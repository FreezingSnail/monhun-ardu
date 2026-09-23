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
#include "quest.hpp"          // questReadDef: quest unlock + reward for COND_QUEST rows (dlp.2)
#include "forge.hpp"          // forge::WEAPON_*/NODE_* for the hub strip marker (ui.5.2)
#include "core/progmem.hpp"   // MH_PROGMEM + mhPgmReadU8 for the chrome strings

namespace mh {

constexpr int16_t SCREEN_TITLE_Y = 0;
constexpr int16_t SCREEN_ROW_Y0 = 11;   // first of 6 rows, 9 px pitch
constexpr int16_t SCREEN_ROW_H = 9;
constexpr int16_t SCREEN_LABEL_X = 10;   // past the cursor tile
constexpr int16_t SCREEN_CURSOR_X = 2;
constexpr int16_t SCREEN_COST_RIGHT = 124;
// Hub bottom strip (ui.5.2): below the 4 hub rows, on the free y=56 line.
constexpr int16_t SCREEN_STRIP_Y = 56;

// Fake cart pointer for a byte offset into the mhScreens raw_t section.
inline const uint8_t *screenCart(uint16_t off) {
    return reinterpret_cast<const uint8_t *>(static_cast<uint16_t>(static_cast<uint16_t>(mhScreens) + off));
}

// Batched string fetch (monhun-ardu-dx5.3): one cart transaction per title/label
// instead of one mhFxReadU8 seek per glyph. The generator caps titles and row
// labels at 16 chars (tools/gen-screens.py TITLE_MAX/LABEL_MAX), so this 16-byte
// stack buffer covers every authored string and the renderer draws the buffered
// prefix only. A corrupt cart with a longer string is truncated, never read past
// the buffer (monhun-ardu-5co.9 dropped the now-dead per-char tail loop).
constexpr uint8_t SCREEN_TEXT_BUF = 16;

inline uint8_t screenReadText(uint16_t off, uint8_t len, char *buf) {
    const uint8_t n = len < SCREEN_TEXT_BUF ? len : SCREEN_TEXT_BUF;
    if (n != 0)
        mhFxReadBytes(screenCart(off), reinterpret_cast<uint8_t *>(buf), n);
    return n;
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

// The packed row fields after the variable label (cost u16, action, flags,
// cond, param) are contiguous and match the front of ScreenRow, so one bulk
// read fills them (monhun-ardu-5co.8); the asserts pin the order.
static_assert(offsetof(ScreenRow, action) == 2 && offsetof(ScreenRow, flags) == 3, "ScreenRow action/flags order drift");
static_assert(offsetof(ScreenRow, cond) == 4 && offsetof(ScreenRow, param) == 5, "ScreenRow cond/param order drift");
inline void screenReadRow(uint16_t off, ScreenRow &row) {
    const uint8_t labelLen = mhFxReadU8(screenCart(off));
    const uint16_t fields = static_cast<uint16_t>(off + 1 + labelLen);
    mhFxReadBytes(screenCart(fields), reinterpret_cast<uint8_t *>(&row.cost), 6);
    row.unlock = 0;
    row.recipe[0].item = 0;
    row.recipe[0].count = 0;
    row.recipe[1].item = 0;
    row.recipe[1].count = 0;
    if (row.cond == screens::COND_QUEST) {
        // dlp.2: the quest def is the source of truth for the chain unlock and
        // the turn-in payout (reward zenny + optional material). Board take rows
        // fill `unlock`; turn-in rows fill recipe[0] + cost and are ignored by
        // the take path. Armor craft bills live on the card (ui.3.1, 5co.6).
        const uint8_t quest = static_cast<uint8_t>(row.param & 15);
        if (quest < quests::QUEST_COUNT) {
            QuestDef def;
            questReadDef(quest, def);
            if (row.action == screens::ACTION_TAKE_QUEST) {
                row.unlock = def.unlockFlag;
            } else if (row.action == screens::ACTION_TURN_IN_QUEST) {
                row.recipe[0].item = def.rewardItem;
                row.recipe[0].count = def.rewardCount;
                row.cost = def.rewardZenny;
            }
        }
    }
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

// gs.2 GEAR skill readout: copy the cached aggregation's per-skill points/tier
// into the ScreenState cache drawScreen reads. The caller refreshes Game::armor
// first (armorApplyToGame), so this is the last step on GEAR entry and after
// every equip action.
inline void screenGearCache(ScreenState &s, const ArmorAgg &agg) {
    for (uint8_t i = 0; i < armor::SKILL_COUNT; i++) {
        s.skillPoints[i] = agg.points[i];
        s.skillTier[i] = agg.tier[i];
    }
}

// ---- ui.5.2 hub chrome -----------------------------------------------------
// Fixed-width flash abbreviation tables + one small PROGMEM string drawer. A
// flat char array beats a pointer table (which would land in .data); the space
// padding keeps every entry 3/4 chars wide so one indexed read serves each
// class/skill ("SWD"/"FL "/"GN " and ATK/DEF/HP/STAM/EVA).
static const char MH_PROGMEM SCREEN_READY[] = "READY";
static const char MH_PROGMEM SCREEN_WCLASS[9] = {'S', 'W', 'D', 'F', 'L', ' ', 'G', 'N', ' '};
static const char MH_PROGMEM SCREEN_SKILL_ABBR[20] = {
    'A', 'T', 'K', ' ', 'D', 'E', 'F', ' ', 'H', 'P', ' ', ' ', 'S', 'T', 'A', 'M', 'E', 'V', 'A', ' ',
};

// Draw `n` glyphs from a flash byte array, returning the next x. The loop body
// is shared across iterations, so a variable `n` costs one textPut() call site
// rather than one per glyph.
inline uint8_t screenTextN(uint24_t sheet, uint8_t x, int16_t y, const char *str, uint8_t n) {
    for (uint8_t i = 0; i < n; i++)
        x = static_cast<uint8_t>(textPut(sheet, x, y, static_cast<char>(mhPgmReadU8(reinterpret_cast<const uint8_t *>(str + i)))));
    return x;
}

// Hub HUNT right column (ui.5.2): the active quest's progress (p/n), READY when
// the goal is met, or `-` with no active quest. Right-aligned like the cost it
// replaces; the quest `need` comes from the cart def, `progress` from the save.
inline void drawHubQuestColumn(const SaveBlock &save, uint8_t y, bool selected) {
    const uint24_t sheet = selected ? fxfontw : fxfontg;
    if (save.activeQuest == SAVE_QUEST_NONE) {
        textPut(sheet, static_cast<int16_t>(SCREEN_COST_RIGHT - 4), y, '-');
        return;
    }
    QuestDef def;
    questReadDef(save.activeQuest, def);
    if (save.progress >= def.need) {
        screenTextN(sheet, static_cast<uint8_t>(SCREEN_COST_RIGHT - 20), y, SCREEN_READY, 5);
        return;
    }
    const uint8_t pd = hudDigits(save.progress);
    const uint8_t nd = hudDigits(def.need);
    const uint8_t x = static_cast<uint8_t>(SCREEN_COST_RIGHT - (pd + 1 + nd) * 4);
    drawNumber(x, y, save.progress, selected ? 3 : 2);
    textPut(sheet, static_cast<int16_t>(x + pd * 4), y, '/');
    drawNumber(static_cast<int16_t>(x + (pd + 1) * 4), y, def.need, selected ? 3 : 2);
}

// Hub bottom strip (ui.5.2): equipped weapon marker (class abbr + tree tier)
// and the active armor skill point totals (tier != 0 only; no all-skills
// screen). Runs on the free y=56 line below the four hub rows.
inline void drawHubStrip(const SaveBlock &save, const Game &g) {
    uint8_t x = 2;
    const uint8_t node = save.equippedNode;
    // Class from the generated tree block starts (sword < flail < gun), so no
    // cart read; a fresh/unequipped save falls back to the sword marker.
    uint8_t cls = forge::WEAPON_SWORD;
    if (node != SAVE_NODE_NONE)
        cls = node >= forge::NODE_GUN_FIRST ? forge::WEAPON_GUN : node >= forge::NODE_FLAIL_FIRST ? forge::WEAPON_FLAIL : forge::WEAPON_SWORD;
    x = screenTextN(fxfontw, x, SCREEN_STRIP_Y, &SCREEN_WCLASS[cls * 3], 3);
    if (node != SAVE_NODE_NONE) {
        // Tree tier as a single digit ("SWD2"): one glyph beats the " T" + a
        // drawNumber call. The class base is the generated first-node constant.
        const uint8_t first = cls == forge::WEAPON_SWORD ? forge::NODE_SWORD_FIRST : cls == forge::WEAPON_FLAIL ? forge::NODE_FLAIL_FIRST : forge::NODE_GUN_FIRST;
        x = static_cast<uint8_t>(textPut(fxfontw, x, SCREEN_STRIP_Y, static_cast<char>('0' + (node - first + 1))));
    }
    x = static_cast<uint8_t>(x + 8);
    for (uint8_t i = 0; i < armor::SKILL_COUNT; i++) {
        if (g.armor.tier[i] == 0)
            continue;
        x = screenTextN(fxfontg, x, SCREEN_STRIP_Y, &SCREEN_SKILL_ABBR[i * 4], 4);
        drawNumber(x, SCREEN_STRIP_Y, static_cast<int16_t>(g.armor.points[i]), 2);
        // An active skill is >= THRESHOLD_S (10), so the points are always two
        // digits: advance 2 glyphs + a 1-glyph gap.
        x = static_cast<uint8_t>(x + 12);
    }
}

// One page of the generic list. Called once per plane (same discipline as
// renderScene/menu), between ArduboyG's plane blits.
inline void drawScreen(const ScreenState &s, const SaveBlock &save, const Game &g) {
    const uint16_t defOff = screenDefOff(s.screen);
    const uint8_t titleLen = mhFxReadU8(screenCart(static_cast<uint16_t>(defOff + 1)));
    char text[SCREEN_TEXT_BUF];
    uint8_t tn = screenReadText(static_cast<uint16_t>(defOff + 2), titleLen, text);
    uint8_t x = 2;
    for (uint8_t i = 0; i < tn; i++)
        x = static_cast<uint8_t>(textPut(fxfontw, x, SCREEN_TITLE_Y, text[i]));

    // Page indicator (ui.5.2): `n/m` after the title when a list spans more than
    // one 6-row page. The 6-row grid fills y=11..63, so the title line is the
    // only free lane (the header zenny owns the far right; this stays left).
    if (s.rowCount > SCREEN_ROWS) {
        // Count pages with a 6-step walk instead of two u8 divisions (AVR has no
        // divide; the loop measured cheaper whole-image).
        uint8_t pages = 1;
        uint8_t page = 1;
        for (uint8_t r = SCREEN_ROWS; r < s.rowCount; r = static_cast<uint8_t>(r + SCREEN_ROWS)) {
            pages++;
            if (r <= s.scroll)
                page++;
        }
        uint8_t px = static_cast<uint8_t>(x + 4);
        drawNumber(px, SCREEN_TITLE_Y, page, 2);
        px = static_cast<uint8_t>(px + hudDigits(page) * 4);
        textPut(fxfontg, px, SCREEN_TITLE_Y, '/');
        px = static_cast<uint8_t>(px + 4);
        drawNumber(px, SCREEN_TITLE_Y, pages, 2);
    }

    // Header zenny (ui.5): right-aligned `$` + live balance on the title line;
    // the fake ZENNY row and its ROW_F_ZENNY token are retired.
    const uint8_t zd = hudDigits(static_cast<int16_t>(save.zenny));
    const uint8_t zx = static_cast<uint8_t>(SCREEN_COST_RIGHT - (zd + 1) * 4);
    textPut(fxfontw, zx, SCREEN_TITLE_Y, '$');
    drawNumber(static_cast<int16_t>(zx + 4), SCREEN_TITLE_Y, static_cast<int16_t>(save.zenny), 3);

    const uint8_t last = static_cast<uint8_t>(s.scroll + SCREEN_ROWS);
    uint16_t rowOff = screenFirstRow(s.screen);
    for (uint8_t i = 0; i < s.rowCount; i++, rowOff = screenRowNext(rowOff)) {
        if (i < s.scroll || i >= last)
            continue;
        const uint8_t y = static_cast<uint8_t>(SCREEN_ROW_Y0 + (i - s.scroll) * SCREEN_ROW_H);
        const bool selected = i == s.cursor;
        if (selected)
            sprDraw(fxchip, SCREEN_CURSOR_X, static_cast<int16_t>(y + 2), FRAME(1));

        const uint8_t labelLen = mhFxReadU8(screenCart(rowOff));
        const uint24_t sheet = selected ? fxfontw : fxfontg;
        const uint8_t ln = screenReadText(static_cast<uint16_t>(rowOff + 1), labelLen, text);
        uint8_t lx = SCREEN_LABEL_X;
        for (uint8_t j = 0; j < ln; j++)
            lx = static_cast<uint8_t>(textPut(sheet, lx, y, text[j]));

        // Hub HUNT row (ui.5.2): the right column is the quest progress instead
        // of the packed cost (the hub cost is always 0).
        if (s.screen == screens::SCREEN_HUB && i == 0) {
            drawHubQuestColumn(save, y, selected);
            continue;
        }

        const uint16_t fields = static_cast<uint16_t>(rowOff + 1 + labelLen);
        const uint8_t flags = mhFxReadU8(screenCart(static_cast<uint16_t>(fields + 3)));
        int16_t value;
        uint8_t tier = 0;
        if ((flags & screens::ROW_F_SKILL) != 0) {
            // Live skill readout (gs.2): `param` is the armor::SKILL_* index; a
            // bad id clamps to skill 0 so a corrupt cart cannot read past the
            // cache. The cached points are already clamped to THRESHOLD_M.
            const uint8_t raw = mhFxReadU8(screenCart(static_cast<uint16_t>(fields + 5)));
            const uint8_t skill = raw < armor::SKILL_COUNT ? raw : 0;
            value = static_cast<int16_t>(s.skillPoints[skill]);
            tier = s.skillTier[skill];
        } else {
            value = static_cast<int16_t>(mhFxReadU16(reinterpret_cast<const uint16_t *>(screenCart(fields))));
        }
        const uint8_t digits = hudDigits(value);
        const uint8_t costX = static_cast<uint8_t>(SCREEN_COST_RIGHT - digits * 4);
        // An active skill (tier 1 = S, 2 = M) marks its points with a letter
        // just left of the number; an inert tier draws the points only.
        if (tier != 0)
            textPut(selected ? fxfontw : fxfontg, static_cast<int16_t>(costX - 8), y, tier == 2 ? 'M' : 'S');
        drawNumber(costX, y, value, selected ? 3 : 2);
    }

    // Hub bottom strip (ui.5.2): only the hub is short enough to leave y=56 free.
    if (s.screen == screens::SCREEN_HUB)
        drawHubStrip(save, g);
}

}   // namespace mh
