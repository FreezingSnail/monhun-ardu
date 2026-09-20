#pragma once
// Smith upgrade runtime logic (bead monhun-ardu-4ug, docs/quests-shops.md
// "Smith (content model)").
//
// Host-testable, no Arduino/cart: the UpgradeDef struct, the (weapon, tier)
// lookup over a plain def array, and the integer-percent multiplier math the
// player damage / move-speed paths consume. The cart read side (record ->
// UpgradeDef) lives in src/smith.hpp so this header compiles on the host with
// plain structs.
//
// Multipliers are integer percent: upgradeMul(v, mul) = v * mul / 100 with
// truncation, matching the mock's integer damage style (combat.hpp
// combatMulPercent uses the same rule). mul == 100 is identity, which is the
// tier-0 / no-upgrade default, so a fresh save keeps the sim byte-identical.

#include <stdint.h>
#include "core/progmem.hpp"   // MH_NOINLINE

namespace mh {

// A smith upgrade record, matching the packed 7 B cart layout from
// tools/gen-smith.py.
struct UpgradeDef {
    uint8_t weaponIdx;   // WeaponId (W_SWORD..), see smith::WEAPON_*
    uint8_t tier;        // 1..smith::TIER_COUNT
    uint16_t cost;
    uint8_t dmgMul;       // integer percent, 100 = no change
    uint8_t spdMul;       // integer percent, 100 = no change
    uint8_t unlockFlag;   // 0 = always; else 1-based quest whose done bit gates it
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

}   // namespace mh
