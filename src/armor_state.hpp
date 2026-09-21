#pragma once
// Armor engine runtime logic (bead monhun-ardu-arm.2, docs/equipment-framework.md
// "Armor data"): the equip slots, the craft gate, and the per-hunt stat/skill
// aggregation the combat path (arm.3) reads.
//
// Host-testable, no Arduino/cart: the ArmorPiece view, the crafted/equip save
// helpers, and armorAggregate() over a plain piece array. The cart read side
// (mhArmor record -> ArmorPiece) lives in src/armor.hpp so this header compiles
// on the host with the armor_data.hpp mirror.
//
// Aggregation contract (docs/equipment-framework.md): total defense is the sum
// of the equipped pieces' defense (u16); each resistance is the signed i8 sum;
// each skill's points are summed across the equipped pieces and clamped to
// THRESHOLD_M (the max useful total). The tier is 0 (inert, below THRESHOLD_S),
// 1 (S active) or 2 (M, the clamp). arm.3 applies
// min(points, maxPoints) * perPoint once the tier is nonzero.
//
// Crafted state reuses the save flags byte (core/save.hpp saveCrafted/
// saveSetCrafted), so no save layout/version change: an older record loads with
// no crafted bits. armorEquipToggle() refuses an uncrafted piece, so the equip
// slots can only ever hold crafted ids; aggregation trusts the slots.

#include <stdint.h>
#include "core/save.hpp"
#include "generated/armor_meta.hpp"

namespace mh {

constexpr uint8_t ARMOR_RESIST_COUNT = 4;   // fire, water, ice, thunder
constexpr uint8_t ARMOR_SKILL_SLOTS = 2;    // packed skill slots per piece

static_assert(armor::PIECE_COUNT <= SAVE_CRAFTED_MAX, "crafted bitmask holds every piece");
static_assert(armor::SKILL_SLOTS == ARMOR_SKILL_SLOTS, "skill slot count ABI drift");

// Plain view of one armor piece, matching the packed mhArmor record fields the
// engine reads. The host fills it from armor_data::PIECES; the device from the
// cart (src/armor.hpp armorReadPiece).
struct ArmorPiece {
    uint8_t slot;   // armor::SLOT_*
    uint8_t defense;
    int8_t resist[ARMOR_RESIST_COUNT];
    uint8_t skillCount;
    uint8_t skill[ARMOR_SKILL_SLOTS];   // skill index + 1, 0 = empty
    uint8_t points[ARMOR_SKILL_SLOTS];
};

// Aggregated, threshold-resolved armor stats. Small and cache-friendly: the
// device stores one in Game (armed at hunt start / on equip change).
struct ArmorAgg {
    uint16_t defense;
    int16_t resist[ARMOR_RESIST_COUNT];
    uint8_t points[armor::SKILL_COUNT];   // clamped total per skill
    uint8_t tier[armor::SKILL_COUNT];     // 0 inert, 1 >= S, 2 >= M
};

inline void armorClear(ArmorAgg &out) {
    out.defense = 0;
    for (uint8_t i = 0; i < ARMOR_RESIST_COUNT; i++)
        out.resist[i] = 0;
    for (uint8_t i = 0; i < armor::SKILL_COUNT; i++) {
        out.points[i] = 0;
        out.tier[i] = 0;
    }
}

// Add one equipped piece's contribution. Caller zeroes first (armorClear).
inline void armorAdd(ArmorAgg &out, const ArmorPiece &p) {
    out.defense = static_cast<uint16_t>(out.defense + p.defense);
    for (uint8_t i = 0; i < ARMOR_RESIST_COUNT; i++)
        out.resist[i] = static_cast<int16_t>(out.resist[i] + p.resist[i]);
    for (uint8_t s = 0; s < p.skillCount && s < ARMOR_SKILL_SLOTS; s++) {
        const uint8_t code = p.skill[s];
        if (code == 0)
            continue;
        const uint8_t skill = static_cast<uint8_t>(code - 1);
        if (skill < armor::SKILL_COUNT)
            out.points[skill] = static_cast<uint8_t>(out.points[skill] + p.points[s]);
    }
}

// Clamp the summed points to the max useful total and resolve the tier.
inline void armorFinalize(ArmorAgg &out) {
    for (uint8_t i = 0; i < armor::SKILL_COUNT; i++) {
        uint8_t points = out.points[i];
        if (points > armor::THRESHOLD_M)
            points = armor::THRESHOLD_M;
        out.points[i] = points;
        out.tier[i] = points >= armor::THRESHOLD_M ? 2 : (points >= armor::THRESHOLD_S ? 1 : 0);
    }
}

// Full host aggregation: walk the piece table, add every piece whose index+1 is
// in its own slot (save.equip[slot] == piece + 1), then finalize. The device
// walks only the three equipped slots and calls armorAdd/armorFinalize directly.
inline void armorAggregate(const SaveBlock &save, const ArmorPiece *pieces, uint8_t count, ArmorAgg &out) {
    armorClear(out);
    for (uint8_t i = 0; i < count; i++) {
        const ArmorPiece &p = pieces[i];
        if (p.slot >= SAVE_EQUIP_COUNT)
            continue;
        if (save.equip[p.slot] != static_cast<uint8_t>(i + 1))
            continue;
        armorAdd(out, p);
    }
    armorFinalize(out);
}

// Toggle a crafted piece into/out of its slot. `piece` is the armor::ARMOR_<ID>
// index, `slot` its SLOT_* (the screen row packs both). An uncrafted piece is
// refused (the craft action sets the bit first); equipping only touches its own
// slot, so a second head piece replaces the first.
inline void armorEquipToggle(SaveBlock &save, uint8_t piece, uint8_t slot) {
    if (piece >= armor::PIECE_COUNT || slot >= SAVE_EQUIP_COUNT)
        return;
    if (!saveCrafted(save, piece))
        return;
    const uint8_t id = static_cast<uint8_t>(piece + 1);
    save.equip[slot] = save.equip[slot] == id ? SAVE_EQUIP_NONE : id;
}

}   // namespace mh
