#pragma once
// Creature combat data loader (beads monhun-ardu-ljj.2, monhun-ardu-cgk).
//
// Design: build/zones-design.md. This is the single production loader shared
// verbatim by the game, the host suites and the Ardens loader test:
//
//   host  -> reads src/generated/combat_data.hpp structs (identity)
//   AVR   -> reads the packed mhCombat blob on the FX cart via mhFxRead* at
//            mhCombat + <combat_meta.hpp offset>
//
// Both backends expose the same typed read functions returning plain value
// structs, so the loader logic above the read layer is one code path. Hot-path
// reads are field-targeted; live RAM caches (CombatState in Game) keep
// per-tick reads at zero.
//
// 3-hitzone model: the body is implicit (creature HP + creature w/h, wins
// ties); a creature may declare a head and/or an appendage zone (one fixed
// record each, no ordinals, no stage tables). Zone boxes are face-relative and
// rotate through combatFacePoint like attack windows. A landed hit tests the
// two optional zone rects, picks the highest dmgMul (tie -> body, then head,
// then appendage), drains the zone pool and flips a single broken bit per zone.
//
// Cache budget: CombatProfile 24 B + CombatAttackCache 25 B + body box 4 B +
// 2x CombatZoneCache 22 B + 4 runtime zone bytes + 4 interpreter bytes = 83 B
// on AVR.

#include <stddef.h>
#include <stdint.h>

#include "game.hpp"   // CombatState caches, Game, Rect

#include "../generated/combat_meta.hpp"

#if !defined(__AVR__)
#include "../generated/combat_data.hpp"   // host mirror (identity reads)
#endif

namespace mh {

// LTO guard: composite record readers and hit resolvers are called from several
// sites (spawn, decision, hit, render). Left inline, LTO clones each body per
// call site and the shipping image grows by kilobytes; one out-of-line copy per
// function is smaller overall. Field accessors stay inline on purpose.
#define MH_COMBAT_NI __attribute__((noinline))

// ------------------------------------------------------------ shared enums
// Mirrors tools/gen-combat.py. Phys is a bitmask (break gating); an attack
// carries exactly one bit. Elements ship inert in v1 (record field only).
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
// Move kinds (tools/gen-combat.py MOVE_TYPES). Lunge and hop are consumed by
// the shipping interpreter; charge is schema-reserved.
enum MoveType : uint8_t {
    MOVE_NONE = 0,
    MOVE_LUNGE = 1,
    MOVE_CHARGE = 2,
    MOVE_HOP = 3
};
// Per-attack windup animation-frame selector (tools/gen-combat.py TELLS,
// feel.5; prg.11 turned the shapes into frame ids). The render uses this to pick
// the beast's bespoke windup pose (mh::tellWindupFrame) and falls back to the
// legacy 2x2 core marker when the frame is unauthored. DOT (0) is the generic
// coil / core marker; the id order mirrors the authored tell order.
enum Tell : uint8_t {
    TELL_DOT = 0,
    TELL_LINE = 1,
    TELL_ARC = 2,
    TELL_RING = 3,
    TELL_ZONE = 4
};
enum GuardPlayer : uint8_t {
    GUARD_PLAYER_ATTACKING = 0x01
};
// Guard facing clause (tools/gen-combat.py GUARD_FACINGS): the pattern matches
// only when the player body centre lies behind / in front of the beast's facing
// vector. ANY (0) is the default and folds the evaluator out via HAS_GUARD_FACING.
enum GuardFacing : uint8_t {
    GUARD_FACING_ANY = 0,
    GUARD_FACING_BEHIND = 1,
    GUARD_FACING_FRONT = 2
};
// Attack facing mode (tools/gen-combat.py FACINGS): track recomputes the
// facing vector from the player delta every tick; the lock modes freeze the
// windup-start facing through WINDUP + ATTACK. lock-at-windup keeps that
// tracked vector; lock-away negates it once at windup entry so the beast turns
// its back to the hunter (heavy's tail_spin, nch.2).
constexpr uint8_t COMBAT_FACING_TRACK = 0;
constexpr uint8_t COMBAT_FACING_LOCK = 1;
constexpr uint8_t COMBAT_FACING_LOCK_AWAY = 2;

// True for either lock mode: the per-tick facing recompute is skipped and the
// windup-start vector survives WINDUP + ATTACK.
inline bool combatFacingLockV(uint8_t facing) {
    return facing == COMBAT_FACING_LOCK || facing == COMBAT_FACING_LOCK_AWAY;
}

// Fixed zone model (build/zones-design.md): zone slot 0 is the head, slot 1 the
// appendage; the broken bitmask uses the same bit order and matches the
// creature record's headZone/appendZone indices.
constexpr uint8_t COMBAT_ZONE_HEAD = 0;
constexpr uint8_t COMBAT_ZONE_APPENDAGE = 1;
constexpr uint8_t COMBAT_ZONE_COUNT = 2;
constexpr uint8_t COMBAT_ZONE_HEAD_BIT = 0x01;
constexpr uint8_t COMBAT_ZONE_APPENDAGE_BIT = 0x02;
constexpr uint8_t COMBAT_NO_ZONE = 0xFF;
// Broken flags (zone record): bit0 hurtOff, bit1 cue.
constexpr uint8_t COMBAT_BROKEN_HURT_OFF = 0x01;
constexpr uint8_t COMBAT_BROKEN_CUE = 0x02;
// Monster::atkIdx sentinel: no attack cached (init / dead / test clear).
constexpr uint8_t COMBAT_NO_ATTACK = 0xFF;
// CombatState::patternIdx sentinel: no pattern cursor active.
constexpr uint8_t COMBAT_NO_PATTERN = 0xFF;
// Carve table (prg.3): the packed slot count must match the runtime constant in
// src/core/game.hpp (kept literal there so the gen.sh bootstrap never depends on
// a freshly generated symbol).
static_assert(combat::CARVE_SLOTS == CARVE_SLOTS, "carve slot count drifted from the generated blob");

// --------------------------------------------------------- value structs
// Plain value mirrors of the blob records (field order = packed ABI order).
// Tests compare these against the generated host structs / pin values.

struct CombatCreature {
    uint8_t skeletonIdx, profileIdx;
    uint8_t headZone, appendZone;
    uint8_t firstAttack, attackCount;
    uint8_t firstPattern, patternCount;
    uint8_t w, h, spd;
    CombatBox collide;   // body-collision rect (legs-only for the chicken)
    uint16_t hp, spawnX, spawnY;
    uint8_t flags;                                                  // bit0: static prop (pole); no FSM/attacks
    uint8_t sheet;                                                  // art sheet id (0 = default monster sheet)
    uint8_t brokenW, brokenH;                                       // target rect on break (0 = unchanged)
    uint8_t enrageHpPct, enrageSpdMul, enrageFaceHold, enrageCue;   // feel.6 (hpPct 0 = disabled)
};

struct CombatSkeleton {
    uint8_t firstAnchor, anchorCount;
};

struct CombatZone {
    CombatBox box;
    uint8_t hp, dmgMul, bodyShare, breakTypes, staggerOnHit;
    uint8_t brokenDmgMul, brokenFlags;
    uint8_t unlockMaskLo, unlockMaskHi;   // u16 bit per global attack idx (feel.10)
};

struct CombatAnchor {
    int8_t ox, oy;
};

// Spawn-scalar projection: the creature record fields initMonster needs beyond
// the body box. Read as one burst at spawn.
struct CombatSpawn {
    uint16_t hp, x, y;
    uint8_t spd;
    CombatBox collide;   // body-collision rect (legs-only for the chicken)
};

// One packed carve-table slot (prg.3): item index into the generated table,
// count 1..3, deterministic drop chance 0..100. count 0 marks an empty slot.
struct CombatCarve {
    uint8_t item, count, chance;
};

struct CombatAttackValue {
    uint8_t moveType, moveSpeedF;
    int8_t moveDx, moveDy;
    uint8_t facing, phys, elem, onHitEffect;
    int8_t onHitPush;
    uint8_t onHitStun, stagger, cue;
    uint8_t wallStun;   // ticks self-stunned on a room-bound clamp (feel.4)
    uint8_t firstWindow, windowCount;
    uint16_t windup, active, recover, dmg;
    uint8_t tell;   // Tell: windup animation-frame selector (feel.5 / prg.11)
};

struct CombatPattern {
    uint8_t firstStep, stepCount, guardIdx;
};

struct CombatGuard {
    uint8_t minDist, maxDist, hpLo, hpHi, playerFlags, cooldown, chance;
    uint8_t zonesBroken;   // bitmask: all listed zones must be broken
    uint8_t facing;        // GuardFacing: 0 any, 1 behind, 2 front
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
struct PkCarve {
    uint8_t item, count, chance;
};
struct PkCreature {
    uint8_t skeletonIdx, profileIdx;
    uint8_t headZone, appendZone;
    uint8_t firstAttack, attackCount;
    uint8_t firstPattern, patternCount;
    uint8_t w, h, spd;
    int8_t collideOx, collideOy;
    uint8_t collideW, collideH;
    uint16_t hp, spawnX, spawnY;
    uint8_t flags, sheet, brokenW, brokenH;
    uint8_t enrageHpPct, enrageSpdMul, enrageFaceHold, enrageCue;
    PkCarve carve[combat::CARVE_SLOTS];   // prg.3 drop table (count 0 = empty)
};
struct PkProfile {
    uint8_t engageDist, keepDist, attackDist;
    uint8_t circleNum, circleDen, retreatNum, retreatDen;
    uint8_t staggerMax, staggerDecay, zoneFlags, faceHold;
    uint8_t turnRate;
    uint16_t cdBase, cdJitter, spawnT, spawnCd, stunRecoverT, staggerRecoverT;
};
struct PkSkeleton {
    uint8_t firstAnchor, anchorCount;
};
struct PkZone {
    int8_t boxOx, boxOy;
    uint8_t boxW, boxH;
    uint8_t hp, dmgMul, bodyShare, breakTypes, staggerOnHit;
    uint8_t brokenDmgMul, brokenFlags;
    uint8_t unlockMaskLo, unlockMaskHi;
};
struct PkAnchor {
    int8_t ox, oy;
};
struct PkAttack {
    uint8_t moveType, moveSpeedF;
    int8_t moveDx, moveDy;
    uint8_t facing, phys, elem, onHitEffect;
    int8_t onHitPush;
    uint8_t onHitStun, stagger, cue;
    uint8_t wallStun;
    uint8_t firstWindow, windowCount;
    uint16_t windup, active, recover, dmg;
    uint8_t tell;
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
    uint8_t zonesBroken;
    uint8_t facing;
};
struct PkStep {
    uint8_t kind, ref, after, chance;
};
#pragma pack(pop)

static_assert(sizeof(PkCreature) == combat::CREATURE_SIZE, "creature ABI drift");
static_assert(sizeof(PkCarve) == combat::CARVE_SIZE, "carve ABI drift");
static_assert(offsetof(PkCreature, carve) == combat::CREATURE_CARVE_OFF, "creature carve table offset drift");
static_assert(sizeof(PkProfile) == combat::PROFILE_SIZE, "profile ABI drift");
static_assert(sizeof(PkSkeleton) == combat::SKELETON_SIZE, "skeleton ABI drift");
static_assert(sizeof(PkZone) == combat::ZONE_SIZE, "zone ABI drift");
static_assert(sizeof(PkAnchor) == combat::ANCHOR_SIZE, "anchor ABI drift");
static_assert(sizeof(PkAttack) == combat::ATTACK_SIZE, "attack ABI drift");
static_assert(sizeof(PkWindow) == combat::WINDOW_SIZE, "window ABI drift");
static_assert(sizeof(PkPattern) == combat::PATTERN_SIZE, "pattern ABI drift");
static_assert(sizeof(PkGuard) == combat::GUARD_SIZE, "guard ABI drift");
static_assert(sizeof(PkStep) == combat::STEP_SIZE, "step ABI drift");
// Packed-pair reads: creature size fields and the pattern head are adjacent
// pairs; attackLoad's scalar burst relies on the same layout as before.
static_assert(offsetof(PkCreature, h) == offsetof(PkCreature, w) + 1, "creature size pair must stay adjacent");
static_assert(offsetof(PkCreature, patternCount) == offsetof(PkCreature, firstPattern) + 1, "pattern head pair must stay adjacent");
static_assert(offsetof(PkCreature, enrageHpPct) == offsetof(PkCreature, brokenH) + 1, "creature enrage quad must follow brokenH");
static_assert(offsetof(PkCreature, enrageCue) == offsetof(PkCreature, enrageHpPct) + 3, "creature enrage quad must stay contiguous");
static_assert(offsetof(PkAttack, moveSpeedF) == offsetof(PkAttack, moveType) + 1, "attack move pair must stay adjacent");
static_assert(offsetof(PkAttack, moveDx) == offsetof(PkAttack, moveType) + 2, "attack hop dx must follow the move pair");
static_assert(offsetof(PkAttack, moveDy) == offsetof(PkAttack, moveDx) + 1, "attack hop dx/dy pair must stay adjacent");
static_assert(offsetof(PkAttack, windowCount) == offsetof(PkAttack, firstWindow) + 1, "attack window pair must stay adjacent");
static_assert(offsetof(PkAttack, wallStun) == offsetof(PkAttack, cue) + 1, "attack wallStun must follow cue");
static_assert(offsetof(PkAttack, dmg) == offsetof(PkAttack, windup) + 6, "attack timing quad must stay contiguous");
static_assert(offsetof(PkAttack, tell) == offsetof(PkAttack, dmg) + 2, "attack tell must follow the timing quad");
// Bulk-read cache mirrors: these caches are byte-identical to their packed
// records, so the reads fetch the whole record in one transaction.
static_assert(sizeof(CombatZone) == combat::ZONE_SIZE, "zone cache must stay 13 B");
static_assert(sizeof(CombatGuard) == combat::GUARD_SIZE, "guard cache must stay 9 B");
static_assert(sizeof(CombatStep) == combat::STEP_SIZE, "step cache must stay 4 B");
static_assert(sizeof(CombatPattern) == combat::PATTERN_SIZE, "pattern cache must stay 3 B");
static_assert(offsetof(CombatProfile, zoneFlags) == offsetof(PkProfile, zoneFlags), "profile mirror drift");
static_assert(offsetof(CombatProfile, cdBase) == offsetof(PkProfile, cdBase), "profile mirror drift");
static_assert(offsetof(CombatGuard, zonesBroken) == offsetof(PkGuard, zonesBroken), "guard mirror drift");
static_assert(offsetof(CombatGuard, facing) == offsetof(PkGuard, facing), "guard facing mirror drift");
static_assert(offsetof(CombatStep, chance) == offsetof(PkStep, chance), "step mirror drift");
static_assert(offsetof(CombatPattern, guardIdx) == offsetof(PkPattern, guardIdx), "pattern mirror drift");
static_assert(sizeof(CombatWindow) == 9, "window cache must stay 9 B");
static_assert(sizeof(CombatAttackCache) == 25, "attack cache must stay 25 B (windup quad + move prefix incl hop dx/dy + facing + wallStun + tell + idx + window)");
static_assert(sizeof(CombatZoneCache) == 12, "zone cache must stay 12 B");
static_assert(sizeof(CombatState) == 95, "CombatState must stay 95 B (zones design + collide + static flag + faceHold + turnRate + wallStun + tell + enrage + hop dx/dy)");

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
MH_NOINLINE inline uint16_t combatReadU16(uint16_t off) {
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
    v.headZone = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, headZone));
    v.appendZone = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, appendZone));
    v.firstAttack = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, firstAttack));
    v.attackCount = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, attackCount));
    v.firstPattern = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, firstPattern));
    v.patternCount = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, patternCount));
    v.w = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, w));
    v.h = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, h));
    v.spd = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, spd));
    v.collide.ox = combatReadI8(b + MH_COMBAT_FIELD(detail::PkCreature, collideOx));
    v.collide.oy = combatReadI8(b + MH_COMBAT_FIELD(detail::PkCreature, collideOy));
    v.collide.w = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, collideW));
    v.collide.h = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, collideH));
    v.hp = combatReadU16(b + MH_COMBAT_FIELD(detail::PkCreature, hp));
    v.spawnX = combatReadU16(b + MH_COMBAT_FIELD(detail::PkCreature, spawnX));
    v.spawnY = combatReadU16(b + MH_COMBAT_FIELD(detail::PkCreature, spawnY));
    v.flags = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, flags));
    v.sheet = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, sheet));
    v.brokenW = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, brokenW));
    v.brokenH = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, brokenH));
    // Enrage quad (feel.6): hpPct/spdMul then faceHold/cue, two adjacent u16.
    const uint16_t en0 = combatReadU16(b + MH_COMBAT_FIELD(detail::PkCreature, enrageHpPct));
    const uint16_t en1 = combatReadU16(b + MH_COMBAT_FIELD(detail::PkCreature, enrageFaceHold));
    v.enrageHpPct = static_cast<uint8_t>(en0 & 0xFF);
    v.enrageSpdMul = static_cast<uint8_t>(en0 >> 8);
    v.enrageFaceHold = static_cast<uint8_t>(en1 & 0xFF);
    v.enrageCue = static_cast<uint8_t>(en1 >> 8);
    return v;
}

// One-shot enrage cache read (feel.6): the packed quad is two adjacent u16
// (hpPct/spdMul, faceHold/cue). Spawn burst only; no per-tick cart access.
inline void combatCreatureEnrageRead(uint8_t i, CombatEnrage &e) {
    const uint16_t b = static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE);
    const uint16_t en0 = combatReadU16(b + MH_COMBAT_FIELD(detail::PkCreature, enrageHpPct));
    const uint16_t en1 = combatReadU16(b + MH_COMBAT_FIELD(detail::PkCreature, enrageFaceHold));
    e.hpPct = static_cast<uint8_t>(en0 & 0xFF);
    e.spdMul = static_cast<uint8_t>(en0 >> 8);
    e.faceHold = static_cast<uint8_t>(en1 & 0xFF);
    e.cue = static_cast<uint8_t>(en1 >> 8);
}

inline uint8_t combatCreatureProfileIdx(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE + MH_COMBAT_FIELD(detail::PkCreature, profileIdx)));
}

inline uint8_t combatCreatureSkeletonIdx(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE + MH_COMBAT_FIELD(detail::PkCreature, skeletonIdx)));
}

inline uint8_t combatCreatureHeadZone(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE + MH_COMBAT_FIELD(detail::PkCreature, headZone)));
}

inline uint8_t combatCreatureAppendZone(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE + MH_COMBAT_FIELD(detail::PkCreature, appendZone)));
}

inline uint8_t combatCreatureFirstAttack(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE + MH_COMBAT_FIELD(detail::PkCreature, firstAttack)));
}

// Packed firstPattern | patternCount<<8: the two adjacent bytes in one cart
// access (chooseAttack's decision head).
inline uint16_t combatCreaturePatternHeadRead(uint8_t i) {
    return combatReadU16(static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE + MH_COMBAT_FIELD(detail::PkCreature, firstPattern)));
}

inline uint16_t combatCreatureSizeRead(uint8_t i) {
    return combatReadU16(static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE + MH_COMBAT_FIELD(detail::PkCreature, w)));
}

inline uint8_t combatCreatureFlags(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE + MH_COMBAT_FIELD(detail::PkCreature, flags)));
}

inline uint8_t combatCreatureStatic(uint8_t i) {
    return static_cast<uint8_t>(combatCreatureFlags(i) & 0x01);
}

inline uint8_t combatCreatureSheet(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE + MH_COMBAT_FIELD(detail::PkCreature, sheet)));
}

inline uint8_t combatCreatureBrokenW(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE + MH_COMBAT_FIELD(detail::PkCreature, brokenW)));
}

inline uint8_t combatCreatureBrokenH(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE + MH_COMBAT_FIELD(detail::PkCreature, brokenH)));
}

inline CombatSpawn combatCreatureSpawnRead(uint8_t i) {
    const uint16_t b = static_cast<uint16_t>(combat::CREATURES_OFF + i * combat::CREATURE_SIZE);
    CombatSpawn v;
    v.spd = combatReadU8(b + MH_COMBAT_FIELD(detail::PkCreature, spd));
    // collide is 4 contiguous bytes (ox, oy, w, h): one bulk read.
    detail::combatReadBytes(static_cast<uint16_t>(b + offsetof(detail::PkCreature, collideOx)), &v.collide, sizeof(v.collide));
    v.hp = combatReadU16(b + MH_COMBAT_FIELD(detail::PkCreature, hp));
    v.x = combatReadU16(b + MH_COMBAT_FIELD(detail::PkCreature, spawnX));
    v.y = combatReadU16(b + MH_COMBAT_FIELD(detail::PkCreature, spawnY));
    return v;
}

// One carve slot (prg.3): 3 contiguous bytes at CREATURE_CARVE_OFF + slot*3.
// Bad ids read inert (all zero), matching the other loaders.
inline CombatCarve combatCarveRead(uint8_t creatureId, uint8_t slot) {
    CombatCarve v{0, 0, 0};
    if (creatureId >= combat::CREATURES_COUNT || slot >= combat::CARVE_SLOTS)
        return v;
    const uint16_t b = static_cast<uint16_t>(combat::CREATURES_OFF + creatureId * combat::CREATURE_SIZE + combat::CREATURE_CARVE_OFF + slot * combat::CARVE_SIZE);
    detail::combatReadBytes(b, &v, sizeof(v));
    return v;
}

// Profile is a byte-identical 23 B mirror: one bulk read at spawn.
inline CombatProfile combatProfileRead(uint8_t i) {
    CombatProfile v;
    detail::combatReadBytes(static_cast<uint16_t>(combat::PROFILES_OFF + i * combat::PROFILE_SIZE), &v, sizeof(v));
    return v;
}

inline void combatProfileLoad(Game &g, uint8_t i) {
    detail::combatReadBytes(static_cast<uint16_t>(combat::PROFILES_OFF + i * combat::PROFILE_SIZE), &g.combat.profile, sizeof(g.combat.profile));
}

inline CombatSkeleton combatSkeletonRead(uint8_t i) {
    const uint16_t b = static_cast<uint16_t>(combat::SKELETONS_OFF + i * combat::SKELETON_SIZE);
    CombatSkeleton v;
    v.firstAnchor = combatReadU8(b + MH_COMBAT_FIELD(detail::PkSkeleton, firstAnchor));
    v.anchorCount = combatReadU8(b + MH_COMBAT_FIELD(detail::PkSkeleton, anchorCount));
    return v;
}

// Zone is a byte-identical 12 B mirror: one bulk read (spawn seeding).
inline CombatZone combatZoneRead(uint8_t i) {
    CombatZone v;
    detail::combatReadBytes(static_cast<uint16_t>(combat::ZONES_OFF + i * combat::ZONE_SIZE), &v, sizeof(v));
    return v;
}

inline CombatAnchor combatAnchorRead(uint8_t i) {
    const uint16_t b = static_cast<uint16_t>(combat::ANCHORS_OFF + i * combat::ANCHOR_SIZE);
    CombatAnchor v;
    v.ox = combatReadI8(b + MH_COMBAT_FIELD(detail::PkAnchor, ox));
    v.oy = combatReadI8(b + MH_COMBAT_FIELD(detail::PkAnchor, oy));
    return v;
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
    v.wallStun = combatReadU8(b + MH_COMBAT_FIELD(detail::PkAttack, wallStun));
    v.firstWindow = combatReadU8(b + MH_COMBAT_FIELD(detail::PkAttack, firstWindow));
    v.windowCount = combatReadU8(b + MH_COMBAT_FIELD(detail::PkAttack, windowCount));
    v.windup = combatReadU16(b + MH_COMBAT_FIELD(detail::PkAttack, windup));
    v.active = combatReadU16(b + MH_COMBAT_FIELD(detail::PkAttack, active));
    v.recover = combatReadU16(b + MH_COMBAT_FIELD(detail::PkAttack, recover));
    v.dmg = combatReadU16(b + MH_COMBAT_FIELD(detail::PkAttack, dmg));
    v.tell = combatReadU8(b + MH_COMBAT_FIELD(detail::PkAttack, tell));
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
inline uint8_t combatAttackWallStun(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::ATTACKS_OFF + i * combat::ATTACK_SIZE + MH_COMBAT_FIELD(detail::PkAttack, wallStun)));
}
inline uint8_t combatAttackFirstWindow(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::ATTACKS_OFF + i * combat::ATTACK_SIZE + MH_COMBAT_FIELD(detail::PkAttack, firstWindow)));
}
inline uint8_t combatAttackWindowCount(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::ATTACKS_OFF + i * combat::ATTACK_SIZE + MH_COMBAT_FIELD(detail::PkAttack, windowCount)));
}
inline uint8_t combatAttackTell(uint8_t i) {
    return combatReadU8(static_cast<uint16_t>(combat::ATTACKS_OFF + i * combat::ATTACK_SIZE + MH_COMBAT_FIELD(detail::PkAttack, tell)));
}

// Window cache is a byte-identical 9 B prefix of the packed record.
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

// Dist-only guard probe (SIMPLE_GUARDS): minDist/maxDist are the guard record's
// leading byte pair, so the range is one u16 cart read.
inline uint16_t combatPatternGuardRangeRead(uint8_t i) {
    const uint8_t guardIdx = combatPatternGuardIdx(i);
    return combatReadU16(static_cast<uint16_t>(combat::GUARDS_OFF + guardIdx * combat::GUARD_SIZE));
}

inline CombatGuard combatGuardRead(uint8_t i) {
    CombatGuard v;
    detail::combatReadBytes(static_cast<uint16_t>(combat::GUARDS_OFF + i * combat::GUARD_SIZE), &v, sizeof(v));
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
    v.headZone = c.headZone;
    v.appendZone = c.appendZone;
    v.firstAttack = c.firstAttack;
    v.attackCount = c.attackCount;
    v.firstPattern = c.firstPattern;
    v.patternCount = c.patternCount;
    v.w = c.w;
    v.h = c.h;
    v.spd = c.spd;
    v.collide.ox = c.collide.ox;
    v.collide.oy = c.collide.oy;
    v.collide.w = c.collide.w;
    v.collide.h = c.collide.h;
    v.hp = c.hp;
    v.spawnX = c.spawnX;
    v.spawnY = c.spawnY;
    v.flags = c.flags;
    v.sheet = c.sheet;
    v.brokenW = c.brokenW;
    v.brokenH = c.brokenH;
    v.enrageHpPct = c.enrageHpPct;
    v.enrageSpdMul = c.enrageSpdMul;
    v.enrageFaceHold = c.enrageFaceHold;
    v.enrageCue = c.enrageCue;
    return v;
}

inline void combatCreatureEnrageRead(uint8_t i, CombatEnrage &e) {
    const combat_data::Creature &c = combat_data::CREATURES[i];
    e.hpPct = c.enrageHpPct;
    e.spdMul = c.enrageSpdMul;
    e.faceHold = c.enrageFaceHold;
    e.cue = c.enrageCue;
}

inline uint8_t combatCreatureProfileIdx(uint8_t i) {
    return combat_data::CREATURES[i].profileIdx;
}

inline uint8_t combatCreatureSkeletonIdx(uint8_t i) {
    return combat_data::CREATURES[i].skeletonIdx;
}

inline uint8_t combatCreatureHeadZone(uint8_t i) {
    return combat_data::CREATURES[i].headZone;
}

inline uint8_t combatCreatureAppendZone(uint8_t i) {
    return combat_data::CREATURES[i].appendZone;
}

inline uint8_t combatCreatureFirstAttack(uint8_t i) {
    return combat_data::CREATURES[i].firstAttack;
}

inline uint16_t combatCreaturePatternHeadRead(uint8_t i) {
    const combat_data::Creature &c = combat_data::CREATURES[i];
    return static_cast<uint16_t>(static_cast<uint16_t>(c.firstPattern) | (static_cast<uint16_t>(c.patternCount) << 8));
}

inline uint16_t combatCreatureSizeRead(uint8_t i) {
    const combat_data::Creature &c = combat_data::CREATURES[i];
    return static_cast<uint16_t>(static_cast<uint16_t>(c.w) | (static_cast<uint16_t>(c.h) << 8));
}

inline uint8_t combatCreatureFlags(uint8_t i) {
    return combat_data::CREATURES[i].flags;
}

inline uint8_t combatCreatureStatic(uint8_t i) {
    return static_cast<uint8_t>(combat_data::CREATURES[i].flags & 0x01);
}

inline uint8_t combatCreatureSheet(uint8_t i) {
    return combat_data::CREATURES[i].sheet;
}

inline uint8_t combatCreatureBrokenW(uint8_t i) {
    return combat_data::CREATURES[i].brokenW;
}

inline uint8_t combatCreatureBrokenH(uint8_t i) {
    return combat_data::CREATURES[i].brokenH;
}

inline CombatSpawn combatCreatureSpawnRead(uint8_t i) {
    const combat_data::Creature &c = combat_data::CREATURES[i];
    CombatSpawn v;
    v.spd = c.spd;
    v.collide.ox = c.collide.ox;
    v.collide.oy = c.collide.oy;
    v.collide.w = c.collide.w;
    v.collide.h = c.collide.h;
    v.hp = c.hp;
    v.x = c.spawnX;
    v.y = c.spawnY;
    return v;
}

// Host carve read: bad ids read inert (all zero).
inline CombatCarve combatCarveRead(uint8_t creatureId, uint8_t slot) {
    CombatCarve v{0, 0, 0};
    if (creatureId >= combat::CREATURES_COUNT || slot >= combat::CARVE_SLOTS)
        return v;
    const combat_data::Carve &c = combat_data::CREATURES[creatureId].carve[slot];
    v.item = c.item;
    v.count = c.count;
    v.chance = c.chance;
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
    v.zoneFlags = p.zoneFlags;
    v.faceHold = p.faceHold;
    v.turnRate = p.turnRate;
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
    v.firstAnchor = s.firstAnchor;
    v.anchorCount = s.anchorCount;
    return v;
}

inline CombatZone combatZoneRead(uint8_t i) {
    const combat_data::Zone &z = combat_data::ZONES[i];
    CombatZone v;
    v.box.ox = z.box.ox;
    v.box.oy = z.box.oy;
    v.box.w = z.box.w;
    v.box.h = z.box.h;
    v.hp = z.hp;
    v.dmgMul = z.dmgMul;
    v.bodyShare = z.bodyShare;
    v.breakTypes = z.breakTypes;
    v.staggerOnHit = z.staggerOnHit;
    v.brokenDmgMul = z.brokenDmgMul;
    v.brokenFlags = z.brokenFlags;
    v.unlockMaskLo = z.unlockMaskLo;
    v.unlockMaskHi = z.unlockMaskHi;
    return v;
}

inline CombatAnchor combatAnchorRead(uint8_t i) {
    const combat_data::Anchor &a = combat_data::ANCHORS[i];
    CombatAnchor v;
    v.ox = a.ox;
    v.oy = a.oy;
    return v;
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
    v.wallStun = a.wallStun;
    v.firstWindow = a.firstWindow;
    v.windowCount = a.windowCount;
    v.windup = a.windup;
    v.active = a.active;
    v.recover = a.recover;
    v.dmg = a.dmg;
    v.tell = a.tell;
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
inline uint8_t combatAttackWallStun(uint8_t i) {
    return combat_data::ATTACKS[i].wallStun;
}
inline uint8_t combatAttackFirstWindow(uint8_t i) {
    return combat_data::ATTACKS[i].firstWindow;
}
inline uint8_t combatAttackWindowCount(uint8_t i) {
    return combat_data::ATTACKS[i].windowCount;
}
inline uint8_t combatAttackTell(uint8_t i) {
    return combat_data::ATTACKS[i].tell;
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
    v.zonesBroken = g.zonesBroken;
    v.facing = g.facing;
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

// ------------------------------------------------------ body box
// The body is implicit: creature w/h at the body anchor (0,0). Also returns the
// creature's head/appendage zone indices so a spawn burst reads them once. Bad
// ids fall back to creature 0 like creatureLoad. Returns false (outputs
// untouched) when the creature declares a zero size.
inline bool combatCreatureBodyBox(uint8_t creatureId, CombatBox &box, uint8_t &headZone, uint8_t &appendZone) {
    if (creatureId >= combat::CREATURES_COUNT)
        creatureId = 0;
    const uint16_t head = combatCreatureSizeRead(creatureId);
    const uint8_t w = static_cast<uint8_t>(head & 0xFF);
    const uint8_t h = static_cast<uint8_t>(head >> 8);
    if (w == 0 || h == 0)
        return false;
    headZone = combatCreatureHeadZone(creatureId);
    appendZone = combatCreatureAppendZone(creatureId);
    box.ox = 0;
    box.oy = 0;
    box.w = w;
    box.h = h;
    return true;
}

// Convenience for callers that only need the box (loader tests, render).
inline bool combatCreatureBodyBox(uint8_t creatureId, CombatBox &box) {
    uint8_t headZone, appendZone;
    return combatCreatureBodyBox(creatureId, box, headZone, appendZone);
}

// Collide box: the rectangle syncMonsterTarget/pushApart use for body collision.
// Authored per creature (epic monhun-ardu-nch: the chicken's two legs) or the
// body box when absent, so every creature without one keeps its old body rect.
// One 4-byte bulk read on AVR.
inline CombatBox combatCreatureCollideBox(uint8_t creatureId) {
    if (creatureId >= combat::CREATURES_COUNT)
        creatureId = 0;
    CombatSpawn spawn = combatCreatureSpawnRead(creatureId);
    return spawn.collide;
}

// ======================================================= cache lifecycle
// creatureCacheReset: identity + runtime caches with no record reads.
inline void creatureCacheReset(Game &g, uint8_t creatureId) {
    g.combat.creature = creatureId;
    g.combat.body = CombatBox{0, 0, 0, 0};
    g.combat.collide = CombatBox{0, 0, 0, 0};
    g.combat.headZone = COMBAT_NO_ZONE;
    g.combat.appendZone = COMBAT_NO_ZONE;
    g.combat.zone[0] = CombatZoneCache{};
    g.combat.zone[1] = CombatZoneCache{};
    g.combat.zoneBroken = 0;
    g.combat.isStatic = 0;
    g.combat.patternIdx = COMBAT_NO_PATTERN;
    g.combat.stepIdx = 0;
    g.combat.stepT = 0;
    g.combat.stagger = 0;
    g.combat.enrage = CombatEnrage{};
    g.combat.attack = CombatAttackCache{};
}

// zoneSeed: cache one zone's live scalars (box, pool, multipliers, break data).
inline void combatZoneSeed(Game &g, uint8_t slot, uint8_t zoneIdx) {
    if (zoneIdx == COMBAT_NO_ZONE) {
        g.combat.zone[slot] = CombatZoneCache{};
        return;
    }
    const CombatZone z = combatZoneRead(zoneIdx);
    CombatZoneCache &c = g.combat.zone[slot];
    c.box = z.box;
    c.hp = z.hp;
    c.hpMax = z.hp;
    c.dmgMul = z.dmgMul;
    c.bodyShare = z.bodyShare;
    c.breakTypes = z.breakTypes;
    c.staggerOnHit = z.staggerOnHit;
    c.unlockMask = static_cast<uint16_t>(z.unlockMaskLo) | (static_cast<uint16_t>(z.unlockMaskHi) << 8);
}

// creatureLoad: read the creature profile index + full profile record + body
// box + collide box + zone records into the Game cache (spawn burst; bad ids
// fall back to creature 0). The attack cache is cleared until attackLoad picks
// an attack.
inline uint8_t creatureLoad(Game &g, uint8_t creatureId) {
    if (creatureId >= combat::CREATURES_COUNT)
        creatureId = 0;
    const uint8_t profileIdx = combatCreatureProfileIdx(creatureId);
    creatureCacheReset(g, creatureId);
    combatProfileLoad(g, profileIdx);
    combatCreatureBodyBox(creatureId, g.combat.body, g.combat.headZone, g.combat.appendZone);
    g.combat.collide = combatCreatureCollideBox(creatureId);
    g.combat.isStatic = combatCreatureStatic(creatureId);
    combatCreatureEnrageRead(creatureId, g.combat.enrage);   // feel.6; fired latched to 0 by reset
    if (ZONES_ENABLED) {
        combatZoneSeed(g, COMBAT_ZONE_HEAD, g.combat.headZone);
        combatZoneSeed(g, COMBAT_ZONE_APPENDAGE, g.combat.appendZone);
    }
    return creatureId;
}

// attackWindowLoad: refresh the cached window during the active phase. This is
// the only mid-attack cart read; per-tick active code consumes the cache.
// Out-of-line: shipped attacks declare one window, so this only runs on the
// cart-load path (measured -4 B).
MH_NOINLINE inline void attackWindowLoad(Game &g, uint8_t windowIdx) {
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
    // Scalar burst in four cart accesses: the 4-byte move prefix
    // (moveType/moveSpeedF/moveDx/moveDy), facing, the wallStun byte, the
    // firstWindow/windowCount pair, and the contiguous u16 quad windup..dmg.
    // Cache field order is ABI-pinned to the packed record by the static_asserts
    // at the top of this file.
    const uint16_t b = static_cast<uint16_t>(combat::ATTACKS_OFF + attackIdx * combat::ATTACK_SIZE);
    uint8_t mv[4];
    detail::combatReadBytes(static_cast<uint16_t>(b + offsetof(detail::PkAttack, moveType)), mv, 4);
    g.combat.attack.moveType = mv[0];
    g.combat.attack.moveSpeedF = mv[1];
    g.combat.attack.moveDx = static_cast<int8_t>(mv[2]);
    g.combat.attack.moveDy = static_cast<int8_t>(mv[3]);
    g.combat.attack.facing = combatReadU8(b + static_cast<uint16_t>(offsetof(detail::PkAttack, facing)));
    g.combat.attack.wallStun = combatReadU8(b + static_cast<uint16_t>(offsetof(detail::PkAttack, wallStun)));
    const uint16_t fw = combatReadU16(b + static_cast<uint16_t>(offsetof(detail::PkAttack, firstWindow)));
    detail::combatReadBytes(static_cast<uint16_t>(b + offsetof(detail::PkAttack, windup)), &g.combat.attack.windup, 8);
    g.combat.attack.tell = combatReadU8(b + static_cast<uint16_t>(offsetof(detail::PkAttack, tell)));
    attackWindowLoad(g, static_cast<uint8_t>(fw & 0xFF));
#else
    g.combat.attack.windup = combatAttackWindup(attackIdx);
    g.combat.attack.active = combatAttackActive(attackIdx);
    g.combat.attack.recover = combatAttackRecover(attackIdx);
    g.combat.attack.dmg = combatAttackDmg(attackIdx);
    g.combat.attack.moveType = combatAttackMoveType(attackIdx);
    g.combat.attack.moveSpeedF = combatAttackMoveSpeedF(attackIdx);
    g.combat.attack.moveDx = combat_data::ATTACKS[attackIdx].moveDx;
    g.combat.attack.moveDy = combat_data::ATTACKS[attackIdx].moveDy;
    g.combat.attack.facing = combatAttackFacing(attackIdx);
    g.combat.attack.wallStun = combatAttackWallStun(attackIdx);
    g.combat.attack.tell = combatAttackTell(attackIdx);
    attackWindowLoad(g, combatAttackFirstWindow(attackIdx));
#endif
    return attackIdx;
}

// combatTick: pattern step countdown; deliberately performs no cart reads
// (cache-warm steady state).
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

// Inclusive integer ranges.
inline bool combatGuardDistOk(uint8_t minDist, uint8_t maxDist, uint8_t dist) {
    return dist >= minDist && dist <= maxDist;
}
inline bool combatGuardHpOk(uint8_t hpLo, uint8_t hpHi, uint8_t hpPct) {
    return hpPct >= hpLo && hpPct <= hpHi;
}
inline bool combatGuardPlayerOk(uint8_t required, uint8_t have) {
    return (required & static_cast<uint8_t>(~have)) == 0;
}
inline bool combatGuardCooldownOk(uint8_t cooldown, uint16_t sinceUse) {
    return sinceUse >= cooldown;
}
// zonesBroken clause: every listed zone must be broken (mask compare).
inline bool combatGuardZonesOk(uint8_t required, uint8_t broken) {
    return (broken & required) == required;
}
// Facing clause helpers (decision-time only; no cart read beyond the guard
// record). combatFacingDot projects the player-centre offset on the beast's
// 1/16 facing vector: negative = behind, positive = in front, 0 = dead abeam
// (neither). Mirrors the body-centre dist math in updateMonster.
inline int16_t combatFacingDot(int16_t px, int16_t py, int16_t mx, int16_t my, int8_t fx, int8_t fy) {
    const int32_t dx = static_cast<int32_t>(px) - static_cast<int32_t>(mx);
    const int32_t dy = static_cast<int32_t>(py) - static_cast<int32_t>(my);
    return static_cast<int16_t>((dx * fx + dy * fy) >> 4);
}
inline bool combatGuardFacingOk(uint8_t required, int16_t dot) {
    if (required == GUARD_FACING_ANY)
        return true;
    if (required == GUARD_FACING_BEHIND)
        return dot < 0;
    return dot > 0;   // GUARD_FACING_FRONT
}

struct CombatGuardInput {
    uint8_t dist;          // integer px to the player
    uint8_t hpPct;         // remaining creature HP percent (0..100)
    uint8_t playerFlags;   // GuardPlayer bits observed on the player
    uint16_t tick;         // decision tick (chance is tick-derived)
    uint16_t sinceUse;     // ticks since this pattern last ran (0xFFFF = never)
    uint8_t stepIdx;       // chance hash step input (0 at decision time)
    int16_t facingDot;     // player-centre dot on the beast facing (HAS_GUARD_FACING)
};

// First-match-wins selection calls this in pattern list order. The clause
// checks the shipped data does not use are compile-time folded by the
// per-clause data facts: the full evaluator returns as soon as a creature opts
// a clause back in.
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
    if (GUARD_ZONES_ENABLED && !combatGuardZonesOk(gu.zonesBroken, g.combat.zoneBroken))
        return false;
    if (combat::HAS_GUARD_FACING && !combatGuardFacingOk(gu.facing, in.facingDot))
        return false;
    if (combat::HAS_GUARD_CHANCE)
        return combatChancePasses(in.tick, g.combat.creature, patternIdx, in.stepIdx, gu.chance);
    return true;
}

// ======================================================= damage routing
// Integer percent, truncating division at each step, 32-bit intermediates.

struct CombatBodyHit {
    uint8_t zone;   // COMBAT_NO_ZONE = body (implicit, dmgMul 100)
    uint8_t mul;    // winning final zone multiplier (percent)
    uint16_t dmg;   // routed to the creature body pool
};

inline uint32_t combatMulPercent(uint32_t value, uint8_t mul) {
    return (value * mul) / 100u;
}

// combatAttackDisabled: a broken zone can disable the attacks listed in its
// unlockMask (u16 bit per global attack index, feel.10). Only present, broken
// zones are consulted; data with no zones/unlock lists folds this out.
inline bool combatAttackDisabled(const Game &g, uint8_t attackIdx) {
    if (!ZONES_ENABLED)
        return false;
    if (attackIdx >= 16)
        return false;
    const uint16_t bit = static_cast<uint16_t>(1u << attackIdx);
    if ((g.combat.zoneBroken & COMBAT_ZONE_HEAD_BIT) && g.combat.headZone != COMBAT_NO_ZONE && (g.combat.zone[COMBAT_ZONE_HEAD].unlockMask & bit))
        return true;
    if ((g.combat.zoneBroken & COMBAT_ZONE_APPENDAGE_BIT) && g.combat.appendZone != COMBAT_NO_ZONE && (g.combat.zone[COMBAT_ZONE_APPENDAGE].unlockMask & bit))
        return true;
    return false;
}

// combatResolveBodyHit: body-only resolution for creatures without zones (or
// when no zone rect contains the point). The body multiplier is always 100, so
// damage is the base value unchanged.
inline CombatBodyHit combatResolveBodyHit(const Game &g, int32_t base) {
    CombatBodyHit r;
    r.zone = COMBAT_NO_ZONE;
    r.mul = 100;
    r.dmg = (base <= 0) ? 0 : ((base > 0xFFFF) ? 0xFFFF : static_cast<uint16_t>(base));
    return r;
}

// A zone box is a face-relative origin: the world rect origin is the body
// anchor plus the DIR8 rotation of (ox, oy); the box itself stays axis-aligned
// (same projection the attack windows use). int32 intermediates keep the
// rotation exact for any int8 box offset. `bx/by` is the resolver's world
// anchor (the beast body) and `fx/fy` the explicit facing; the resolver never
// needs g.monster.fx/fy when the caller passes its own vector.
inline bool combatZoneContains(int16_t bx, int16_t by, int16_t fx, int16_t fy, const CombatBox &b, int16_t hx, int16_t hy) {
    int16_t dx, dy;
    combatFacePoint(fx, fy, b.ox, b.oy, dx, dy);
    // Battlefield coords: anchor <= WORLD_W/H (256) and the int8 box rotation
    // with |fx|,|fy| <= 16 gives |dx|,|dy| <= 254, so x+w <= 511.
    const int16_t x = static_cast<int16_t>(bx + static_cast<int16_t>(dx));
    const int16_t y = static_cast<int16_t>(by + static_cast<int16_t>(dy));
    return hx >= x && hx < x + b.w && hy >= y && hy < y + b.h;
}

// Beast convenience: the anchor and facing are the live monster fields.
inline bool combatZoneContains(const Game &g, const CombatBox &b, int16_t hx, int16_t hy) {
    return combatZoneContains(g.monster.x, g.monster.y, g.monster.fx, g.monster.fy, b, hx, hy);
}

// Landed player hit against the 3-hitzone model. The body is implicit and wins
// ties: a zone candidate replaces it only on a strictly higher final
// multiplier (tie -> body, then head, then appendage), matching the zone test
// order. Any landed hit on a zone drains its pool; a drained pool flips a
// single broken bit when the hit's phys is in breakTypes (the shared monster
// rule, now also used by static props). Zero cart reads (all zone scalars were
// cached at spawn).
inline MH_COMBAT_NI CombatBodyHit combatZoneHitResolveAt(Game &g, int32_t base, uint8_t phys, int16_t hx, int16_t hy, int16_t bx, int16_t by, int16_t fx, int16_t fy) {
    CombatBodyHit r;
    r.zone = COMBAT_NO_ZONE;
    r.mul = 100;
    r.dmg = 0;
    if (base <= 0)
        return r;

    uint8_t best = COMBAT_NO_ZONE;
    uint32_t bestMul = 100;   // implicit body candidate

    // Head first, then appendage: strict `>` means head wins a head/appendage
    // tie and the body wins any tie at 100.
    if (g.combat.headZone != COMBAT_NO_ZONE && !(g.combat.zoneBroken & COMBAT_ZONE_HEAD_BIT)) {
        const CombatZoneCache &z = g.combat.zone[COMBAT_ZONE_HEAD];
        if (combatZoneContains(bx, by, fx, fy, z.box, hx, hy) && z.dmgMul > bestMul) {
            best = COMBAT_ZONE_HEAD;
            bestMul = z.dmgMul;
        }
    }
    if (g.combat.appendZone != COMBAT_NO_ZONE && !(g.combat.zoneBroken & COMBAT_ZONE_APPENDAGE_BIT)) {
        const CombatZoneCache &z = g.combat.zone[COMBAT_ZONE_APPENDAGE];
        if (combatZoneContains(bx, by, fx, fy, z.box, hx, hy) && z.dmgMul > bestMul) {
            best = COMBAT_ZONE_APPENDAGE;
            bestMul = z.dmgMul;
        }
    }

    if (best == COMBAT_NO_ZONE) {
        r.dmg = (base > 0xFFFF) ? 0xFFFF : static_cast<uint16_t>(base);
        return r;
    }

    const uint8_t dmgMul = static_cast<uint8_t>(bestMul);
    uint32_t out = combatMulPercent(static_cast<uint32_t>(base), dmgMul);
    if (out > 0xFFFFu)
        out = 0xFFFFu;

    CombatZoneCache &z = g.combat.zone[best];
    const uint8_t pct = (out > 255u) ? 255u : static_cast<uint8_t>(out);
    z.hp = (pct < z.hp) ? static_cast<uint8_t>(z.hp - pct) : 0;
    if (z.hp == 0 && (phys & z.breakTypes)) {
        g.combat.zoneBroken |= (best == COMBAT_ZONE_HEAD) ? COMBAT_ZONE_HEAD_BIT : COMBAT_ZONE_APPENDAGE_BIT;
    }

    const uint32_t body = combatMulPercent(out, z.bodyShare);
    r.zone = best;
    r.mul = dmgMul;
    r.dmg = (body > 0xFFFFu) ? 0xFFFFu : static_cast<uint16_t>(body);
    return r;
}

// Beast resolve: anchor/facing come from the live monster record.
inline CombatBodyHit combatZoneHitResolve(Game &g, int32_t base, uint8_t phys, int16_t hx, int16_t hy) {
    return combatZoneHitResolveAt(g, base, phys, hx, hy, g.monster.x, g.monster.y, g.monster.fx, g.monster.fy);
}

// combatZoneStagger: the staggerOnHit of the zone a hit landed on (0 for the
// body). Reads only the cached scalars.
inline uint8_t combatZoneStagger(const Game &g, uint8_t zone) {
    if (zone >= COMBAT_ZONE_COUNT)
        return 0;
    return g.combat.zone[zone].staggerOnHit;
}

// Overlay frame index (render): frames are east-intact, east-broken,
// west-intact, west-broken (tools/gen-art.py tail sheet). `broken` is the
// zone's broken bit (0 intact, 1 broken). Pure so host/device suites pin the
// data-art linkage.
inline uint8_t combatPartArtFrame(bool west, uint8_t broken) {
    return static_cast<uint8_t>((west ? 2 : 0) + (broken ? 1 : 0));
}

#undef MH_COMBAT_NI

}   // namespace mh
