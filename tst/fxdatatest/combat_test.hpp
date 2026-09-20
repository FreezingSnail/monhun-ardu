#pragma once
// Ardens device loader test for src/core/combat.hpp (beads monhun-ardu-ljj.2,
// cgk).
//
// Reads the real mhCombat blob out of the single FX image on d1 through the
// shipping loader (mhFxRead* path), so a host-padded struct, a meta offset or
// a packing slip cannot pass silently. All cart reads happen after the
// FX::enableOLED / waitForNextPlane / FX::disableOLED bracket the render loop
// uses (the OLED owns the shared SPI bus while a plane is painted).
//
// The spawned-read count is asserted against the cache model: one spawn burst
// <= 40 accesses, an attack load <= 24, a guard decision <= 12, a landed hit
// <= 12, and 0 cart reads per tick in the steady state (combatTick). The
// counter is enabled by MH_FX_READ_COUNT in test_combat.ino.
//
// cgk: the blob carries the ravager head + appendage zones and a two-window
// tail_sweep, so the zone/break/guard paths below read real records. 4t4 added
// the heavy appendage (long tail) zone; 76y added the chicken's lunge head +
// legs (appendage) zones and its legs-only collide box, so SWEEP stays
// HEAVY now resolves its tail.
#include "harness/fxtest.hpp"
#include "src/core/combat.hpp"
#include "src/core/monster.hpp"       // migration A: sim consumes the attack cache
#include "src/core/projectiles.hpp"   // addEffect (playerHurt side)
#include "src/generated/combat_expect.hpp"

#include <avr/pgmspace.h>
#include <stdint.h>

namespace combatcheck {

using namespace mh;

namespace {

// Byte-compare a loader-decoded value struct against a PROGMEM expectation.
// The device image has a hard flash ceiling, so the per-record spot checks are
// one assert per record (one call + one shared label) instead of one per field.
// Every field is still pinned, and the same blob bytes are pinned against the
// generated structs by the host pack suite (tst/combat_pack_test.hpp).
inline bool progEq(const void *ram, const void *flash, uint8_t n) {
    const uint8_t *r = static_cast<const uint8_t *>(ram);
    const uint8_t *f = static_cast<const uint8_t *>(flash);
    for (uint8_t i = 0; i < n; i++)
        if (r[i] != pgm_read_byte(f + i))
            return false;
    return true;
}

// The byte compare is only valid while the AVR value structs stay padding-free
// and equal to the packed ABI (uint16 alignment is 1 on AVR; the packed record
// sizes come from the generated expect header). CombatWindow drops the packed
// record's trailing reserved flags byte.
static_assert(sizeof(CombatCreature) == combat_expect::CREATURE_SIZE, "creature value struct must stay packed");
static_assert(sizeof(CombatZone) == combat_expect::ZONE_SIZE, "zone value struct must stay packed");
static_assert(sizeof(CombatAttackValue) == combat_expect::ATTACK_SIZE, "attack value struct must stay packed");
static_assert(sizeof(CombatWindow) == combat_expect::WINDOW_SIZE - 1, "window value struct must stay packed minus flags");
static_assert(sizeof(CombatGuard) == combat_expect::GUARD_SIZE, "guard value struct must stay packed");
static_assert(sizeof(CombatPattern) == combat_expect::PATTERN_SIZE, "pattern value struct must stay packed");
static_assert(sizeof(CombatStep) == combat_expect::STEP_SIZE, "step value struct must stay packed");
static_assert(sizeof(CombatSkeleton) == combat_expect::SKELETON_SIZE, "skeleton value struct must stay packed");

// Expected records read through the cart loader. Values are the generated
// expect constants where they exist (so a data regen that changes a value
// fails here too), literals otherwise; `combat::` index constants keep the
// graph links symbolic.
static const uint8_t kCreatureIds[] PROGMEM = {combat::CREATURE_HEAVY, combat::CREATURE_LUNGE, combat::CREATURE_SWEEP};
static const CombatCreature kCreatures[] PROGMEM = {
    // skeletonIdx, profileIdx, headZone, appendZone, firstAttack, attackCount,
    // firstPattern, patternCount, w, h, spd, collide, hp, spawnX, spawnY,
    // flags, sheet, brokenW, brokenH, enrageHpPct/SpdMul/FaceHold/Cue
    {combat::SKELETON_LONGTAIL,
     0,
     COMBAT_NO_ZONE,
     combat::ZONE_HEAVY_APPENDAGE,
     combat::ATTACK_HEAVY_BITE,
     combat_expect::CREATURE_HEAVY_ATTACKS,
     combat::PATTERN_HEAVY_P_SPIN,
     combat_expect::CREATURE_HEAVY_PATTERNS,
     combat_expect::CREATURE_HEAVY_W,
     combat_expect::CREATURE_HEAVY_H,
     combat_expect::CREATURE_HEAVY_SPD,
     {combat_expect::CREATURE_HEAVY_COLLIDE_OX, combat_expect::CREATURE_HEAVY_COLLIDE_OY, combat_expect::CREATURE_HEAVY_COLLIDE_W, combat_expect::CREATURE_HEAVY_COLLIDE_H},
     combat_expect::CREATURE_HEAVY_HP,
     200,
     40,
     combat_expect::CREATURE_HEAVY_STATIC,
     combat_expect::CREATURE_HEAVY_SHEET,
     combat_expect::CREATURE_HEAVY_BROKEN_W,
     combat_expect::CREATURE_HEAVY_BROKEN_H,
     0,
     0,
     0,
     0},
    {combat::SKELETON_CHICKEN,
     1,
     combat::ZONE_LUNGE_HEAD,
     combat::ZONE_LUNGE_APPENDAGE,
     combat::ATTACK_LUNGE_PECK,
     combat_expect::CREATURE_LUNGE_ATTACKS,
     combat::PATTERN_LUNGE_P_PECK,
     combat_expect::CREATURE_LUNGE_PATTERNS,
     combat_expect::CREATURE_LUNGE_W,
     combat_expect::CREATURE_LUNGE_H,
     combat_expect::CREATURE_LUNGE_SPD,
     {combat_expect::CREATURE_LUNGE_COLLIDE_OX, combat_expect::CREATURE_LUNGE_COLLIDE_OY, combat_expect::CREATURE_LUNGE_COLLIDE_W, combat_expect::CREATURE_LUNGE_COLLIDE_H},
     combat_expect::CREATURE_LUNGE_HP,
     200,
     40,
     combat_expect::CREATURE_LUNGE_STATIC,
     combat_expect::CREATURE_LUNGE_SHEET,
     combat_expect::CREATURE_LUNGE_BROKEN_W,
     combat_expect::CREATURE_LUNGE_BROKEN_H,
     0,
     0,
     0,
     0},
    {combat::SKELETON_BULL,
     7,
     combat::ZONE_SWEEP_HEAD,
     combat::ZONE_SWEEP_APPENDAGE,
     combat::ATTACK_SWEEP_STOMP,
     combat_expect::CREATURE_SWEEP_ATTACKS,
     combat::PATTERN_SWEEP_P_STOMP,
     combat_expect::CREATURE_SWEEP_PATTERNS,
     combat_expect::CREATURE_SWEEP_W,
     combat_expect::CREATURE_SWEEP_H,
     combat_expect::CREATURE_SWEEP_SPD,
     {combat_expect::CREATURE_SWEEP_COLLIDE_OX, combat_expect::CREATURE_SWEEP_COLLIDE_OY, combat_expect::CREATURE_SWEEP_COLLIDE_W, combat_expect::CREATURE_SWEEP_COLLIDE_H},
     combat_expect::CREATURE_SWEEP_HP,
     200,
     40,
     combat_expect::CREATURE_SWEEP_STATIC,
     combat_expect::CREATURE_SWEEP_SHEET,
     combat_expect::CREATURE_SWEEP_BROKEN_W,
     combat_expect::CREATURE_SWEEP_BROKEN_H,
     0,
     0,
     0,
     0},
};

static const uint8_t kZoneIds[] PROGMEM = {combat::ZONE_HEAVY_APPENDAGE, combat::ZONE_LUNGE_HEAD,   combat::ZONE_LUNGE_APPENDAGE,  combat::ZONE_SWEEP_HEAD,
                                           combat::ZONE_SWEEP_APPENDAGE, combat::ZONE_RAVAGER_HEAD, combat::ZONE_RAVAGER_APPENDAGE};
static const CombatZone kZones[] PROGMEM = {
    // box, hp, dmgMul, bodyShare, breakTypes, staggerOnHit, brokenDmgMul, brokenFlags, unlockMask
    {{-24, 0, 24, 16},
     combat_expect::ZONE_HEAVY_APPENDAGE_HP,
     combat_expect::ZONE_HEAVY_APPENDAGE_DMG_MUL,
     combat_expect::ZONE_HEAVY_APPENDAGE_BODY_SHARE,
     PHYS_SLASH,
     30,
     200,
     COMBAT_BROKEN_HURT_OFF | COMBAT_BROKEN_CUE,
     static_cast<uint8_t>(1u << combat::ATTACK_HEAVY_TAIL_SPIN)},
    {{18, 0, 11, 7}, combat_expect::ZONE_LUNGE_HEAD_HP, combat_expect::ZONE_LUNGE_HEAD_DMG_MUL, combat_expect::ZONE_LUNGE_HEAD_BODY_SHARE, PHYS_SLASH, 12, 130, COMBAT_BROKEN_HURT_OFF, 0},
    {{9, 0, 9, 24},
     combat_expect::ZONE_LUNGE_APPENDAGE_HP,
     combat_expect::ZONE_LUNGE_APPENDAGE_DMG_MUL,
     combat_expect::ZONE_LUNGE_APPENDAGE_BODY_SHARE,
     PHYS_SLASH,
     30,
     200,
     COMBAT_BROKEN_HURT_OFF | COMBAT_BROKEN_CUE,
     static_cast<uint8_t>(1u << combat::ATTACK_LUNGE_LEAP)},
    {{17, -4, 12, 10}, combat_expect::ZONE_SWEEP_HEAD_HP, combat_expect::ZONE_SWEEP_HEAD_DMG_MUL, combat_expect::ZONE_SWEEP_HEAD_BODY_SHARE, PHYS_SLASH, 12, 130, COMBAT_BROKEN_HURT_OFF, 0},
    {{4, 12, 20, 10},
     combat_expect::ZONE_SWEEP_APPENDAGE_HP,
     combat_expect::ZONE_SWEEP_APPENDAGE_DMG_MUL,
     combat_expect::ZONE_SWEEP_APPENDAGE_BODY_SHARE,
     PHYS_SLASH,
     30,
     200,
     COMBAT_BROKEN_HURT_OFF | COMBAT_BROKEN_CUE,
     static_cast<uint8_t>(1u << combat::ATTACK_SWEEP_STOMP)},
    {{20, 4, 12, 12}, combat_expect::ZONE_RAVAGER_HEAD_HP, combat_expect::ZONE_RAVAGER_HEAD_DMG_MUL, combat_expect::ZONE_RAVAGER_HEAD_BODY_SHARE, PHYS_SLASH, 12, 130, COMBAT_BROKEN_HURT_OFF, 0},
    {{-14, 8, 18, 10},
     combat_expect::ZONE_RAVAGER_APPENDAGE_HP,
     combat_expect::ZONE_RAVAGER_APPENDAGE_DMG_MUL,
     combat_expect::ZONE_RAVAGER_APPENDAGE_BODY_SHARE,
     PHYS_SLASH,
     30,
     200,
     COMBAT_BROKEN_HURT_OFF | COMBAT_BROKEN_CUE,
     static_cast<uint8_t>(1u << combat::ATTACK_RAVAGER_TAIL_SWEEP)},
};

static const uint8_t kAttackIds[] PROGMEM = {combat::ATTACK_HEAVY_BITE, combat::ATTACK_HEAVY_TAIL_SPIN, combat::ATTACK_LUNGE_PECK, combat::ATTACK_SWEEP_STOMP, combat::ATTACK_SWEEP_GORE};
static const CombatAttackValue kAttacks[] PROGMEM = {
    // moveType, moveSpeedF, moveDx, moveDy, facing, phys, elem, onHitEffect,
    // onHitPush, onHitStun, stagger, cue, wallStun, firstWindow, windowCount,
    // windup, active, recover, dmg, tell
    {MOVE_LUNGE,
     26,
     0,
     0,
     COMBAT_FACING_TRACK,
     PHYS_BLUNT,
     ELEM_NONE,
     0,
     0,
     0,
     0,
     1,
     combat_expect::ATTACK_HEAVY_BITE_WALLSTUN,
     combat::WINDOW_HEAVY_BITE_0,
     1,
     combat_expect::ATTACK_HEAVY_BITE_WINDUP,
     combat_expect::ATTACK_HEAVY_BITE_ACTIVE,
     combat_expect::ATTACK_HEAVY_BITE_RECOVER,
     combat_expect::ATTACK_HEAVY_BITE_DMG,
     combat_expect::ATTACK_HEAVY_BITE_TELL},
    {MOVE_NONE, 0, 0, 0, COMBAT_FACING_LOCK_AWAY, PHYS_BLUNT, ELEM_NONE, 0, 0, 0, 0, 1, 0, combat::WINDOW_HEAVY_TAIL_SPIN_0, 4, 42, 20, 55, 8, 0},
    {MOVE_LUNGE,
     18,
     0,
     0,
     COMBAT_FACING_TRACK,
     PHYS_BLUNT,
     ELEM_NONE,
     0,
     0,
     0,
     0,
     1,
     combat_expect::ATTACK_LUNGE_PECK_WALLSTUN,
     combat::WINDOW_LUNGE_PECK_0,
     1,
     combat_expect::ATTACK_LUNGE_PECK_WINDUP,
     combat_expect::ATTACK_LUNGE_PECK_ACTIVE,
     combat_expect::ATTACK_LUNGE_PECK_RECOVER,
     combat_expect::ATTACK_LUNGE_PECK_DMG,
     combat_expect::ATTACK_LUNGE_PECK_TELL},
    {MOVE_NONE,
     0,
     0,
     0,
     COMBAT_FACING_TRACK,
     PHYS_BLUNT,
     ELEM_NONE,
     0,
     0,
     0,
     0,
     1,
     combat_expect::ATTACK_SWEEP_STOMP_WALLSTUN,
     combat::WINDOW_SWEEP_STOMP_0,
     1,
     combat_expect::ATTACK_SWEEP_STOMP_WINDUP,
     combat_expect::ATTACK_SWEEP_STOMP_ACTIVE,
     combat_expect::ATTACK_SWEEP_STOMP_RECOVER,
     combat_expect::ATTACK_SWEEP_STOMP_DMG,
     combat_expect::ATTACK_SWEEP_STOMP_TELL},
    {MOVE_LUNGE, 34, 0, 0, COMBAT_FACING_LOCK, PHYS_BLUNT, ELEM_NONE, 0, 0, 0, 0, 1, 0, combat::WINDOW_SWEEP_GORE_0, 2, 46, 12, 55, 14, 0},
};

static const uint8_t kWindowIds[] PROGMEM = {combat::WINDOW_HEAVY_BITE_0,      combat::WINDOW_HEAVY_TAIL_SPIN_0, combat::WINDOW_HEAVY_TAIL_SPIN_1, combat::WINDOW_HEAVY_TAIL_SPIN_2,
                                             combat::WINDOW_HEAVY_TAIL_SPIN_3, combat::WINDOW_LUNGE_PECK_0,      combat::WINDOW_LUNGE_LEAP_0,      combat::WINDOW_RAVAGER_TAIL_SWEEP_0,
                                             combat::WINDOW_SWEEP_STOMP_0,     combat::WINDOW_SWEEP_GORE_0,      combat::WINDOW_SWEEP_GORE_1};
static const CombatWindow kWindows[] PROGMEM = {
    // t0, t1, box, dmgMul
    {0, 8, {14, 0, 18, 14}, 100},   {0, 5, {-20, 0, 24, 16}, 100}, {6, 10, {0, -22, 16, 24}, 100}, {11, 15, {22, 0, 24, 16}, 100}, {16, 20, {0, 22, 16, 24}, 100}, {0, 6, {14, -6, 12, 10}, 100},
    {0, 10, {12, -2, 18, 16}, 100}, {0, 5, {-22, 0, 30, 22}, 100}, {0, 10, {10, 2, 24, 14}, 100},  {0, 6, {16, -2, 16, 10}, 100},  {7, 12, {12, 2, 20, 14}, 100},
};

static const CombatPattern kPatterns[] PROGMEM = {
    {combat::STEP_LUNGE_P_PECK_0, 1, combat::GUARD_LUNGE_P_PECK}, {combat::STEP_SWEEP_P_STOMP_0, 1, combat::GUARD_SWEEP_P_STOMP}, {combat::STEP_SWEEP_P_GORE_0, 1, combat::GUARD_SWEEP_P_GORE}};
static const CombatGuard kGuards[] PROGMEM = {
    {combat_expect::PATTERN_LUNGE_P_PECK_MIN_DIST, combat_expect::PATTERN_LUNGE_P_PECK_MAX_DIST, 0, 100, 0, 0, combat_expect::PATTERN_LUNGE_P_PECK_CHANCE, 0, GUARD_FACING_ANY},
    {combat_expect::PATTERN_SWEEP_P_STOMP_MIN_DIST, combat_expect::PATTERN_SWEEP_P_STOMP_MAX_DIST, 0, 100, 0, 0, combat_expect::PATTERN_SWEEP_P_STOMP_CHANCE, 0, GUARD_FACING_ANY},
    {24, 255, 0, 100, 0, 0, 100, 0, GUARD_FACING_ANY}};
static const CombatStep kSteps[] PROGMEM = {{STEP_ATK, combat::ATTACK_LUNGE_PECK, 0, 100}};
static const CombatSkeleton kSkeleton[] PROGMEM = {{2, 2}};

}   // namespace

inline void test_combat(FxTest &test) {
    // -------------------------------------------------- FX/OLED bracket
    FX::enableOLED();
    arduboy.waitForNextPlane();
    FX::disableOLED();

    // ------------------------------------------------------- blob header
    // Direct FX reads (not through mhFxRead*, so not in the load counter).
    FX::seekData(mhCombat);
    const uint8_t magicLo = FX::readPendingUInt8();
    const uint8_t magicHi = FX::readEnd();
    FX::seekData(mhCombat + 2);
    const uint8_t version = FX::readPendingUInt8();
    const uint8_t flags = FX::readEnd();
    // One packed word per adjacent pair: magic, then version|flags. The device
    // image has a hard flash ceiling, so adjacent header bytes share an assert
    // (same coverage, one call/string each).
    test.expectEq(static_cast<uint16_t>(magicLo) | (static_cast<uint16_t>(magicHi) << 8), 0x4D43, F("header magic"));
    test.expectEq(static_cast<uint16_t>(version) | (static_cast<uint16_t>(flags) << 8), static_cast<uint16_t>(combat::VERSION) | (static_cast<uint16_t>(combat::FLAGS) << 8),
                  F("header version+flags"));

    static const uint16_t counts[10] = {
        combat::CREATURES_COUNT, combat::PROFILES_COUNT, combat::SKELETONS_COUNT, combat::ZONES_COUNT,  combat::ANCHORS_COUNT,
        combat::ATTACKS_COUNT,   combat::WINDOWS_COUNT,  combat::PATTERNS_COUNT,  combat::GUARDS_COUNT, combat::STEPS_COUNT,
    };
    for (uint8_t i = 0; i < 10; i++) {
        FX::seekData(mhCombat + 4 + static_cast<uint24_t>(i) * 2);
        const uint8_t lo = FX::readPendingUInt8();
        const uint8_t hi = FX::readEnd();
        test.expectEqIdx(static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8)), counts[i], F("header count"), i);
    }
    for (uint8_t i = 0; i < 4; i++) {
        FX::seekData(mhCombat + 4 + static_cast<uint24_t>(10 + i) * 2);
        const uint8_t lo = FX::readPendingUInt8();
        const uint8_t hi = FX::readEnd();
        test.expectEqIdx(static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8)), 0, F("reserved count"), i);
    }
    test.expectEq(combat::STEPS_OFF + static_cast<uint16_t>(combat::STEP_SIZE) * combat::STEPS_COUNT, combat::SIZE, F("sections end at size"));
    test.expectEq(combat_expect::BLOB_SIZE, combat::SIZE, F("expect blob size"));

    // ------------------------------------ per-record spot checks vs expect
    // One assert per record: the loader-decoded value struct is compared
    // byte-for-byte against a PROGMEM expectation (progEq), so every field is
    // pinned with one call and one shared label. The same blob bytes are pinned
    // against the generated structs by the host pack suite
    // (tst/combat_pack_test.hpp), and the rows below keep the expect-header
    // constants as the expected values.
    CombatCreature gotCreatures[3];
    for (uint8_t i = 0; i < 3; i++) {
        gotCreatures[i] = combatCreatureRead(pgm_read_byte(kCreatureIds + i));
        test.expectEq(progEq(&gotCreatures[i], &kCreatures[i], sizeof(CombatCreature)), 1, F("creature record"));
    }
    const CombatCreature lunge = gotCreatures[1];
    const CombatCreature sweep = gotCreatures[2];

    for (uint8_t i = 0; i < 7; i++) {
        const CombatZone got = combatZoneRead(pgm_read_byte(kZoneIds + i));
        test.expectEq(progEq(&got, &kZones[i], sizeof(CombatZone)), 1, F("zone record"));
    }
    for (uint8_t i = 0; i < 5; i++) {
        const CombatAttackValue got = combatAttackRead(pgm_read_byte(kAttackIds + i));
        test.expectEq(progEq(&got, &kAttacks[i], sizeof(CombatAttackValue)), 1, F("attack record"));
    }
    for (uint8_t i = 0; i < 11; i++) {
        const CombatWindow got = combatWindowRead(pgm_read_byte(kWindowIds + i));
        test.expectEq(progEq(&got, &kWindows[i], sizeof(CombatWindow)), 1, F("window record"));
    }
    // Named pin for the lock-away facing mode (the heavy spin row above
    // already carries facing 2).
    test.expectEq(COMBAT_FACING_LOCK_AWAY, 2, F("lock-away facing value"));

    // --------------------------------------------- cross-reference walk
    // Follow the record graph the live decision code follows
    // (creature -> attack -> window, creature -> pattern -> guard / step) and
    // compare each decoded record with its expectation row.
    const CombatSkeleton sk = combatSkeletonRead(lunge.skeletonIdx);
    test.expectEq(progEq(&sk, &kSkeleton[0], sizeof(CombatSkeleton)), 1, F("lunge skeleton record"));

    const CombatAttackValue lungeAtk = combatAttackRead(lunge.firstAttack);
    test.expectEq(progEq(&lungeAtk, &kAttacks[2], sizeof(CombatAttackValue)), 1, F("lunge first attack record"));

    const CombatPattern pat = combatPatternRead(lunge.firstPattern);
    test.expectEq(progEq(&pat, &kPatterns[0], sizeof(CombatPattern)), 1, F("lunge pattern record"));
    const CombatGuard guard = combatGuardRead(pat.guardIdx);
    test.expectEq(progEq(&guard, &kGuards[0], sizeof(CombatGuard)), 1, F("lunge guard record"));
    const CombatStep step = combatStepRead(pat.firstStep);
    test.expectEq(progEq(&step, &kSteps[0], sizeof(CombatStep)), 1, F("lunge step record"));

    const CombatWindow win = combatWindowRead(lungeAtk.firstWindow);
    test.expectEq(progEq(&win, &kWindows[5], sizeof(CombatWindow)), 1, F("lunge window record"));

    // BULL (nch.9) opens with p_stomp (<=24) then p_gore (>=24).
    const CombatPattern sweepPat = combatPatternRead(sweep.firstPattern);
    test.expectEq(progEq(&sweepPat, &kPatterns[1], sizeof(CombatPattern)), 1, F("bull stomp pattern record"));
    const CombatGuard sweepGuard = combatGuardRead(sweepPat.guardIdx);
    test.expectEq(progEq(&sweepGuard, &kGuards[1], sizeof(CombatGuard)), 1, F("bull stomp guard record"));
    const CombatPattern sweepGorePat = combatPatternRead(static_cast<uint8_t>(sweep.firstPattern + 1));
    test.expectEq(progEq(&sweepGorePat, &kPatterns[2], sizeof(CombatPattern)), 1, F("bull gore pattern record"));
    const CombatGuard sweepGoreGuard = combatGuardRead(sweepGorePat.guardIdx);
    test.expectEq(progEq(&sweepGoreGuard, &kGuards[2], sizeof(CombatGuard)), 1, F("bull gore guard record"));

    // -------------------------------------------------- loader read budget
    static Game g;
    uint16_t before = mhFxReadCount;
    const uint8_t loaded = creatureLoad(g, combat::CREATURE_LUNGE);
    const uint16_t spawnReads = static_cast<uint16_t>(mhFxReadCount - before);
    test.expectEq(loaded, combat::CREATURE_LUNGE, F("creatureLoad returns idx"));
    test.expectEq(g.combat.creature, combat::CREATURE_LUNGE, F("cache creature"));
    test.expectEq(spawnReads <= 40, 1, F("spawn burst <= 40 reads"));

    // Cache values read for real (profile record through the loader).
    test.expectEq(g.combat.profile.engageDist, 36, F("cache engageDist"));
    test.expectEq(g.combat.profile.keepDist, 16, F("cache keepDist"));
    test.expectEq(g.combat.profile.faceHold, combat_expect::PROFILE_LUNGE_FACE_HOLD, F("cache faceHold"));
    test.expectEq(g.combat.profile.attackDist, 42, F("cache attackDist"));
    test.expectEq(g.combat.profile.cdBase, 55, F("cache cdBase"));
    test.expectEq(g.combat.profile.cdJitter, 40, F("cache cdJitter"));
    test.expectEq(g.combat.profile.spawnT, 90, F("cache spawnT"));
    test.expectEq(g.combat.profile.spawnCd, 140, F("cache spawnCd"));
    test.expectEq(g.combat.profile.stunRecoverT, 24, F("cache stunRecoverT"));
    test.expectEq(g.combat.zoneBroken, 0, F("zones reset"));
    test.expectEq(g.combat.stepT, 0, F("step timer reset"));

    before = mhFxReadCount;
    const uint8_t atk = attackLoad(g, combat::ATTACK_LUNGE_PECK);
    const uint16_t attackReads = static_cast<uint16_t>(mhFxReadCount - before);
    test.expectEq(atk, combat::ATTACK_LUNGE_PECK, F("attackLoad returns idx"));
    test.expectEq(attackReads <= 24, 1, F("attack load <= 24 reads"));
    test.expectEq(g.combat.attack.windup, 22, F("cache windup"));
    test.expectEq(g.combat.attack.active, 6, F("cache active"));
    test.expectEq(g.combat.attack.recover, 30, F("cache recover"));
    test.expectEq(g.combat.attack.dmg, 7, F("cache dmg"));
    test.expectEq(g.combat.attack.moveType, 1, F("cache moveType"));
    test.expectEq(g.combat.attack.moveSpeedF, 18, F("cache moveSpeedF"));
    // feel.7: the hop dx/dy couple is read in the same move burst; shipped
    // none/lunge leaves both 0. One packed word keeps the device image in budget.
    test.expectEq(static_cast<uint16_t>(static_cast<uint8_t>(g.combat.attack.moveDx)) | (static_cast<uint16_t>(static_cast<uint8_t>(g.combat.attack.moveDy)) << 8), 0, F("cache hop moveDx/Dy"));
    test.expectEq(g.combat.attack.facing, 0, F("cache facing"));
    test.expectEq(g.combat.attack.wallStun, combat_expect::ATTACK_LUNGE_PECK_WALLSTUN, F("cache wallStun"));
    test.expectEq(g.combat.attack.tell, combat_expect::ATTACK_LUNGE_PECK_TELL, F("cache tell"));
    test.expectEq(g.combat.attack.winIdx, combat::WINDOW_LUNGE_PECK_0, F("cache winIdx"));
    test.expectEq(g.combat.attack.win.t1, 6, F("cache win t1"));
    test.expectEq(g.combat.attack.win.box.ox, 14, F("cache win ox"));
    test.expectEq(g.combat.attack.win.box.w, 12, F("cache win w"));
    test.expectEq(g.combat.attack.win.dmgMul, 100, F("cache win dmgMul"));

    attackWindowLoad(g, combat::WINDOW_LUNGE_LEAP_0);
    test.expectEq(g.combat.attack.winIdx, combat::WINDOW_LUNGE_LEAP_0, F("window switch idx"));
    test.expectEq(g.combat.attack.win.t1, 10, F("window switch t1"));
    test.expectEq(g.combat.attack.win.box.ox, 12, F("window switch ox"));
    test.expectEq(g.combat.attack.win.box.w, 18, F("window switch w"));

    // ---------------------------------------------------- guard evaluation
    before = mhFxReadCount;
    CombatGuardInput in = {33, 100, 0, 0, 0xFFFF, 0};
    const bool lungeAt33 = combatGuardPasses(g, combat::PATTERN_LUNGE_P_LEAP, in);
    const uint16_t guardReads = static_cast<uint16_t>(mhFxReadCount - before);
    test.expectEq(lungeAt33, 1, F("leap guard dist 33 accepted"));
    test.expectEq(guardReads <= 12, 1, F("guard decision <= 12 reads"));

    in.dist = 29;
    test.expectEq(combatGuardPasses(g, combat::PATTERN_LUNGE_P_PECK, in), 0, F("peck guard dist 29 rejected"));
    test.expectEq(combatGuardPasses(g, combat::PATTERN_LUNGE_P_LEAP, in), 1, F("leap guard dist 29 accepted"));
    in.dist = 28;
    test.expectEq(combatGuardPasses(g, combat::PATTERN_LUNGE_P_PECK, in), 1, F("peck guard dist 28 accepted"));
    test.expectEq(combatGuardPasses(g, combat::PATTERN_LUNGE_P_LEAP, in), 1, F("leap guard dist 28 accepted"));
    in.dist = 0;
    test.expectEq(combatGuardPasses(g, combat::PATTERN_LUNGE_P_LEAP, in), 0, F("leap guard dist 0 rejected"));
    test.expectEq(combatGuardPasses(g, 99, in), 0, F("unknown pattern rejected"));

    creatureLoad(g, combat::CREATURE_HEAVY);
    test.expectEq(g.combat.profile.keepDist, 12, F("heavy cache keepDist 12"));
    test.expectEq(g.combat.profile.faceHold, combat_expect::PROFILE_HEAVY_FACE_HOLD, F("heavy cache faceHold"));
    in.dist = 30;
    test.expectEq(combatGuardPasses(g, combat::PATTERN_HEAVY_P_SPIN, in), 1, F("heavy spin dist 30 accepted"));
    test.expectEq(combatGuardPasses(g, combat::PATTERN_HEAVY_P_BITE, in), 1, F("heavy bite band reaches 30"));
    in.dist = 31;
    test.expectEq(combatGuardPasses(g, combat::PATTERN_HEAVY_P_SPIN, in), 0, F("heavy spin dist 31 rejected"));
    test.expectEq(combatGuardPasses(g, combat::PATTERN_HEAVY_P_BITE, in), 1, F("heavy bite dist 31 accepted"));

    // Deterministic chance (pinned roll vectors, same function host-tested).
    test.expectEq(combatChanceRoll(0, 1, 2, 0), 9, F("roll tick0"));
    test.expectEq(combatChanceRoll(10, 1, 3, 0), 28, F("roll tick10"));
    test.expectEq(combatChancePasses(0, 1, 2, 0, 10), 1, F("roll 9 < 10 passes"));
    test.expectEq(combatChancePasses(0, 1, 2, 0, 9), 0, F("roll 9 < 9 fails"));
    test.expectEq(combatChancePasses(0, 1, 2, 0, 100), 1, F("chance 100 passes"));
    test.expectEq(combatChancePasses(0, 1, 2, 0, 0), 0, F("chance 0 fails"));

    // --------------------------------------------------- damage routing
    creatureLoad(g, combat::CREATURE_LUNGE);
    before = mhFxReadCount;
    CombatBodyHit r = combatResolveBodyHit(g, 12);
    const uint16_t hitReads = static_cast<uint16_t>(mhFxReadCount - before);
    test.expectEq(r.zone, COMBAT_NO_ZONE, F("body hit zone"));
    test.expectEq(r.mul, 100, F("body hit multiplier"));
    test.expectEq(r.dmg, 12, F("body hit damage"));
    test.expectEq(hitReads <= 12, 1, F("body hit <= 12 reads"));

    // ------------------------------------- body box + spawn (migration B)
    {
        CombatBox bb = {9, 9, 9, 9};
        before = mhFxReadCount;
        const bool boxOk = combatCreatureBodyBox(combat::CREATURE_LUNGE, bb);
        const uint16_t boxReads = static_cast<uint16_t>(mhFxReadCount - before);
        test.expectEq(boxOk, 1, F("lunge body box readable"));
        test.expectEq(bb.ox, 0, F("lunge box ox"));
        test.expectEq(bb.oy, 0, F("lunge box oy"));
        test.expectEq(bb.w, combat_expect::CREATURE_LUNGE_W, F("lunge box w"));
        test.expectEq(bb.h, combat_expect::CREATURE_LUNGE_H, F("lunge box h"));
        test.expectEq(boxReads <= 8, 1, F("body box <= 8 reads"));

        combatCreatureBodyBox(combat::CREATURE_SWEEP, bb);
        test.expectEq(bb.w, combat_expect::CREATURE_SWEEP_W, F("sweep box w"));
        combatCreatureBodyBox(combat::CREATURE_HEAVY, bb);
        test.expectEq(bb.w, combat_expect::CREATURE_HEAVY_W, F("heavy box w"));

        const CombatSpawn sp = combatCreatureSpawnRead(combat::CREATURE_LUNGE);
        test.expectEq(sp.hp, combat_expect::CREATURE_LUNGE_HP, F("spawn hp"));
        test.expectEq(sp.spd, combat_expect::CREATURE_LUNGE_SPD, F("spawn spd"));
        test.expectEq(sp.x, 200, F("spawn x"));
        test.expectEq(sp.y, 40, F("spawn y"));

        before = mhFxReadCount;
        initMonster(g, MON_HEAVY);
        const uint16_t initReads = static_cast<uint16_t>(mhFxReadCount - before);
        test.expectEq(g.monster.w, combat_expect::CREATURE_HEAVY_W, F("spawn width from box"));
        test.expectEq(g.monster.h, combat_expect::CREATURE_HEAVY_H, F("spawn height from box"));
        test.expectEq(g.monster.hp, combat_expect::CREATURE_HEAVY_HP, F("spawn hp from record"));
        test.expectEq(g.monster.spd, combat_expect::CREATURE_HEAVY_SPD, F("spawn spd from record"));
        test.expectEq(g.combat.body.w, g.monster.w, F("cached body box w"));
        test.expectEq(g.combat.body.h, g.monster.h, F("cached body box h"));
        // nch.11: HEAVY's target rect is the tail-inclusive collide box, not body.
        test.expectEq(g.target.rect.w, combat_expect::CREATURE_HEAVY_COLLIDE_W, F("target rect w from collide box"));
        test.expectEq(g.target.rect.x, g.monster.x + combat_expect::CREATURE_HEAVY_COLLIDE_OX, F("target rect x at collide box origin"));
        test.expectEq(g.combat.zoneBroken, 0, F("spawn zones intact"));
        test.expectEq(initReads <= 40, 1, F("initMonster burst <= 40 reads"));
    }

    // ------------------------------- ravager zones (3-hitzone model)
    // Device-side proof the real cart records drive the zone path: record spot
    // values, spawn pool seeding, break -> attack disable, enrage guard swap.
    {
        const CombatZone head = combatZoneRead(combat::ZONE_RAVAGER_HEAD);
        test.expectEq(head.hp, combat_expect::ZONE_RAVAGER_HEAD_HP, F("head hp"));
        test.expectEq(head.dmgMul, combat_expect::ZONE_RAVAGER_HEAD_DMG_MUL, F("head dmgMul"));
        test.expectEq(head.staggerOnHit, 12, F("head stagger"));
        const CombatZone tail = combatZoneRead(combat::ZONE_RAVAGER_APPENDAGE);
        test.expectEq(tail.hp, combat_expect::ZONE_RAVAGER_APPENDAGE_HP, F("tail hp"));
        test.expectEq(tail.dmgMul, combat_expect::ZONE_RAVAGER_APPENDAGE_DMG_MUL, F("tail dmgMul"));
        test.expectEq(tail.bodyShare, combat_expect::ZONE_RAVAGER_APPENDAGE_BODY_SHARE, F("tail bodyShare"));
        test.expectEq(tail.breakTypes, PHYS_SLASH, F("tail break slash"));
        test.expectEq(tail.unlockMask, static_cast<uint8_t>(1u << combat::ATTACK_RAVAGER_TAIL_SWEEP), F("tail unlock sweep"));

        before = mhFxReadCount;
        creatureLoad(g, combat::CREATURE_RAVAGER);
        const uint16_t ravReads = static_cast<uint16_t>(mhFxReadCount - before);
        test.expectEq(g.combat.headZone, combat::ZONE_RAVAGER_HEAD, F("head zone index"));
        test.expectEq(g.combat.appendZone, combat::ZONE_RAVAGER_APPENDAGE, F("tail zone index"));
        test.expectEq(g.combat.zone[COMBAT_ZONE_HEAD].hp, combat_expect::ZONE_RAVAGER_HEAD_HP, F("head pool seeded"));
        test.expectEq(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp, combat_expect::ZONE_RAVAGER_APPENDAGE_HP, F("tail pool seeded"));
        test.expectEq(ravReads <= 40, 1, F("ravager spawn <= 40 reads"));

        // Head point hit drains the head pool by base*130/100.
        g.monster.x = 100;
        g.monster.y = 40;
        g.monster.fx = 16;
        g.monster.fy = 0;
        const CombatBodyHit headHit = combatZoneHitResolve(g, 10, PHYS_BLUNT, 125, 48);
        test.expectEq(headHit.zone, COMBAT_ZONE_HEAD, F("head zone wins"));
        test.expectEq(headHit.mul, 130, F("head multiplier"));
        test.expectEq(headHit.dmg, 13, F("head body share 100"));

        // Tail point hit drains the tail pool; break disables tail_sweep.
        g.combat.zoneBroken = COMBAT_ZONE_APPENDAGE_BIT;
        test.expectEq(combatAttackDisabled(g, combat::ATTACK_RAVAGER_TAIL_SWEEP), 1, F("broken tail disables sweep"));
        test.expectEq(combatAttackDisabled(g, combat::ATTACK_RAVAGER_BITE), 0, F("broken tail keeps bite"));
        CombatGuardInput en = {0, 100, 0, 0, 0xFFFF, 0};
        test.expectEq(combatGuardPasses(g, combat::PATTERN_RAVAGER_P_ENRAGED, en), 1, F("broken tail enrage guard"));
        test.expectEq(combatGuardPasses(g, combat::PATTERN_RAVAGER_P_SWEEP, en), 1, F("p_sweep dist 0 still matches"));
        g.combat.zoneBroken = 0;
        test.expectEq(combatGuardPasses(g, combat::PATTERN_RAVAGER_P_ENRAGED, en), 0, F("intact tail not enraged"));
    }

    // ------------------------------- multi-window refresh (ljj.8)
    {
        initMonster(g, MON_RAVAGER);
        before = mhFxReadCount;
        monsterAttackSet(g, combat::ATTACK_RAVAGER_TAIL_SWEEP);
        const uint16_t mwReads = static_cast<uint16_t>(mhFxReadCount - before);
        test.expectEq(g.monster.winRemain, 1, F("two windows pending"));
        test.expectEq(g.combat.attack.win.t1, 5, F("window 0 t1"));
        test.expectEq(mwReads <= 24, 1, F("multi-window load <= 24 reads"));
        g.monster.t = 6;   // past the first window's t1
        monsterWindowNext(g);
        test.expectEq(g.combat.attack.winIdx, combat::WINDOW_RAVAGER_TAIL_SWEEP_1, F("window 1 loaded"));
        test.expectEq(g.monster.winRemain, 0, F("windows exhausted"));
    }

    // --------------------------------------------------- bad-id fallback
    const uint8_t fallback = creatureLoad(g, 99);
    test.expectEq(fallback, 0, F("bad creature id -> creature 0"));
    test.expectEq(g.combat.creature, combat::CREATURE_HEAVY, F("fallback creature cache"));
    test.expectEq(g.combat.profile.cdBase, 55, F("fallback profile cdBase"));
    test.expectEq(g.combat.zoneBroken, 0, F("fallback resets zones"));
    const uint8_t badAtk = attackLoad(g, 200);
    test.expectEq(badAtk, 0, F("bad attack id -> attack 0"));
    test.expectEq(g.combat.attack.winIdx, combat::WINDOW_HEAVY_BITE_0, F("fallback window"));

    // --------------------------------- sim attack path consumes the cache
    // (migration A / ljj.3): attack start loads the identity + first window
    // once; the windup and active ticks then perform zero cart reads.
    initGame(g, W_SWORD);
    initMonster(g, MON_LUNGE);
    before = mhFxReadCount;
    const uint8_t simAtk = monsterAttackSet(g, combat::ATTACK_LUNGE_PECK);
    const uint16_t simAtkReads = static_cast<uint16_t>(mhFxReadCount - before);
    test.expectEq(simAtk, combat::ATTACK_LUNGE_PECK, F("sim attack set"));
    test.expectEq(simAtkReads <= 24, 1, F("sim attack start <= 24 reads"));
    test.expectEq(g.monster.atkIdx, combat::ATTACK_LUNGE_PECK, F("sim attack identity"));
    test.expectEq(g.monster.winRemain, 0, F("sim single window"));

    g.monster.state = MS_WINDUP;
    g.monster.t = 2;
    g.monster.windupMax = 2;
    before = mhFxReadCount;
    updateMonster(g);   // tell tick 1
    updateMonster(g);   // tell tick 2 -> startMonsterAttack (cache only)
    const uint16_t windupReads = static_cast<uint16_t>(mhFxReadCount - before);
    test.expectEq(g.monster.state, MS_ATTACK, F("windup releases to attack"));
    test.expectEq(windupReads, 0, F("windup ticks read-free"));

    g.monster.x = 224;   // far from the player: no contact, no new decision
    g.monster.y = 40;
    before = mhFxReadCount;
    for (uint8_t i = 0; i < 5; i++)
        updateMonster(g);
    const uint16_t activeReads = static_cast<uint16_t>(mhFxReadCount - before);
    test.expectEq(activeReads, 0, F("active ticks read-free"));

    before = mhFxReadCount;
    attackWindowLoad(g, combat::WINDOW_LUNGE_LEAP_0);
    const uint16_t windowReads = static_cast<uint16_t>(mhFxReadCount - before);
    test.expectEq(windowReads <= 8, 1, F("window switch <= 8 reads"));

    // ------------------------------------- steady-state per-tick read gate
    before = mhFxReadCount;
    for (uint16_t i = 0; i < 256; i++)
        combatTick(g);
    const uint16_t tickReads = static_cast<uint16_t>(mhFxReadCount - before);
    test.expectEq(tickReads, 0, F("steady-state 0 reads/tick"));

    Serial.print(F("C reads spawn="));
    Serial.print(spawnReads);
    Serial.print(F(" attack="));
    Serial.print(attackReads);
    Serial.print(F(" guard="));
    Serial.print(guardReads);
    Serial.print(F(" hit="));
    Serial.print(hitReads);
    Serial.print(F(" tick256="));
    Serial.print(tickReads);
    Serial.print(F(" simAtk="));
    Serial.print(simAtkReads);
    Serial.print(F(" simTk="));
    Serial.print(static_cast<uint16_t>(windupReads + activeReads));
    Serial.print(F(" winSw="));
    Serial.println(windowReads);
}

}   // namespace combatcheck
