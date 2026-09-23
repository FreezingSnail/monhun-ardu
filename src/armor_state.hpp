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

// EVADE_WINDOW i-frame cap (bead monhun-ardu-arm.4): the base dodge iT is 14
// ticks, so the raw M-tier magnitude (+15) would be absurd. The data keeps
// perPoint 1; this bounds the resolved bonus instead (cheaper than a piecewise
// per-point curve, and it survives a hand-built skill table).
constexpr uint8_t ARMOR_EVADE_IT_CAP = 4;

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

// ------------------------------------------------------------- arm.3 effects
// Plain view of one skill record (the packed 3 B mhArmor skill entry): the
// effect kind + the per-point magnitude. The host passes armor_data::SKILLS;
// the device reads the cart records (src/armor.hpp armorReadSkill).
struct ArmorSkill {
    uint8_t kind;        // armor::KIND_*
    uint8_t maxPoints;   // per-skill cap (THRESHOLD_M shipped)
    uint8_t perPoint;    // effect magnitude contributed per skill point
    uint8_t pad;         // host struct padding; device reads fields by name
};

// Resolved armor effects the combat path reads every hit (arm.3). The ArmorAgg
// above is the defense / skill-point cache; this is the magnitude cache armed
// alongside it at hunt start / on equip change (Game::armorFx).
struct ArmorEffects {
    uint16_t defense;   // summed defense + DEFENSE_UP bonus
    uint8_t hpMax;      // HEALTH_UP: 100 + bonus, clamped to 255
    uint8_t stamMax;    // STAMINA_UP: 100 + bonus, clamped to 255
    uint8_t dmgMul;     // ATTACK_UP: 100 + bonus (integer percent, 100 = none)
    uint8_t iT;         // EVADE_WINDOW: extra dodge i-frames
};

// Base (no armor equipped) block: the identity every default Game holds.
inline void armorEffectsBase(ArmorEffects &out) {
    out.defense = 0;
    out.hpMax = 100;
    out.stamMax = 100;
    out.dmgMul = 100;
    out.iT = 0;
}

// Effect magnitude of one aggregated skill: 0 while inert (tier 0), else
// min(points, maxPoints) * perPoint. armorFinalize already clamps points to
// THRESHOLD_M; the extra min keeps a hand-built ArmorAgg honest.
inline uint16_t armorSkillBonus(const ArmorAgg &agg, uint8_t skill, const ArmorSkill &def) {
    if (skill >= armor::SKILL_COUNT || agg.tier[skill] == 0)
        return 0;
    uint8_t points = agg.points[skill];
    if (points > def.maxPoints)
        points = def.maxPoints;
    return static_cast<uint16_t>(points) * def.perPoint;
}

inline uint8_t armorClampStat(uint16_t v) {
    return v > 255 ? 255 : static_cast<uint8_t>(v);
}

// Resolve the cached aggregation + skill table into the combat effect block:
// base defense plus every active skill's magnitude by kind.
inline void armorEffects(const ArmorAgg &agg, const ArmorSkill *skills, ArmorEffects &out) {
    armorEffectsBase(out);
    out.defense = agg.defense;
    for (uint8_t i = 0; i < armor::SKILL_COUNT; i++) {
        const uint16_t bonus = armorSkillBonus(agg, i, skills[i]);
        if (bonus == 0)
            continue;
        switch (skills[i].kind) {
        case armor::KIND_ATTACK_UP:
            out.dmgMul = armorClampStat(static_cast<uint16_t>(100 + bonus));
            break;
        case armor::KIND_DEFENSE_UP:
            out.defense = static_cast<uint16_t>(out.defense + bonus);
            break;
        case armor::KIND_HEALTH_UP:
            out.hpMax = armorClampStat(static_cast<uint16_t>(100 + bonus));
            break;
        case armor::KIND_STAMINA_UP:
            out.stamMax = armorClampStat(static_cast<uint16_t>(100 + bonus));
            break;
        case armor::KIND_EVADE_WINDOW:
            out.iT = bonus > ARMOR_EVADE_IT_CAP ? ARMOR_EVADE_IT_CAP : static_cast<uint8_t>(bonus);
            break;
        default:
            break;
        }
    }
}

// Incoming-damage reduction (arm.3): dmg * 100 / (100 + def), floor 1. Applied
// in playerHurt before the guard/parry branches, so a guard chip is computed
// off the reduced value. int32 so a hostile/garbage def cannot overflow the
// denominator; positive damage never drops below 1.
inline int16_t armorReduce(int16_t dmg, uint16_t def) {
    if (dmg <= 0)
        return dmg;
    const int32_t denom = 100 + static_cast<int32_t>(def);
    const int32_t reduced = (static_cast<int32_t>(dmg) * 100) / denom;
    return reduced < 1 ? 1 : static_cast<int16_t>(reduced);
}

// Hunt start / camp return: arm the live hp/stam fields from the resolved
// effects and top them up, so a hunt with HEALTH_UP starts at its new max.
// Clamping the maxes keeps a corrupt/synthetic bonus from wrapping a u8.
inline void armorRestoreStats(const ArmorEffects &fx, uint8_t &hp, uint8_t &hpMax, uint8_t &stam, uint8_t &stamMax) {
    hpMax = fx.hpMax;
    stamMax = fx.stamMax;
    hp = hpMax;
    stam = stamMax;
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
// slot, so a second head piece replaces the first. Returns true only when the
// slot actually changed (gs.1: the GEAR action reports the save delta).
inline bool armorEquipToggle(SaveBlock &save, uint8_t piece, uint8_t slot) {
    if (piece >= armor::PIECE_COUNT || slot >= SAVE_EQUIP_COUNT)
        return false;
    if (!saveCrafted(save, piece))
        return false;
    const uint8_t id = static_cast<uint8_t>(piece + 1);
    const uint8_t next = save.equip[slot] == id ? SAVE_EQUIP_NONE : id;
    if (next == save.equip[slot])
        return false;
    save.equip[slot] = next;
    return true;
}

}   // namespace mh
