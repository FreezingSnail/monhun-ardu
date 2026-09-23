#pragma once
// List-screen renderer + cart readers (bead monhun-ardu-cgz,
// docs/quests-shops.md; prebaked pages: epic monhun-ardu-hbk,
// docs/ui-design.md "Screen prebake v2"). Device-only (like render.hpp): blits
// a baked 4-shade page per 6-row window and draws only the live chrome on top
// (cursor, selected label, node/armor markers, quest progress, skill numbers,
// zenny, page indicator), then reads the ScreenDef/ScreenRow records
// from the mhScreens cart blob during the scan/render window.
//
// Layout: the title band, rule, row labels, section bands and costs are baked
// into the page; rows stay on the y=11 + 9*i grid, 6 per page, scroll by 6. The
// selected row's label is re-drawn in white over its baked copy (section
// headers bake centered white text and are skipped). Conditions gate the row
// action (screen_state.hpp), not the render.
//
// The pure state machine (nav, conditions, action switch, save) lives in
// screen_state.hpp so the host suite can exercise it without the cart.

#include "render.hpp"
#include "screen_state.hpp"
#include "quest.hpp"          // questReadDef: quest unlock + reward for COND_QUEST rows (dlp.2)
#include "core/progmem.hpp"   // MH_PROGMEM + mhPgmReadU8 for the chrome strings

namespace mh {

constexpr int16_t SCREEN_TITLE_Y = 0;
constexpr int16_t SCREEN_ROW_Y0 = 11;   // first of 6 rows, 9 px pitch
constexpr int16_t SCREEN_ROW_H = 9;
constexpr int16_t SCREEN_LABEL_X = 10;   // past the cursor tile
constexpr int16_t SCREEN_CURSOR_X = 2;
// Live-chrome right edge (header zenny + hub quest progress), right-aligned on
// the title/row lane. The baked cost column ends further left (SCREEN_BAKE_COST_RIGHT)
// so the live marker column x=118..121 stays clear.
constexpr int16_t SCREEN_COST_RIGHT = 124;
constexpr int16_t SCREEN_BAKE_COST_RIGHT = 112;   // baked cost / live skill digits end here

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

// Prebaked page table (hbk.3, fixed stride hbk.9, docs/ui-design.md): per screen
// in index order one SCREEN_PAGE_STRIDE-byte slot -- a u8 page count then
// SCREEN_PAGE_MAX u24 absolute FX addresses of the baked mh_screen_<name>_<page>
// layer arrays. Indexing a screen is PAGE_TABLE_OFF + screen * SCREEN_PAGE_STRIDE
// (the generated SCREEN_*_PAGE_TABLE constants), no walk. pageCount == 0 (a
// corrupt or unauthored cart) renders as an empty page plus the live chrome.
inline uint8_t screenPageCount(uint8_t screen) {
    return mhFxReadU8(screenCart(static_cast<uint16_t>(screens::PAGE_TABLE_OFF + screen * screens::SCREEN_PAGE_STRIDE)));
}

// u24 little-endian page address (same decode style as cardPageOffset).
inline uint32_t screenPageAddr(uint8_t screen, uint8_t page) {
    const uint16_t off = static_cast<uint16_t>(screens::PAGE_TABLE_OFF + screen * screens::SCREEN_PAGE_STRIDE + 1 + page * 3);
    const uint16_t lo = mhFxReadU16(reinterpret_cast<const uint16_t *>(screenCart(off)));
    const uint8_t hi = mhFxReadU8(screenCart(static_cast<uint16_t>(off + 2)));
    return static_cast<uint32_t>(lo) | (static_cast<uint32_t>(hi) << 16);
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

// ---- hub quest column (ui.5.2, leaned hbk.9) -------------------------------
// The HUNT row's live right column: the active quest's progress pair (p/n),
// right-aligned like the cost it replaces and white when the row is selected
// (fxfontw) / light gray otherwise (fxfontg). The quest `need` comes from the
// cart def, `progress` from the save; a save with no active quest draws nothing.
inline void drawHubQuestColumn(const SaveBlock &save, uint8_t y, bool selected) {
    if (save.activeQuest == SAVE_QUEST_NONE)
        return;
    const uint24_t sheet = selected ? fxfontw : fxfontg;
    QuestDef def;
    questReadDef(save.activeQuest, def);
    const uint8_t pd = hudDigits(save.progress);
    const uint8_t nd = hudDigits(def.need);
    const uint8_t x = static_cast<uint8_t>(SCREEN_COST_RIGHT - (pd + 1 + nd) * 4);
    drawNumber(x, y, save.progress, selected ? 3 : 2);
    textPut(sheet, static_cast<int16_t>(x + pd * 4), y, '/');
    drawNumber(static_cast<int16_t>(x + (pd + 1) * 4), y, def.need, selected ? 3 : 2);
}

// Live marker column (x=118..121): one 4x4 square whose shade is the state --
// white (3) when equipped, light gray (2) when owned/crafted, nothing
// otherwise. Pure save bits, no cart read.
inline void screenMarker(bool equipped, bool owned, int16_t y) {
    if (equipped || owned)
        hudBlk(118, static_cast<int16_t>(y + 3), 4, 4, equipped ? 3 : 2);
}

// One page of the list. Called once per plane (same discipline as
// renderScene/menu), between ArduboyG's plane blits. hbk.3: every shipped
// screen is prebaked -- blit the page for the visible window, then draw only
// the live chrome.
inline void drawScreen(const ScreenState &s, const SaveBlock &save, const Game &g) {
    // Prebaked page for the visible window: page = scroll / SCREEN_ROWS (scroll
    // is always a multiple of 6). Every shipped screen prebakes pages
    // (test_screens pins it); the count guard keeps a hypothetical future
    // screen without pages from blitting a null address (it then renders as an
    // empty page plus the live chrome).
    if (screenPageCount(s.screen) != 0) {
        const uint24_t page = static_cast<uint24_t>(screenPageAddr(s.screen, static_cast<uint8_t>(s.scroll / SCREEN_ROWS)));
#if MH_ROOM_BOUNDS
        cardBlit(page);
#else
        // render.hpp folds cardBlit out when MH_ROOM_BOUNDS is 0 (the carved
        // hub/screen test images), so blit the page layer inline there. Same
        // 128x64 3x 1bpp page-major family as the detail cards.
        FX::readDataBytes(page + static_cast<uint24_t>(arduboy.currentPlane()) * 1024u, arduboy.getBuffer(), 1024);
#endif
    }

    // Header zenny (ui.5): right-aligned `$` + live balance on the title line;
    // the fake ZENNY row and its ROW_F_ZENNY token are retired.
    const uint8_t zd = hudDigits(static_cast<int16_t>(save.zenny));
    const uint8_t zx = static_cast<uint8_t>(SCREEN_COST_RIGHT - (zd + 1) * 4);
    textPut(fxfontw, zx, SCREEN_TITLE_Y, '$');
    drawNumber(static_cast<int16_t>(zx + 4), SCREEN_TITLE_Y, static_cast<int16_t>(save.zenny), 3);

    char text[SCREEN_TEXT_BUF];
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
        const uint16_t fields = static_cast<uint16_t>(rowOff + 1 + labelLen);
        // One bulk read of the packed tail (cost u16, action, flags, cond,
        // param) -- one cart transaction instead of a seek per field.
        uint8_t packed[6];
        mhFxReadBytes(screenCart(fields), packed, 6);
        const uint8_t action = packed[2];
        const uint8_t flags = packed[3];
        const uint8_t param = packed[5];

        // Selected row: redraw the label white over its baked shade-2 copy.
        // Section headers (action none, no skill flag) bake centered white text
        // at a different x, so they are skipped; skill rows still highlight.
        if (selected && (action != screens::ACTION_NONE || (flags & screens::ROW_F_SKILL) != 0)) {
            const uint8_t ln = screenReadText(static_cast<uint16_t>(rowOff + 1), labelLen, text);
            uint8_t lx = SCREEN_LABEL_X;
            for (uint8_t j = 0; j < ln; j++)
                lx = static_cast<uint8_t>(textPut(fxfontw, lx, y, text[j]));
        }

        if ((flags & screens::ROW_F_FORGE) != 0) {
            // FORGE + GEAR weapon rows: node id in `param`; marker from save
            // bits only.
            screenMarker(save.equippedNode == param, saveWeaponOwned(save, param), static_cast<int16_t>(y));
        } else if ((flags & screens::ROW_F_SKILL) != 0) {
            // GEAR skill rows: live points + S/M tier letter at the baked cost
            // column. A bad id clamps to skill 0 so a corrupt cart cannot read
            // past the cache; the cached points are already clamped to M.
            const uint8_t skill = param < armor::SKILL_COUNT ? param : 0;
            const int16_t value = static_cast<int16_t>(s.skillPoints[skill]);
            const uint8_t tier = s.skillTier[skill];
            const uint8_t digits = hudDigits(value);
            const uint8_t costX = static_cast<uint8_t>(SCREEN_BAKE_COST_RIGHT - digits * 4);
            if (tier != 0)
                textPut(selected ? fxfontw : fxfontg, static_cast<int16_t>(costX - 8), y, tier == 2 ? 'M' : 'S');
            drawNumber(costX, y, value, selected ? 3 : 2);
        } else if (s.screen == screens::SCREEN_HUB && i == 0) {
            // Hub HUNT row: live quest progress instead of a baked cost.
            drawHubQuestColumn(save, y, selected);
        }
    }
}

}   // namespace mh
