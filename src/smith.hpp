#pragma once
// Cart readers for the smith upgrade table (bead monhun-ardu-4ug, qs.3).
// Device-only (like src/screens.hpp / src/quest.hpp): reads the UpgradeDef
// records out of the mhSmith cart blob through core/fxmem.hpp during the
// screen/render window and resolves the tier multipliers. The host suite
// exercises src/upgrade_state.hpp with plain UpgradeDef structs instead.

#include "core/fxmem.hpp"
#include "generated/smith_meta.hpp"
#include "upgrade_state.hpp"

namespace mh {

// Fake cart pointer for a byte offset into the mhSmith raw_t section.
inline const uint8_t *smithCart(uint16_t off) {
    return reinterpret_cast<const uint8_t *>(static_cast<uint16_t>(static_cast<uint16_t>(mhSmith) + off));
}

// Fixed 7 B records: weaponIdx u8, tier u8, cost u16, dmgMul u8, spdMul u8,
// unlockFlag u8.
inline void smithReadDef(uint8_t index, UpgradeDef &def) {
    const uint16_t off = static_cast<uint16_t>(smith::HEADER_SIZE + smith::RECORD_SIZE * index);
    def.weaponIdx = mhFxReadU8(smithCart(static_cast<uint16_t>(off + smith::UPG_WEAPON_OFF)));
    def.tier = mhFxReadU8(smithCart(static_cast<uint16_t>(off + smith::UPG_TIER_OFF)));
    def.cost = mhFxReadU16(reinterpret_cast<const uint16_t *>(smithCart(static_cast<uint16_t>(off + smith::UPG_COST_OFF))));
    def.dmgMul = mhFxReadU8(smithCart(static_cast<uint16_t>(off + smith::UPG_DMG_OFF)));
    def.spdMul = mhFxReadU8(smithCart(static_cast<uint16_t>(off + smith::UPG_SPD_OFF)));
    def.unlockFlag = mhFxReadU8(smithCart(static_cast<uint16_t>(off + smith::UPG_UNLOCK_OFF)));
}

// Resolve a weapon/tier pair straight off the cart (tier 0 -> 100/100).
inline void smithResolve(uint8_t weapon, uint8_t tier, uint8_t &dmgMul, uint8_t &spdMul) {
    dmgMul = UPGRADE_MUL_BASE;
    spdMul = UPGRADE_MUL_BASE;
    if (tier == 0)
        return;
    for (uint8_t i = 0; i < smith::UPGRADE_COUNT; i++) {
        UpgradeDef def;
        smithReadDef(i, def);
        if (def.weaponIdx == weapon && def.tier == tier) {
            dmgMul = def.dmgMul;
            spdMul = def.spdMul;
            return;
        }
    }
}

}   // namespace mh
