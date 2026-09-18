#pragma once
// Creature combat data loader (bead monhun-ardu-ljj.2).
//
// Design: docs/creature-framework.md sections 3-9 and 11. This is the single
// production loader shared verbatim by the game (migrations ljj.3-ljj.5), the
// host suites and the Ardens loader test:
//
//   host  -> reads src/generated/combat_data.hpp structs (identity)
//   AVR   -> reads the packed mhCombat blob on the FX cart via mhFxRead* at
//            mhCombat + <combat_meta.hpp offset>
//
// Both backends expose the same typed read functions returning plain value
// structs, so the loader logic above the read layer is one code path. Hot-path
// reads are field-targeted (spawn/problem/hit reads, never bulk table loads);
// live RAM caches (CombatState in Game) keep per-tick reads at zero. Part
// multipliers are resolved natively at hit time, never in a per-tick loop.
//
// Migration A (bead monhun-ardu-ljj.3) wires the monster lunge/sweep through
// the attack cache: chooseAttack loads the attack + first window, the per-tick
// FSM consumes CombatState and performs zero cart reads, and render/debug draw
// the telegraph from the same cached window scalars. Behavior stays
// byte-identical; the migrations that follow (stages/pattern interpreter)
// reuse the same read layer.
//
// Cache budget (docs section 9): CombatProfile 22 B + CombatAttackCache 21 B +
// body box 4 B + hurtbox-list head 2 B + runtime 7 B = 56 B on AVR.

#include <stddef.h>
#include <stdint.h>

#include "game.hpp"   // CombatState caches, Game, Rect

#include "../generated/combat_meta.hpp"

#if !defined(__AVR__)
#include "../generated/combat_data.hpp"   // host mirror (identity reads)
#endif

namespace mh {

// LTO guard (ljj.6 flash fit): the composite record readers and hit resolvers
// are called from several sites (spawn, decision, hit, render). Left inline,
// LTO clones each body per call site and the shipping image grows by kilobytes;
// one out-of-line copy per function is smaller overall. Field accessors stay
// inline on purpose (a call would cost more than the load).
#define MH_COMBAT_NI __attribute__((noinline))

// ------------------------------------------------------------ shared enums
// Mirrors tools/gen-combat.py. Phys is a bitmask (break gating); an attack
// carries exactly one bit. Elements ship inert in v1 (multiplier table lookup
// only; ELEMS is empty until a creature opts in).
enum Phys : uint8_t {
    PHYS_SLASH = 0x01,
    PHYS_BLUNT = 0x02,
    PHYS_SHOT = 0x04
};
enum Element : uint8_t {
    ELEM_NONE = 0,
    ELEM_FIRE = 1,
    ELEM_WATER = 2,
    ELEM_ICE = 3,
    ELEM_THUNDER = 4
};
enum StepKind : uint8_t {
    STEP_ATK = 0,
    STEP_WAIT = 1
};
enum PredOp : uint8_t {
    PRED_GE = 0,
    PRED_LE = 1,
    PRED_EQ = 2
};
// Move kinds (tools/gen-combat.py MOVE_TYPES). Migration A consumes none/lunge;
// charge/hop are schema-reserved for later interpreter work.
enum MoveType : uint8_t {
    MOVE_NONE = 0,
    MOVE_LUNGE = 1,
    MOVE_CHARGE = 2,
    MOVE_HOP = 3
};
enum GuardPlayer : uint8_t {
    GUARD_PLAYER_ATTACKING = 0x01
};
// Stage `flags` bits (tools/gen-combat.py): a bit marks the effect as present
// in the stage. 0x01 is the hurtOn override (v1 convention: the packed record
// carries presence only, and the doc's only example turns hurt off when broken,
// so the bit means "no longer hurtable while this stage is active").
constexpr uint8_t COMBAT_STAGE_FLAG_HURT_OFF = 0x01;
constexpr uint8_t COMBAT_STAGE_FLAG_DMG_MUL = 0x02;
constexpr uint8_t COMBAT_STAGE_FLAG_SPEED_MUL = 0x04;

// Part stages are 2 bits each in CombatState::stages (up to 8 effective parts;
// COMBAT_MAX_PARTS lives in game.hpp with the cache definitions).
constexpr uint8_t COMBAT_STAGE_MAX = 3;
constexpr uint8_t COMBAT_NO_PART = 0xFF;
// Monster::atkIdx sentinel: no attack cached (init / dead / test clear).
constexpr uint8_t COMBAT_NO_ATTACK = 0xFF;
// CombatState::patternIdx sentinel: no pattern cursor active.
constexpr uint8_t COMBAT_NO_PATTERN = 0xFF;

// --------------------------------------------------------- value structs
// Plain value mirrors of the blob records (field order = packed ABI order).
// Tests compare these against the generated host structs / pin values; the
// loader copies the needed ones into the Game caches.

struct CombatCreature {
    uint8_t skeletonIdx, profileIdx;
    uint8_t firstPart, partCount;
    uint8_t firstAttack, attackCount;
    uint8_t firstPattern, patternCount;
    uint8_t w, h, spd;
    uint16_t hp, spawnX, spawnY;
};

struct CombatSkeleton {
    uint8_t firstPart, partCount;
    uint8_t firstAnchor, anchorCount;
};

struct CombatPart {
    CombatBox box;
    uint8_t dmgMul, bodyShare, breakTypes, hurtOn;
    uint8_t physSlash, physBlunt, physShot;
    uint8_t firstStage, stageCount, firstElem, elemCount;
    uint16_t hp;
    uint8_t flags;
};

struct CombatStage {
    uint8_t at, flags, dmgMulOverride, speedMul, stagger, cue;
    uint8_t firstDisable, disableCount, firstEnable, enableCount;
};

struct CombatAnchor {
    int8_t ox, oy;
};

// Spawn-scalar projection (migration B): the creature record fields initMonster
// needs beyond the body box. Read as one burst at spawn.
struct CombatSpawn {
    uint16_t hp, x, y;
    uint8_t spd;
};

struct CombatElem {
    uint8_t elem, mul;
};

struct CombatAttackValue {
    uint8_t moveType, moveSpeedF;
    int8_t moveDx, moveDy;
    uint8_t facing, phys, elem, onHitEffect;
    int8_t onHitPush;
    uint8_t onHitStun, stagger, cue;
    uint8_t firstWindow, windowCount;
    uint16_t windup, active, recover, dmg;
};

struct CombatPattern {
    uint8_t firstStep, stepCount, guardIdx;
};

struct CombatGuard {
    uint8_t minDist, maxDist, hpLo, hpHi, playerFlags, cooldown, chance;
    uint8_t firstPartPred, partPredCount;
};

struct CombatPredicate {
    uint8_t partIdx, op, stage;
};

struct CombatStep {
    uint8_t kind, ref, after, chance;
};

// ============================================================ read layer
// One typed read per record. AVR reads field bytes at the combat_meta offsets
// (one cart access per scalar); the host reads the generated mirror structs.
// Field offsets for the AVR path come from packed ABI mirrors pinned to the
// generated record sizes, so a blob layout drift fails the static_asserts.

#if defined(__AVR__)

namespace detail {

#pragma pack(push, 1)
struct PkCreature {
    uint8_t skeletonIdx, profileIdx;
    uint8_t firstPart, partCount;
    uint8_t firstAttack, attackCount;
    uint8_t firstPattern, patternCount;
    uint8_t w, h, spd;
    uint16_t hp, spawnX, spawnY;
};
struct PkProfile {
    uint8_t engageDist, keepDist, attackDist;
    uint8_t circleNum, circleDen, retreatNum, retreatDen;
    uint8_t staggerMax, staggerDecay, partCount;
    uint16_t cdBase, cdJitter, spawnT, spawnCd, stunRecoverT, staggerRecoverT;
};
struct PkSkeleton {
    uint8_t firstPart, partCount, firstAnchor, anchorCount;
};
struct PkPart {
    int8_t boxOx, boxOy;
    uint8_t boxW, boxH;
    uint8_t dmgMul, bodyShare, breakTypes, hurtOn;
    uint8_t physSlash, physBlunt, physShot;
    uint8_t firstStage, stageCount, firstElem, elemCount;
    uint16_t hp;
    uint8_t flags;
};
struct PkStage {
    uint8_t at, flags, dmgMulOverride, speedMul, stagger, cue;
    uint8_t firstDisable, disableCount, firstEnable, enableCount;
};
struct PkAnchor {
    int8_t ox, oy;
};
struct PkElem {
    uint8_t elem, mul;
};
struct PkAttack {
    uint8_t moveType, moveSpeedF;
    int8_t moveDx, moveDy;
    uint8_t facing, phys, elem, onHitEffect;
    int8_t onHitPush;
    uint8_t onHitStun, stagger, cue;
    uint8_t firstWindow, windowCount;
    uint16_t windup, active, recover, dmg;
};
struct PkWindow {
    uint16_t t0, t1;
    int8_t boxOx, boxOy;
    uint8_t boxW, boxH;
    uint8_t dmgMul, flags;
};
struct PkPattern {
    uint8_t firstStep, stepCount, guardIdx;
};
struct PkGuard {
    uint8_t minDist, maxDist, hpLo, hpHi, playerFlags, cooldown, chance;
    uint8_t firstPartPred, partPredCount;
};
struct PkPredicate {
    uint8_t partIdx, op, stage;
};
struct PkStep {
    uint8_t kind, ref, after, chance;
};
#pragma pack(pop)

static_assert(sizeof(PkCreature) == combat::CREATURE_SIZE, "creature ABI drift");
static_assert(sizeof(PkProfile) == combat::PROFILE_SIZE, "profile ABI drift");
static_assert(sizeof(PkSkeleton) == combat::SKELETON_SIZE, "skeleton ABI drift");
static_assert(sizeof(PkPart) == combat::PART_SIZE, "part ABI drift");
static_assert(sizeof(PkStage) == combat::STAGE_SIZE, "stage ABI drift");
static_assert(sizeof(PkAnchor) == combat::ANCHOR_SIZE, "anchor ABI drift");
static_assert(sizeof(PkElem) == combat::ELEM_SIZE, "elem ABI drift");
static_assert(sizeof(PkAttack) == combat::ATTACK_SIZE, "attack ABI drift");
static_assert(sizeof(PkWindow) == combat::WINDOW_SIZE, "window ABI drift");
static_assert(sizeof(PkPattern) == combat::PATTERN_SIZE, "pattern ABI drift");
static_assert(sizeof(PkGuard) == combat::GUARD_SIZE, "guard ABI drift");
static_assert(sizeof(PkPredicate) == combat::PREDICATE_SIZE, "predicate ABI drift");
static_assert(sizeof(PkStep) == combat::STEP_SIZE, "step ABI drift");
// Packed-pair reads (migration B): combatSkeletonHeadRead and combatPartBoxRead
// read adjacent bytes as one u16, so these pairs must stay contiguous.
static_assert(offsetof(PkSkeleton, partCount) == offsetof(PkSkeleton, firstPart) + 1, "skeleton head pair must stay adjacent");
static_assert(offsetof(PkPart, boxW) == offsetof(PkPart, boxOx) + 2, "part box pairs must stay adjacent");
// attackLoad's scalar burst (migration C): moveType/moveSpeedF and
// firstWindow/windowCount are leading byte pairs and windup..dmg is one
// contiguous 8 B quad in the packed record.
static_assert(offsetof(PkAttack, moveSpeedF) == offsetof(PkAttack, moveType) + 1, "attack move pair must stay adjacent");
static_assert(offsetof(PkAttack, windowCount) == offsetof(PkAttack, firstWindow) + 1, "attack window pair must stay adjacent");
static_assert(offsetof(PkAttack, dmg) == offsetof(PkAttack, windup) + 6, "attack timing quad must stay contiguous");
static_assert(offsetof(PkWindow, boxOx) == offsetof(PkWindow, t1) + 2, "window cache prefix must stay contiguous");
// chooseAttack's pattern head: firstPattern/patternCount are one adjacent pair.
static_assert(offsetof(PkCreature, patternCount) == offsetof(PkCreature, firstPattern) + 1, "pattern head pair must stay adjacent");
// Bulk-read cache mirrors (migration C): these caches are byte-identical to
// their packed blob records, so the profile/guard/step/pattern reads fetch the
// whole record with one mhFxReadBytes transaction instead of field by field.
static_assert(sizeof(CombatGuard) == 9, "guard cache must stay 9 B");
static_assert(sizeof(CombatStep) == 4, "step cache must stay 4 B");
static_assert(sizeof(CombatPattern) == 3, "pattern cache must stay 3 B");
static_assert(offsetof(CombatProfile, partCount) == offsetof(PkProfile, partCount), "profile mirror drift");
static_assert(offsetof(CombatProfile, cdBase) == offsetof(PkProfile, cdBase), "profile mirror drift");
static_assert(offsetof(CombatGuard, partPredCount) == offsetof(PkGuard, partPredCount), "guard mirror drift");
static_assert(offsetof(CombatStep, chance) == offsetof(PkStep, chance), "step mirror drift");
static_assert(offsetof(CombatPattern, guardIdx) == offsetof(PkPattern, guardIdx), "pattern mirror drift");
static_assert(sizeof(CombatWindow) == 9, "window cache must stay 9 B");
static_assert(sizeof(CombatAttackCache) == 21, "attack cache must stay 21 B");
static_assert(sizeof(CombatState) == (PARTS_ENABLED ? 78 : 64), "CombatState must stay 78 B with parts, 64 B without (ljj.6 part caches fold to 1 slot when the blob declares none)");

// Fake cart pointer: the blob lives below 64 KB (generator hard-fails above).
inline uint16_t combatCartAddr(uint16_t off) {
    return static_cast<uint16_t>(static_cast<uint16_t>(mhCombat) + off);
}
inline uint8_t combatReadU8(uint16_t off) {
    return mhFxReadU8(reinterpret_cast<const uint8_t *>(combatCartAddr(off)));
}
inline int8_t combatReadI8(uint16_t off) {
    return static_cast<int8_t>(combatReadU8(off));
}
inline uint16_t combatReadU16(uint16_t off) {
    return mhFxReadU16(reinterpret_cast<const uint16_t *>(combatCartAddr(off)));
}
// Bulk per-record fetch: one transaction into a byte-identical cache mirror.
inline void combatReadBytes(uint16_t off, void *dst, uint16_t n) {
    mhFxReadBytes(reinterpret_cast<const void *>(combatCartAddr(off)), static_cast<uint8_t *>(dst), n);
}

}   // namespace detail

using detail::combatReadI8;
using detail::combatReadU16;
using detail::combatReadU8;

#define MH_COMBAT_FIELD(type, field) static_cast<uint16_t>(offsetof(type, field))

inline CombatCreature combatCreatureRead(uint8_t i) {
    const uint16_t b = static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE);
    CombatCreature v;
    v.skeletonIdx = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, skeletonIdx));
    v.profileIdx = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, profileIdx));
    v.firstPart = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, firstPart));
    v.partCount = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, partCount));
    v.firstAttack = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, firstAttack));
    v.attackCount = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, attackCount));
    v.firstPattern = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, firstPattern));
    v.patternCount = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, patternCount));
    v.w = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, w));
    v.h = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, h));
    v.spd = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, spd));
    v.hp = combatReadU16(b + MH_COMBAT_FIELD(detail::PkCreature, hp));
    v.spawnX = combatReadU16(b + MH_COMBAT_FIELD(detail::PkCreature, spawnX));
    v.spawnY = combatReadU16(b + MH_COMBAT_FIELD(detail::PkCreature, spawnY));
    return v;
}

inline uint8_t combatCreatureProfileIdx(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE + MH_COMBAT_FIELD(detail::PkCreature, profileIdx)));
}

inline uint8_t combatCreatureFirstAttack(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE + MH_COMBAT_FIELD(detail::PkCreature, firstAttack)));
}

inline uint8_t combatCreatureFirstPattern(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE + MH_COMBAT_FIELD(detail::PkCreature, firstPattern)));
}

inline uint8_t combatCreaturePatternCount(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE + MH_COMBAT_FIELD(detail::PkCreature, patternCount)));
}

// Packed firstPattern | patternCount<<8: the two adjacent bytes in one cart
// access (chooseAttack's decision head).
inline uint16_t combatCreaturePatternHeadRead(uint8_t i) {
    return combatReadU16(static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE + MH_COMBAT_FIELD(detail::PkCreature, firstPattern)));
}

inline uint8_t combatCreatureSkeletonIdx(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE + MH_COMBAT_FIELD(detail::PkCreature, skeletonIdx)));
}

inline uint8_t combatCreatureFirstPart(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE + MH_COMBAT_FIELD(detail::PkCreature, firstPart)));
}

inline uint8_t combatCreaturePartCount(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE + MH_COMBAT_FIELD(detail::PkCreature, partCount)));
}

inline CombatSpawn combatCreatureSpawnRead(uint8_t i) {
    const uint16_t b = static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE);
    CombatSpawn v;
    v.spd = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, spd));
    v.hp = combatReadU16(b + MH_COMBAT_FIELD(detail::PkCreature, hp));
    v.x = combatReadU16(b + MH_COMBAT_FIELD(detail::PkCreature, spawnX));
    v.y = combatReadU16(b + MH_COMBAT_FIELD(detail::PkCreature, spawnY));
    return v;
}

// Profile is a byte-identical 22 B mirror: one bulk read at spawn (migration C
// caches the whole record the interpreter consumes).
inline CombatProfile combatProfileRead(uint8_t i) {
    CombatProfile v;
    detail::combatReadBytes(static_cast<uint16_t>(combat::PROFILES_OFF + i * combat::PROFILE_SIZE), &v, sizeof(v));
    return v;
}

// Bulk variant that fills the Game cache in place (spawn: no 22 B copy).
inline void combatProfileLoad(Game &g, uint8_t i) {
    detail::combatReadBytes(static_cast<uint16_t>(combat::PROFILES_OFF + i * combat::PROFILE_SIZE), &g.combat.profile, sizeof(g.combat.profile));
}

inline CombatSkeleton combatSkeletonRead(uint8_t i) {
    const uint16_t b = static_cast<uint16_t>(combat::SKELETONS_OFF + i * combat::SKELETON_SIZE);
    CombatSkeleton v;
    v.firstPart = combatReadU8(b + MH_COMBAT_FIELD(detail::PkSkeleton, firstPart));
    v.partCount = combatReadU8(b + MH_COMBAT_FIELD(detail::PkSkeleton, partCount));
    v.firstAnchor = combatReadU8(b + MH_COMBAT_FIELD(detail::PkSkeleton, firstAnchor));
    v.anchorCount = combatReadU8(b + MH_COMBAT_FIELD(detail::PkSkeleton, anchorCount));
    return v;
}

inline uint8_t combatSkeletonFirstPart(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::SKELETONS_OFF + i * combat::SKELETON_SIZE + MH_COMBAT_FIELD(detail::PkSkeleton, firstPart)));
}

inline uint8_t combatSkeletonPartCount(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::SKELETONS_OFF + i * combat::SKELETON_SIZE + MH_COMBAT_FIELD(detail::PkSkeleton, partCount)));
}

// Packed firstPart | partCount<<8: the two adjacent bytes in one cart access
// (spawn burst).
inline uint16_t combatSkeletonHeadRead(uint8_t i) {
    return combatReadU16(static_cast<uint16_t>(combat::SKELETONS_OFF + i * combat::SKELETON_SIZE + MH_COMBAT_FIELD(detail::PkSkeleton, firstPart)));
}

inline CombatBox combatPartBoxRead(uint8_t i) {
    // boxOx/boxOy and boxW/boxH are adjacent byte pairs: two cart accesses.
    const uint16_t b = static_cast<uint16_t>(combat::PARTS_OFF + i * combat::PART_SIZE);
    const uint16_t o = combatReadU16(b + MH_COMBAT_FIELD(detail::PkPart, boxOx));
    const uint16_t w = combatReadU16(b + MH_COMBAT_FIELD(detail::PkPart, boxW));
    CombatBox v;
    v.ox = static_cast<int8_t>(o & 0xFF);
    v.oy = static_cast<int8_t>(o >> 8);
    v.w = static_cast<uint8_t>(w & 0xFF);
    v.h = static_cast<uint8_t>(w >> 8);
    return v;
}

inline CombatPart combatPartRead(uint8_t i) {
    const uint16_t b = static_cast<uint16_t>(combat::PARTS_OFF + i * combat::PART_SIZE);
    CombatPart v;
    v.box.ox = combatReadI8(b + MH_COMBAT_FIELD(detail::PkPart, boxOx));
    v.box.oy = combatReadI8(b + MH_COMBAT_FIELD(detail::PkPart, boxOy));
    v.box.w = combatReadU8(b + MH_COMBAT_FIELD(detail::PkPart, boxW));
    v.box.h = combatReadU8(b + MH_COMBAT_FIELD(detail::PkPart, boxH));
    v.dmgMul = combatReadU8(b + MH_COMBAT_FIELD(detail::PkPart, dmgMul));
    v.bodyShare = combatReadU8(b + MH_COMBAT_FIELD(detail::PkPart, bodyShare));
    v.breakTypes = combatReadU8(b + MH_COMBAT_FIELD(detail::PkPart, breakTypes));
    v.hurtOn = combatReadU8(b + MH_COMBAT_FIELD(detail::PkPart, hurtOn));
    v.physSlash = combatReadU8(b + MH_COMBAT_FIELD(detail::PkPart, physSlash));
    v.physBlunt = combatReadU8(b + MH_COMBAT_FIELD(detail::PkPart, physBlunt));
    v.physShot = combatReadU8(b + MH_COMBAT_FIELD(detail::PkPart, physShot));
    v.firstStage = combatReadU8(b + MH_COMBAT_FIELD(detail::PkPart, firstStage));
    v.stageCount = combatReadU8(b + MH_COMBAT_FIELD(detail::PkPart, stageCount));
    v.firstElem = combatReadU8(b + MH_COMBAT_FIELD(detail::PkPart, firstElem));
    v.elemCount = combatReadU8(b + MH_COMBAT_FIELD(detail::PkPart, elemCount));
    v.hp = combatReadU16(b + MH_COMBAT_FIELD(detail::PkPart, hp));
    v.flags = combatReadU8(b + MH_COMBAT_FIELD(detail::PkPart, flags));
    return v;
}

// Bulk part/stage fetch for the hit path (ljj.6): the cache mirrors are pinned
// to the packed record bytes, so one mhFxReadBytes transaction returns every
// multiplier/pool field instead of 15+ field reads.
static_assert(sizeof(CombatPart) == combat::PART_SIZE, "part cache must stay 18 B");
inline void combatPartLoad(uint8_t i, CombatPart &v) {
    detail::combatReadBytes(static_cast<uint16_t>(combat::PARTS_OFF + i * combat::PART_SIZE), &v, sizeof(v));
}

inline uint8_t combatPartDmgMul(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::PARTS_OFF + i * combat::PART_SIZE + MH_COMBAT_FIELD(detail::PkPart, dmgMul)));
}
inline uint8_t combatPartBodyShare(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::PARTS_OFF + i * combat::PART_SIZE + MH_COMBAT_FIELD(detail::PkPart, bodyShare)));
}
inline uint8_t combatPartHurtOn(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::PARTS_OFF + i * combat::PART_SIZE + MH_COMBAT_FIELD(detail::PkPart, hurtOn)));
}
inline uint8_t combatPartPhysSlash(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::PARTS_OFF + i * combat::PART_SIZE + MH_COMBAT_FIELD(detail::PkPart, physSlash)));
}
inline uint8_t combatPartPhysBlunt(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::PARTS_OFF + i * combat::PART_SIZE + MH_COMBAT_FIELD(detail::PkPart, physBlunt)));
}
inline uint8_t combatPartPhysShot(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::PARTS_OFF + i * combat::PART_SIZE + MH_COMBAT_FIELD(detail::PkPart, physShot)));
}
inline uint8_t combatPartBreakTypes(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::PARTS_OFF + i * combat::PART_SIZE + MH_COMBAT_FIELD(detail::PkPart, breakTypes)));
}
inline uint8_t combatPartFirstStage(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::PARTS_OFF + i * combat::PART_SIZE + MH_COMBAT_FIELD(detail::PkPart, firstStage)));
}
inline uint8_t combatPartStageCount(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::PARTS_OFF + i * combat::PART_SIZE + MH_COMBAT_FIELD(detail::PkPart, stageCount)));
}
inline uint8_t combatPartFirstElem(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::PARTS_OFF + i * combat::PART_SIZE + MH_COMBAT_FIELD(detail::PkPart, firstElem)));
}
inline uint8_t combatPartElemCount(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::PARTS_OFF + i * combat::PART_SIZE + MH_COMBAT_FIELD(detail::PkPart, elemCount)));
}
inline uint16_t combatPartHp(uint8_t i) {
    return combatReadU16(static_cast<uint16_t>(combat::PARTS_OFF + i * combat::PART_SIZE + MH_COMBAT_FIELD(detail::PkPart, hp)));
}

inline CombatStage combatStageRead(uint8_t i) {
    const uint16_t b = static_cast<uint16_t>(combat::STAGES_OFF + i * combat::STAGE_SIZE);
    CombatStage v;
    v.at = combatReadU8(b + MH_COMBAT_FIELD(detail::PkStage, at));
    v.flags = combatReadU8(b + MH_COMBAT_FIELD(detail::PkStage, flags));
    v.dmgMulOverride = combatReadU8(b + MH_COMBAT_FIELD(detail::PkStage, dmgMulOverride));
    v.speedMul = combatReadU8(b + MH_COMBAT_FIELD(detail::PkStage, speedMul));
    v.stagger = combatReadU8(b + MH_COMBAT_FIELD(detail::PkStage, stagger));
    v.cue = combatReadU8(b + MH_COMBAT_FIELD(detail::PkStage, cue));
    v.firstDisable = combatReadU8(b + MH_COMBAT_FIELD(detail::PkStage, firstDisable));
    v.disableCount = combatReadU8(b + MH_COMBAT_FIELD(detail::PkStage, disableCount));
    v.firstEnable = combatReadU8(b + MH_COMBAT_FIELD(detail::PkStage, firstEnable));
    v.enableCount = combatReadU8(b + MH_COMBAT_FIELD(detail::PkStage, enableCount));
    return v;
}

static_assert(sizeof(CombatStage) == combat::STAGE_SIZE, "stage cache must stay 10 B");
inline void combatStageLoad(uint8_t i, CombatStage &v) {
    detail::combatReadBytes(static_cast<uint16_t>(combat::STAGES_OFF + i * combat::STAGE_SIZE), &v, sizeof(v));
}

inline CombatAnchor combatAnchorRead(uint8_t i) {
    const uint16_t b = static_cast<uint16_t>(combat::ANCHORS_OFF + i * combat::ANCHOR_SIZE);
    CombatAnchor v;
    v.ox = combatReadI8(b + MH_COMBAT_FIELD(detail::PkAnchor, ox));
    v.oy = combatReadI8(b + MH_COMBAT_FIELD(detail::PkAnchor, oy));
    return v;
}

inline CombatElem combatElemRead(uint8_t i) {
    const uint16_t b = static_cast<uint16_t>(combat::ELEMS_OFF + i * combat::ELEM_SIZE);
    CombatElem v;
    v.elem = combatReadU8(b + MH_COMBAT_FIELD(detail::PkElem, elem));
    v.mul = combatReadU8(b + MH_COMBAT_FIELD(detail::PkElem, mul));
    return v;
}

inline uint8_t combatRefRead(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::REFS_OFF + i * combat::REF_SIZE));
}

inline CombatAttackValue combatAttackRead(uint8_t i) {
    const uint16_t b = static_cast<uint16_t>(combat::ATTACKS_OFF + i * combat::ATTACK_SIZE);
    CombatAttackValue v;
    v.moveType = combatReadU8(b + MH_COMBAT_FIELD(detail::PkAttack, moveType));
    v.moveSpeedF = combatReadU8(b + MH_COMBAT_FIELD(detail::PkAttack, moveSpeedF));
    v.moveDx = combatReadI8(b + MH_COMBAT_FIELD(detail::PkAttack, moveDx));
    v.moveDy = combatReadI8(b + MH_COMBAT_FIELD(detail::PkAttack, moveDy));
    v.facing = combatReadU8(b + MH_COMBAT_FIELD(detail::PkAttack, facing));
    v.phys = combatReadU8(b + MH_COMBAT_FIELD(detail::PkAttack, phys));
    v.elem = combatReadU8(b + MH_COMBAT_FIELD(detail::PkAttack, elem));
    v.onHitEffect = combatReadU8(b + MH_COMBAT_FIELD(detail::PkAttack, onHitEffect));
    v.onHitPush = combatReadI8(b + MH_COMBAT_FIELD(detail::PkAttack, onHitPush));
    v.onHitStun = combatReadU8(b + MH_COMBAT_FIELD(detail::PkAttack, onHitStun));
    v.stagger = combatReadU8(b + MH_COMBAT_FIELD(detail::PkAttack, stagger));
    v.cue = combatReadU8(b + MH_COMBAT_FIELD(detail::PkAttack, cue));
    v.firstWindow = combatReadU8(b + MH_COMBAT_FIELD(detail::PkAttack, firstWindow));
    v.windowCount = combatReadU8(b + MH_COMBAT_FIELD(detail::PkAttack, windowCount));
    v.windup = combatReadU16(b + MH_COMBAT_FIELD(detail::PkAttack, windup));
    v.active = combatReadU16(b + MH_COMBAT_FIELD(detail::PkAttack, active));
    v.recover = combatReadU16(b + MH_COMBAT_FIELD(detail::PkAttack, recover));
    v.dmg = combatReadU16(b + MH_COMBAT_FIELD(detail::PkAttack, dmg));
    return v;
}

inline uint16_t combatAttackWindup(uint8_t i) {
    return combatReadU16(static_cast<uint16_t>(combat::ATTACKS_OFF + i * combat::ATTACK_SIZE + MH_COMBAT_FIELD(detail::PkAttack, windup)));
}
inline uint16_t combatAttackActive(uint8_t i) {
    return combatReadU16(static_cast<uint16_t>(combat::ATTACKS_OFF + i * combat::ATTACK_SIZE + MH_COMBAT_FIELD(detail::PkAttack, active)));
}
inline uint16_t combatAttackRecover(uint8_t i) {
    return combatReadU16(static_cast<uint16_t>(combat::ATTACKS_OFF + i * combat::ATTACK_SIZE + MH_COMBAT_FIELD(detail::PkAttack, recover)));
}
inline uint16_t combatAttackDmg(uint8_t i) {
    return combatReadU16(static_cast<uint16_t>(combat::ATTACKS_OFF + i * combat::ATTACK_SIZE + MH_COMBAT_FIELD(detail::PkAttack, dmg)));
}
inline uint8_t combatAttackMoveType(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::ATTACKS_OFF + i * combat::ATTACK_SIZE + MH_COMBAT_FIELD(detail::PkAttack, moveType)));
}
inline uint8_t combatAttackMoveSpeedF(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::ATTACKS_OFF + i * combat::ATTACK_SIZE + MH_COMBAT_FIELD(detail::PkAttack, moveSpeedF)));
}
inline uint8_t combatAttackFacing(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::ATTACKS_OFF + i * combat::ATTACK_SIZE + MH_COMBAT_FIELD(detail::PkAttack, facing)));
}
inline uint8_t combatAttackFirstWindow(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::ATTACKS_OFF + i * combat::ATTACK_SIZE + MH_COMBAT_FIELD(detail::PkAttack, firstWindow)));
}
inline uint8_t combatAttackWindowCount(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::ATTACKS_OFF + i * combat::ATTACK_SIZE + MH_COMBAT_FIELD(detail::PkAttack, windowCount)));
}

// Window cache is a byte-identical 9 B prefix of the packed record (t0, t1,
// box, dmgMul; the record's trailing flags byte is not cached): one bulk read.
inline CombatWindow combatWindowRead(uint8_t i) {
    CombatWindow v;
    detail::combatReadBytes(static_cast<uint16_t>(combat::WINDOWS_OFF + i * combat::WINDOW_SIZE), &v, sizeof(v));
    return v;
}

inline CombatPattern combatPatternRead(uint8_t i) {
    CombatPattern v;
    detail::combatReadBytes(static_cast<uint16_t>(combat::PATTERNS_OFF + i * combat::PATTERN_SIZE), &v, sizeof(v));
    return v;
}

inline uint8_t combatPatternGuardIdx(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::PATTERNS_OFF + i * combat::PATTERN_SIZE + MH_COMBAT_FIELD(detail::PkPattern, guardIdx)));
}
inline uint8_t combatPatternFirstStep(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::PATTERNS_OFF + i * combat::PATTERN_SIZE + MH_COMBAT_FIELD(detail::PkPattern, firstStep)));
}
inline uint8_t combatPatternStepCount(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::PATTERNS_OFF + i * combat::PATTERN_SIZE + MH_COMBAT_FIELD(detail::PkPattern, stepCount)));
}

// Dist-only guard probe (SIMPLE_GUARDS): minDist/maxDist are the
// guard record's leading byte pair, so the range is one u16 cart read. Complex
// guards go through combatGuardPasses instead.
inline uint16_t combatPatternGuardRangeRead(uint8_t i) {
    const uint8_t guardIdx = combatPatternGuardIdx(i);
    return combatReadU16(static_cast<uint16_t>(combat::GUARDS_OFF + guardIdx * combat::GUARD_SIZE));
}

inline CombatGuard combatGuardRead(uint8_t i) {
    CombatGuard v;
    detail::combatReadBytes(static_cast<uint16_t>(combat::GUARDS_OFF + i * combat::GUARD_SIZE), &v, sizeof(v));
    return v;
}

inline CombatPredicate combatPredicateRead(uint8_t i) {
    CombatPredicate v;
    detail::combatReadBytes(static_cast<uint16_t>(combat::PREDICATES_OFF + i * combat::PREDICATE_SIZE), &v, sizeof(v));
    return v;
}

inline CombatStep combatStepRead(uint8_t i) {
    CombatStep v;
    detail::combatReadBytes(static_cast<uint16_t>(combat::STEPS_OFF + i * combat::STEP_SIZE), &v, sizeof(v));
    return v;
}

// Single-field step read (single-step interpreter fast path): one byte, no
// 4 B stack mirror.
inline uint8_t combatStepRef(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::STEPS_OFF + i * combat::STEP_SIZE + static_cast<uint16_t>(offsetof(detail::PkStep, ref))));
}

#undef MH_COMBAT_FIELD

#else   // ------------------------------------------------------------ host

inline CombatCreature combatCreatureRead(uint8_t i) {
    const combat_data::Creature &c = combat_data::CREATURES[i];
    CombatCreature v;
    v.skeletonIdx = c.skeletonIdx;
    v.profileIdx = c.profileIdx;
    v.firstPart = c.firstPart;
    v.partCount = c.partCount;
    v.firstAttack = c.firstAttack;
    v.attackCount = c.attackCount;
    v.firstPattern = c.firstPattern;
    v.patternCount = c.patternCount;
    v.w = c.w;
    v.h = c.h;
    v.spd = c.spd;
    v.hp = c.hp;
    v.spawnX = c.spawnX;
    v.spawnY = c.spawnY;
    return v;
}

inline uint8_t combatCreatureProfileIdx(uint8_t i) {
    return combat_data::CREATURES[i].profileIdx;
}

inline uint8_t combatCreatureFirstAttack(uint8_t i) {
    return combat_data::CREATURES[i].firstAttack;
}

inline uint8_t combatCreatureFirstPattern(uint8_t i) {
    return combat_data::CREATURES[i].firstPattern;
}

inline uint8_t combatCreaturePatternCount(uint8_t i) {
    return combat_data::CREATURES[i].patternCount;
}

inline uint16_t combatCreaturePatternHeadRead(uint8_t i) {
    const combat_data::Creature &c = combat_data::CREATURES[i];
    return static_cast<uint16_t>(static_cast<uint16_t>(c.firstPattern) | (static_cast<uint16_t>(c.patternCount) << 8));
}

inline uint8_t combatCreatureSkeletonIdx(uint8_t i) {
    return combat_data::CREATURES[i].skeletonIdx;
}

inline uint8_t combatCreatureFirstPart(uint8_t i) {
    return combat_data::CREATURES[i].firstPart;
}

inline uint8_t combatCreaturePartCount(uint8_t i) {
    return combat_data::CREATURES[i].partCount;
}

inline CombatSpawn combatCreatureSpawnRead(uint8_t i) {
    const combat_data::Creature &c = combat_data::CREATURES[i];
    CombatSpawn v;
    v.spd = c.spd;
    v.hp = c.hp;
    v.x = c.spawnX;
    v.y = c.spawnY;
    return v;
}

inline CombatProfile combatProfileRead(uint8_t i) {
    const combat_data::Profile &p = combat_data::PROFILES[i];
    CombatProfile v;
    v.engageDist = p.engageDist;
    v.keepDist = p.keepDist;
    v.attackDist = p.attackDist;
    v.circleNum = p.circleNum;
    v.circleDen = p.circleDen;
    v.retreatNum = p.retreatNum;
    v.retreatDen = p.retreatDen;
    v.staggerMax = p.staggerMax;
    v.staggerDecay = p.staggerDecay;
    v.partCount = p.partCount;
    v.cdBase = p.cdBase;
    v.cdJitter = p.cdJitter;
    v.spawnT = p.spawnT;
    v.spawnCd = p.spawnCd;
    v.stunRecoverT = p.stunRecoverT;
    v.staggerRecoverT = p.staggerRecoverT;
    return v;
}

inline void combatProfileLoad(Game &g, uint8_t i) {
    g.combat.profile = combatProfileRead(i);
}

inline CombatSkeleton combatSkeletonRead(uint8_t i) {
    const combat_data::Skeleton &s = combat_data::SKELETONS[i];
    CombatSkeleton v;
    v.firstPart = s.firstPart;
    v.partCount = s.partCount;
    v.firstAnchor = s.firstAnchor;
    v.anchorCount = s.anchorCount;
    return v;
}

inline uint8_t combatSkeletonFirstPart(uint8_t i) {
    return combat_data::SKELETONS[i].firstPart;
}

inline uint8_t combatSkeletonPartCount(uint8_t i) {
    return combat_data::SKELETONS[i].partCount;
}

inline CombatBox combatPartBoxRead(uint8_t i) {
    const combat_data::Part &p = combat_data::PARTS[i];
    CombatBox v;
    v.ox = p.box.ox;
    v.oy = p.box.oy;
    v.w = p.box.w;
    v.h = p.box.h;
    return v;
}

inline uint16_t combatSkeletonHeadRead(uint8_t i) {
    const combat_data::Skeleton &s = combat_data::SKELETONS[i];
    return static_cast<uint16_t>(static_cast<uint16_t>(s.firstPart) | (static_cast<uint16_t>(s.partCount) << 8));
}

inline CombatPart combatPartRead(uint8_t i) {
    const combat_data::Part &p = combat_data::PARTS[i];
    CombatPart v;
    v.box.ox = p.box.ox;
    v.box.oy = p.box.oy;
    v.box.w = p.box.w;
    v.box.h = p.box.h;
    v.dmgMul = p.dmgMul;
    v.bodyShare = p.bodyShare;
    v.breakTypes = p.breakTypes;
    v.hurtOn = p.hurtOn;
    v.physSlash = p.physSlash;
    v.physBlunt = p.physBlunt;
    v.physShot = p.physShot;
    v.firstStage = p.firstStage;
    v.stageCount = p.stageCount;
    v.firstElem = p.firstElem;
    v.elemCount = p.elemCount;
    v.hp = p.hp;
    v.flags = p.flags;
    return v;
}

inline void combatPartLoad(uint8_t i, CombatPart &v) {
    v = combatPartRead(i);
}

inline uint8_t combatPartDmgMul(uint8_t i) {
    return combat_data::PARTS[i].dmgMul;
}
inline uint8_t combatPartBodyShare(uint8_t i) {
    return combat_data::PARTS[i].bodyShare;
}
inline uint8_t combatPartHurtOn(uint8_t i) {
    return combat_data::PARTS[i].hurtOn;
}
inline uint8_t combatPartPhysSlash(uint8_t i) {
    return combat_data::PARTS[i].physSlash;
}
inline uint8_t combatPartPhysBlunt(uint8_t i) {
    return combat_data::PARTS[i].physBlunt;
}
inline uint8_t combatPartPhysShot(uint8_t i) {
    return combat_data::PARTS[i].physShot;
}
inline uint8_t combatPartBreakTypes(uint8_t i) {
    return combat_data::PARTS[i].breakTypes;
}
inline uint8_t combatPartFirstStage(uint8_t i) {
    return combat_data::PARTS[i].firstStage;
}
inline uint8_t combatPartStageCount(uint8_t i) {
    return combat_data::PARTS[i].stageCount;
}
inline uint8_t combatPartFirstElem(uint8_t i) {
    return combat_data::PARTS[i].firstElem;
}
inline uint8_t combatPartElemCount(uint8_t i) {
    return combat_data::PARTS[i].elemCount;
}
inline uint16_t combatPartHp(uint8_t i) {
    return combat_data::PARTS[i].hp;
}

inline CombatStage combatStageRead(uint8_t i) {
    const combat_data::Stage &s = combat_data::STAGES[i];
    CombatStage v;
    v.at = s.at;
    v.flags = s.flags;
    v.dmgMulOverride = s.dmgMulOverride;
    v.speedMul = s.speedMul;
    v.stagger = s.stagger;
    v.cue = s.cue;
    v.firstDisable = s.firstDisable;
    v.disableCount = s.disableCount;
    v.firstEnable = s.firstEnable;
    v.enableCount = s.enableCount;
    return v;
}

inline void combatStageLoad(uint8_t i, CombatStage &v) {
    v = combatStageRead(i);
}

inline CombatAnchor combatAnchorRead(uint8_t i) {
    const combat_data::Anchor &a = combat_data::ANCHORS[i];
    CombatAnchor v;
    v.ox = a.ox;
    v.oy = a.oy;
    return v;
}

inline CombatElem combatElemRead(uint8_t i) {
    const combat_data::Elem &e = combat_data::ELEMS[i];
    CombatElem v;
    v.elem = e.elem;
    v.mul = e.mul;
    return v;
}

inline uint8_t combatRefRead(uint8_t i) {
    return combat_data::REFS[i].attackIdx;
}

inline CombatAttackValue combatAttackRead(uint8_t i) {
    const combat_data::Attack &a = combat_data::ATTACKS[i];
    CombatAttackValue v;
    v.moveType = a.moveType;
    v.moveSpeedF = a.moveSpeedF;
    v.moveDx = a.moveDx;
    v.moveDy = a.moveDy;
    v.facing = a.facing;
    v.phys = a.phys;
    v.elem = a.elem;
    v.onHitEffect = a.onHitEffect;
    v.onHitPush = a.onHitPush;
    v.onHitStun = a.onHitStun;
    v.stagger = a.stagger;
    v.cue = a.cue;
    v.firstWindow = a.firstWindow;
    v.windowCount = a.windowCount;
    v.windup = a.windup;
    v.active = a.active;
    v.recover = a.recover;
    v.dmg = a.dmg;
    return v;
}

inline uint16_t combatAttackWindup(uint8_t i) {
    return combat_data::ATTACKS[i].windup;
}
inline uint16_t combatAttackActive(uint8_t i) {
    return combat_data::ATTACKS[i].active;
}
inline uint16_t combatAttackRecover(uint8_t i) {
    return combat_data::ATTACKS[i].recover;
}
inline uint16_t combatAttackDmg(uint8_t i) {
    return combat_data::ATTACKS[i].dmg;
}
inline uint8_t combatAttackMoveType(uint8_t i) {
    return combat_data::ATTACKS[i].moveType;
}
inline uint8_t combatAttackMoveSpeedF(uint8_t i) {
    return combat_data::ATTACKS[i].moveSpeedF;
}
inline uint8_t combatAttackFacing(uint8_t i) {
    return combat_data::ATTACKS[i].facing;
}
inline uint8_t combatAttackFirstWindow(uint8_t i) {
    return combat_data::ATTACKS[i].firstWindow;
}
inline uint8_t combatAttackWindowCount(uint8_t i) {
    return combat_data::ATTACKS[i].windowCount;
}

inline CombatWindow combatWindowRead(uint8_t i) {
    const combat_data::Window &w = combat_data::WINDOWS[i];
    CombatWindow v;
    v.t0 = w.t0;
    v.t1 = w.t1;
    v.box.ox = w.box.ox;
    v.box.oy = w.box.oy;
    v.box.w = w.box.w;
    v.box.h = w.box.h;
    v.dmgMul = w.dmgMul;
    return v;
}

inline CombatPattern combatPatternRead(uint8_t i) {
    const combat_data::Pattern &p = combat_data::PATTERNS[i];
    CombatPattern v;
    v.firstStep = p.firstStep;
    v.stepCount = p.stepCount;
    v.guardIdx = p.guardIdx;
    return v;
}

inline uint8_t combatPatternGuardIdx(uint8_t i) {
    return combat_data::PATTERNS[i].guardIdx;
}
inline uint8_t combatPatternFirstStep(uint8_t i) {
    return combat_data::PATTERNS[i].firstStep;
}
inline uint8_t combatPatternStepCount(uint8_t i) {
    return combat_data::PATTERNS[i].stepCount;
}

inline uint16_t combatPatternGuardRangeRead(uint8_t i) {
    const combat_data::Guard &gu = combat_data::GUARDS[combat_data::PATTERNS[i].guardIdx];
    return static_cast<uint16_t>(static_cast<uint16_t>(gu.minDist) | (static_cast<uint16_t>(gu.maxDist) << 8));
}

inline CombatGuard combatGuardRead(uint8_t i) {
    const combat_data::Guard &g = combat_data::GUARDS[i];
    CombatGuard v;
    v.minDist = g.minDist;
    v.maxDist = g.maxDist;
    v.hpLo = g.hpLo;
    v.hpHi = g.hpHi;
    v.playerFlags = g.playerFlags;
    v.cooldown = g.cooldown;
    v.chance = g.chance;
    v.firstPartPred = g.firstPartPred;
    v.partPredCount = g.partPredCount;
    return v;
}

inline CombatPredicate combatPredicateRead(uint8_t i) {
    const combat_data::Predicate &p = combat_data::PREDICATES[i];
    CombatPredicate v;
    v.partIdx = p.partIdx;
    v.op = p.op;
    v.stage = p.stage;
    return v;
}

inline CombatStep combatStepRead(uint8_t i) {
    const combat_data::Step &s = combat_data::STEPS[i];
    CombatStep v;
    v.kind = s.kind;
    v.ref = s.ref;
    v.after = s.after;
    v.chance = s.chance;
    return v;
}

inline uint8_t combatStepRef(uint8_t i) {
    return combat_data::STEPS[i].ref;
}

#endif   // __AVR__

// ------------------------------------------------------ body box (migration B)
// combatCreatureBodyBox: the creature's hurt/collide box is its skeleton's
// first body part (docs sections 4/11; per-creature overrides add parts, not a
// replacement body). Also returns the skeleton's part list head, so a spawn
// burst reads skeletonIdx/firstPart/partCount once. Bad ids fall back to
// creature 0 like creatureLoad. Returns false (outputs untouched) when the
// skeleton declares no parts.
inline bool combatCreatureBodyBox(uint8_t creatureId, CombatBox &box, uint8_t &firstPart, uint8_t &partCount) {
    if (creatureId >= combat::CREATURES_COUNT)
        creatureId = 0;
    const uint8_t skeletonIdx = combatCreatureSkeletonIdx(creatureId);
    const uint16_t head = combatSkeletonHeadRead(skeletonIdx);
    const uint8_t count = static_cast<uint8_t>(head >> 8);
    if (count == 0)
        return false;
    firstPart = static_cast<uint8_t>(head & 0xFF);
    partCount = count;
    box = combatPartBoxRead(firstPart);
    return true;
}

// Convenience for callers that only need the box (loader tests, render).
inline bool combatCreatureBodyBox(uint8_t creatureId, CombatBox &box) {
    uint8_t firstPart, partCount;
    return combatCreatureBodyBox(creatureId, box, firstPart, partCount);
}

// ======================================================= cache lifecycle
// Combat part geometry (ljj.6): a part box origin is face-relative (docs
// section 3), so its axis-aligned world rect is the body anchor plus the DIR8
// rotation of (ox, oy); the box itself stays axis-aligned (the body part box
// {0,0,w,h} is therefore the fixed body rect for every facing).
// combatPartRectRot does one rotation into an anchor-relative rect;
// combatPartHitRect rotates the cart-loaded box for the current facing (hit +
// render paths, no cached geometry). The spawn burst precomputes the union of
// every part rect over the 8 facings into partsHurt, so the per-tick target
// sync is plain stores.
inline void combatPartRectRot(int16_t fx, int16_t fy, const CombatBox &body, const CombatBox &b, CombatBox &out) {
    int32_t dx, dy;
    combatFacePoint(fx, fy, b.ox, b.oy, dx, dy);
    int32_t x = static_cast<int32_t>(body.ox) + dx;
    int32_t y = static_cast<int32_t>(body.oy) + dy;
    if (x < -128)
        x = -128;
    else if (x > 127)
        x = 127;
    if (y < -128)
        y = -128;
    else if (y > 127)
        y = 127;
    out.ox = static_cast<int8_t>(x);
    out.oy = static_cast<int8_t>(y);
    out.w = b.w;
    out.h = b.h;
}

// Effective part list accessors: skeleton parts first, then creature overrides.
inline uint8_t combatPartOrdinal(const Game &g, uint8_t partIdx) {
    if (partIdx >= g.combat.bodyFirst && partIdx < static_cast<uint8_t>(g.combat.bodyFirst + g.combat.bodyCount))
        return static_cast<uint8_t>(partIdx - g.combat.bodyFirst);
    if (partIdx >= g.combat.overFirst && partIdx < static_cast<uint8_t>(g.combat.overFirst + g.combat.overCount))
        return static_cast<uint8_t>(g.combat.bodyCount + partIdx - g.combat.overFirst);
    return COMBAT_NO_PART;
}

inline uint8_t combatPartCount(const Game &g) {
    const uint8_t n = static_cast<uint8_t>(g.combat.bodyCount + g.combat.overCount);
    return (n > COMBAT_MAX_PARTS) ? COMBAT_MAX_PARTS : n;
}

inline uint8_t combatPartAt(const Game &g, uint8_t ordinal) {
    if (ordinal < g.combat.bodyCount)
        return static_cast<uint8_t>(g.combat.bodyFirst + ordinal);
    return static_cast<uint8_t>(g.combat.overFirst + (ordinal - g.combat.bodyCount));
}

// Rotated part rect for the current facing (anchor-relative), from a
// cart-loaded box: hit resolution and render use this directly.
inline void combatPartHitRect(const Game &g, const CombatBox &b, CombatBox &out) {
    combatPartRectRot(g.monster.fx, g.monster.fy, g.combat.body, b, out);
}

// combatCreaturePartsLoad: cache the active creature's effective part list
// (skeleton parts then per-creature overrides; docs section 4) into the RAM
// part caches: live pools + the facing-independent hurt envelope (union of
// every part rect over the 8 facings) + list heads. One spawn burst; the hit,
// target and render paths then need at most one box read per part. Only
// compiled when the shipped blob declares pools/stages (PARTS_ENABLED);
// creatures over the ordinal cap keep their extra parts untracked (generator
// still packs them).
inline void combatCreaturePartsLoad(Game &g, uint8_t creatureId) {
    if (!PARTS_ENABLED)
        return;
    // Lazy pool fill: only override parts carry a pool in v1 (skeleton parts
    // are hp 0, so their slot is never read -- combatPartDamage returns early on
    // hpMax == 0). Override part ordinals still start at bodyCount.
    const uint8_t bodyCount = g.combat.bodyCount;
    uint8_t overCount = combatCreaturePartCount(creatureId);
    if (bodyCount >= COMBAT_PART_SLOTS)
        overCount = 0;
    else if (overCount > static_cast<uint8_t>(COMBAT_PART_SLOTS - bodyCount))
        overCount = static_cast<uint8_t>(COMBAT_PART_SLOTS - bodyCount);
    g.combat.overFirst = combatCreatureFirstPart(creatureId);
    g.combat.overCount = overCount;
    for (uint8_t i = 0; i < overCount; i++)
        g.combat.partHp[static_cast<uint8_t>(bodyCount + i)] = combatPartHp(static_cast<uint8_t>(g.combat.overFirst + i));

    // Hurt envelope: a conservative bounding box of every part rect over the
    // 8 DIR8 facings. A part box is a face-relative origin (ox,oy); DIR8 has
    // (16,0)/(0,16) axis steps and (11,11) diagonals, so the rotated origin
    // stays within +/-m of (ox,oy) with m = max(|ox|, |oy|, 11*(|ox|+|oy|)/16).
    // The envelope is the body rect extended by each part's [ox-m, ox+m+w].
    // (Exact per-facet rects are still tested at hit time, so an over-wide
    // target rect can only cost an early-out, never a wrong hit.)
    int16_t minX = g.combat.body.ox, minY = g.combat.body.oy;
    int16_t maxX = static_cast<int16_t>(g.combat.body.ox + g.combat.body.w);
    int16_t maxY = static_cast<int16_t>(g.combat.body.oy + g.combat.body.h);
    const uint8_t count = combatPartCount(g);
    for (uint8_t ord = 0; ord < count; ord++) {
        const uint8_t partIdx = combatPartAt(g, ord);
        const CombatBox b = combatPartBoxRead(partIdx);
        const int16_t ax = (b.ox < 0) ? static_cast<int16_t>(-b.ox) : b.ox;
        const int16_t ay = (b.oy < 0) ? static_cast<int16_t>(-b.oy) : b.oy;
        int16_t m = (ax > ay) ? ax : ay;
        const int16_t diag = static_cast<int16_t>((11 * static_cast<int16_t>(ax + ay)) >> 4);
        if (diag > m)
            m = diag;
        const int16_t x0 = static_cast<int16_t>(g.combat.body.ox + b.ox - m);
        const int16_t y0 = static_cast<int16_t>(g.combat.body.oy + b.oy - m);
        const int16_t x1 = static_cast<int16_t>(g.combat.body.ox + b.ox + m + b.w);
        const int16_t y1 = static_cast<int16_t>(g.combat.body.oy + b.oy + m + b.h);
        if (x0 < minX)
            minX = x0;
        if (y0 < minY)
            minY = y0;
        if (x1 > maxX)
            maxX = x1;
        if (y1 > maxY)
            maxY = y1;
    }
    g.combat.partsHurt.ox = static_cast<int8_t>((minX < -128) ? -128 : (minX > 127 ? 127 : minX));
    g.combat.partsHurt.oy = static_cast<int8_t>((minY < -128) ? -128 : (minY > 127 ? 127 : minY));
    g.combat.partsHurt.w = static_cast<uint8_t>((maxX - minX > 255) ? 255 : (maxX - minX));
    g.combat.partsHurt.h = static_cast<uint8_t>((maxY - minY > 255) ? 255 : (maxY - minY));
}

// creatureCacheReset: identity + runtime caches with no record reads. Migration
// A spawn (initMonster) uses this: attacks only need the creature index (whose
// authored list slot 0/1 is the lunge/sweep), so the profile read burst stays
// out of shipping flash until the pattern interpreter lands (migration C).
inline void creatureCacheReset(Game &g, uint8_t creatureId) {
    g.combat.creature = creatureId;
    g.combat.body = CombatBox{0, 0, 0, 0};
    g.combat.bodyFirst = 0;
    g.combat.bodyCount = 0;
    g.combat.overFirst = 0;
    g.combat.overCount = 0;
    g.combat.stages = 0;
    g.combat.patternIdx = COMBAT_NO_PATTERN;
    g.combat.stepIdx = 0;
    g.combat.stepT = 0;
    g.combat.stagger = 0;
    g.combat.attack = CombatAttackCache{};
    for (uint8_t i = 0; i < COMBAT_PART_SLOTS; i++)
        g.combat.partHp[i] = 0;
}

// creatureLoad: read the creature's profile index + the full profile record
// into the Game cache (spawn burst; bad ids fall back to creature 0). Part
// stages and the pattern cursor reset to intact/idle. The attack cache is
// cleared until attackLoad picks an attack.
inline uint8_t creatureLoad(Game &g, uint8_t creatureId) {
    if (creatureId >= combat::CREATURES_COUNT)
        creatureId = 0;
    const uint8_t profileIdx = combatCreatureProfileIdx(creatureId);
    creatureCacheReset(g, creatureId);
    combatProfileLoad(g, profileIdx);
    // Migration B: the hurtbox-list head (skeleton parts) + body box cache, so
    // a loader-only caller resolves hits without extra cart reads.
    combatCreatureBodyBox(creatureId, g.combat.body, g.combat.bodyFirst, g.combat.bodyCount);
    // ljj.6: effective part list + pools (HAS_PARTS data only).
    combatCreaturePartsLoad(g, creatureId);
    return creatureId;
}

// attackWindowLoad: refresh the cached window during the active phase. This is
// the only mid-attack cart read; per-tick active code consumes the cache.
inline void attackWindowLoad(Game &g, uint8_t windowIdx) {
    if (windowIdx >= combat::WINDOWS_COUNT)
        return;
    g.combat.attack.winIdx = windowIdx;
    g.combat.attack.win = combatWindowRead(windowIdx);
}

// attackLoad: cache the attack scalars + first window (attack start). Returns
// the attack index actually loaded; bad ids fall back to attack 0.
inline uint8_t attackLoad(Game &g, uint8_t attackIdx) {
    if (attackIdx >= combat::ATTACKS_COUNT)
        attackIdx = 0;
#ifdef __AVR__
    // Scalar burst in four cart accesses: the moveType/moveSpeedF byte pair,
    // facing, the firstWindow/windowCount pair, and the contiguous u16 quad
    // windup..dmg. Cache field order is ABI-pinned to the packed record by the
    // static_asserts at the top of this file.
    const uint16_t b = static_cast<uint16_t>(combat::ATTACKS_OFF + attackIdx * combat::ATTACK_SIZE);
    const uint16_t mv = combatReadU16(b + static_cast<uint16_t>(offsetof(detail::PkAttack, moveType)));
    g.combat.attack.moveType = static_cast<uint8_t>(mv & 0xFF);
    g.combat.attack.moveSpeedF = static_cast<uint8_t>(mv >> 8);
    g.combat.attack.facing = combatReadU8(b + static_cast<uint16_t>(offsetof(detail::PkAttack, facing)));
    const uint16_t fw = combatReadU16(b + static_cast<uint16_t>(offsetof(detail::PkAttack, firstWindow)));
    detail::combatReadBytes(static_cast<uint16_t>(b + offsetof(detail::PkAttack, windup)), &g.combat.attack.windup, 8);
    attackWindowLoad(g, static_cast<uint8_t>(fw & 0xFF));
#else
    g.combat.attack.windup = combatAttackWindup(attackIdx);
    g.combat.attack.active = combatAttackActive(attackIdx);
    g.combat.attack.recover = combatAttackRecover(attackIdx);
    g.combat.attack.dmg = combatAttackDmg(attackIdx);
    g.combat.attack.moveType = combatAttackMoveType(attackIdx);
    g.combat.attack.moveSpeedF = combatAttackMoveSpeedF(attackIdx);
    g.combat.attack.facing = combatAttackFacing(attackIdx);
    attackWindowLoad(g, combatAttackFirstWindow(attackIdx));
#endif
    return attackIdx;
}

// combatTick: pattern step countdown; deliberately performs no cart reads
// (cache-warm steady state). Stagger/decay handling lands with migration C.
inline void combatTick(Game &g) {
    if (g.combat.stepT > 0)
        g.combat.stepT--;
}

// patternStateSet: test/migration helper to install a pattern cursor.
inline void patternStateSet(Game &g, uint8_t patternIdx, uint8_t stepIdx, uint8_t stepT) {
    g.combat.patternIdx = patternIdx;
    g.combat.stepIdx = stepIdx;
    g.combat.stepT = stepT;
}

// =========================================================== part stages
// 2 bits per part, saturating at COMBAT_STAGE_MAX; parts >= COMBAT_MAX_PARTS
// are not stage-tracked (v1 data uses one part per creature).
inline uint8_t combatStageOf(uint16_t stages, uint8_t partIdx) {
    if (partIdx >= COMBAT_MAX_PARTS)
        return 0;
    return static_cast<uint8_t>((stages >> (partIdx * 2)) & 0x03);
}

inline uint8_t combatPartStageGet(const Game &g, uint8_t partIdx) {
    return combatStageOf(g.combat.stages, partIdx);
}

inline void combatPartStageSet(Game &g, uint8_t partIdx, uint8_t stage) {
    if (partIdx >= COMBAT_MAX_PARTS)
        return;
    if (stage > COMBAT_STAGE_MAX)
        stage = COMBAT_STAGE_MAX;
    const uint16_t shift = static_cast<uint16_t>(partIdx * 2);
    g.combat.stages = static_cast<uint16_t>((g.combat.stages & ~(static_cast<uint16_t>(0x03) << shift)) | (static_cast<uint16_t>(stage) << shift));
}

// One descending threshold fold: pct <= at advances the crossed-stage ordinal.
inline uint8_t combatStageCross(uint8_t ordinal, uint8_t pct, uint8_t at) {
    return (pct <= at) ? static_cast<uint8_t>(ordinal + 1) : ordinal;
}

// Stage ordinal for a part's remaining HP (0 = intact); reads only that part's
// stage records, no bulk loads.
inline uint8_t combatPartStageForHp(uint8_t partIdx, uint16_t hp, uint16_t hpMax) {
    const uint8_t count = combatPartStageCount(partIdx);
    if (count == 0 || hpMax == 0)
        return 0;
    const uint8_t first = combatPartFirstStage(partIdx);
    const uint8_t pct = static_cast<uint8_t>((static_cast<uint32_t>(hp) * 100u) / hpMax);
    uint8_t stage = 0;
    for (uint8_t i = 0; i < count; i++)
        stage = combatStageCross(stage, pct, combatStageRead(first + i).at);
    return (stage > COMBAT_STAGE_MAX) ? COMBAT_STAGE_MAX : stage;
}

// Stage effect projections (data only; the interpreter consumes these once the
// migrations land). Flags mark presence; a crossed stage overrides the part
// value it declares.
inline uint8_t combatStageDmgMul(const CombatStage &s, uint8_t base) {
    return (s.flags & COMBAT_STAGE_FLAG_DMG_MUL) ? s.dmgMulOverride : base;
}
inline uint8_t combatStageSpeedMul(const CombatStage &s, uint8_t base) {
    return (s.flags & COMBAT_STAGE_FLAG_SPEED_MUL) ? s.speedMul : base;
}
inline uint8_t combatStageStagger(const CombatStage &s) {
    return s.stagger;
}
inline uint8_t combatStageCue(const CombatStage &s) {
    return s.cue;
}
inline bool combatStageHurtOff(const CombatStage &s) {
    return (s.flags & COMBAT_STAGE_FLAG_HURT_OFF) != 0;
}

// Effective part dmgMul with crossed stage overrides applied in order. Two FX
// reads when the part has no stages (dmgMul + stageCount), so hit resolution
// stays cheap.
inline uint8_t combatPartDmgMulNow(const Game &g, uint8_t partIdx) {
    uint8_t mul = combatPartDmgMul(partIdx);
    const uint8_t count = combatPartStageCount(partIdx);
    if (count == 0)
        return mul;
    const uint8_t stage = combatPartStageGet(g, partIdx);
    if (stage == 0)
        return mul;
    const uint8_t first = combatPartFirstStage(partIdx);
    for (uint8_t i = 0; i < count && i < stage; i++)
        mul = combatStageDmgMul(combatStageRead(first + i), mul);
    return mul;
}

// part hurtOff: base hurtOn == 0, or any crossed stage declares hurt off.
inline bool combatPartHurtOff(const Game &g, uint8_t partIdx) {
    if (!combatPartHurtOn(partIdx))
        return true;
    const uint8_t count = combatPartStageCount(partIdx);
    if (count == 0)
        return false;
    const uint8_t stage = combatPartStageGet(g, partIdx);
    if (stage == 0)
        return false;
    const uint8_t first = combatPartFirstStage(partIdx);
    bool off = false;
    for (uint8_t i = 0; i < count && i < stage; i++) {
        if (combatStageHurtOff(combatStageRead(first + i)))
            off = true;
    }
    return off;
}

inline uint8_t combatPartSpeedMulNow(const Game &g, uint8_t partIdx) {
    uint8_t mul = 100;
    const uint8_t count = combatPartStageCount(partIdx);
    if (count == 0)
        return mul;
    const uint8_t stage = combatPartStageGet(g, partIdx);
    if (stage == 0)
        return mul;
    const uint8_t first = combatPartFirstStage(partIdx);
    for (uint8_t i = 0; i < count && i < stage; i++)
        mul = combatStageSpeedMul(combatStageRead(first + i), mul);
    return mul;
}

inline uint8_t combatPartStaggerNow(const Game &g, uint8_t partIdx) {
    uint8_t stagger = 0;
    const uint8_t count = combatPartStageCount(partIdx);
    if (count == 0)
        return stagger;
    const uint8_t stage = combatPartStageGet(g, partIdx);
    if (stage == 0)
        return stagger;
    const uint8_t first = combatPartFirstStage(partIdx);
    for (uint8_t i = 0; i < count && i < stage; i++)
        stagger = combatStageStagger(combatStageRead(first + i));
    return stagger;
}

inline uint8_t combatPartCueNow(const Game &g, uint8_t partIdx) {
    uint8_t cue = 0;
    const uint8_t count = combatPartStageCount(partIdx);
    if (count == 0)
        return cue;
    const uint8_t stage = combatPartStageGet(g, partIdx);
    if (stage == 0)
        return cue;
    const uint8_t first = combatPartFirstStage(partIdx);
    for (uint8_t i = 0; i < count && i < stage; i++)
        cue = combatStageCue(combatStageRead(first + i));
    return cue;
}

// combatAttackDisabled: scan the creature's effective parts, apply crossed
// stage disable/enable refs in order; the last match wins. Stage-less data
// short-circuits after stageCount (no ref reads).
inline bool combatAttackDisabled(const Game &g, uint8_t attackIdx) {
    // Only per-creature override parts can carry attack disable/enable refs
    // (the generator rejects skeleton-part stage attack refs), so the effective
    // override list cached at spawn is the whole search space; no creature or
    // skeleton record reads.
    bool disabled = false;
    const uint8_t first = g.combat.overFirst;
    const uint8_t count = g.combat.overCount;
    for (uint8_t p = 0; p < count; p++) {
        const uint8_t partIdx = static_cast<uint8_t>(first + p);
        const uint8_t stage = combatPartStageGet(g, partIdx);
        if (stage == 0)
            continue;
        const uint8_t stageCount = combatPartStageCount(partIdx);
        if (stageCount == 0)
            continue;
        const uint8_t firstStage = combatPartFirstStage(partIdx);
        for (uint8_t s = 0; s < stageCount && s < stage; s++) {
            const CombatStage st = combatStageRead(firstStage + s);
            for (uint8_t d = 0; d < st.disableCount; d++) {
                if (combatRefRead(static_cast<uint8_t>(st.firstDisable + d)) == attackIdx)
                    disabled = true;
            }
            for (uint8_t e = 0; e < st.enableCount; e++) {
                if (combatRefRead(static_cast<uint8_t>(st.firstEnable + e)) == attackIdx)
                    disabled = false;
            }
        }
    }
    return disabled;
}

// ========================================================== guard eval
// Deterministic, tick-derived chance (docs section 6): no RNG state.
// hash(tick, creature, pattern, step) % 100 < chance.
inline uint8_t combatChanceRoll(uint16_t tick, uint8_t creature, uint8_t pattern, uint8_t step) {
    uint16_t h = static_cast<uint16_t>(tick ^ 0x9E37u);
    h = static_cast<uint16_t>((h ^ creature) * 0x85EBu);
    h = static_cast<uint16_t>((h ^ pattern) * 0xC2B2u);
    h = static_cast<uint16_t>((h ^ step) * 0x27D4u);
    h ^= static_cast<uint16_t>(h >> 7);
    return static_cast<uint8_t>(h % 100u);
}

inline bool combatChancePasses(uint16_t tick, uint8_t creature, uint8_t pattern, uint8_t step, uint8_t chance) {
    if (chance >= 100)
        return true;
    if (chance == 0)
        return false;
    return combatChanceRoll(tick, creature, pattern, step) < chance;
}

// Inclusive integer ranges (docs section 6; spike finding 2).
inline bool combatGuardDistOk(uint8_t minDist, uint8_t maxDist, uint8_t dist) {
    return dist >= minDist && dist <= maxDist;
}
inline bool combatGuardHpOk(uint8_t hpLo, uint8_t hpHi, uint8_t hpPct) {
    return hpPct >= hpLo && hpPct <= hpHi;
}
inline bool combatGuardPlayerOk(uint8_t required, uint8_t have) {
    return (required & static_cast<uint8_t>(~have)) == 0;
}
// `sinceUse` is ticks since this pattern last ran (0xFFFF when never).
inline bool combatGuardCooldownOk(uint8_t cooldown, uint16_t sinceUse) {
    return sinceUse >= cooldown;
}

inline bool combatPredicatePasses(const CombatPredicate &p, uint16_t stages) {
    const uint8_t stage = combatStageOf(stages, p.partIdx);
    switch (p.op) {
    case PRED_GE:
        return stage >= p.stage;
    case PRED_LE:
        return stage <= p.stage;
    case PRED_EQ:
        return stage == p.stage;
    default:
        return false;
    }
}

struct CombatGuardInput {
    uint8_t dist;          // integer px to the player
    uint8_t hpPct;         // remaining creature HP percent (0..100)
    uint8_t playerFlags;   // GuardPlayer bits observed on the player
    uint16_t tick;         // decision tick (chance is tick-derived)
    uint16_t sinceUse;     // ticks since this pattern last ran (0xFFFF = never)
    uint8_t stepIdx;       // chance hash step input (0 at decision time)
};

// First-match-wins selection calls this in pattern list order (docs section 6).
// The clause checks the shipped data does not use are compile-time folded by
// the per-clause data facts (same pattern as the interpreter gating): the full
// evaluator returns as soon as a creature opts a clause back in.
inline bool combatGuardPasses(const Game &g, uint8_t patternIdx, const CombatGuardInput &in) {
    if (patternIdx >= combat::PATTERNS_COUNT)
        return false;
    const CombatGuard gu = combatGuardRead(combatPatternGuardIdx(patternIdx));
    if (!combatGuardDistOk(gu.minDist, gu.maxDist, in.dist))
        return false;
    if (combat::HAS_GUARD_HP && !combatGuardHpOk(gu.hpLo, gu.hpHi, in.hpPct))
        return false;
    if (combat::HAS_GUARD_PLAYER && !combatGuardPlayerOk(gu.playerFlags, in.playerFlags))
        return false;
    if (combat::HAS_GUARD_COOLDOWN && !combatGuardCooldownOk(gu.cooldown, in.sinceUse))
        return false;
    if (GUARD_PARTS_ENABLED) {
        for (uint8_t i = 0; i < gu.partPredCount; i++) {
            if (!combatPredicatePasses(combatPredicateRead(static_cast<uint8_t>(gu.firstPartPred + i)), g.combat.stages))
                return false;
        }
    }
    if (combat::HAS_GUARD_CHANCE)
        return combatChancePasses(in.tick, g.combat.creature, patternIdx, in.stepIdx, gu.chance);
    return true;
}

// ======================================================= damage routing
// docs section 3: integer percent, truncating division at each step, 32-bit
// intermediates. Overlap resolution (section 4): highest final multiplier wins,
// tie -> lowest part id. Box overlap itself is caller-side (native, migration B);
// `candidates` are the overlapping global part indices.

struct CombatHitResult {
    uint8_t partIdx;    // COMBAT_NO_PART when no candidate was hurtable
    uint32_t mul;       // winning final part multiplier (percent)
    uint16_t partDmg;   // routed to the part pool
    uint16_t bodyDmg;   // routed to the creature body pool
    uint8_t stagger;    // attack stagger x winning multiplier
};

inline uint32_t combatMulPercent(uint32_t value, uint8_t mul) {
    return (value * mul) / 100u;
}

// Part multiplier chain with truncation at each step: dmgMul, then phys, then
// elem (elem == NONE or absent -> 100).
inline uint32_t combatPartMul(uint8_t dmgMul, uint8_t physMul, uint8_t elemMul) {
    return combatMulPercent(combatMulPercent(dmgMul, physMul), elemMul);
}

inline bool combatMulBeats(uint32_t mul, uint8_t idx, uint32_t bestMul, uint8_t bestIdx) {
    return mul > bestMul || (mul == bestMul && idx < bestIdx);
}

inline uint8_t combatPartPhysMul(uint8_t partIdx, uint8_t phys) {
    if (phys & PHYS_SLASH)
        return combatPartPhysSlash(partIdx);
    if (phys & PHYS_BLUNT)
        return combatPartPhysBlunt(partIdx);
    if (phys & PHYS_SHOT)
        return combatPartPhysShot(partIdx);
    return 100;
}

inline uint8_t combatPartElemMul(uint8_t partIdx, uint8_t elem) {
    if (elem == ELEM_NONE)
        return 100;
    const uint8_t first = combatPartFirstElem(partIdx);
    const uint8_t count = combatPartElemCount(partIdx);
    for (uint8_t i = 0; i < count; i++) {
        const CombatElem e = combatElemRead(static_cast<uint8_t>(first + i));
        if (e.elem == elem)
            return e.mul;
    }
    return 100;
}

inline CombatHitResult combatResolveHit(const Game &g, int32_t base, uint8_t phys, uint8_t elem, uint8_t windowDmgMul, uint8_t attackStagger, const uint8_t *candidates, uint8_t count) {
    CombatHitResult r;
    r.partIdx = COMBAT_NO_PART;
    r.mul = 0;
    r.partDmg = 0;
    r.bodyDmg = 0;
    r.stagger = 0;

    uint32_t bestMul = 0;
    uint8_t bestIdx = COMBAT_NO_PART;
    for (uint8_t i = 0; i < count; i++) {
        const uint8_t partIdx = candidates[i];
        if (partIdx >= combat::PARTS_COUNT)
            continue;
        if (combatPartHurtOff(g, partIdx))
            continue;
        const uint32_t mul = combatPartMul(combatPartDmgMulNow(g, partIdx), combatPartPhysMul(partIdx, phys), combatPartElemMul(partIdx, elem));
        if (bestIdx == COMBAT_NO_PART || combatMulBeats(mul, partIdx, bestMul, bestIdx)) {
            bestMul = mul;
            bestIdx = partIdx;
        }
    }
    if (bestIdx == COMBAT_NO_PART)
        return r;

    uint32_t out = static_cast<uint32_t>(base);
    out = combatMulPercent(out, windowDmgMul);
    out = combatMulPercent(out, combatPartDmgMulNow(g, bestIdx));
    out = combatMulPercent(out, combatPartPhysMul(bestIdx, phys));
    out = combatMulPercent(out, combatPartElemMul(bestIdx, elem));

    r.partIdx = bestIdx;
    r.mul = bestMul;
    r.partDmg = (out > 0xFFFFu) ? 0xFFFFu : static_cast<uint16_t>(out);
    const uint32_t body = combatMulPercent(out, combatPartBodyShare(bestIdx));
    r.bodyDmg = (body > 0xFFFFu) ? 0xFFFFu : static_cast<uint16_t>(body);
    const uint32_t stag = combatMulPercent(attackStagger, static_cast<uint8_t>(bestMul > 255u ? 255u : bestMul));
    r.stagger = (stag > 255u) ? 255u : static_cast<uint8_t>(stag);
    return r;
}

// Shipping body-hit result (migration B): small enough to return in registers
// on AVR. dmg is the routed damage; mul the winning multiplier percent.
struct CombatBodyHit {
    uint8_t partIdx;   // COMBAT_NO_PART when no candidate was hurtable
    uint8_t mul;
    uint16_t dmg;
};

// combatResolveBodyHit: resolve a landed player attack against the creature's
// hurtbox list. The list head (skeleton parts) is cached at spawn; per-creature
// override parts merge into it when a creature carries them (none do today --
// combatAttackDisabled already walks both lists for attack gating). The caller
// has already gated overlap against the RAM Target::rect, so the cached list is
// the candidate set.
//
// Shipping hit-time path (migration B): two FX reads (hurtOn, dmgMul) on a
// landed hit only, never per tick. The shipped 3 declare exactly one body part
// per skeleton, so the cached head is the winning candidate; the per-part
// selection rule (highest final multiplier, tie -> lowest part id) lands with
// the first multi-part creature. Every shipped multiplier is 100, so the
// damage result is the base value unchanged; combatResolveHit above is the
// stage-aware reference path that applies the section-3 chain and is exercised
// by the host/device loader suites.
inline CombatBodyHit combatResolveBodyHit(const Game &g, int32_t base) {
    CombatBodyHit r;
    r.partIdx = COMBAT_NO_PART;
    r.mul = 0;
    r.dmg = 0;

    const uint8_t partIdx = g.combat.bodyFirst;
    if (g.combat.bodyCount == 0 || partIdx >= combat::PARTS_COUNT || !combatPartHurtOn(partIdx))
        return r;

    r.partIdx = partIdx;
    r.mul = combatPartDmgMul(partIdx);
    r.dmg = (base <= 0) ? 0 : ((base > 0xFFFF) ? 0xFFFF : static_cast<uint16_t>(base));
    return r;
}

// ================================================== breakable parts (ljj.6)
// Shipping multi-part hit path (docs section 4). Effective part ordinals run
// skeleton parts first, then the per-creature override list; each candidate is
// one bulk part read (multipliers + pool fields) rotated into its current-facing
// rect. The reference damage resolver above (combatResolveHit) stays the generic
// stage-aware path the suites pin; this is the lean shipping projection:
// containment -> highest final multiplier (tie -> lowest part id) -> section-3
// damage chain -> pool drain + break gating.

// One part's effective values: base record fields plus crossed-stage overrides.
// Single stage walk (the reference suite helpers project one value each).
struct CombatPartNow {
    CombatBox box;
    uint16_t hpMax;
    uint8_t dmgMul, bodyShare, breakTypes;
    uint8_t stagger, speedMul, cue;
    uint8_t physSlash, physBlunt, physShot;
    bool hurtOff;
};

inline CombatPartNow combatPartNow(const Game &g, uint8_t partIdx) {
    CombatPart p;
    combatPartLoad(partIdx, p);
    CombatPartNow n;
    n.box = p.box;
    n.hpMax = p.hp;
    n.dmgMul = p.dmgMul;
    n.bodyShare = p.bodyShare;
    n.breakTypes = p.breakTypes;
    n.stagger = 0;
    n.speedMul = 0;
    n.cue = 0;
    n.physSlash = p.physSlash;
    n.physBlunt = p.physBlunt;
    n.physShot = p.physShot;
    n.hurtOff = (p.hurtOn == 0);
    const uint8_t stage = combatPartStageGet(g, partIdx);
    if (p.stageCount == 0 || stage == 0)
        return n;
    for (uint8_t i = 0; i < p.stageCount && i < stage; i++) {
        CombatStage s;
        combatStageLoad(static_cast<uint8_t>(p.firstStage + i), s);
        n.dmgMul = combatStageDmgMul(s, n.dmgMul);
        n.speedMul = combatStageSpeedMul(s, n.speedMul);
        if (s.stagger)
            n.stagger = s.stagger;
        if (s.cue)
            n.cue = s.cue;
        if (combatStageHurtOff(s))
            n.hurtOff = true;
    }
    return n;
}

inline uint8_t combatPartNowPhysMul(const CombatPartNow &n, uint8_t phys) {
    if (phys & PHYS_SLASH)
        return n.physSlash;
    if (phys & PHYS_BLUNT)
        return n.physBlunt;
    if (phys & PHYS_SHOT)
        return n.physShot;
    return 100;
}

// Apply hit damage to a part pool, then advance the 2-bit stage when a
// threshold crossed and the hit's phys is in breakTypes (docs section 4:
// breakTypes gates break progress only; other types still drain the pool).
inline void combatPartDamage(Game &g, uint8_t partIdx, uint8_t phys, uint16_t dmg, uint16_t hpMax, uint8_t breakTypes) {
    const uint8_t ord = combatPartOrdinal(g, partIdx);
    if (ord == COMBAT_NO_PART || ord >= COMBAT_PART_SLOTS || hpMax == 0)
        return;   // no pool: part hp flows straight to the body
    uint16_t hp = g.combat.partHp[ord];
    hp = (dmg < hp) ? static_cast<uint16_t>(hp - dmg) : 0;
    g.combat.partHp[ord] = hp;
    if ((phys & breakTypes) == 0)
        return;
    const uint8_t stage = combatPartStageForHp(partIdx, hp, hpMax);
    if (stage > combatPartStageGet(g, partIdx))
        combatPartStageSet(g, partIdx, stage);
}

// Landed player hit against a multi-part creature: pick the overlapping part
// with the highest final multiplier, route the section-3 chain, drain the
// pool/stage. `phys` is the hit's physical type (player weapon -> PHY bit);
// player attacks carry no element in v1, so the elem chain is neutral here.
// Returns COMBAT_NO_PART when no overlapping part is hurtable.
inline CombatBodyHit combatPartHitResolve(Game &g, int32_t base, uint8_t phys, int16_t hx, int16_t hy) {
    CombatBodyHit r;
    r.partIdx = COMBAT_NO_PART;
    r.mul = 0;
    r.dmg = 0;
    if (base <= 0)
        return r;

    const uint8_t count = combatPartCount(g);
    uint8_t best = COMBAT_NO_PART;
    uint32_t bestMul = 0;
    uint8_t bestDmgMul = 100, bestPhysMul = 100, bestBodyShare = 100, bestBreak = 0;
    uint16_t bestHpMax = 0;
    for (uint8_t ord = 0; ord < count; ord++) {
        const uint8_t partIdx = combatPartAt(g, ord);
        // One bulk part read; stage overrides applied in place only when the
        // part has crossed at least one threshold (skeleton parts never do).
        CombatPart p;
        combatPartLoad(partIdx, p);
        uint8_t dmgMul = p.dmgMul;
        bool hurtOff = (p.hurtOn == 0);
        const uint8_t stage = combatPartStageGet(g, partIdx);
        if (p.stageCount != 0 && stage != 0) {
            for (uint8_t i = 0; i < p.stageCount && i < stage; i++) {
                CombatStage s;
                combatStageLoad(static_cast<uint8_t>(p.firstStage + i), s);
                dmgMul = combatStageDmgMul(s, dmgMul);
                if (combatStageHurtOff(s))
                    hurtOff = true;
            }
        }
        if (hurtOff)
            continue;
        const uint8_t physMul = (phys & PHYS_SLASH) ? p.physSlash : ((phys & PHYS_BLUNT) ? p.physBlunt : ((phys & PHYS_SHOT) ? p.physShot : 100));
        // Part boxes are face-relative origins: world rect origin is the body
        // anchor plus the DIR8 rotation of (ox, oy); int16 suffices (products
        // are <= 16 * 127). Same projection the attack windows use.
        const int16_t dx = static_cast<int16_t>((static_cast<int16_t>(g.monster.fx) * p.box.ox - static_cast<int16_t>(g.monster.fy) * p.box.oy) >> 4);
        const int16_t dy = static_cast<int16_t>((static_cast<int16_t>(g.monster.fy) * p.box.ox + static_cast<int16_t>(g.monster.fx) * p.box.oy) >> 4);
        const int16_t x = static_cast<int16_t>(g.monster.x + dx);
        const int16_t y = static_cast<int16_t>(g.monster.y + dy);
        if (hx < x || hx >= x + p.box.w || hy < y || hy >= y + p.box.h)
            continue;
        const uint32_t mul = combatPartMul(dmgMul, physMul, 100);
        if (best == COMBAT_NO_PART || combatMulBeats(mul, partIdx, bestMul, best)) {
            best = partIdx;
            bestMul = mul;
            bestDmgMul = dmgMul;
            bestPhysMul = physMul;
            bestBodyShare = p.bodyShare;
            bestBreak = p.breakTypes;
            bestHpMax = p.hp;
        }
    }
    if (best == COMBAT_NO_PART)
        return r;

    // Section-3 chain, truncating at each step (docs section 3): dmgMul, then
    // the physical multiplier (element is neutral for v1 player hits).
    uint32_t out = static_cast<uint32_t>(base);
    out = combatMulPercent(out, bestDmgMul);
    out = combatMulPercent(out, bestPhysMul);
    combatPartDamage(g, best, phys, (out > 0xFFFFu) ? 0xFFFFu : static_cast<uint16_t>(out), bestHpMax, bestBreak);

    const uint32_t body = combatMulPercent(out, bestBodyShare);
    r.partIdx = best;
    r.mul = static_cast<uint8_t>(bestMul > 255u ? 255u : bestMul);
    r.dmg = (body > 0xFFFFu) ? 0xFFFFu : static_cast<uint16_t>(body);
    return r;
}

// Overlay frame index for the first per-creature override part (render.hpp):
// frames are east-intact, east-broken, west-intact, west-broken (tools/
// gen-art.py tail sheet). Pure so host/device suites pin the data-art linkage.
inline uint8_t combatPartArtFrame(bool west, uint8_t stage) {
    return static_cast<uint8_t>((west ? 2 : 0) + (stage > 0 ? 1 : 0));
}

}   // namespace mh
