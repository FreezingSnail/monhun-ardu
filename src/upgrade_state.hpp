#pragma once
// Smith upgrade runtime logic (bead monhun-ardu-4ug, docs/quests-shops.md
// "Smith (content model)"; recipe materials bead monhun-ardu-prg.7).
//
// Host-testable, no Arduino/cart: the UpgradeDef struct, the (weapon, tier)
// lookup over a plain def array, the integer-percent multiplier math the
// player damage / move-speed paths consume, and the recipe material debit
// (prg.7). The cart read side (record -> UpgradeDef) lives in src/smith.hpp so
// this header compiles on the host with plain structs.
//
// Multipliers are integer percent: upgradeMul(v, mul) = v * mul / 100 with
// truncation, matching the mock's integer damage style (combat.hpp
// combatMulPercent uses the same rule). mul == 100 is identity, which is the
// tier-0 / no-upgrade default, so a fresh save keeps the sim byte-identical.
//
// Recipes (prg.7): a tier also carries a material bill -- packed {itemIdx+1,
// count} pairs, 0 = empty slot -- alongside the zenny cost. The caller debits
// both through upgradeAffordable() / upgradeDebitRecipe() + the zenny spend;
// `matIdx` 0 is never a real item (ids are 0-based) so slot 0 means empty.
//
// Armor recipes (arm.1) are a second fixed record array packed after the
// weapon tiers in the same mhSmith blob (smith::ARMOR_RECIPES_OFF,
// smith::AREC_*). They carry `armorIdx` (armor::ARMOR_<ID>) + cost + the same
// mat[2] binder, but this header stays weapon-only until the arm.2 craft UI
// resolves them; nothing here assumes the weapon path is the only one forever.

#include <stdint.h>
#include "core/game.hpp"      // ITEM_COUNT / Game::items[] for the recipe checks
#include "core/progmem.hpp"   // MH_NOINLINE

namespace mh {

// Packed recipe material pair count (must match smith::MAT_SLOTS).
constexpr uint8_t UPGRADE_MAT_SLOTS = 2;

// A smith upgrade record, matching the packed 12 B cart layout from
// tools/gen-smith.py (prg.7 appended the two material pairs).
struct UpgradeDef {
    uint8_t weaponIdx;   // WeaponId (W_SWORD..), see smith::WEAPON_*
    uint8_t tier;        // 1..smith::TIER_COUNT
    uint16_t cost;
    uint8_t dmgMul;       // integer percent, 100 = no change
    uint8_t spdMul;       // integer percent, 100 = no change
    uint8_t unlockFlag;   // 0 = always; else 1-based quest whose done bit gates it
    // Recipe bill: mat[i].item is the item index + 1 (0 = empty slot),
    // mat[i].count its required count. `item` is a `mat` binder on device:
    // mat[] is indexed by the generated smith::mat::* ids in the data.
    struct MatSlot {
        uint8_t item;   // item index + 1, 0 = empty
        uint8_t count;
    };
    MatSlot mat[UPGRADE_MAT_SLOTS];
};

constexpr uint8_t UPGRADE_MUL_BASE = 100;

// Integer percent with truncation at each step (no float anywhere).
MH_NOINLINE inline int16_t upgradeMul(int16_t value, uint8_t mul) {
    if (mul == 0)
        return value;   // 0 = unset -> identity, same as 100
    return static_cast<int16_t>((static_cast<int32_t>(value) * mul) / 100);
}

// Linear scan for a (weapon, tier) record. Returns -1 when absent; tier 0 is
// never stored (tier 0 is "no upgrade").
inline int16_t upgradeFind(const UpgradeDef *defs, uint8_t count, uint8_t weapon, uint8_t tier) {
    for (uint8_t i = 0; i < count; i++) {
        if (defs[i].weaponIdx == weapon && defs[i].tier == tier)
            return static_cast<int16_t>(i);
    }
    return -1;
}

// Resolve a weapon/tier pair to its multipliers. Tier 0 or a missing record
// leaves the 100/100 identity.
inline void upgradeResolve(const UpgradeDef *defs, uint8_t count, uint8_t weapon, uint8_t tier, uint8_t &dmgMul, uint8_t &spdMul) {
    dmgMul = UPGRADE_MUL_BASE;
    spdMul = UPGRADE_MUL_BASE;
    const int16_t i = upgradeFind(defs, count, weapon, tier);
    if (i >= 0) {
        dmgMul = defs[i].dmgMul;
        spdMul = defs[i].spdMul;
    }
}

// Is the recipe bill in `mats` satisfied by the save inventory? `owned` is the
// inventory accessor (SaveBlock::items on device, a plain array on host); the
// two-arg form keeps this header free of core/save.hpp. Empty slots (item 0)
// are skipped.
inline bool upgradeRecipeOk(const UpgradeDef &def, const uint8_t *owned, uint8_t ownedCount) {
    for (uint8_t i = 0; i < UPGRADE_MAT_SLOTS; i++) {
        const uint8_t code = def.mat[i].item;
        if (code == 0)
            continue;
        const uint8_t slot = static_cast<uint8_t>(code - 1);
        if (slot >= ownedCount || owned[slot] < def.mat[i].count)
            return false;
    }
    return true;
}

}   // namespace mh
