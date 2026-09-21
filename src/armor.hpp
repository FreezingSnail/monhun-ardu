#pragma once
// Cart readers for the armor piece table (bead monhun-ardu-arm.2). Device-only
// (like src/smith.hpp): reads the packed mhArmor records through core/fxmem.hpp
// during the run/screen window and folds the equipped pieces into the Game's
// ArmorAgg cache. The host suite exercises src/armor_state.hpp with plain
// ArmorPiece structs instead.

#include "core/fxmem.hpp"
#include "core/world.hpp"
#include "generated/armor_meta.hpp"
#include "armor_state.hpp"

namespace mh {

// Fake cart pointer for a byte offset into the mhArmor raw_t section.
inline const uint8_t *armorCart(uint16_t off) {
    return reinterpret_cast<const uint8_t *>(static_cast<uint16_t>(static_cast<uint16_t>(mhArmor) + off));
}

// Decode piece `index` into the plain engine view (armor_state.hpp ArmorPiece).
inline void armorReadPiece(uint8_t index, ArmorPiece &p) {
    const uint16_t off = static_cast<uint16_t>(armor::PIECES_OFF + armor::PIECE_SIZE * index);
    p.slot = mhFxReadU8(armorCart(static_cast<uint16_t>(off + armor::PIECE_SLOT_OFF)));
    p.defense = mhFxReadU8(armorCart(static_cast<uint16_t>(off + armor::PIECE_DEFENSE_OFF)));
    for (uint8_t i = 0; i < ARMOR_RESIST_COUNT; i++)
        p.resist[i] = static_cast<int8_t>(mhFxReadU8(armorCart(static_cast<uint16_t>(off + armor::PIECE_RESIST_OFF + i))));
    p.skillCount = mhFxReadU8(armorCart(static_cast<uint16_t>(off + armor::PIECE_SKILL_COUNT_OFF)));
    for (uint8_t s = 0; s < ARMOR_SKILL_SLOTS; s++) {
        const uint16_t m = static_cast<uint16_t>(off + armor::PIECE_SKILLS_OFF + s * armor::PIECE_SKILL_STRIDE);
        p.skill[s] = mhFxReadU8(armorCart(m));
        p.points[s] = mhFxReadU8(armorCart(static_cast<uint16_t>(m + 1)));
    }
}

// Decode skill `index` into the plain engine view (armor_state.hpp ArmorSkill).
inline void armorReadSkill(uint8_t index, ArmorSkill &s) {
    const uint16_t off = static_cast<uint16_t>(armor::SKILLS_OFF + armor::SKILL_SIZE * index);
    s.kind = mhFxReadU8(armorCart(static_cast<uint16_t>(off + armor::SKILL_KIND_OFF)));
    s.maxPoints = mhFxReadU8(armorCart(static_cast<uint16_t>(off + armor::SKILL_MAX_OFF)));
    s.perPoint = mhFxReadU8(armorCart(static_cast<uint16_t>(off + armor::SKILL_PER_POINT_OFF)));
    s.pad = 0;
}

// Cache the save's equipped pieces into g.armor (+ g.armorHead for the render
// slot loop) and resolve the skill magnitudes into g.armorFx (arm.3). A slot
// holding an out-of-range or wrong-slot id is ignored, so a hand-edited/corrupt
// save cannot read past the table. Called at hunt start / camp return, so the
// live hp/stam are re-armed from the resolved maxes (a fresh hunt starts full).
inline void armorApplyToGame(Game &g, const SaveBlock &save) {
    ArmorAgg agg;
    armorClear(agg);
    g.armorHead = SAVE_EQUIP_NONE;
    for (uint8_t slot = 0; slot < SAVE_EQUIP_COUNT; slot++) {
        const uint8_t id = save.equip[slot];
        if (id == SAVE_EQUIP_NONE || id > armor::PIECE_COUNT)
            continue;
        ArmorPiece p;
        armorReadPiece(static_cast<uint8_t>(id - 1), p);
        if (p.slot != slot)
            continue;
        armorAdd(agg, p);
        if (slot == armor::SLOT_HEAD)
            g.armorHead = id;
    }
    armorFinalize(agg);
    ArmorSkill skills[armor::SKILL_COUNT];
    for (uint8_t i = 0; i < armor::SKILL_COUNT; i++)
        armorReadSkill(i, skills[i]);
    armorEffects(agg, skills, g.armorFx);
    armorRestoreStats(g.armorFx, g.player.hp, g.player.hpMax, g.player.stam, g.player.stamMax);
    g.armor = agg;
}

}   // namespace mh
