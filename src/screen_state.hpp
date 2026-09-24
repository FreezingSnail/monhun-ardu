#pragma once
// Generic list-screen logic (bead monhun-ardu-cgz, docs/quests-shops.md).
//
// Host-testable state machine shared by the device suite and src/screens.hpp:
//   * ScreenState: current screen index, cursor, page scroll
//   * debounced up/down nav (tap/hold feel shared with the deleted opening menu)
//   * row condition evaluation (quest / crafted)
//   * the fixed action switch (TAKE_QUEST / TURN_IN_QUEST / LEAVE; armor and
//     weapon equip are card/slot actions handled in src/screens.hpp)
//
// Conditions gate the action (A on a locked row does nothing), so the state
// machine needs no per-row visibility mask. The cart side (reading ScreenDef/
// ScreenRow records) lives in src/screens.hpp so this header compiles on the
// host with plain ScreenRow structs.
//
// Quest rows (bead monhun-ardu-me6): COND_QUEST picks the check from the row's
// action -- a TAKE_QUEST row is takeable when no quest is active and this one
// is neither taken nor done; a TURN_IN_QUEST row is ready when it is the active
// quest and progress >= need. `param` packs (need << 4) | quest id; the turn-in
// payout is the row `cost` (the quest's reward, from data/quests/*.json).
//
// Gear rows (bead monhun-ardu-mn6.1, reworked hbk.12): GEAR is an equipment-box
// slot view -- four slot_pick rows (weapon/head/body/charm) whose shown
// candidate + marker come from the blob candidate table and the save bits, and
// the five live skill rows. A on a slot row rotates to the next owned candidate
// and equips it in place (screenGearSlotCycle, src/screens.hpp). The old
// ACTION_EQUIP_ARMOR GEAR rows and their card path are gone; the smithy lists
// keep the armor/weapon cards.
//
// Armor crafting (ui.3.1, 5co.6) moved off the screen rows onto the detail card:
// the craft bill bakes into the mhCards record and src/card_state.hpp
// armorCardState()/cardArmorApply() gate + debit it before the same
// armorEquipToggle. The old COND_ARMOR / ACTION_CRAFT_ARMOR / smith armor-recipe
// cart read are gone.

#include <stdint.h>
#include "core/input.hpp"
#include "core/save.hpp"
#include "quest_state.hpp"
#include "upgrade_state.hpp"
#include "generated/screen_meta.hpp"
#include "generated/forge_meta.hpp"   // forge::WEAPON_* / NODE_* (weapon rows)
#include "generated/armor_meta.hpp"   // armor::PIECE_COUNT / SKILL_COUNT (arm.2, gs.2)
#include "armor_state.hpp"            // armorEquipToggle (arm.2)

namespace mh {

constexpr uint8_t SCREEN_ROWS = 6;   // rows per page
// Equipment-box slots on the GEAR screen (hbk.12): weapon/head/body/charm.
constexpr uint8_t SCREEN_SLOT_COUNT = 4;

// Same d-pad repeat feel as the deleted opening menu: a fresh direction steps
// at once, a held one waits SCREEN_NAV_DELAY ticks then steps every
// SCREEN_NAV_REPEAT. uint8 timers, no floats.
constexpr uint8_t SCREEN_NAV_DELAY = 16;
constexpr uint8_t SCREEN_NAV_REPEAT = 6;

// The recipe bill of a craftable row (armor piece), decoded from a
// caller-supplied array or from the cart by src/screens.hpp. `item` is the item
// index + 1 (0 = empty slot), so the row scan can gate and debit without this
// header depending on smith.hpp.
struct ScreenRecipe {
    uint8_t item;
    uint8_t count;
};

// One decoded row (label lives on the cart, not needed for logic). `flags` is
// the packed ScreenRow flags byte (reserved for qs.2/qs.3 content).
struct ScreenRow {
    uint16_t cost;
    uint8_t action;
    uint8_t flags;
    uint8_t cond;
    uint8_t param;
    // Quest-chain unlock flag (dlp.2): 0 = always unlocked, else the 1-based
    // prior quest whose done bit gates a COND_QUEST take row. Resolved from the
    // quest def by screens.hpp screenReadRow (or supplied by a test); zeroed for
    // every other row.
    uint8_t unlock;
    // Quest turn-in material reward, filled from the quest def by the caller
    // (screens.hpp screenReadRow) or supplied by a test: recipe[0] is
    // (rewardItem + 1, rewardCount), zero for every other row. Armor craft bills
    // live on the detail card (ui.3.1, 5co.6), not the row.
    ScreenRecipe recipe[UPGRADE_MAT_SLOTS];
};

struct ScreenState {
    uint8_t screen = 0;   // screens::SCREEN_* index
    uint8_t cursor = 0;   // selected row index
    uint8_t scroll = 0;   // first row of the current page (multiple of 6)
    uint8_t rowCount = 0;
    bool active = false;
    bool prevA = false;   // screen-owned edges (never Game::prevA)
    bool prevB = false;
    int8_t navY = 0;         // last vertical direction; 0 = released
    uint8_t navYTimer = 0;   // ticks until the next repeat (0 = disarmed)
    // gs.2 GEAR skill readout cache: copied from Game::armor on GEAR entry and
    // after every equip action (src/screens.hpp screenGearCache). drawScreen
    // reads these for ROW_F_SKILL rows; screenReset() zeroes them so a stale
    // screen never shows another save's points.
    uint8_t skillPoints[armor::SKILL_COUNT];
    uint8_t skillTier[armor::SKILL_COUNT];
    // hbk.12 GEAR equipment-box selection: one index per slot (weapon/head/
    // body/charm) into that slot's candidate list in the blob table. Resolved
    // on GEAR entry (screenGearSlotDefaults) and advanced by the A handler.
    uint8_t slotSel[SCREEN_SLOT_COUNT];
};

enum ScreenEvent : int8_t {
    SCREEN_NONE = 0,
    SCREEN_ACCEPT,   // A rising edge on the cursor row
    SCREEN_BACK      // B rising edge: return to the caller
};

// ACTION_EQUIP_ARMOR param decoders: (slot << 5) | pieceIdx. Used by the armor
// card path (src/card_state.hpp cardArmorApply).
inline uint8_t screenArmorPiece(uint8_t param) {
    return static_cast<uint8_t>(param & 31);
}
inline uint8_t screenArmorSlot(uint8_t param) {
    return static_cast<uint8_t>((param >> 5) & 3);
}

// Row condition: 0 = always or a quest state query (see header note). Armor
// rows are always live on the GEAR list; the card's bill gates the craft.
inline bool screenCondOk(const SaveBlock &save, const ScreenRow &row) {
    switch (row.cond) {
    case screens::COND_QUEST: {
        const uint8_t quest = static_cast<uint8_t>(row.param & 15);
        if (row.action == screens::ACTION_TURN_IN_QUEST)
            return questReady(save, quest, static_cast<uint8_t>((row.param >> 4) & 15));
        // dlp.2: a take row is live only when the chain unlock holds too.
        return questTakeable(save, quest) && questUnlocked(save, row.unlock);
    }
    default:
        return true;
    }
}

// Wrap a pick by delta within [0, count). Two compares beat a runtime modulo
// (same rule as menuCycle).
inline uint8_t screenCycle(uint8_t v, int8_t delta, uint8_t count) {
    if (count == 0)
        return 0;
    int16_t n = static_cast<int16_t>(v) + delta;
    if (n < 0)
        n = static_cast<int16_t>(n + count);
    else if (n >= count)
        n = static_cast<int16_t>(n - count);
    return static_cast<uint8_t>(n);
}

// Move the pick to the page base the cursor lands on (scroll by 6).
inline uint8_t screenPageStart(uint8_t cursor) {
    return static_cast<uint8_t>((cursor / SCREEN_ROWS) * SCREEN_ROWS);
}

// Enter/reset a screen. Pure: the caller supplies the row count (the device
// reads it off the cart in screenEnter, src/screens.hpp; the boot-flow routing
// in src/app_state.hpp uses the generated SCREEN_*_ROWS constants). Clears the
// cursor, scroll, nav hold state and the A/B edges so the press that opened the
// screen cannot immediately re-fire inside it.
MH_NOINLINE inline void screenReset(ScreenState &s, uint8_t screen, uint8_t rowCount) {
    s.screen = screen;
    s.cursor = 0;
    s.scroll = 0;
    s.rowCount = rowCount;
    s.active = true;
    s.prevA = false;
    s.prevB = false;
    s.navY = 0;
    s.navYTimer = 0;
    for (uint8_t i = 0; i < armor::SKILL_COUNT; i++) {
        s.skillPoints[i] = 0;
        s.skillTier[i] = 0;
    }
    for (uint8_t i = 0; i < SCREEN_SLOT_COUNT; i++)
        s.slotSel[i] = 0;
}

// One input tick: debounced vertical nav + A/B edges. The caller evaluates the
// cursor row and dispatches the action on SCREEN_ACCEPT.
inline ScreenEvent screenStep(ScreenState &s, const Input &in) {
    if (in.my == 0) {
        s.navY = 0;
        s.navYTimer = 0;
    } else if (in.my != s.navY) {
        s.cursor = screenCycle(s.cursor, in.my, s.rowCount);
        s.navY = in.my;
        s.navYTimer = SCREEN_NAV_DELAY;
    } else if (s.navYTimer > 0) {
        s.navYTimer--;
        if (s.navYTimer == 0) {
            s.cursor = screenCycle(s.cursor, in.my, s.rowCount);
            s.navYTimer = SCREEN_NAV_REPEAT;
        }
    }
    s.scroll = screenPageStart(s.cursor);

    bool aP, bP, bR;
    inputEdges(in, s.prevA, s.prevB, aP, bP, bR);
    (void)bR;
    if (aP)
        return SCREEN_ACCEPT;
    if (bP)
        return SCREEN_BACK;
    return SCREEN_NONE;
}

// Apply the fixed action switch. Returns true when the save changed and must be
// committed (the caller then calls saveStore once). Armor craft/equip is a card
// action (src/card_state.hpp cardArmorApply), so this switch no longer carries
// an armor case; quest rows take/turn in through src/quest_state.hpp (turn-in
// pays the row cost + the optional recipe[0] material reward).
inline bool screenApplyAction(SaveBlock &save, const ScreenRow &row) {
    switch (row.action) {
    case screens::ACTION_TAKE_QUEST:
        // dlp.2: re-check the chain unlock so a stale row cannot take a locked
        // quest (mirrors the recipe re-check on the smith rows).
        if (!questUnlocked(save, row.unlock))
            return false;
        return questTake(save, static_cast<uint8_t>(row.param & 15));
    case screens::ACTION_TURN_IN_QUEST:
        // Quest v2 (dlp.1/dlp.2): the row's recipe[0] carries the optional
        // material reward as (itemIdx+1, count) and the row cost is the
        // def.rewardZenny, both filled from the cart quest def by
        // screens.hpp screenReadRow.
        return questTurnIn(save, static_cast<uint8_t>(row.param & 15), static_cast<uint8_t>((row.param >> 4) & 15), row.cost, row.recipe[0].item, row.recipe[0].count);
    default:   // ACTION_LEAVE
        return false;
    }
}

}   // namespace mh
