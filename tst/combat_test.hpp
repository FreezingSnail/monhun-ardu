#pragma once
// Host unit tests for src/core/combat.hpp (bead monhun-ardu-ljj.2).
//
// Permanent, co-located suite. Covers the host read path (generated
// combat_data.hpp) against the loader, cache lifecycle, guard evaluation
// (inclusive ranges, predicates, player flags, cooldown, deterministic chance),
// damage/stagger routing reference vectors, part stages + break-effect
// projections, and the creature fallback.
//
// The shipped blob has no stage/elem/predicate records yet (STAGES/ELEMS/
// PREDICATES counts are 0), so the stage/predicate *value* helpers are pinned
// with synthetic records here; the data plumbing is pinned by the pack-parity
// suite and the Ardens loader test.
#include "test.hpp"
#include "../src/core/combat.hpp"

using namespace mh;

void CombatSuite(TestRunner &runner) {
    TestSuite suite("Combat loader (src/core/combat.hpp): host structs, caches, guards, routing");

    {
        Test t("creature records match combat_data.hpp");
        for (uint8_t i = 0; i < combat::CREATURES_COUNT; i++) {
            const CombatCreature c = combatCreatureRead(i);
            const combat_data::Creature &h = combat_data::CREATURES[i];
            t.assert(c.skeletonIdx, h.skeletonIdx, "creature skeletonIdx");
            t.assert(c.profileIdx, h.profileIdx, "creature profileIdx");
            t.assert(c.firstPart, h.firstPart, "creature firstPart");
            t.assert(c.partCount, h.partCount, "creature partCount");
            t.assert(c.firstAttack, h.firstAttack, "creature firstAttack");
            t.assert(c.attackCount, h.attackCount, "creature attackCount");
            t.assert(c.firstPattern, h.firstPattern, "creature firstPattern");
            t.assert(c.patternCount, h.patternCount, "creature patternCount");
            t.assert(c.w, h.w, "creature w");
            t.assert(c.h, h.h, "creature h");
            t.assert(c.spd, h.spd, "creature spd");
            t.assert(c.hp, h.hp, "creature hp");
            t.assert(c.spawnX, h.spawnX, "creature spawnX");
            t.assert(c.spawnY, h.spawnY, "creature spawnY");
            t.assert(combatCreatureFirstAttack(i), h.firstAttack, "creature firstAttack accessor");
        }
        // Migration A scaffold: slot 0 of every shipped creature is its lunge.
        t.assert(combatCreatureFirstAttack(combat_data::CREATURE_LUNGE), combat_data::ATTACK_LUNGE_LUNGE, "lunge creature first attack");
        t.assert(combatCreatureFirstAttack(combat_data::CREATURE_SWEEP), combat_data::ATTACK_SWEEP_LUNGE, "sweep creature first attack");
        t.assert(combatCreatureFirstAttack(combat_data::CREATURE_HEAVY), combat_data::ATTACK_HEAVY_LUNGE, "heavy creature first attack");
        suite.addTest(t);
    }

    {
        Test t("profile records match combat_data.hpp");
        for (uint8_t i = 0; i < combat::PROFILES_COUNT; i++) {
            const CombatProfile p = combatProfileRead(i);
            const combat_data::Profile &h = combat_data::PROFILES[i];
            t.assert(p.engageDist, h.engageDist, "profile engageDist");
            t.assert(p.keepDist, h.keepDist, "profile keepDist");
            t.assert(p.attackDist, h.attackDist, "profile attackDist");
            t.assert(p.circleNum, h.circleNum, "profile circleNum");
            t.assert(p.circleDen, h.circleDen, "profile circleDen");
            t.assert(p.retreatNum, h.retreatNum, "profile retreatNum");
            t.assert(p.retreatDen, h.retreatDen, "profile retreatDen");
            t.assert(p.staggerMax, h.staggerMax, "profile staggerMax");
            t.assert(p.staggerDecay, h.staggerDecay, "profile staggerDecay");
            t.assert(p.partCount, h.partCount, "profile partCount");
            t.assert(p.cdBase, h.cdBase, "profile cdBase");
            t.assert(p.cdJitter, h.cdJitter, "profile cdJitter");
            t.assert(p.spawnT, h.spawnT, "profile spawnT");
            t.assert(p.spawnCd, h.spawnCd, "profile spawnCd");
            t.assert(p.stunRecoverT, h.stunRecoverT, "profile stunRecoverT");
            t.assert(p.staggerRecoverT, h.staggerRecoverT, "profile staggerRecoverT");
        }
        suite.addTest(t);
    }

    {
        Test t("skeleton + part records match combat_data.hpp");
        for (uint8_t i = 0; i < combat::SKELETONS_COUNT; i++) {
            const CombatSkeleton s = combatSkeletonRead(i);
            const combat_data::Skeleton &h = combat_data::SKELETONS[i];
            t.assert(s.firstPart, h.firstPart, "skeleton firstPart");
            t.assert(s.partCount, h.partCount, "skeleton partCount");
            t.assert(s.firstAnchor, h.firstAnchor, "skeleton firstAnchor");
            t.assert(s.anchorCount, h.anchorCount, "skeleton anchorCount");
        }
        for (uint8_t i = 0; i < combat::PARTS_COUNT; i++) {
            const CombatPart p = combatPartRead(i);
            const combat_data::Part &h = combat_data::PARTS[i];
            t.assert(p.box.ox, h.box.ox, "part box.ox");
            t.assert(p.box.oy, h.box.oy, "part box.oy");
            t.assert(p.box.w, h.box.w, "part box.w");
            t.assert(p.box.h, h.box.h, "part box.h");
            t.assert(p.dmgMul, h.dmgMul, "part dmgMul");
            t.assert(p.bodyShare, h.bodyShare, "part bodyShare");
            t.assert(p.breakTypes, h.breakTypes, "part breakTypes");
            t.assert(p.hurtOn, h.hurtOn, "part hurtOn");
            t.assert(p.physSlash, h.physSlash, "part physSlash");
            t.assert(p.physBlunt, h.physBlunt, "part physBlunt");
            t.assert(p.physShot, h.physShot, "part physShot");
            t.assert(p.firstStage, h.firstStage, "part firstStage");
            t.assert(p.stageCount, h.stageCount, "part stageCount");
            t.assert(p.firstElem, h.firstElem, "part firstElem");
            t.assert(p.elemCount, h.elemCount, "part elemCount");
            t.assert(p.hp, h.hp, "part hp");
            t.assert(p.flags, h.flags, "part flags");
            t.assert(combatPartDmgMul(i), h.dmgMul, "part dmgMul accessor");
            t.assert(combatPartBodyShare(i), h.bodyShare, "part bodyShare accessor");
            t.assert(combatPartHurtOn(i), h.hurtOn, "part hurtOn accessor");
            t.assert(combatPartPhysSlash(i), h.physSlash, "part physSlash accessor");
            t.assert(combatPartPhysBlunt(i), h.physBlunt, "part physBlunt accessor");
            t.assert(combatPartPhysShot(i), h.physShot, "part physShot accessor");
            t.assert(combatPartFirstStage(i), h.firstStage, "part firstStage accessor");
            t.assert(combatPartStageCount(i), h.stageCount, "part stageCount accessor");
            t.assert(combatPartFirstElem(i), h.firstElem, "part firstElem accessor");
            t.assert(combatPartElemCount(i), h.elemCount, "part elemCount accessor");
            t.assert(combatPartHp(i), h.hp, "part hp accessor");
        }
        suite.addTest(t);
    }

    {
        Test t("attack + window records match combat_data.hpp");
        for (uint8_t i = 0; i < combat::ATTACKS_COUNT; i++) {
            const CombatAttackValue a = combatAttackRead(i);
            const combat_data::Attack &h = combat_data::ATTACKS[i];
            t.assert(a.moveType, h.moveType, "attack moveType");
            t.assert(combatAttackMoveType(i), h.moveType, "attack moveType accessor");
            t.assert(a.moveSpeedF, h.moveSpeedF, "attack moveSpeedF");
            t.assert(a.moveDx, h.moveDx, "attack moveDx");
            t.assert(a.moveDy, h.moveDy, "attack moveDy");
            t.assert(a.facing, h.facing, "attack facing");
            t.assert(a.phys, h.phys, "attack phys");
            t.assert(a.elem, h.elem, "attack elem");
            t.assert(a.onHitEffect, h.onHitEffect, "attack onHitEffect");
            t.assert(a.onHitPush, h.onHitPush, "attack onHitPush");
            t.assert(a.onHitStun, h.onHitStun, "attack onHitStun");
            t.assert(a.stagger, h.stagger, "attack stagger");
            t.assert(a.cue, h.cue, "attack cue");
            t.assert(a.firstWindow, h.firstWindow, "attack firstWindow");
            t.assert(a.windowCount, h.windowCount, "attack windowCount");
            t.assert(a.windup, h.windup, "attack windup");
            t.assert(a.active, h.active, "attack active");
            t.assert(a.recover, h.recover, "attack recover");
            t.assert(a.dmg, h.dmg, "attack dmg");
            t.assert(combatAttackWindup(i), h.windup, "attack windup accessor");
            t.assert(combatAttackFirstWindow(i), h.firstWindow, "attack firstWindow accessor");
            t.assert(combatAttackWindowCount(i), h.windowCount, "attack windowCount accessor");
        }
        for (uint8_t i = 0; i < combat::WINDOWS_COUNT; i++) {
            const CombatWindow w = combatWindowRead(i);
            const combat_data::Window &h = combat_data::WINDOWS[i];
            t.assert(w.t0, h.t0, "window t0");
            t.assert(w.t1, h.t1, "window t1");
            t.assert(w.box.ox, h.box.ox, "window box.ox");
            t.assert(w.box.oy, h.box.oy, "window box.oy");
            t.assert(w.box.w, h.box.w, "window box.w");
            t.assert(w.box.h, h.box.h, "window box.h");
            t.assert(w.dmgMul, h.dmgMul, "window dmgMul");
        }
        suite.addTest(t);
    }

    {
        Test t("pattern + guard + step records match combat_data.hpp");
        for (uint8_t i = 0; i < combat::PATTERNS_COUNT; i++) {
            const CombatPattern p = combatPatternRead(i);
            const combat_data::Pattern &h = combat_data::PATTERNS[i];
            t.assert(p.firstStep, h.firstStep, "pattern firstStep");
            t.assert(p.stepCount, h.stepCount, "pattern stepCount");
            t.assert(p.guardIdx, h.guardIdx, "pattern guardIdx");
            t.assert(combatPatternGuardIdx(i), h.guardIdx, "pattern guardIdx accessor");
            t.assert(combatPatternFirstStep(i), h.firstStep, "pattern firstStep accessor");
            t.assert(combatPatternStepCount(i), h.stepCount, "pattern stepCount accessor");
        }
        for (uint8_t i = 0; i < combat::GUARDS_COUNT; i++) {
            const CombatGuard g = combatGuardRead(i);
            const combat_data::Guard &h = combat_data::GUARDS[i];
            t.assert(g.minDist, h.minDist, "guard minDist");
            t.assert(g.maxDist, h.maxDist, "guard maxDist");
            t.assert(g.hpLo, h.hpLo, "guard hpLo");
            t.assert(g.hpHi, h.hpHi, "guard hpHi");
            t.assert(g.playerFlags, h.playerFlags, "guard playerFlags");
            t.assert(g.cooldown, h.cooldown, "guard cooldown");
            t.assert(g.chance, h.chance, "guard chance");
            t.assert(g.firstPartPred, h.firstPartPred, "guard firstPartPred");
            t.assert(g.partPredCount, h.partPredCount, "guard partPredCount");
        }
        for (uint8_t i = 0; i < combat::STEPS_COUNT; i++) {
            const CombatStep s = combatStepRead(i);
            const combat_data::Step &h = combat_data::STEPS[i];
            t.assert(s.kind, h.kind, "step kind");
            t.assert(s.ref, h.ref, "step ref");
            t.assert(s.after, h.after, "step after");
            t.assert(s.chance, h.chance, "step chance");
        }
        for (uint8_t i = 0; i < combat::ANCHORS_COUNT; i++) {
            const CombatAnchor a = combatAnchorRead(i);
            t.assert(a.ox, combat_data::ANCHORS[i].ox, "anchor ox");
            t.assert(a.oy, combat_data::ANCHORS[i].oy, "anchor oy");
        }
        suite.addTest(t);
    }

    {
        Test t("creatureLoad caches profile, resets stages, falls back on bad id");
        Game g;
        const uint8_t idx = creatureLoad(g, combat_data::CREATURE_LUNGE);
        const combat_data::Profile &hp = combat_data::PROFILES[combat_data::CREATURES[combat_data::CREATURE_LUNGE].profileIdx];
        t.assert(idx, combat_data::CREATURE_LUNGE, "load returns id");
        t.assert(g.combat.creature, combat_data::CREATURE_LUNGE, "cache creature");
        t.assert(g.combat.profile.engageDist, hp.engageDist, "cache engageDist");
        t.assert(g.combat.profile.attackDist, hp.attackDist, "cache attackDist");
        t.assert(g.combat.profile.cdBase, hp.cdBase, "cache cdBase");
        t.assert(g.combat.profile.cdJitter, hp.cdJitter, "cache cdJitter");
        t.assert(g.combat.profile.spawnCd, hp.spawnCd, "cache spawnCd");
        t.assert(g.combat.profile.partCount, hp.partCount, "cache partCount");
        t.assert(g.combat.stages, 0, "stages start intact");
        t.assert(g.combat.patternIdx, 0, "pattern cursor reset");
        t.assert(g.combat.stepIdx, 0, "step cursor reset");
        t.assert(g.combat.stepT, 0, "step timer reset");
        t.assert(g.combat.stagger, 0, "stagger reset");
        t.assert(g.combat.attack.windup, 0, "attack cache cleared");

        combatPartStageSet(g, 0, 2);
        t.assert(combatPartStageGet(g, 0), 2, "stage set before reload");
        const uint8_t fallback = creatureLoad(g, 99);
        const combat_data::Profile &hh = combat_data::PROFILES[combat_data::CREATURES[combat_data::CREATURE_HEAVY].profileIdx];
        t.assert(fallback, 0, "bad id falls back to creature 0");
        t.assert(g.combat.creature, 0, "fallback cache creature");
        t.assert(g.combat.profile.attackDist, hh.attackDist, "fallback profile");
        t.assert(g.combat.profile.partCount, hh.partCount, "fallback partCount");
        t.assert(combatPartStageGet(g, 0), 0, "reload resets stages");
        suite.addTest(t);
    }

    {
        Test t("attackLoad + attackWindowLoad cache lifecycle");
        Game g;
        creatureLoad(g, combat_data::CREATURE_LUNGE);
        const uint8_t atk = attackLoad(g, combat_data::ATTACK_LUNGE_LUNGE);
        t.assert(atk, combat_data::ATTACK_LUNGE_LUNGE, "load returns attack idx");
        t.assert(g.combat.attack.windup, 40, "cache windup");
        t.assert(g.combat.attack.active, 10, "cache active");
        t.assert(g.combat.attack.recover, 55, "cache recover");
        t.assert(g.combat.attack.dmg, 12, "cache dmg");
        t.assert(g.combat.attack.moveType, 1, "cache moveType lunge");
        t.assert(g.combat.attack.moveSpeedF, 34, "cache moveSpeedF");
        t.assert(g.combat.attack.facing, 0, "cache facing track");
        t.assert(g.combat.attack.winIdx, combat_data::WINDOW_LUNGE_LUNGE_0, "cache winIdx");
        t.assert(g.combat.attack.win.t0, 0, "cache window t0");
        t.assert(g.combat.attack.win.t1, 10, "cache window t1");
        t.assert(g.combat.attack.win.box.ox, 12, "cache window ox");
        t.assert(g.combat.attack.win.box.oy, 0, "cache window oy");
        t.assert(g.combat.attack.win.box.w, 24, "cache window w");
        t.assert(g.combat.attack.win.box.h, 22, "cache window h");
        t.assert(g.combat.attack.win.dmgMul, 100, "cache window dmgMul");

        attackWindowLoad(g, combat_data::WINDOW_LUNGE_SWEEP_0);
        t.assert(g.combat.attack.winIdx, combat_data::WINDOW_LUNGE_SWEEP_0, "window switch idx");
        t.assert(g.combat.attack.win.t1, 12, "window switch t1");
        t.assert(g.combat.attack.win.box.ox, 17, "window switch ox");
        t.assert(g.combat.attack.win.box.w, 32, "window switch w");
        t.assert(g.combat.attack.win.box.h, 24, "window switch h");

        // Attack scalars stay cached across a window switch (active phase).
        t.assert(g.combat.attack.dmg, 12, "scalars survive window switch");

        const uint8_t bad = attackLoad(g, 200);
        t.assert(bad, 0, "bad attack id falls back to 0");
        t.assert(g.combat.attack.winIdx, combat_data::WINDOW_HEAVY_LUNGE_0, "fallback window idx");
        suite.addTest(t);
    }

    {
        Test t("pattern cursor + combatTick countdown (no reads)");
        Game g;
        creatureLoad(g, combat_data::CREATURE_HEAVY);
        patternStateSet(g, combat_data::PATTERN_HEAVY_P_LUNGE, 1, 3);
        t.assert(g.combat.patternIdx, combat_data::PATTERN_HEAVY_P_LUNGE, "pattern idx set");
        t.assert(g.combat.stepIdx, 1, "step idx set");
        t.assert(g.combat.stepT, 3, "step timer set");
        combatTick(g);
        t.assert(g.combat.stepT, 2, "tick decrements");
        combatTick(g);
        combatTick(g);
        t.assert(g.combat.stepT, 0, "tick reaches 0");
        for (int i = 0; i < 10; i++)
            combatTick(g);
        t.assert(g.combat.stepT, 0, "tick floors at 0");
        t.assert(g.combat.patternIdx, combat_data::PATTERN_HEAVY_P_LUNGE, "cursor untouched");
        suite.addTest(t);
    }

    {
        Test t("guard eval: inclusive distance boundaries");
        Game g;
        creatureLoad(g, combat_data::CREATURE_LUNGE);
        CombatGuardInput in = {0, 100, 0, 0, 0xFFFF, 0};

        in.dist = 32;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_LUNGE, in), 0, "lunge dist 32 rejected");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_SWEEP, in), 1, "sweep dist 32 accepted");
        in.dist = 33;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_LUNGE, in), 1, "lunge dist 33 accepted");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_SWEEP, in), 0, "sweep dist 33 rejected");
        in.dist = 255;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_LUNGE, in), 1, "lunge dist 255 accepted");
        in.dist = 0;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_LUNGE, in), 0, "lunge dist 0 rejected");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_SWEEP, in), 1, "sweep dist 0 accepted");

        creatureLoad(g, combat_data::CREATURE_HEAVY);
        in.dist = 24;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_LUNGE, in), 0, "heavy swap dist 24 rejected");
        in.dist = 25;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_LUNGE, in), 1, "heavy swap dist 25 accepted");

        creatureLoad(g, combat_data::CREATURE_SWEEP);
        in.dist = 0;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_SWEEP, in), 1, "sweep always matches 0");
        in.dist = 255;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_SWEEP, in), 1, "sweep always matches 255");
        t.assert(combatGuardPasses(g, 99, in), 0, "unknown pattern rejected");
        suite.addTest(t);
    }

    {
        Test t("guard clauses: inclusive integer pure helpers");
        t.assert(combatGuardDistOk(33, 255, 33), 1, "min inclusive");
        t.assert(combatGuardDistOk(33, 255, 32), 0, "below min");
        t.assert(combatGuardDistOk(0, 32, 32), 1, "max inclusive");
        t.assert(combatGuardDistOk(0, 32, 33), 0, "above max");
        t.assert(combatGuardDistOk(10, 10, 10), 1, "degenerate range");
        t.assert(combatGuardHpOk(0, 100, 0), 1, "hp lo inclusive");
        t.assert(combatGuardHpOk(0, 100, 100), 1, "hp hi inclusive");
        t.assert(combatGuardHpOk(10, 50, 10), 1, "hp band lo");
        t.assert(combatGuardHpOk(10, 50, 50), 1, "hp band hi");
        t.assert(combatGuardHpOk(10, 50, 9), 0, "hp band below");
        t.assert(combatGuardHpOk(10, 50, 51), 0, "hp band above");
        t.assert(combatGuardPlayerOk(0, 0), 1, "no player requirement");
        t.assert(combatGuardPlayerOk(0, GUARD_PLAYER_ATTACKING), 1, "no requirement, attacking");
        t.assert(combatGuardPlayerOk(GUARD_PLAYER_ATTACKING, GUARD_PLAYER_ATTACKING), 1, "required flag present");
        t.assert(combatGuardPlayerOk(GUARD_PLAYER_ATTACKING, 0), 0, "required flag missing");
        t.assert(combatGuardCooldownOk(0, 0), 1, "zero cooldown");
        t.assert(combatGuardCooldownOk(10, 10), 1, "cooldown boundary");
        t.assert(combatGuardCooldownOk(10, 9), 0, "cooldown unmet");
        suite.addTest(t);
    }

    {
        Test t("guard predicates: >=, <=, == against packed part stages");
        CombatPredicate ge = {3, PRED_GE, 1};
        CombatPredicate le = {3, PRED_LE, 1};
        CombatPredicate eq = {3, PRED_EQ, 2};
        t.assert(combatPredicatePasses(ge, 0), 0, "ge stage 0");
        t.assert(combatPredicatePasses(ge, static_cast<uint16_t>(1u << 6)), 1, "ge stage 1");
        t.assert(combatPredicatePasses(ge, static_cast<uint16_t>(2u << 6)), 1, "ge stage 2");
        t.assert(combatPredicatePasses(le, 0), 1, "le stage 0");
        t.assert(combatPredicatePasses(le, static_cast<uint16_t>(1u << 6)), 1, "le stage 1");
        t.assert(combatPredicatePasses(le, static_cast<uint16_t>(2u << 6)), 0, "le stage 2");
        t.assert(combatPredicatePasses(eq, static_cast<uint16_t>(1u << 6)), 0, "eq stage 1");
        t.assert(combatPredicatePasses(eq, static_cast<uint16_t>(2u << 6)), 1, "eq stage 2");
        t.assert(combatPredicatePasses(eq, static_cast<uint16_t>(3u << 6)), 0, "eq stage 3");
        CombatPredicate unknown = {0, 200, 0};
        t.assert(combatPredicatePasses(unknown, 0xFFFF), 0, "unknown op rejects");
        suite.addTest(t);
    }

    {
        Test t("deterministic tick-derived chance");
        t.assert(combatChancePasses(0, 1, 2, 0, 100), 1, "chance 100 always passes");
        t.assert(combatChancePasses(0, 1, 2, 0, 0), 0, "chance 0 always fails");
        t.assert(combatChanceRoll(0, 1, 2, 0), 9, "roll vector tick0");
        t.assert(combatChanceRoll(2, 1, 2, 0), 78, "roll vector tick2");
        t.assert(combatChanceRoll(10, 1, 3, 0), 28, "roll vector tick10");
        t.assert(combatChanceRoll(100, 2, 0, 0), 55, "roll vector tick100");
        t.assert(combatChanceRoll(255, 0, 1, 1), 1, "roll vector tick255");
        t.assert(combatChancePasses(0, 1, 2, 0, 10), 1, "roll 9 < 10 passes");
        t.assert(combatChancePasses(0, 1, 2, 0, 9), 0, "roll 9 < 9 fails");
        bool stable = true;
        bool inRange = true;
        for (uint16_t tick = 0; tick < 256; tick++) {
            const uint8_t first = combatChanceRoll(tick, 1, 2, 0);
            if (first != combatChanceRoll(tick, 1, 2, 0))
                stable = false;
            if (first >= 100)
                inRange = false;
        }
        t.assert(stable, 1, "rolls are reproducible");
        t.assert(inRange, 1, "rolls stay in 0..99");
        suite.addTest(t);
    }

    {
        Test t("damage math: truncating percent chain (reference vectors)");
        t.assert(combatMulPercent(100, 0), 0, "mul 0");
        t.assert(combatMulPercent(100, 50), 50, "mul 50");
        t.assert(combatMulPercent(9, 150), 13, "9*150/100 truncates to 13");
        t.assert(combatMulPercent(13, 150), 19, "13*150/100 truncates to 19");
        t.assert(combatMulPercent(65535, 255), 167114, "32-bit intermediate");
        t.assert(combatPartMul(100, 100, 100), 100, "neutral chain");
        t.assert(combatPartMul(150, 100, 100), 150, "dmgMul only");
        t.assert(combatPartMul(100, 150, 100), 150, "phys only");
        t.assert(combatPartMul(150, 150, 200), 450, "full chain truncating");
        t.assert(combatPartMul(99, 99, 99), 97, "chain truncates at each step");   // 99*99/100=98, 98*99/100=97
        t.assert(combatMulBeats(150, 5, 100, 1), 1, "higher multiplier wins");
        t.assert(combatMulBeats(100, 5, 150, 1), 0, "lower multiplier loses");
        t.assert(combatMulBeats(100, 1, 100, 5), 1, "tie -> lower part id wins");
        t.assert(combatMulBeats(100, 5, 100, 1), 0, "tie -> higher part id loses");
        suite.addTest(t);
    }

    {
        Test t("resolveHit: routing, tie-break, stagger (shipped records)");
        Game g;
        creatureLoad(g, combat_data::CREATURE_LUNGE);
        const uint8_t body = combat::PART_QUAD_32X24_BODY;

        const uint8_t one[1] = {body};
        CombatHitResult r = combatResolveHit(g, 12, PHYS_BLUNT, ELEM_NONE, 100, 0, one, 1);
        t.assert(r.partIdx, body, "lunge hit part");
        t.assert(r.mul, 100, "neutral multiplier");
        t.assert(r.partDmg, 12, "part damage 12");
        t.assert(r.bodyDmg, 12, "body damage 12 (share 100)");
        t.assert(r.stagger, 0, "stagger gain 0");

        r = combatResolveHit(g, 12, PHYS_BLUNT, ELEM_NONE, 50, 0, one, 1);
        t.assert(r.partDmg, 6, "window 50 halves damage");
        t.assert(r.bodyDmg, 6, "window 50 halves body share");

        r = combatResolveHit(g, 13, PHYS_BLUNT, ELEM_NONE, 100, 11, one, 1);
        t.assert(r.partDmg, 13, "base 13 passes through");
        t.assert(r.stagger, 11, "stagger scales by neutral mul");

        r = combatResolveHit(g, 0, PHYS_BLUNT, ELEM_NONE, 100, 0, one, 1);
        t.assert(r.partDmg, 0, "zero base stays zero");
        t.assert(r.bodyDmg, 0, "zero base body");

        // Overlap tie-break: all shipped parts are 100/100/100, so the lowest
        // candidate part id must win regardless of candidate order.
        const uint8_t tie[3] = {2, 1, 0};
        r = combatResolveHit(g, 12, PHYS_BLUNT, ELEM_NONE, 100, 0, tie, 3);
        t.assert(r.partIdx, 0, "tie -> lowest part id (0)");
        const uint8_t tie2[2] = {2, 1};
        r = combatResolveHit(g, 12, PHYS_BLUNT, ELEM_NONE, 100, 0, tie2, 2);
        t.assert(r.partIdx, 1, "tie -> lowest part id (1)");

        r = combatResolveHit(g, 12, PHYS_BLUNT, ELEM_NONE, 100, 0, nullptr, 0);
        t.assert(r.partIdx, COMBAT_NO_PART, "no candidates -> no part");
        t.assert(r.partDmg, 0, "no candidates -> no damage");
        const uint8_t bad[2] = {200, 201};
        r = combatResolveHit(g, 12, PHYS_BLUNT, ELEM_NONE, 100, 0, bad, 2);
        t.assert(r.partIdx, COMBAT_NO_PART, "out-of-range candidates ignored");

        const CombatPart b = combatPartRead(body);
        t.assert(combatPartPhysMul(body, PHYS_BLUNT), b.physBlunt, "phys lookup blunt");
        t.assert(combatPartPhysMul(body, PHYS_SLASH), b.physSlash, "phys lookup slash");
        t.assert(combatPartPhysMul(body, 0), 100, "no phys -> neutral");
        t.assert(combatPartElemMul(body, ELEM_NONE), 100, "no elem -> neutral");
        t.assert(combatPartElemMul(body, ELEM_FIRE), 100, "absent elem -> neutral");
        suite.addTest(t);
    }

    {
        Test t("part stages: bitfield, threshold fold, break-effect projections");
        t.assert(combatStageCross(0, 30, 30), 1, "pct == at crosses");
        t.assert(combatStageCross(0, 31, 30), 0, "pct > at holds");
        t.assert(combatStageCross(1, 0, 0), 2, "second threshold crosses");
        t.assert(combatStageCross(2, 5, 4), 2, "later threshold holds");
        t.assert(combatStageCross(2, 5, 5), 3, "later threshold crosses");

        CombatStage st;
        st.at = 0;
        st.flags = 0;
        st.dmgMulOverride = 0;
        st.speedMul = 0;
        st.stagger = 0;
        st.cue = 0;
        st.firstDisable = 0;
        st.disableCount = 0;
        st.firstEnable = 0;
        st.enableCount = 0;
        t.assert(combatStageDmgMul(st, 100), 100, "no flag -> base dmgMul");
        t.assert(combatStageSpeedMul(st, 100), 100, "no flag -> base speedMul");
        t.assert(combatStageHurtOff(st), 0, "no flag -> still hurtable");
        st.flags = COMBAT_STAGE_FLAG_DMG_MUL | COMBAT_STAGE_FLAG_SPEED_MUL | COMBAT_STAGE_FLAG_HURT_OFF;
        st.dmgMulOverride = 200;
        st.speedMul = 50;
        st.stagger = 30;
        st.cue = 2;
        t.assert(combatStageDmgMul(st, 100), 200, "override dmgMul");
        t.assert(combatStageSpeedMul(st, 100), 50, "override speedMul");
        t.assert(combatStageHurtOff(st), 1, "hurt off flag");
        t.assert(combatStageStagger(st), 30, "stage stagger");
        t.assert(combatStageCue(st), 2, "stage cue");

        Game g;
        creatureLoad(g, combat_data::CREATURE_LUNGE);
        t.assert(combatPartStageGet(g, 0), 0, "stages start 0");
        combatPartStageSet(g, 0, 1);
        t.assert(combatPartStageGet(g, 0), 1, "part 0 stage 1");
        combatPartStageSet(g, 3, 3);
        t.assert(combatPartStageGet(g, 3), 3, "part 3 stage 3");
        t.assert(combatPartStageGet(g, 0), 1, "part bitfields independent");
        combatPartStageSet(g, 0, 9);
        t.assert(combatPartStageGet(g, 0), COMBAT_STAGE_MAX, "stage saturates");
        combatPartStageSet(g, 8, 1);
        t.assert(combatPartStageGet(g, 8), 0, "parts >= 8 untracked");
        combatPartStageSet(g, 0, 0);
        t.assert(combatPartStageGet(g, 0), 0, "stage clears");

        // No shipped stage records: threshold queries stay intact, effect
        // projections stay neutral, attack gating stays enabled.
        t.assert(combatPartStageCount(combat::PART_QUAD_32X24_BODY), 0, "shipped part has no stages");
        t.assert(combatPartStageForHp(combat::PART_QUAD_32X24_BODY, 100, 100), 0, "no stages -> stage 0");
        t.assert(combatPartStageForHp(combat::PART_QUAD_32X24_BODY, 0, 0), 0, "hpMax 0 -> stage 0");
        t.assert(combatPartDmgMulNow(g, combat::PART_QUAD_32X24_BODY), 100, "effective dmgMul neutral");
        t.assert(combatPartHurtOff(g, combat::PART_QUAD_32X24_BODY), 0, "body part hurtable");
        t.assert(combatPartSpeedMulNow(g, combat::PART_QUAD_32X24_BODY), 100, "effective speedMul neutral");
        t.assert(combatPartStaggerNow(g, combat::PART_QUAD_32X24_BODY), 0, "stage stagger 0");
        t.assert(combatPartCueNow(g, combat::PART_QUAD_32X24_BODY), 0, "stage cue none");
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_LUNGE_LUNGE), 0, "lunge enabled");
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_LUNGE_SWEEP), 0, "sweep enabled");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
