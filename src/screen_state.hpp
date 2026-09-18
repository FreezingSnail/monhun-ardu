#pragma once
// Generic list-screen logic (bead monhun-ardu-cgz, docs/quests-shops.md).
//
// Host-testable state machine shared by the device suite and src/screens.hpp:
//   * ScreenState: current screen index, cursor, page scroll
//   * debounced up/down nav (same tap/hold feel as menu_state.hpp)
//   * row condition evaluation (zenny >= cost / save flag / tier < max / quest)
//   * the fixed action switch (BUY_UPGRADE / TAKE_QUEST / TURN_IN_QUEST / LEAVE)
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

#include <stdint.h>
#include "core/input.hpp"
#include "core/save.hpp"
#include "quest_state.hpp"
#include "generated/screen_meta.hpp"

namespace mh {

constexpr uint8_t SCREEN_ROWS = 6;       // rows per page
constexpr uint8_t SCREEN_MAX_TIER = 3;   // smith cap; data holds the costs

// Same d-pad repeat feel as the opening menu (menu_state.hpp): a fresh
// direction steps at once, a held one waits SCREEN_NAV_DELAY ticks then steps
// every SCREEN_NAV_REPEAT. uint8 timers, no floats.
constexpr uint8_t SCREEN_NAV_DELAY = 16;
constexpr uint8_t SCREEN_NAV_REPEAT = 6;

// One decoded row (label lives on the cart, not needed for logic). `flags` is
// the packed ScreenRow flags byte (reserved for qs.2/qs.3 content).
struct ScreenRow {
    uint16_t cost;
    uint8_t action;
    uint8_t flags;
    uint8_t cond;
    uint8_t param;
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
};

enum ScreenEvent : int8_t {
    SCREEN_NONE = 0,
    SCREEN_ACCEPT,   // A rising edge on the cursor row
    SCREEN_BACK      // B rising edge: return to the caller
};

// Row condition: 0 = always, zenny >= cost, save flag set, tier < max, or the
// quest state query (action-dependent; see the header note).
inline bool screenCondOk(const SaveBlock &save, const ScreenRow &row) {
    switch (row.cond) {
    case screens::COND_ZENNY:
        return save.zenny >= row.cost;
    case screens::COND_FLAG:
        return saveQuestGet(save, static_cast<uint8_t>(row.param & 15), static_cast<uint8_t>((row.param >> 4) & 1));
    case screens::COND_TIER:
        return save.tier[row.param < SAVE_TIER_COUNT ? row.param : 0] < SCREEN_MAX_TIER;
    case screens::COND_QUEST: {
        const uint8_t quest = static_cast<uint8_t>(row.param & 15);
        if (row.action == screens::ACTION_TURN_IN_QUEST)
            return questReady(save, quest, static_cast<uint8_t>((row.param >> 4) & 15));
        return questTakeable(save, quest);
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
// committed (the caller then calls saveStore once). Buying a tier is gated by
// the tier cap and the zenny cost; quest rows take/turn in through
// src/quest_state.hpp (turn-in pays the row cost, u16-clamped).
inline bool screenApplyAction(SaveBlock &save, const ScreenRow &row) {
    switch (row.action) {
    case screens::ACTION_BUY_UPGRADE: {
        const uint8_t weapon = row.param < SAVE_TIER_COUNT ? row.param : 0;
        if (save.tier[weapon] >= SCREEN_MAX_TIER || save.zenny < row.cost)
            return false;
        save.zenny = static_cast<uint16_t>(save.zenny - row.cost);
        save.tier[weapon]++;
        return true;
    }
    case screens::ACTION_TAKE_QUEST:
        return questTake(save, static_cast<uint8_t>(row.param & 15));
    case screens::ACTION_TURN_IN_QUEST:
        return questTurnIn(save, static_cast<uint8_t>(row.param & 15), static_cast<uint8_t>((row.param >> 4) & 15), row.cost);
    default:   // ACTION_LEAVE
        return false;
    }
}

}   // namespace mh
