#pragma once
// Cart readers for the armor piece table (bead monhun-ardu-arm.2). Device-only
// (like src/smith.hpp): reads the packed mhArmor records through core/fxmem.hpp
// during the run/screen window and folds the equipped pieces into the Game's
// ArmorAgg cache. The host suite exercises src/armor_state.hpp with plain
// ArmorPiece structs instead.
//
// Trim (monhun-ardu-5co.8): the device path bulk-reads each packed record in one
// cart transaction instead of one seek per field, walks only the three equipped
// slots, and skips the never-read resist/zenny/mat/sheet bytes. The point-clamp
// + tier resolve shares the effect loop, so the finalize and effects passes are
// one traversal of the five skill records.

#include "core/fxmem.hpp"
#include "core/world.hpp"
#include "generated/armor_meta.hpp"
#include "armor_state.hpp"

namespace mh {

// Fake cart pointer for a byte offset into the mhArmor raw_t section.
inline const uint8_t *armorCart(uint16_t off) {
    return reinterpret_cast<const uint8_t *>(static_cast<uint16_t>(static_cast<uint16_t>(mhArmor) + off));
}

// Resolve the aggregated armor skill points into the combat effect block and
// cache the clamped points/tier for the GEAR readout (arm.3/gs.2). One bulk read
// of the packed 3 B x SKILL_COUNT skill records; the point clamp, tier and
// magnitude all share the loop. `agg` is updated in place (points/tier).
inline void armorResolve(ArmorAgg &agg, ArmorEffects &out) {
    armorEffectsBase(out);
    out.defense = agg.defense;
    uint8_t sktab[armor::SKILL_COUNT * armor::SKILL_SIZE];
    mhFxReadBytes(armorCart(armor::SKILLS_OFF), sktab, sizeof(sktab));
    for (uint8_t i = 0; i < armor::SKILL_COUNT; i++) {
        uint8_t points = agg.points[i];
        if (points > armor::THRESHOLD_M)
            points = armor::THRESHOLD_M;
        agg.points[i] = points;
        const uint8_t tier = points >= armor::THRESHOLD_M ? 2 : (points >= armor::THRESHOLD_S ? 1 : 0);
        agg.tier[i] = tier;
        if (tier == 0)
            continue;
        const uint8_t *sk = &sktab[static_cast<uint8_t>(i * armor::SKILL_SIZE)];
        if (points > sk[armor::SKILL_MAX_OFF])
            points = sk[armor::SKILL_MAX_OFF];
        const uint16_t bonus = static_cast<uint16_t>(points) * sk[armor::SKILL_PER_POINT_OFF];
        if (bonus == 0)
            continue;
        switch (sk[armor::SKILL_KIND_OFF]) {
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

// Cache the save's equipped pieces into g.armor (+ g.armorHead for the render
// slot loop) and resolve the skill magnitudes into g.armorFx (arm.3). A slot
// holding an out-of-range or wrong-slot id is ignored, so a hand-edited/corrupt
// save cannot read past the table. Called at hunt start / camp return, so the
// live hp/stam are re-armed from the resolved maxes (a fresh hunt starts full).
// Aggregates straight into g.armor (one bulk read per equipped piece); the
// resist/zenny/mat/sheet record bytes are never read by the runtime.
inline void armorApplyToGame(Game &g, const SaveBlock &save) {
    ArmorAgg &agg = g.armor;
    for (uint8_t i = 0; i < armor::SKILL_COUNT; i++) {
        agg.points[i] = 0;
        agg.tier[i] = 0;
    }
    agg.defense = 0;
    g.armorHead = SAVE_EQUIP_NONE;
    uint8_t rec[armor::PIECE_SIZE];
    for (uint8_t slot = 0; slot < SAVE_EQUIP_COUNT; slot++) {
        const uint8_t id = save.equip[slot];
        if (id == SAVE_EQUIP_NONE || id > armor::PIECE_COUNT)
            continue;
        mhFxReadBytes(armorCart(static_cast<uint16_t>(armor::PIECES_OFF + armor::PIECE_SIZE * static_cast<uint8_t>(id - 1))), rec, sizeof(rec));
        if (rec[armor::PIECE_SLOT_OFF] != slot)
            continue;
        agg.defense = static_cast<uint16_t>(agg.defense + rec[armor::PIECE_DEFENSE_OFF]);
        for (uint8_t s = 0; s < ARMOR_SKILL_SLOTS; s++) {
            const uint8_t code = rec[static_cast<uint8_t>(armor::PIECE_SKILLS_OFF + s * armor::PIECE_SKILL_STRIDE)];
            if (static_cast<uint8_t>(code - 1) < armor::SKILL_COUNT)
                agg.points[code - 1] = static_cast<uint8_t>(agg.points[code - 1] + rec[static_cast<uint8_t>(armor::PIECE_SKILLS_OFF + s * armor::PIECE_SKILL_STRIDE + 1)]);
        }
        if (slot == armor::SLOT_HEAD)
            g.armorHead = id;
    }
    armorResolve(agg, g.armorFx);
    armorRestoreStats(g.armorFx, g.player.hp, g.player.hpMax, g.player.stam, g.player.stamMax);
}

}   // namespace mh
