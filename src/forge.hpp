#pragma once
// Cart reader for the weapon forge tree (bead monhun-ardu-5co.4, docs/ui-design.md).
// Device-only (like src/screens.hpp / src/smith.hpp): reads the ForgeNode
// records out of the mhForge cart blob through core/fxmem.hpp during the
// run/render window and resolves the equipped node's class + multipliers. The
// host suite exercises src/forge_state.hpp with plain ForgeNode structs.

#include <stddef.h>
#include "core/fxmem.hpp"
#include "generated/forge_meta.hpp"
#include "forge_state.hpp"

namespace mh {

// The packed node record is exactly the ForgeNode tail starting at `cls`:
// index u8, then cls/parent/flags/dmg/spd (5), cost/directCost (4), mats (4),
// directMats (4) = 17 B, and AVR (like the host) has no padding here and is
// little-endian, so forgeReadNode can bulk-read straight into the struct with
// no field decode.
static_assert(sizeof(ForgeNode) == 18, "ForgeNode must pack to 18 B");
static_assert(offsetof(ForgeNode, cls) == 1, "ForgeNode field order");
static_assert(offsetof(ForgeNode, cost) == 6, "ForgeNode u16 alignment");
static_assert(offsetof(ForgeNode, mats) == 10, "ForgeNode mats after directCost");
static_assert(offsetof(ForgeNode, directMats) == 14, "ForgeNode directMats last");

// Fake cart pointer for a byte offset into the mhForge raw_t section.
inline const uint8_t *forgeCart(uint16_t off) {
    return reinterpret_cast<const uint8_t *>(static_cast<uint16_t>(static_cast<uint16_t>(mhForge) + off));
}

// Read node `index` into `n` (one bulk record fetch, then field decode). An
// out-of-range id yields a node with `index` set (forgeNodeState rejects it as
// FORGE_DEAD before touching the bill fields, so only index/flags/parent need
// defined values here).
inline void forgeReadNode(uint8_t index, ForgeNode &n) {
    n.index = index;
    if (index >= forge::NODE_COUNT) {
        n.parent = forge::NODE_NONE;
        n.flags = 0;
        return;
    }
    // One bulk fetch straight into the struct tail (see the static_asserts
    // above): no per-field decode, no stack staging buffer.
    mhFxReadBytes(forgeCart(static_cast<uint16_t>(forge::HEADER_SIZE + forge::RECORD_SIZE * index)), reinterpret_cast<uint8_t *>(&n.cls), forge::RECORD_SIZE);
}

// Weapon class (WeaponId) of a node id, straight off the cart.
inline uint8_t forgeNodeClass(uint8_t index) {
    if (index >= forge::NODE_COUNT)
        return forge::WEAPON_SWORD;
    return mhFxReadU8(forgeCart(static_cast<uint16_t>(forge::HEADER_SIZE + forge::RECORD_SIZE * index + forge::N_CLASS_OFF)));
}

// The equipped node's class (sword when nothing is equipped) + damage/speed
// multipliers (identity when nothing is equipped).
inline uint8_t forgeEquippedClass(const SaveBlock &save) {
    return save.equippedNode == SAVE_NODE_NONE ? forge::WEAPON_SWORD : forgeNodeClass(save.equippedNode);
}

// forgeEquippedSheet (the equipped node's sheet kind) lives in
// src/forge_state.hpp: it is a plain NODE_SHEET index with no cart access, so
// the host suite drives the same code.

inline void forgeEquippedMul(const SaveBlock &save, uint8_t &dmgMul, uint8_t &spdMul) {
    dmgMul = 100;
    spdMul = 100;
    if (save.equippedNode == SAVE_NODE_NONE || save.equippedNode >= forge::NODE_COUNT)
        return;
    // Only the two multiplier bytes are needed, so read them directly rather
    // than pulling the whole node record through forgeReadNode.
    const uint16_t off = static_cast<uint16_t>(forge::HEADER_SIZE + forge::RECORD_SIZE * save.equippedNode);
    dmgMul = mhFxReadU8(forgeCart(static_cast<uint16_t>(off + forge::N_DMG_OFF)));
    spdMul = mhFxReadU8(forgeCart(static_cast<uint16_t>(off + forge::N_SPD_OFF)));
}

}   // namespace mh
