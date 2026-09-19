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

#include <stdint.h>

namespace combatcheck {

using namespace mh;

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
    test.expectEq(magicLo, 0x43, F("header magic lo"));
    test.expectEq(magicHi, 0x4D, F("header magic hi"));
    test.expectEq(version, combat::VERSION, F("header version"));
    test.expectEq(flags, combat::FLAGS, F("header flags"));

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
    const CombatCreature heavy = combatCreatureRead(combat::CREATURE_HEAVY);
    test.expectEq(heavy.hp, combat_expect::CREATURE_HEAVY_HP, F("heavy hp"));
    test.expectEq(heavy.spd, combat_expect::CREATURE_HEAVY_SPD, F("heavy spd"));
    test.expectEq(heavy.w, combat_expect::CREATURE_HEAVY_W, F("heavy w"));
    test.expectEq(heavy.h, combat_expect::CREATURE_HEAVY_H, F("heavy h"));
    test.expectEq(heavy.attackCount, combat_expect::CREATURE_HEAVY_ATTACKS, F("heavy attacks"));
    test.expectEq(heavy.patternCount, combat_expect::CREATURE_HEAVY_PATTERNS, F("heavy patterns"));
    test.expectEq(heavy.headZone, COMBAT_NO_ZONE, F("heavy no head zone"));
    test.expectEq(heavy.appendZone, combat::ZONE_HEAVY_APPENDAGE, F("heavy appendage zone"));

    // 4t4: heavy's long tail is a real appendage record with the overlay box.
    const CombatZone heavyTail = combatZoneRead(combat::ZONE_HEAVY_APPENDAGE);
    test.expectEq(static_cast<uint32_t>(heavyTail.box.ox), static_cast<uint32_t>(-24), F("heavy tail box ox"));
    test.expectEq(heavyTail.box.oy, 0, F("heavy tail box oy"));
    test.expectEq(heavyTail.box.w, 24, F("heavy tail box w"));
    test.expectEq(heavyTail.box.h, 16, F("heavy tail box h"));
    test.expectEq(heavyTail.hp, combat_expect::ZONE_HEAVY_APPENDAGE_HP, F("heavy tail hp"));
    test.expectEq(heavyTail.dmgMul, combat_expect::ZONE_HEAVY_APPENDAGE_DMG_MUL, F("heavy tail dmgMul"));
    test.expectEq(heavyTail.bodyShare, combat_expect::ZONE_HEAVY_APPENDAGE_BODY_SHARE, F("heavy tail bodyShare"));
    test.expectEq(heavyTail.breakTypes, PHYS_SLASH, F("heavy tail break slash"));
    test.expectEq(heavyTail.unlockMask, static_cast<uint8_t>(1u << combat::ATTACK_HEAVY_TAIL_SPIN), F("heavy tail unlocks tail_spin"));

    // nch.1: heavy's bite + 4-window tail_spin, decoded from the real blob.
    const CombatAttackValue heavyBite = combatAttackRead(combat::ATTACK_HEAVY_BITE);
    test.expectEq(heavyBite.windup, combat_expect::ATTACK_HEAVY_BITE_WINDUP, F("heavy bite windup"));
    test.expectEq(heavyBite.active, combat_expect::ATTACK_HEAVY_BITE_ACTIVE, F("heavy bite active"));
    test.expectEq(heavyBite.dmg, combat_expect::ATTACK_HEAVY_BITE_DMG, F("heavy bite dmg"));
    test.expectEq(heavyBite.facing, COMBAT_FACING_TRACK, F("heavy bite tracks"));
    const CombatWindow biteWin = combatWindowRead(heavyBite.firstWindow);
    test.expectEq(biteWin.t0, 0, F("heavy bite t0"));
    test.expectEq(biteWin.t1, 8, F("heavy bite t1"));
    test.expectEq(biteWin.box.ox, 14, F("heavy bite ox"));
    test.expectEq(biteWin.box.w, 18, F("heavy bite w"));
    const CombatAttackValue heavySpin = combatAttackRead(combat::ATTACK_HEAVY_TAIL_SPIN);
    test.expectEq(heavySpin.windowCount, 4, F("heavy spin four windows"));
    test.expectEq(heavySpin.facing, COMBAT_FACING_LOCK_AWAY, F("heavy spin locks away"));
    test.expectEq(COMBAT_FACING_LOCK_AWAY, 2, F("lock-away facing value"));
    const CombatWindow spin0 = combatWindowRead(combat::WINDOW_HEAVY_TAIL_SPIN_0);
    const CombatWindow spin1 = combatWindowRead(combat::WINDOW_HEAVY_TAIL_SPIN_1);
    const CombatWindow spin2 = combatWindowRead(combat::WINDOW_HEAVY_TAIL_SPIN_2);
    const CombatWindow spin3 = combatWindowRead(combat::WINDOW_HEAVY_TAIL_SPIN_3);
    test.expectEq(spin0.t0, 0, F("spin0 t0"));
    test.expectEq(spin0.t1, 5, F("spin0 t1"));
    test.expectEq(static_cast<uint32_t>(spin0.box.ox), static_cast<uint32_t>(-20), F("spin0 behind"));
    test.expectEq(spin1.t0, 6, F("spin1 t0"));
    test.expectEq(static_cast<uint32_t>(spin1.box.oy), static_cast<uint32_t>(-22), F("spin1 north"));
    test.expectEq(spin2.t0, 11, F("spin2 t0"));
    test.expectEq(spin2.box.ox, 22, F("spin2 front"));
    test.expectEq(spin3.t0, 16, F("spin3 t0"));
    test.expectEq(spin3.box.oy, 22, F("spin3 south"));
    test.expectEq(heavy.firstAttack, combat::ATTACK_HEAVY_BITE, F("heavy first attack bite"));

    const CombatCreature lunge = combatCreatureRead(combat::CREATURE_LUNGE);
    test.expectEq(lunge.hp, combat_expect::CREATURE_LUNGE_HP, F("lunge hp"));
    test.expectEq(lunge.spd, combat_expect::CREATURE_LUNGE_SPD, F("lunge spd"));
    test.expectEq(lunge.w, combat_expect::CREATURE_LUNGE_W, F("lunge w"));
    test.expectEq(lunge.h, combat_expect::CREATURE_LUNGE_H, F("lunge h"));
    // 76y: chicken head + legs (appendage) zones and the legs-only collide box.
    test.expectEq(lunge.headZone, combat::ZONE_LUNGE_HEAD, F("lunge head zone"));
    test.expectEq(lunge.appendZone, combat::ZONE_LUNGE_APPENDAGE, F("lunge legs zone"));
    test.expectEq(lunge.collide.ox, 9, F("lunge legs collide ox"));
    test.expectEq(lunge.collide.oy, 11, F("lunge legs collide oy"));
    test.expectEq(lunge.collide.w, 12, F("lunge legs collide w"));
    test.expectEq(lunge.collide.h, 13, F("lunge legs collide h"));
    const CombatZone lungeHead = combatZoneRead(combat::ZONE_LUNGE_HEAD);
    test.expectEq(lungeHead.box.ox, 18, F("lunge head box ox"));
    test.expectEq(lungeHead.box.w, 11, F("lunge head box w"));
    test.expectEq(lungeHead.box.h, 7, F("lunge head box h"));
    test.expectEq(lungeHead.hp, combat_expect::ZONE_LUNGE_HEAD_HP, F("lunge head hp"));
    test.expectEq(lungeHead.dmgMul, combat_expect::ZONE_LUNGE_HEAD_DMG_MUL, F("lunge head dmgMul"));
    const CombatZone lungeLegs = combatZoneRead(combat::ZONE_LUNGE_APPENDAGE);
    test.expectEq(lungeLegs.box.ox, 9, F("lunge legs box ox"));
    test.expectEq(lungeLegs.box.h, 24, F("lunge legs box h"));
    test.expectEq(lungeLegs.hp, combat_expect::ZONE_LUNGE_APPENDAGE_HP, F("lunge legs hp"));
    test.expectEq(lungeLegs.dmgMul, combat_expect::ZONE_LUNGE_APPENDAGE_DMG_MUL, F("lunge legs dmgMul"));
    test.expectEq(lungeLegs.bodyShare, combat_expect::ZONE_LUNGE_APPENDAGE_BODY_SHARE, F("lunge legs bodyShare"));
    test.expectEq(lungeLegs.breakTypes, PHYS_SLASH, F("lunge legs break slash"));

    const CombatCreature sweep = combatCreatureRead(combat::CREATURE_SWEEP);
    test.expectEq(sweep.hp, combat_expect::CREATURE_SWEEP_HP, F("sweep hp"));
    test.expectEq(sweep.spd, combat_expect::CREATURE_SWEEP_SPD, F("sweep spd"));
    test.expectEq(sweep.w, combat_expect::CREATURE_SWEEP_W, F("sweep w"));
    test.expectEq(sweep.h, combat_expect::CREATURE_SWEEP_H, F("sweep h"));
    // nch.9: bull horns/hooves zones and the wide low hooves collide box.
    test.expectEq(sweep.headZone, combat::ZONE_SWEEP_HEAD, F("sweep head zone"));
    test.expectEq(sweep.appendZone, combat::ZONE_SWEEP_APPENDAGE, F("sweep hooves zone"));
    test.expectEq(sweep.collide.ox, combat_expect::CREATURE_SWEEP_COLLIDE_OX, F("sweep hooves collide ox"));
    test.expectEq(sweep.collide.oy, combat_expect::CREATURE_SWEEP_COLLIDE_OY, F("sweep hooves collide oy"));
    test.expectEq(sweep.collide.w, combat_expect::CREATURE_SWEEP_COLLIDE_W, F("sweep hooves collide w"));
    test.expectEq(sweep.collide.h, combat_expect::CREATURE_SWEEP_COLLIDE_H, F("sweep hooves collide h"));
    const CombatZone bullHead = combatZoneRead(combat::ZONE_SWEEP_HEAD);
    test.expectEq(bullHead.box.ox, 17, F("bull head ox"));
    test.expectEq(static_cast<uint32_t>(bullHead.box.oy), static_cast<uint32_t>(-4), F("bull head oy"));
    test.expectEq(bullHead.box.w, 12, F("bull head w"));
    test.expectEq(bullHead.box.h, 10, F("bull head h"));
    test.expectEq(bullHead.dmgMul, combat_expect::ZONE_SWEEP_HEAD_DMG_MUL, F("bull head dmgMul"));
    test.expectEq(bullHead.hp, combat_expect::ZONE_SWEEP_HEAD_HP, F("bull head hp"));
    const CombatZone bullHooves = combatZoneRead(combat::ZONE_SWEEP_APPENDAGE);
    test.expectEq(bullHooves.box.ox, 4, F("bull hooves ox"));
    test.expectEq(bullHooves.box.oy, 12, F("bull hooves oy"));
    test.expectEq(bullHooves.box.w, 20, F("bull hooves w"));
    test.expectEq(bullHooves.box.h, 10, F("bull hooves h"));
    test.expectEq(bullHooves.dmgMul, combat_expect::ZONE_SWEEP_APPENDAGE_DMG_MUL, F("bull hooves dmgMul"));
    test.expectEq(bullHooves.unlockMask, static_cast<uint8_t>(1u << combat::ATTACK_SWEEP_STOMP), F("bull hooves disable stomp"));

    // nch.9: the bull swaps lunge/sweep for stomp + a two-window gore.
    const CombatAttackValue bullStomp = combatAttackRead(combat::ATTACK_SWEEP_STOMP);
    test.expectEq(sweep.firstAttack, combat::ATTACK_SWEEP_STOMP, F("bull first attack stomp"));
    test.expectEq(bullStomp.windup, combat_expect::ATTACK_SWEEP_STOMP_WINDUP, F("bull stomp windup"));
    test.expectEq(bullStomp.active, combat_expect::ATTACK_SWEEP_STOMP_ACTIVE, F("bull stomp active"));
    test.expectEq(bullStomp.recover, combat_expect::ATTACK_SWEEP_STOMP_RECOVER, F("bull stomp recover"));
    test.expectEq(bullStomp.dmg, combat_expect::ATTACK_SWEEP_STOMP_DMG, F("bull stomp dmg"));
    test.expectEq(bullStomp.moveType, 0, F("bull stomp stationary"));
    test.expectEq(bullStomp.firstWindow, combat::WINDOW_SWEEP_STOMP_0, F("bull stomp window"));
    const CombatWindow stompWin = combatWindowRead(bullStomp.firstWindow);
    test.expectEq(stompWin.t0, 0, F("bull stomp t0"));
    test.expectEq(stompWin.t1, 10, F("bull stomp t1"));
    test.expectEq(stompWin.box.ox, 10, F("bull stomp ox"));
    test.expectEq(stompWin.box.oy, 2, F("bull stomp oy"));
    test.expectEq(stompWin.box.w, 24, F("bull stomp w"));
    test.expectEq(stompWin.box.h, 14, F("bull stomp h"));
    const CombatAttackValue bullGore = combatAttackRead(combat::ATTACK_SWEEP_GORE);
    test.expectEq(bullGore.windup, 46, F("bull gore windup"));
    test.expectEq(bullGore.active, 12, F("bull gore active"));
    test.expectEq(bullGore.recover, 55, F("bull gore recover"));
    test.expectEq(bullGore.dmg, 14, F("bull gore dmg"));
    test.expectEq(bullGore.moveType, 1, F("bull gore lunges"));
    test.expectEq(bullGore.moveSpeedF, 34, F("bull gore speedF"));
    test.expectEq(bullGore.facing, COMBAT_FACING_LOCK, F("bull gore locks at windup"));
    test.expectEq(bullGore.windowCount, 2, F("bull gore two windows"));
    const CombatWindow gore0 = combatWindowRead(combat::WINDOW_SWEEP_GORE_0);
    const CombatWindow gore1 = combatWindowRead(combat::WINDOW_SWEEP_GORE_1);
    test.expectEq(gore0.t0, 0, F("gore0 t0"));
    test.expectEq(gore0.t1, 6, F("gore0 t1"));
    test.expectEq(gore0.box.ox, 16, F("gore0 horns ox"));
    test.expectEq(static_cast<uint32_t>(gore0.box.oy), static_cast<uint32_t>(-2), F("gore0 horns oy"));
    test.expectEq(gore1.t0, 7, F("gore1 t0"));
    test.expectEq(gore1.t1, 12, F("gore1 t1"));
    test.expectEq(gore1.box.ox, 12, F("gore1 trample ox"));
    test.expectEq(gore1.box.oy, 2, F("gore1 trample oy"));
    test.expectEq(gore1.box.w, 20, F("gore1 trample w"));
    test.expectEq(gore1.box.h, 14, F("gore1 trample h"));

    // --------------------------------------------- cross-reference walk
    const CombatSkeleton sk = combatSkeletonRead(lunge.skeletonIdx);
    test.expectEq(lunge.skeletonIdx, combat::SKELETON_CHICKEN, F("lunge skeleton idx"));
    test.expectEq(sk.anchorCount, 2, F("lunge skeleton anchors"));

    const CombatAttackValue lungeAtk = combatAttackRead(lunge.firstAttack);
    test.expectEq(lunge.firstAttack, combat::ATTACK_LUNGE_PECK, F("lunge first attack"));
    test.expectEq(lungeAtk.windup, combat_expect::ATTACK_LUNGE_PECK_WINDUP, F("lunge windup"));
    test.expectEq(lungeAtk.active, combat_expect::ATTACK_LUNGE_PECK_ACTIVE, F("lunge active"));
    test.expectEq(lungeAtk.recover, combat_expect::ATTACK_LUNGE_PECK_RECOVER, F("lunge recover"));
    test.expectEq(lungeAtk.dmg, combat_expect::ATTACK_LUNGE_PECK_DMG, F("lunge dmg"));
    test.expectEq(lungeAtk.moveType, 1, F("lunge moveType"));
    test.expectEq(lungeAtk.moveSpeedF, 18, F("lunge speedF"));
    test.expectEq(lungeAtk.phys, PHYS_BLUNT, F("lunge phys"));
    test.expectEq(lungeAtk.windowCount, 1, F("lunge windows"));
    test.expectEq(lungeAtk.firstWindow, combat::WINDOW_LUNGE_PECK_0, F("lunge first window"));
    const CombatWindow win = combatWindowRead(lungeAtk.firstWindow);
    test.expectEq(win.t0, 0, F("lunge window t0"));
    test.expectEq(win.t1, 6, F("lunge window t1"));
    test.expectEq(win.box.ox, 14, F("lunge window ox"));
    test.expectEq(win.box.oy, -6, F("lunge window oy"));
    test.expectEq(win.box.w, 12, F("lunge window w"));
    test.expectEq(win.box.h, 10, F("lunge window h"));
    test.expectEq(win.dmgMul, 100, F("lunge window dmgMul"));

    const CombatPattern pat = combatPatternRead(lunge.firstPattern);
    test.expectEq(lunge.firstPattern, combat::PATTERN_LUNGE_P_PECK, F("lunge first pattern"));
    test.expectEq(pat.guardIdx, combat::GUARD_LUNGE_P_PECK, F("lunge guard idx"));
    const CombatGuard guard = combatGuardRead(pat.guardIdx);
    test.expectEq(guard.minDist, combat_expect::PATTERN_LUNGE_P_PECK_MIN_DIST, F("lunge minDist"));
    test.expectEq(guard.maxDist, combat_expect::PATTERN_LUNGE_P_PECK_MAX_DIST, F("lunge maxDist"));
    test.expectEq(guard.hpLo, 0, F("lunge hpLo"));
    test.expectEq(guard.hpHi, 100, F("lunge hpHi"));
    test.expectEq(guard.chance, combat_expect::PATTERN_LUNGE_P_PECK_CHANCE, F("lunge chance"));
    test.expectEq(guard.zonesBroken, 0, F("lunge no zone clause"));
    const CombatStep step = combatStepRead(pat.firstStep);
    test.expectEq(step.kind, STEP_ATK, F("lunge step kind"));
    test.expectEq(step.ref, combat::ATTACK_LUNGE_PECK, F("lunge step attack ref"));
    test.expectEq(step.after, 0, F("lunge step after"));

    // BULL (nch.9) opens with p_stomp (<=24) then p_gore (>=24).
    const CombatPattern sweepPat = combatPatternRead(sweep.firstPattern);
    test.expectEq(sweep.patternCount, 2, F("bull two patterns"));
    test.expectEq(sweepPat.guardIdx, combat::GUARD_SWEEP_P_STOMP, F("bull stomp guard idx"));
    const CombatGuard sweepGuard = combatGuardRead(sweepPat.guardIdx);
    test.expectEq(sweepGuard.minDist, combat_expect::PATTERN_SWEEP_P_STOMP_MIN_DIST, F("bull stomp minDist"));
    test.expectEq(sweepGuard.maxDist, combat_expect::PATTERN_SWEEP_P_STOMP_MAX_DIST, F("bull stomp maxDist"));
    test.expectEq(sweepGuard.chance, combat_expect::PATTERN_SWEEP_P_STOMP_CHANCE, F("bull stomp chance"));
    const CombatPattern sweepGorePat = combatPatternRead(static_cast<uint8_t>(sweep.firstPattern + 1));
    test.expectEq(sweepGorePat.guardIdx, combat::GUARD_SWEEP_P_GORE, F("bull gore guard idx"));
    const CombatGuard sweepGoreGuard = combatGuardRead(sweepGorePat.guardIdx);
    test.expectEq(sweepGoreGuard.minDist, 24, F("bull gore minDist"));
    test.expectEq(sweepGoreGuard.maxDist, 255, F("bull gore maxDist"));

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
    test.expectEq(g.combat.attack.facing, 0, F("cache facing"));
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
        test.expectEq(g.target.rect.w, g.combat.body.w, F("target rect w from box"));
        test.expectEq(g.target.rect.x, g.monster.x, F("target rect x at box origin"));
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
