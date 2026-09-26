#pragma once
// List-screen renderer + cart readers (bead monhun-ardu-cgz,
// docs/quests-shops.md; prebaked pages: epic monhun-ardu-hbk,
// docs/ui-design.md "Screen prebake v2"). Device-only (like render.hpp): blits
// a baked 4-shade page per 6-row window and draws only the live chrome on top
// (cursor, selected label, node/armor markers, GEAR slot candidate names +
// markers, skill numbers, zenny, page indicator), then reads the
// ScreenDef/ScreenRow records
// from the mhScreens cart blob during the scan/render window.
//
// Layout: the title band, rule, row labels, section bands and costs are baked
// into the page; rows stay on the y=11 + 9*i grid, 6 per page, scroll by 6. The
// selected row's label is re-drawn in white over its baked copy (section
// headers bake centered white text and are skipped; GEAR slot rows redraw the
// candidate name instead). Conditions gate the row action (screen_state.hpp),
// not the render.
//
// The pure state machine (nav, conditions, action switch, save) lives in
// screen_state.hpp so the host suite can exercise it without the cart.

#include "render.hpp"
#include "screen_state.hpp"
#include "quest.hpp"                 // questReadDef: quest unlock + reward for COND_QUEST rows (dlp.2)
#include "forge.hpp"                 // forge::NODE_UPGRADE_COST + NODE_*_FIRST (hbk.13 UPGRADE rows)
#include "core/progmem.hpp"          // MH_PROGMEM + mhPgmReadU8 for the chrome strings
#include "generated/zone_meta.hpp"   // zone::ROOM_* / ROOMS_COUNT (imx MAP panels)

namespace mh {

constexpr int16_t SCREEN_TITLE_Y = 0;
constexpr int16_t SCREEN_ROW_Y0 = 11;   // first of 6 rows, 9 px pitch
constexpr int16_t SCREEN_ROW_H = 9;
constexpr int16_t SCREEN_LABEL_X = 10;   // past the cursor tile
constexpr int16_t SCREEN_CURSOR_X = 2;
// Live-chrome right edge (header zenny), right-aligned on the title/row lane.
// The baked cost column ends further left (SCREEN_BAKE_COST_RIGHT) so the live
// marker column x=118..121 stays clear.
constexpr int16_t SCREEN_COST_RIGHT = 124;
constexpr int16_t SCREEN_BAKE_COST_RIGHT = 112;   // baked cost / live skill digits end here
// hbk.11 UPGRADE rows: left edge of the live `owned>next` tier pair (3 glyph
// cells, 12 px); the upgrade cost right-aligns to SCREEN_BAKE_COST_RIGHT.
constexpr int16_t SCREEN_UPGRADE_X = 48;

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

// Draw a buffered label glyph by glyph (the selected-row white redraw and the
// hbk.12 GEAR slot candidate names); returns the x past the last glyph.
inline uint8_t screenTextLabel(uint24_t sheet, uint8_t x, uint8_t y, const char *text, uint8_t len) {
    for (uint8_t i = 0; i < len; i++)
        x = static_cast<uint8_t>(textPut(sheet, x, y, text[i]));
    return x;
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

// First forge-node id of a class (forge::WEAPON_*), from the generated
// NODE_CLASS_FIRST table (the class spines are three contiguous nodes at each
// class's first id -- branches append after the spine, so the old cls*3
// formula no longer holds).
inline uint8_t screenClassFirst(uint8_t cls) {
    return forge::NODE_CLASS_FIRST[cls];
}
// Each class's spine (root/t1/t2) is three contiguous nodes starting at its
// class first; the branches are appended after the spine, so every next class
// first is at least three past the previous one and the UPGRADE walk stays
// first..first+2. The NODE_CLASS_FIRST entries mirror the generated firsts.
static_assert(forge::NODE_CLASS_FIRST[0] == forge::NODE_SWORD_FIRST && forge::NODE_CLASS_FIRST[1] == forge::NODE_FLAIL_FIRST && forge::NODE_CLASS_FIRST[2] == forge::NODE_GUN_FIRST &&
                  forge::NODE_CLASS_FIRST[1] >= forge::NODE_CLASS_FIRST[0] + 3 && forge::NODE_CLASS_FIRST[2] >= forge::NODE_CLASS_FIRST[1] + 3,
              "class spines are three contiguous nodes at their class firsts (screenClassFirst)");

// Resolve a class's upgrade target from the save + generated tables (hbk.13):
// walk the three-node spine and take the highest owned node; when it exists and
// is not the last tier, next = node + 1 and cost = NODE_UPGRADE_COST[next]. A
// fresh class spine owns only its root, so the next node is the first upgrade; a
// maxed spine (or one owning nothing) returns false. drawScreen and the sketch's
// UPGRADE row both call this, so the row always tracks the save with no
// ScreenState cache to refresh.
inline bool screenUpgradeNext(uint8_t cls, const SaveBlock &save, uint8_t &next, uint16_t &cost) {
    if (cls >= forge::WEAPON_COUNT)
        return false;
    const uint8_t first = screenClassFirst(cls);
    const uint8_t last = static_cast<uint8_t>(first + 2);
    // Walk the spine; the last owned node found is the highest.
    uint8_t owned = 0xFF;
    for (uint8_t node = first; node <= last; node++) {
        if (saveWeaponOwned(save, node))
            owned = node;
    }
    if (owned == 0xFF || owned == last)
        return false;   // nothing owned, or the class is maxed
    next = static_cast<uint8_t>(owned + 1);
    cost = forge::NODE_UPGRADE_COST[next];
    return true;
}

// ---- hbk.12 GEAR equipment-box slot view -----------------------------------
// The candidate table (tools/gen-screens.py) at SCREEN_GEAR_SLOT_TABLE: u8
// slotStart[5] (cumulative counts), then 4-byte {u8 id, u24 labelOff} entries
// in slot order, then the label records (u8 len + bytes). Slot 0 = the forge
// nodes, 1/2/3 the armor pieces by slot. Pure cart reads; the draw and the A
// handler share them.
inline uint8_t screenGearSlotFirst(uint8_t slot) {
    return mhFxReadU8(screenCart(static_cast<uint16_t>(screens::SCREEN_GEAR_SLOT_TABLE + slot)));
}

// MH_NOINLINE: the A handler and the entry default both walk the table, so one
// shared copy beats two inlined expansions (LTO cost math is whole-image only;
// this is the measured win).
MH_NOINLINE inline uint8_t screenGearSlotCount(uint8_t slot) {
    const uint16_t base = screens::SCREEN_GEAR_SLOT_TABLE;
    return static_cast<uint8_t>(mhFxReadU8(screenCart(static_cast<uint16_t>(base + slot + 1))) - mhFxReadU8(screenCart(static_cast<uint16_t>(base + slot))));
}

// One candidate entry: its id (node / piece index) and the absolute blob offset
// of its label record.
MH_NOINLINE inline void screenGearSlotEntry(uint8_t slot, uint8_t index, uint8_t &id, uint16_t &labelOff) {
    const uint16_t off = static_cast<uint16_t>(screens::SCREEN_GEAR_SLOT_TABLE + 5 + static_cast<uint16_t>((screenGearSlotFirst(slot) + index) * 4));
    id = mhFxReadU8(screenCart(off));
    const uint16_t lo = mhFxReadU16(reinterpret_cast<const uint16_t *>(screenCart(static_cast<uint16_t>(off + 1))));
    const uint8_t hi = mhFxReadU8(screenCart(static_cast<uint16_t>(off + 3)));
    labelOff = static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
}

// Just the candidate id (the default scan and the A rotation never need the
// label): one cart read instead of the full entry decode.
MH_NOINLINE inline uint8_t screenGearSlotId(uint8_t slot, uint8_t index) {
    return mhFxReadU8(screenCart(static_cast<uint16_t>(screens::SCREEN_GEAR_SLOT_TABLE + 5 + static_cast<uint16_t>((screenGearSlotFirst(slot) + index) * 4))));
}

// Candidate state: 0 none, 1 owned/crafted, 2 equipped. One decode shared by
// the entry default, the A rotation and the draw (weapons use the owned bitset
// + equippedNode; armor slots 1..3 the crafted bit + the 1-based equip id).
MH_NOINLINE inline uint8_t screenGearSlotState(const SaveBlock &save, uint8_t slot, uint8_t id) {
    if (slot == 0) {
        if (save.equippedNode == id)
            return 2;
        return saveWeaponOwned(save, id) ? 1 : 0;
    }
    if (save.equip[slot - 1] == static_cast<uint8_t>(id + 1))
        return 2;
    return saveCrafted(save, id) ? 1 : 0;
}

// Default slot selection on GEAR entry: the equipped candidate when one is
// equipped, else the first owned candidate, else index 0. `sel` starts at 0, so
// "no owned candidate" and "index 0 owned" collapse to the same correct answer
// (one pass, no sentinel).
MH_NOINLINE inline void screenGearSlotDefaults(ScreenState &s, const SaveBlock &save) {
    for (uint8_t slot = 0; slot < SCREEN_SLOT_COUNT; slot++) {
        const uint8_t count = screenGearSlotCount(slot);
        uint8_t sel = 0;
        for (uint8_t i = 0; i < count; i++) {
            const uint8_t state = screenGearSlotState(save, slot, screenGearSlotId(slot, i));
            if (state == 2) {
                sel = i;
                break;
            }
            if (state == 1 && sel == 0)
                sel = i;
        }
        s.slotSel[slot] = sel;
    }
}

// A on a GEAR slot row: advance to the next owned candidate (wrapping over the
// slot's count) and equip it in place -- no card. Returns true when an owned
// candidate was found (the caller saves); a slot with nothing owned is a no-op.
inline bool screenGearSlotCycle(SaveBlock &save, ScreenState &s, uint8_t slot) {
    if (slot >= SCREEN_SLOT_COUNT)
        return false;
    const uint8_t count = screenGearSlotCount(slot);
    if (count == 0)
        return false;
    uint8_t index = s.slotSel[slot];
    for (uint8_t step = 0; step < count; step++) {
        index = screenCycle(index, 1, count);
        const uint8_t id = screenGearSlotId(slot, index);
        if (screenGearSlotState(save, slot, id) == 0)
            continue;
        s.slotSel[slot] = index;
        if (slot == 0) {
            save.equippedNode = save.equippedNode == id ? SAVE_NODE_NONE : id;
            return true;
        }
        return armorEquipToggle(save, id, static_cast<uint8_t>(slot - 1));
    }
    return false;
}

// Enter a screen: reset cursor/scroll/edges and load its row count off the
// cart (the pure reset lives in screen_state.hpp screenReset()). GEAR also
// resolves its equipment-box slot selections from the save.
inline void screenEnter(ScreenState &s, uint8_t screen, const SaveBlock &save) {
    screenReset(s, screen, screenRowCount(screen));
    if (screen == screens::SCREEN_GEAR)
        screenGearSlotDefaults(s, save);
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

// ---- hub quest column (retired hbk.13) -------------------------------------
// The live hub quest progress column (ui.5.2, leaned hbk.9) is gone: the HUNT
// row shows its baked label alone. Quest progress stays visible on the quest
// card's PROG page and the QUESTS board.

// Live marker column (x=118..121): one 4x4 square whose shade is the state --
// white (3) when equipped, light gray (2) when owned/crafted, nothing
// otherwise. Pure save bits, no cart read.
inline void screenMarker(bool equipped, bool owned, int16_t y) {
    if (equipped || owned)
        hudBlk(118, static_cast<int16_t>(y + 3), 4, 4, equipped ? 3 : 2);
}

// ---- MAP screen (monhun-ardu-imx, docs/ui-design.md) -----------------------
// The room graph is baked art (images/screens/mh_screen_map_0_128x64.png ->
// mh_screen_map_0, the single SCREEN_MAP page); only the current-room cursor
// and the active quest's target marker are live pixels. The panel rects below
// mirror that art, indexed by the generated zone::ROOM_* order (data/map.json
// rooms sorted by id: area 0, camp 1, cavern 2, ridge 3).
//
// The MAP screen's pages are all baked art (images/screens/mh_screen_map_<i>):
// page 0 is the graph + the v1 cursor (camp, baked), pages 1..ROOMS_COUNT carry
// a marker box stamped on the active quest's target room. The only live work is
// the page pick below -- no overlay draw code, which keeps the screen at the
// repo's prebaked-screens discipline and the flash cost to the quest-hint
// table (measured: the overlay version cost 106 B, this costs ~16 B).
// Active quest's MAP target room: a zone room index, or QUEST_ROOM_HINT_NONE
// when there is no active quest / no hint (no marker page). Inert-safe: no
// active quest must never index QUEST_ROOM_HINT. The device suites assert the
// mapping through this helper.
inline uint8_t screenMapQuestRoom(const SaveBlock &save) {
    if (save.activeQuest == SAVE_QUEST_NONE || save.activeQuest >= quests::QUEST_COUNT)
        return quests::QUEST_ROOM_HINT_NONE;
    return mhPgmReadU8(&quests::QUEST_ROOM_HINT[save.activeQuest]);
}

inline uint8_t screenMapPage(const SaveBlock &save) {
    // The MAP screen's page is the active quest's room hint (+1; no quest ->
    // page 0). gen-quests validates every hint against the room list, and an
    // absent hint is QUEST_ROOM_HINT_NONE (0xFF), which +1 wraps back to page
    // 0 -- so one compare covers both.
    // The hint read is spelled out (not via screenMapQuestRoom) so the
    // shipping image carries one copy; the helper stays for the device suites.
    // QUEST_ROOM_HINT_NONE (0xFF) + 1 wraps to page 0, so one compare covers
    // both "no quest" and "no hint".
    return static_cast<uint8_t>(mhPgmReadU8(&quests::QUEST_ROOM_HINT[save.activeQuest]) + 1);
}

// One page of the list. Called once per plane (same discipline as
// renderScene/menu), between ArduboyG's plane blits. hbk.3: every shipped
// screen is prebaked -- blit the page for the visible window, then draw only
// the live chrome. The MAP screen picks its page from the quest hint (above)
// and skips the list chrome (imx).
inline void drawScreen(const ScreenState &s, const SaveBlock &save, const Game &g) {
    // Prebaked page for the visible window: page = scroll / SCREEN_ROWS (scroll
    // is always a multiple of 6). Every shipped screen prebakes pages
    // (test_screens pins it); the count guard keeps a hypothetical future
    // screen without pages from blitting a null address (it then renders as an
    // empty page plus the live chrome).
    if (screenPageCount(s.screen) != 0) {
        const uint8_t pageIdx = s.screen == screens::SCREEN_MAP ? screenMapPage(save) : static_cast<uint8_t>(s.scroll / SCREEN_ROWS);
        const uint24_t page = static_cast<uint24_t>(screenPageAddr(s.screen, pageIdx));
#if MH_ROOM_BOUNDS
        cardBlit(page);
#else
        // render.hpp folds cardBlit out when MH_ROOM_BOUNDS is 0 (the carved
        // hub/screen test images), so blit the page layer inline there. Same
        // 128x64 3x 1bpp page-major family as the detail cards.
        FX::readDataBytes(page + static_cast<uint24_t>(arduboy.currentPlane()) * 1024u, arduboy.getBuffer(), 1024);
#endif
    }
    if (s.screen == screens::SCREEN_MAP)
        return;   // baked panel + baked marker page: no list chrome

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
        // Slot rows redraw the candidate name below instead of the slot label.
        if (selected && action != screens::ACTION_SLOT_PICK && (action != screens::ACTION_NONE || (flags & screens::ROW_F_SKILL) != 0)) {
            const uint8_t ln = screenReadText(static_cast<uint16_t>(rowOff + 1), labelLen, text);
            screenTextLabel(fxfontw, static_cast<uint8_t>(SCREEN_LABEL_X), y, text, ln);
        }

        if ((flags & screens::ROW_F_FORGE) != 0) {
            // FORGE/CRAFT weapon rows: node id in `param`; marker from save
            // bits only.
            screenMarker(save.equippedNode == param, saveWeaponOwned(save, param), static_cast<int16_t>(y));
        } else if (action == screens::ACTION_SLOT_PICK) {
            // hbk.12 GEAR equipment-box row: the shown candidate's name over
            // the baked slot label plus its state marker (white equipped, gray
            // owned). A slot with nothing owned keeps the baked label alone.
            // slotSel is always a valid index (entry default + A rotation), so
            // the draw needs no count clamp.
            const uint8_t slot = param < SCREEN_SLOT_COUNT ? param : 0;
            uint8_t id;
            uint16_t labelOff;
            screenGearSlotEntry(slot, s.slotSel[slot], id, labelOff);
            const uint8_t state = screenGearSlotState(save, slot, id);
            if (state != 0) {
                // Erase the baked slot label over its full baked width first:
                // the candidate name's glyph gaps let the baked ink through, so
                // a shorter name ("GN T1" over "WEAPON") read as the label
                // written over itself. Nothing owned keeps the baked label.
                hudBlk(SCREEN_LABEL_X, y, static_cast<int16_t>(labelLen * 4), 8, 0);
                const uint8_t ln = screenReadText(static_cast<uint16_t>(labelOff + 1), mhFxReadU8(screenCart(labelOff)), text);
                screenTextLabel(selected ? fxfontw : fxfontg, static_cast<uint8_t>(SCREEN_LABEL_X), y, text, ln);
                screenMarker(state == 2, true, static_cast<int16_t>(y));
            }
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
        } else if (action == screens::ACTION_UPGRADE_ROW) {
            // hbk.11/hbk.13 UPGRADE class row: resolve the class's next node
            // straight from the save + generated tables (`owned>next` + its
            // upgrade cost). false = nothing upgradeable, so the baked label
            // stays alone. Single tier digits draw through textPut (the digits
            // are 1..3) to keep the inlined drawNumber copies down; only the
            // cost keeps the variable-width path.
            uint8_t next;
            uint16_t cost;
            if (screenUpgradeNext(param, save, next, cost)) {
                const uint8_t ownedTier = static_cast<uint8_t>(next - screenClassFirst(param));
                const uint24_t sheet = selected ? fxfontw : fxfontg;
                uint8_t tx = static_cast<uint8_t>(SCREEN_UPGRADE_X);
                tx = static_cast<uint8_t>(textPut(sheet, tx, y, static_cast<char>('0' + ownedTier)));
                tx = static_cast<uint8_t>(textPut(sheet, tx, y, '>'));
                textPut(sheet, tx, y, static_cast<char>('0' + ownedTier + 1));
                const uint8_t digits = hudDigits(static_cast<int16_t>(cost));
                drawNumber(static_cast<int16_t>(SCREEN_BAKE_COST_RIGHT - digits * 4), y, static_cast<int16_t>(cost), selected ? 3 : 2);
            }
        }
    }
}

}   // namespace mh
