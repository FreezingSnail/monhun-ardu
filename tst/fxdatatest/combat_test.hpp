#pragma once
// Ardens device loader test for src/core/combat.hpp (bead monhun-ardu-ljj.2).
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
// Shipped blob limits, called out honestly: STAGES/ELEMS/PREDICATES counts are
// 0, so stage transitions and predicate ops are exercised on the packed
// bitfield + value helpers, while every other path reads real records.
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

    static const uint16_t counts[14] = {
        combat::CREATURES_COUNT, combat::PROFILES_COUNT, combat::SKELETONS_COUNT, combat::PARTS_COUNT,    combat::STAGES_COUNT, combat::ANCHORS_COUNT,    combat::ELEMS_COUNT,
        combat::REFS_COUNT,      combat::ATTACKS_COUNT,  combat::WINDOWS_COUNT,   combat::PATTERNS_COUNT, combat::GUARDS_COUNT, combat::PREDICATES_COUNT, combat::STEPS_COUNT,
    };
    for (uint8_t i = 0; i < 14; i++) {
        FX::seekData(mhCombat + 4 + static_cast<uint24_t>(i) * 2);
        const uint8_t lo = FX::readPendingUInt8();
        const uint8_t hi = FX::readEnd();
        test.expectEqIdx(static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8)), counts[i], F("header count"), i);
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

    const CombatCreature lunge = combatCreatureRead(combat::CREATURE_LUNGE);
    test.expectEq(lunge.hp, combat_expect::CREATURE_LUNGE_HP, F("lunge hp"));
    test.expectEq(lunge.spd, combat_expect::CREATURE_LUNGE_SPD, F("lunge spd"));
    test.expectEq(lunge.w, combat_expect::CREATURE_LUNGE_W, F("lunge w"));
    test.expectEq(lunge.h, combat_expect::CREATURE_LUNGE_H, F("lunge h"));
    test.expectEq(lunge.attackCount, combat_expect::CREATURE_LUNGE_ATTACKS, F("lunge attacks"));
    test.expectEq(lunge.patternCount, combat_expect::CREATURE_LUNGE_PATTERNS, F("lunge patterns"));

    const CombatCreature sweep = combatCreatureRead(combat::CREATURE_SWEEP);
    test.expectEq(sweep.hp, combat_expect::CREATURE_SWEEP_HP, F("sweep hp"));
    test.expectEq(sweep.spd, combat_expect::CREATURE_SWEEP_SPD, F("sweep spd"));
    test.expectEq(sweep.w, combat_expect::CREATURE_SWEEP_W, F("sweep w"));
    test.expectEq(sweep.h, combat_expect::CREATURE_SWEEP_H, F("sweep h"));
    test.expectEq(sweep.attackCount, combat_expect::CREATURE_SWEEP_ATTACKS, F("sweep attacks"));
    test.expectEq(sweep.patternCount, combat_expect::CREATURE_SWEEP_PATTERNS, F("sweep patterns"));

    // --------------------------------------------- cross-reference walk
    const CombatSkeleton sk = combatSkeletonRead(lunge.skeletonIdx);
    test.expectEq(lunge.skeletonIdx, combat::SKELETON_QUAD_32X24, F("lunge skeleton idx"));
    test.expectEq(sk.partCount, 1, F("lunge skeleton parts"));
    test.expectEq(sk.firstPart, combat::PART_QUAD_32X24_BODY, F("lunge body part idx"));
    const CombatPart body = combatPartRead(sk.firstPart);
    test.expectEq(body.box.w, 32, F("body box w"));
    test.expectEq(body.box.h, 24, F("body box h"));
    test.expectEq(body.dmgMul, 100, F("body dmgMul"));
    test.expectEq(body.bodyShare, 100, F("body bodyShare"));
    test.expectEq(body.physBlunt, 100, F("body physBlunt"));

    const CombatAttackValue lungeAtk = combatAttackRead(lunge.firstAttack);
    test.expectEq(lunge.firstAttack, combat::ATTACK_LUNGE_LUNGE, F("lunge first attack"));
    test.expectEq(lungeAtk.windup, combat_expect::ATTACK_LUNGE_LUNGE_WINDUP, F("lunge windup"));
    test.expectEq(lungeAtk.active, combat_expect::ATTACK_LUNGE_LUNGE_ACTIVE, F("lunge active"));
    test.expectEq(lungeAtk.recover, combat_expect::ATTACK_LUNGE_LUNGE_RECOVER, F("lunge recover"));
    test.expectEq(lungeAtk.dmg, combat_expect::ATTACK_LUNGE_LUNGE_DMG, F("lunge dmg"));
    test.expectEq(lungeAtk.moveType, 1, F("lunge moveType"));
    test.expectEq(lungeAtk.moveSpeedF, 34, F("lunge speedF"));
    test.expectEq(lungeAtk.phys, PHYS_BLUNT, F("lunge phys"));
    test.expectEq(lungeAtk.windowCount, 1, F("lunge windows"));
    test.expectEq(lungeAtk.firstWindow, combat::WINDOW_LUNGE_LUNGE_0, F("lunge first window"));
    const CombatWindow win = combatWindowRead(lungeAtk.firstWindow);
    test.expectEq(win.t0, 0, F("lunge window t0"));
    test.expectEq(win.t1, 10, F("lunge window t1"));
    test.expectEq(win.box.ox, 12, F("lunge window ox"));
    test.expectEq(win.box.oy, 0, F("lunge window oy"));
    test.expectEq(win.box.w, 24, F("lunge window w"));
    test.expectEq(win.box.h, 22, F("lunge window h"));
    test.expectEq(win.dmgMul, 100, F("lunge window dmgMul"));

    const CombatPattern pat = combatPatternRead(lunge.firstPattern);
    test.expectEq(lunge.firstPattern, combat::PATTERN_LUNGE_P_LUNGE, F("lunge first pattern"));
    test.expectEq(pat.guardIdx, combat::GUARD_LUNGE_P_LUNGE, F("lunge guard idx"));
    const CombatGuard guard = combatGuardRead(pat.guardIdx);
    test.expectEq(guard.minDist, combat_expect::PATTERN_LUNGE_P_LUNGE_MIN_DIST, F("lunge minDist"));
    test.expectEq(guard.maxDist, combat_expect::PATTERN_LUNGE_P_LUNGE_MAX_DIST, F("lunge maxDist"));
    test.expectEq(guard.hpLo, 0, F("lunge hpLo"));
    test.expectEq(guard.hpHi, 100, F("lunge hpHi"));
    test.expectEq(guard.chance, combat_expect::PATTERN_LUNGE_P_LUNGE_CHANCE, F("lunge chance"));
    const CombatStep step = combatStepRead(pat.firstStep);
    test.expectEq(step.kind, STEP_ATK, F("lunge step kind"));
    test.expectEq(step.ref, combat::ATTACK_LUNGE_LUNGE, F("lunge step attack ref"));
    test.expectEq(step.after, 0, F("lunge step after"));
    test.expectEq(step.chance, 100, F("lunge step chance"));

    // SWEEP has no lunge pattern: one match-all sweep pattern.
    const CombatPattern sweepPat = combatPatternRead(sweep.firstPattern);
    test.expectEq(sweep.patternCount, 1, F("sweep single pattern"));
    test.expectEq(sweepPat.guardIdx, combat::GUARD_SWEEP_P_SWEEP, F("sweep guard idx"));
    const CombatGuard sweepGuard = combatGuardRead(sweepPat.guardIdx);
    test.expectEq(sweepGuard.minDist, combat_expect::PATTERN_SWEEP_P_SWEEP_MIN_DIST, F("sweep minDist"));
    test.expectEq(sweepGuard.maxDist, combat_expect::PATTERN_SWEEP_P_SWEEP_MAX_DIST, F("sweep maxDist"));
    test.expectEq(sweepGuard.chance, combat_expect::PATTERN_SWEEP_P_SWEEP_CHANCE, F("sweep chance"));

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
    test.expectEq(g.combat.profile.keepDist, 24, F("cache keepDist"));
    test.expectEq(g.combat.profile.attackDist, 42, F("cache attackDist"));
    test.expectEq(g.combat.profile.circleNum, 8, F("cache circleNum"));
    test.expectEq(g.combat.profile.circleDen, 10, F("cache circleDen"));
    test.expectEq(g.combat.profile.cdBase, 55, F("cache cdBase"));
    test.expectEq(g.combat.profile.cdJitter, 40, F("cache cdJitter"));
    test.expectEq(g.combat.profile.spawnT, 90, F("cache spawnT"));
    test.expectEq(g.combat.profile.spawnCd, 140, F("cache spawnCd"));
    test.expectEq(g.combat.profile.stunRecoverT, 24, F("cache stunRecoverT"));
    test.expectEq(g.combat.stages, 0, F("stages reset"));
    test.expectEq(g.combat.stepT, 0, F("step timer reset"));

    before = mhFxReadCount;
    const uint8_t atk = attackLoad(g, combat::ATTACK_LUNGE_LUNGE);
    const uint16_t attackReads = static_cast<uint16_t>(mhFxReadCount - before);
    test.expectEq(atk, combat::ATTACK_LUNGE_LUNGE, F("attackLoad returns idx"));
    test.expectEq(attackReads <= 24, 1, F("attack load <= 24 reads"));
    test.expectEq(g.combat.attack.windup, 40, F("cache windup"));
    test.expectEq(g.combat.attack.active, 10, F("cache active"));
    test.expectEq(g.combat.attack.recover, 55, F("cache recover"));
    test.expectEq(g.combat.attack.dmg, 12, F("cache dmg"));
    test.expectEq(g.combat.attack.moveType, 1, F("cache moveType"));
    test.expectEq(g.combat.attack.moveSpeedF, 34, F("cache moveSpeedF"));
    test.expectEq(g.combat.attack.facing, 0, F("cache facing"));
    test.expectEq(g.combat.attack.winIdx, combat::WINDOW_LUNGE_LUNGE_0, F("cache winIdx"));
    test.expectEq(g.combat.attack.win.t1, 10, F("cache win t1"));
    test.expectEq(g.combat.attack.win.box.ox, 12, F("cache win ox"));
    test.expectEq(g.combat.attack.win.box.w, 24, F("cache win w"));
    test.expectEq(g.combat.attack.win.dmgMul, 100, F("cache win dmgMul"));

    attackWindowLoad(g, combat::WINDOW_LUNGE_SWEEP_0);
    test.expectEq(g.combat.attack.winIdx, combat::WINDOW_LUNGE_SWEEP_0, F("window switch idx"));
    test.expectEq(g.combat.attack.win.t1, 12, F("window switch t1"));
    test.expectEq(g.combat.attack.win.box.ox, 17, F("window switch ox"));
    test.expectEq(g.combat.attack.win.box.w, 32, F("window switch w"));

    // ---------------------------------------------------- guard evaluation
    before = mhFxReadCount;
    CombatGuardInput in = {33, 100, 0, 0, 0xFFFF, 0};
    const bool lungeAt33 = combatGuardPasses(g, combat::PATTERN_LUNGE_P_LUNGE, in);
    const uint16_t guardReads = static_cast<uint16_t>(mhFxReadCount - before);
    test.expectEq(lungeAt33, 1, F("lunge guard dist 33 accepted"));
    test.expectEq(guardReads <= 12, 1, F("guard decision <= 12 reads"));

    in.dist = 32;
    test.expectEq(combatGuardPasses(g, combat::PATTERN_LUNGE_P_LUNGE, in), 0, F("lunge guard dist 32 rejected"));
    test.expectEq(combatGuardPasses(g, combat::PATTERN_LUNGE_P_SWEEP, in), 1, F("sweep guard dist 32 accepted"));
    in.dist = 255;
    test.expectEq(combatGuardPasses(g, combat::PATTERN_LUNGE_P_LUNGE, in), 1, F("lunge guard dist 255 accepted"));
    in.dist = 0;
    test.expectEq(combatGuardPasses(g, combat::PATTERN_LUNGE_P_SWEEP, in), 1, F("sweep guard dist 0 accepted"));
    test.expectEq(combatGuardPasses(g, 99, in), 0, F("unknown pattern rejected"));

    creatureLoad(g, combat::CREATURE_HEAVY);
    in.dist = 24;
    test.expectEq(combatGuardPasses(g, combat::PATTERN_HEAVY_P_LUNGE, in), 0, F("heavy guard dist 24 rejected"));
    in.dist = 25;
    test.expectEq(combatGuardPasses(g, combat::PATTERN_HEAVY_P_LUNGE, in), 1, F("heavy guard dist 25 accepted"));

    // Deterministic chance (pinned roll vectors, same function host-tested).
    test.expectEq(combatChanceRoll(0, 1, 2, 0), 9, F("roll tick0"));
    test.expectEq(combatChanceRoll(10, 1, 3, 0), 28, F("roll tick10"));
    test.expectEq(combatChancePasses(0, 1, 2, 0, 10), 1, F("roll 9 < 10 passes"));
    test.expectEq(combatChancePasses(0, 1, 2, 0, 9), 0, F("roll 9 < 9 fails"));
    test.expectEq(combatChancePasses(0, 1, 2, 0, 100), 1, F("chance 100 passes"));
    test.expectEq(combatChancePasses(0, 1, 2, 0, 0), 0, F("chance 0 fails"));

    // --------------------------------------------------- damage routing
    creatureLoad(g, combat::CREATURE_LUNGE);
    uint8_t hits[3] = {static_cast<uint8_t>(combat::PART_QUAD_32X24_BODY), 0, 0};
    before = mhFxReadCount;
    CombatHitResult r = combatResolveHit(g, 12, PHYS_BLUNT, ELEM_NONE, 100, 0, hits, 1);
    const uint16_t hitReads = static_cast<uint16_t>(mhFxReadCount - before);
    test.expectEq(r.partIdx, combat::PART_QUAD_32X24_BODY, F("hit part"));
    test.expectEq(r.mul, 100, F("hit multiplier"));
    test.expectEq(r.partDmg, 12, F("hit part damage"));
    test.expectEq(r.bodyDmg, 12, F("hit body damage"));
    test.expectEq(r.stagger, 0, F("hit stagger"));
    test.expectEq(hitReads <= 12, 1, F("landed hit <= 12 reads"));

    r = combatResolveHit(g, 12, PHYS_BLUNT, ELEM_NONE, 50, 0, hits, 1);
    test.expectEq(r.partDmg, 6, F("window 50 halves damage"));
    r = combatResolveHit(g, 12, PHYS_BLUNT, ELEM_NONE, 100, 11, hits, 1);
    test.expectEq(r.stagger, 11, F("stagger scales by multiplier"));
    hits[0] = 2;
    hits[1] = 1;
    hits[2] = 0;
    r = combatResolveHit(g, 12, PHYS_BLUNT, ELEM_NONE, 100, 0, hits, 3);
    test.expectEq(r.partIdx, 0, F("tie-break lowest part id"));
    r = combatResolveHit(g, 12, PHYS_BLUNT, ELEM_NONE, 100, 0, hits, 0);
    test.expectEq(r.partIdx, COMBAT_NO_PART, F("no candidates -> no part"));

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
        test.expectEq(bb.h, combat_expect::CREATURE_SWEEP_H, F("sweep box h"));
        combatCreatureBodyBox(combat::CREATURE_HEAVY, bb);
        test.expectEq(bb.w, combat_expect::CREATURE_HEAVY_W, F("heavy box w"));
        test.expectEq(bb.h, combat_expect::CREATURE_HEAVY_H, F("heavy box h"));

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
        test.expectEq(g.combat.bodyFirst, combat::PART_QUAD_40X28_BODY, F("cached hurtbox head"));
        test.expectEq(g.combat.bodyCount, 1, F("cached hurtbox count"));
        test.expectEq(g.target.rect.w, g.combat.body.w, F("target rect w from box"));
        test.expectEq(g.target.rect.x, g.monster.x, F("target rect x at box origin"));
        test.expectEq(g.combat.stages, 0, F("spawn stages intact"));
        // Migration C: the spawn burst also caches the whole profile record
        // (loader model: creature record + profile + skeleton body box <= 40).
        test.expectEq(initReads <= 40, 1, F("initMonster burst <= 40 reads"));

        // Landed player hit resolves the creature's hurtbox list (one body
        // part on the shipped 3; multipliers all 100 -> damage unchanged).
        creatureLoad(g, combat::CREATURE_LUNGE);
        before = mhFxReadCount;
        const CombatBodyHit bodyHit = combatResolveBodyHit(g, 12);
        const uint16_t bodyHitReads = static_cast<uint16_t>(mhFxReadCount - before);
        test.expectEq(bodyHit.partIdx, combat::PART_QUAD_32X24_BODY, F("body hit part"));
        test.expectEq(bodyHit.dmg, 12, F("body hit damage unchanged"));
        test.expectEq(bodyHit.mul, 100, F("body hit multiplier neutral"));

        test.expectEq(bodyHitReads <= 16, 1, F("body hit <= 16 reads"));
    }

    // ------------------------------------------- break-stage transition
    test.expectEq(combatStageCross(0, 30, 30), 1, F("stage cross at threshold"));
    test.expectEq(combatStageCross(0, 31, 30), 0, F("stage holds above threshold"));
    test.expectEq(combatStageCross(2, 0, 0), 3, F("stage cross second threshold"));
    test.expectEq(combatPartStageGet(g, 0), 0, F("part stage intact"));
    test.expectEq(combatPartStageCount(combat::PART_QUAD_32X24_BODY), 0, F("shipped part has no stages"));
    test.expectEq(combatPartStageForHp(combat::PART_QUAD_32X24_BODY, 0, 100), 0, F("no stages -> stage 0"));
    combatPartStageSet(g, 0, 1);
    test.expectEq(combatPartStageGet(g, 0), 1, F("part stage 1"));
    combatPartStageSet(g, 3, 3);
    test.expectEq(combatPartStageGet(g, 3), 3, F("part 3 stage 3"));
    test.expectEq(combatPartStageGet(g, 0), 1, F("stages independent"));
    combatPartStageSet(g, 0, 9);
    test.expectEq(combatPartStageGet(g, 0), COMBAT_STAGE_MAX, F("stage saturates"));
    test.expectEq(g.combat.stages, static_cast<uint16_t>(3u | (3u << 6)), F("packed stage bits"));
    test.expectEq(combatPartDmgMulNow(g, combat::PART_QUAD_32X24_BODY), 100, F("effective dmgMul stays neutral"));
    test.expectEq(combatAttackDisabled(g, combat::ATTACK_LUNGE_LUNGE), 0, F("no stage disables lunge"));

    // --------------------------------------------------- bad-id fallback
    const uint8_t fallback = creatureLoad(g, 99);
    test.expectEq(fallback, 0, F("bad creature id -> creature 0"));
    test.expectEq(g.combat.creature, combat::CREATURE_HEAVY, F("fallback creature cache"));
    test.expectEq(g.combat.profile.cdBase, 55, F("fallback profile cdBase"));
    test.expectEq(g.combat.stages, 0, F("fallback resets stages"));
    const uint8_t badAtk = attackLoad(g, 200);
    test.expectEq(badAtk, 0, F("bad attack id -> attack 0"));
    test.expectEq(g.combat.attack.winIdx, combat::WINDOW_HEAVY_LUNGE_0, F("fallback window"));

    // --------------------------------- sim attack path consumes the cache
    // (migration A / ljj.3): attack start loads the identity + first window
    // once; the windup and active ticks then perform zero cart reads.
    initGame(g, W_SWORD);
    initMonster(g, MON_LUNGE);
    before = mhFxReadCount;
    const uint8_t simAtk = monsterAttackSet(g, combat::ATTACK_LUNGE_LUNGE);
    const uint16_t simAtkReads = static_cast<uint16_t>(mhFxReadCount - before);
    test.expectEq(simAtk, combat::ATTACK_LUNGE_LUNGE, F("sim attack set"));
    test.expectEq(simAtkReads <= 24, 1, F("sim attack start <= 24 reads"));
    test.expectEq(g.monster.atkIdx, combat::ATTACK_LUNGE_LUNGE, F("sim attack identity"));
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
    attackWindowLoad(g, combat::WINDOW_LUNGE_SWEEP_0);
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
