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
//
// Smith rows (bead monhun-ardu-4ug): COND_UPGRADE rows carry a packed `param`
// -- (unlockFlag << 4) | (weaponIdx << 2) | tier -- and are live only when the
// tier is the weapon's next unbought tier, its unlockFlag (0 = always, else the
// 1-based quest whose done bit gates it) holds, and zenny >= cost. "bought"
// (tier already >= the row tier) and "locked" rows read as dead, so A does
// nothing. prg.7 adds the recipe bill: a row whose tier needs materials the
// inventory lacks is dead too, and the cart-side row scan (src/screens.hpp
// screenCursorRecipeOk) reads the packed UpgradeDef. The host parity of that
// scan is screenRecipeOk() below, so both paths share the same rule.

#include <stdint.h>
#include "core/input.hpp"
#include "core/save.hpp"
#include "quest_state.hpp"
#include "upgrade_state.hpp"
#include "generated/screen_meta.hpp"
#include "generated/armor_meta.hpp"   // armor::PIECE_COUNT for the armor rows (arm.2)
#include "armor_state.hpp"            // armorEquipToggle (arm.2)

namespace mh {

constexpr uint8_t SCREEN_ROWS = 6;       // rows per page
constexpr uint8_t SCREEN_MAX_TIER = 3;   // smith cap; data holds the costs

// Same d-pad repeat feel as the opening menu (menu_state.hpp): a fresh
// direction steps at once, a held one waits SCREEN_NAV_DELAY ticks then steps
// every SCREEN_NAV_REPEAT. uint8 timers, no floats.
constexpr uint8_t SCREEN_NAV_DELAY = 16;
constexpr uint8_t SCREEN_NAV_REPEAT = 6;

// The recipe bill of the upgrade def a COND_UPGRADE row names, decoded from a
// caller-supplied upgrade array (the host suite / boot routing) or from the
// cart by src/screens.hpp. `item` is the item index + 1 (0 = empty slot), so
// the row scan can gate and debit without this header depending on smith.hpp.
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
    // prg.7 recipe bill, resolved from the cart upgrade def for COND_UPGRADE
    // rows by the caller (screens.hpp screenReadRow) or supplied by a test.
    // Zeroed for every other row / a zenny-only recipe.
    ScreenRecipe recipe[UPGRADE_MAT_SLOTS];
};

// Is the row's recipe bill satisfied by the save inventory? Empty slots
// (item 0) are skipped. Pure: the caller fills row.recipe (cart or test).
inline bool screenRecipeOk(const SaveBlock &save, const ScreenRecipe *recipe) {
    for (uint8_t i = 0; i < UPGRADE_MAT_SLOTS; i++) {
        const uint8_t code = recipe[i].item;
        if (code == 0)
            continue;
        const uint8_t slot = static_cast<uint8_t>(code - 1);
        if (slot >= item::ITEM_COUNT || save.items[slot] < recipe[i].count)
            return false;
    }
    return true;
}

// Debit the row's recipe bill from the save inventory. Caller must have
// checked screenRecipeOk first; a missing slot (should not happen) is a no-op.
inline void screenRecipeDebit(SaveBlock &save, const ScreenRecipe *recipe) {
    for (uint8_t i = 0; i < UPGRADE_MAT_SLOTS; i++) {
        const uint8_t code = recipe[i].item;
        if (code == 0)
            continue;
        const uint8_t slot = static_cast<uint8_t>(code - 1);
        if (slot >= item::ITEM_COUNT)
            continue;
        save.items[slot] = static_cast<uint8_t>(save.items[slot] - recipe[i].count);
    }
}

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

// COND_UPGRADE param decoders: (unlockFlag << 4) | (weaponIdx << 2) | tier.
MH_NOINLINE inline uint8_t screenUpgradeWeapon(uint8_t param) {
    return static_cast<uint8_t>((param >> 2) & 3);
}
inline uint8_t screenUpgradeTier(uint8_t param) {
    return static_cast<uint8_t>(param & 3);
}
inline uint8_t screenUpgradeUnlock(uint8_t param) {
    return static_cast<uint8_t>((param >> 4) & 15);
}

// COND_ARMOR / ACTION_CRAFT_ARMOR param decoders: (slot << 5) | pieceIdx.
inline uint8_t screenArmorPiece(uint8_t param) {
    return static_cast<uint8_t>(param & 31);
}
inline uint8_t screenArmorSlot(uint8_t param) {
    return static_cast<uint8_t>((param >> 5) & 3);
}

// Row condition: 0 = always, zenny >= cost, save flag set, tier < max, quest
// state query, or the smith upgrade availability check (see header note).
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
    case screens::COND_UPGRADE: {
        const uint8_t weapon = screenUpgradeWeapon(row.param);
        const uint8_t tier = screenUpgradeTier(row.param);
        if (weapon >= SAVE_TIER_COUNT || tier == 0 || tier > SCREEN_MAX_TIER)
            return false;
        if (!questUnlocked(save, screenUpgradeUnlock(row.param)))
            return false;
        if (save.tier[weapon] + 1 != tier)
            return false;
        if (save.zenny < row.cost)
            return false;
        // prg.7: the recipe bill. The host/device caller passes the cart def
        // (screens.hpp) so this pure condition stays cart-free; when omitted
        // (a hand-built row with no def) the bill is treated as empty, which is
        // the pre-prg.7 zenny-only content.
        if (!screenRecipeOk(save, row.recipe))
            return false;
        return true;
    }
    case screens::COND_ARMOR: {
        // arm.2: a crafted piece is always live (A toggles equip); an uncrafted
        // one needs the zenny + material bill. The caller fills row.recipe from
        // the mhSmith armor record (screens.hpp) so this stays cart-free.
        const uint8_t piece = screenArmorPiece(row.param);
        const uint8_t slot = screenArmorSlot(row.param);
        if (piece >= armor::PIECE_COUNT || slot >= SAVE_EQUIP_COUNT)
            return false;
        if (saveCrafted(save, piece))
            return true;
        return save.zenny >= row.cost && screenRecipeOk(save, row.recipe);
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
// the tier cap, the zenny cost and the recipe bill (prg.7); COND_UPGRADE rows
// decode the (weapon, tier) pair from `param` (see the header note) and land
// exactly on that tier, while legacy/other rows keep the incremental behaviour
// for the hub stub. Quest rows take/turn in through src/quest_state.hpp
// (turn-in pays the row cost). The recipe bill is re-checked here (not just in
// the condition) so a stale row cannot debit more than the hunter owns.
inline bool screenApplyAction(SaveBlock &save, const ScreenRow &row) {
    switch (row.action) {
    case screens::ACTION_BUY_UPGRADE: {
        uint8_t weapon;
        uint8_t target;
        if (row.cond == screens::COND_UPGRADE) {
            weapon = screenUpgradeWeapon(row.param);
            target = screenUpgradeTier(row.param);
            if (weapon >= SAVE_TIER_COUNT || save.tier[weapon] + 1 != target)
                return false;
        } else {
            weapon = row.param < SAVE_TIER_COUNT ? row.param : 0;
            target = static_cast<uint8_t>(save.tier[weapon] + 1);
        }
        if (target == 0 || target > SCREEN_MAX_TIER || save.zenny < row.cost)
            return false;
        if (!screenRecipeOk(save, row.recipe))
            return false;
        screenRecipeDebit(save, row.recipe);
        save.zenny = static_cast<uint16_t>(save.zenny - row.cost);
        save.tier[weapon] = target;
        return true;
    }
    case screens::ACTION_CRAFT_ARMOR: {
        // arm.2: craft (if needed) then toggle the piece into its slot. Craft
        // debits the material bill + zenny and sets the crafted bit; a second A
        // on a crafted piece unequips it. Re-checks the bill so a stale row
        // cannot debit more than the hunter owns.
        const uint8_t piece = screenArmorPiece(row.param);
        const uint8_t slot = screenArmorSlot(row.param);
        if (piece >= armor::PIECE_COUNT || slot >= SAVE_EQUIP_COUNT)
            return false;
        if (!saveCrafted(save, piece)) {
            if (save.zenny < row.cost)
                return false;
            if (!screenRecipeOk(save, row.recipe))
                return false;
            screenRecipeDebit(save, row.recipe);
            save.zenny = static_cast<uint16_t>(save.zenny - row.cost);
            saveSetCrafted(save, piece);
        }
        armorEquipToggle(save, piece, slot);
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
