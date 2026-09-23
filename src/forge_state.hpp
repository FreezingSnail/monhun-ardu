#pragma once
// Weapon forge-tree runtime logic (bead monhun-ardu-5co.4, docs/ui-design.md
// "Weapon tree model"; trim bead monhun-ardu-5co.7).
//
// Host-testable, no Arduino/cart: the ForgeNode view, the direct-vs-upgrade
// semantics and the forge/equip save mutations. The cart read side (mhForge
// record -> ForgeNode) lives in src/forge.hpp so this header compiles on the
// host with plain structs.
//
// A node is forged either by upgrading an owned parent (cheaper: `cost`/`mats`)
// or directly (pricier: `directCost`/`directMats`); `direct` false means the
// node has no direct path. Forging sets the owned bit; if the parent was the
// equipped node the equipped id follows the child (an upgrade transforms the
// wielded weapon). Skipped nodes stay unowned.
//
// The equipped node replaces the old save class byte (save v5): the hunt glue
// resolves the class + multipliers from the node table. Equipping an owned node
// is a single-slot toggle; "unequipping" the equipped node falls back to
// SAVE_NODE_NONE (the hunt then uses the sword base).
//
// The bill gate/debit (`billAffordable`/`billDebit`) is the shared
// (cost, packed mats) machinery the armor card craft path reuses
// (src/card_state.hpp), so the two card kinds have one debit loop.

#include <stdint.h>
#include "core/save.hpp"
#include "core/progmem.hpp"
#include "generated/forge_meta.hpp"
#include "generated/items_meta.hpp"

namespace mh {

constexpr uint8_t FORGE_MAT_SLOTS = forge::MAT_SLOTS;

// Packed bill material: item is the item index + 1 (0 = empty slot), matching
// the cart record and the armor craft bill.
struct ForgeMat {
    uint8_t item;
    uint8_t count;
};

// Plain view of one forge node, matching the packed mhForge record fields the
// engine reads. The host fills it from the generated constants; the device
// from the cart (src/forge.hpp forgeReadNode).
struct ForgeNode {
    uint8_t index;    // node id (== record index)
    uint8_t cls;      // forge::WEAPON_* (WeaponId)
    uint8_t parent;   // forge::NODE_NONE = root
    uint8_t flags;    // forge::FLAG_DIRECT
    uint8_t dmgMul;   // integer percent, 100 = base
    uint8_t spdMul;
    uint16_t cost;         // upgrade bill (parent owned)
    uint16_t directCost;   // direct-forge bill
    ForgeMat mats[FORGE_MAT_SLOTS];
    ForgeMat directMats[FORGE_MAT_SLOTS];
};

inline bool forgeNodeDirect(const ForgeNode &n) {
    return (n.flags & forge::FLAG_DIRECT) != 0;
}
inline bool forgeHasParent(const ForgeNode &n) {
    return n.parent != forge::NODE_NONE;
}
inline bool forgeParentOwned(const SaveBlock &save, const ForgeNode &n) {
    return forgeHasParent(n) && saveWeaponOwned(save, n.parent);
}

// Shared material-bill gate/debit (ui.4.1). `mats` is `slots` packed
// {itemCode (item idx + 1, 0 = empty), count} byte pairs -- the exact layout of
// a ForgeNode's mats/directMats arrays and of the armor card's baked craft
// bill, so one loop serves both card paths. billShort reports which gate
// failed, so a caller needs one pass for both the reason and the go/no-go.
enum BillShort : uint8_t {
    BILL_OK = 0,
    BILL_NEED_ZENNY = 1,
    BILL_NEED_PARTS = 2
};

// noinline: the gate is shared by the forge and armor card paths, so a single
// out-of-line copy beats one inlined loop per caller (AVR flash).
MH_NOINLINE inline uint8_t billShort(const SaveBlock &save, uint16_t cost, const uint8_t *mats, uint8_t slots) {
    if (save.zenny < cost)
        return BILL_NEED_ZENNY;
    for (uint8_t i = 0; i < slots; i++) {
        const uint8_t code = mats[i * 2];
        if (code == 0)
            continue;
        const uint8_t idx = static_cast<uint8_t>(code - 1);
        if (idx >= item::ITEM_COUNT || save.items[idx] < mats[i * 2 + 1])
            return BILL_NEED_PARTS;
    }
    return BILL_OK;
}

inline bool billAffordable(const SaveBlock &save, uint16_t cost, const uint8_t *mats, uint8_t slots) {
    return billShort(save, cost, mats, slots) == BILL_OK;
}
MH_NOINLINE inline void billDebit(SaveBlock &save, uint16_t cost, const uint8_t *mats, uint8_t slots) {
    for (uint8_t i = 0; i < slots; i++) {
        const uint8_t code = mats[i * 2];
        if (code == 0)
            continue;
        const uint8_t idx = static_cast<uint8_t>(code - 1);
        if (idx < item::ITEM_COUNT)
            save.items[idx] = static_cast<uint8_t>(save.items[idx] - mats[i * 2 + 1]);
    }
    save.zenny = static_cast<uint16_t>(save.zenny - cost);
}

// The active bill's packed mats + cost: the upgrade bill when the parent is
// owned (and the node has one), else the direct bill.
inline const uint8_t *forgeActiveMats(const SaveBlock &save, const ForgeNode &n, uint16_t &cost) {
    if (forgeParentOwned(save, n)) {
        cost = n.cost;
        return &n.mats[0].item;
    }
    cost = n.directCost;
    return &n.directMats[0].item;
}

// The bill the next forge would charge as a value (host-test convenience; the
// shipping card path uses forgeActiveMats directly).
struct ForgeBill {
    uint16_t cost;
    ForgeMat mats[FORGE_MAT_SLOTS];
};

inline ForgeBill forgeActiveBill(const SaveBlock &save, const ForgeNode &n) {
    ForgeBill b;
    const bool up = forgeParentOwned(save, n);
    b.cost = up ? n.cost : n.directCost;
    for (uint8_t i = 0; i < FORGE_MAT_SLOTS; i++)
        b.mats[i] = up ? n.mats[i] : n.directMats[i];
    return b;
}

// Forge outcome; also the card hint source. The values are ordered so the card
// hint map is a simple table (see src/card_state.hpp).
enum ForgeState : uint8_t {
    FORGE_DEAD = 0,   // bad node id, or locked with no owned parent
    FORGE_EQUIPPED,   // owned and currently wielded
    FORGE_OWNED,      // owned, not wielded
    FORGE_UPGRADE,    // parent owned -> upgrade path
    FORGE_DIRECT,     // no owned parent -> direct path
    FORGE_NEED_PARTS,
    FORGE_NEED_ZENNY
};

// Can the save afford the active bill? (pure; used by the state classifier)
inline bool forgeAffordable(const SaveBlock &save, const ForgeNode &n) {
    uint16_t cost;
    const uint8_t *mats = forgeActiveMats(save, n, cost);
    return billAffordable(save, cost, mats, FORGE_MAT_SLOTS);
}

// Classify a node against the save. `nodeCount` bounds the node id (the host
// passes forge::NODE_COUNT; a corrupt cart id classifies DEAD).
inline ForgeState forgeNodeState(const SaveBlock &save, const ForgeNode &n, uint8_t nodeCount) {
    if (n.index >= nodeCount)
        return FORGE_DEAD;
    if (save.equippedNode == n.index)
        return FORGE_EQUIPPED;
    if (saveWeaponOwned(save, n.index))
        return FORGE_OWNED;
    const bool up = forgeParentOwned(save, n);
    if (!up && !forgeNodeDirect(n))
        return FORGE_DEAD;   // locked: no owned parent and no direct path
    uint16_t cost;
    const uint8_t *mats = forgeActiveMats(save, n, cost);
    const uint8_t sh = billShort(save, cost, mats, FORGE_MAT_SLOTS);
    if (sh != BILL_OK)
        return sh == BILL_NEED_ZENNY ? FORGE_NEED_ZENNY : FORGE_NEED_PARTS;
    return up ? FORGE_UPGRADE : FORGE_DIRECT;
}

// Debit the active bill. Caller re-checks the state so a stale card cannot
// over-debit. Pure field math, no cart.
inline void forgeDebit(SaveBlock &save, const ForgeNode &n) {
    uint16_t cost;
    const uint8_t *mats = forgeActiveMats(save, n, cost);
    billDebit(save, cost, mats, FORGE_MAT_SLOTS);
}

// Forge (or upgrade) a node. Returns true when the save changed and must be
// persisted. Re-checks the state: only UPGRADE/DIRECT act. An owned parent that
// is currently equipped moves the equipped id to the child.
inline bool forgeNodeApply(SaveBlock &save, const ForgeNode &n, uint8_t nodeCount) {
    const ForgeState st = forgeNodeState(save, n, nodeCount);
    if (st != FORGE_UPGRADE && st != FORGE_DIRECT)
        return false;
    const bool wasEquipped = forgeParentOwned(save, n) && save.equippedNode == n.parent;
    forgeDebit(save, n);
    saveSetWeaponOwned(save, n.index);
    if (wasEquipped)
        save.equippedNode = n.index;
    return true;
}

// Equip/unequip an owned node. Equipping sets the node; pressing on the
// equipped node unequips to SAVE_NODE_NONE. Refuses unowned nodes (no EEPROM
// write), matching the armor toggle discipline.
inline bool forgeNodeEquipToggle(SaveBlock &save, const ForgeNode &n) {
    if (!saveWeaponOwned(save, n.index))
        return false;
    save.equippedNode = save.equippedNode == n.index ? SAVE_NODE_NONE : n.index;
    return true;
}

}   // namespace mh
