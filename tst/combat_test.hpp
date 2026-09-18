#pragma once
// Host unit tests for src/core/combat.hpp (beads monhun-ardu-ljj.2, cgk).
//
// Permanent, co-located suite. Covers the host read path (generated
// combat_data.hpp) against the loader, cache lifecycle, guard evaluation
// (inclusive ranges, zones-broken clause, player flags, cooldown, deterministic
// chance), damage routing and the fixed 3-hitzone resolve (body implicit, head
// + appendage records), zone pools/break bits and the creature fallback.
//
// cgk: the shipped blob carries the ravager head (dmgMul 130, staggerOnHit 12)
// and appendage tail (dmgMul 150, share 40, SLASH, broken override 200) plus
// the zonesBroken enrage guard, so the zone/stagger paths run against real
// records; the synthetic value-helper vectors stay as boundary coverage.
#include "test.hpp"
#include "../src/core/world.hpp"                // game + player + projectiles + monster (combat) + addEffect
#include "../src/generated/art_dims.hpp"        // fxtail frame layout (combatPartArtFrame)
#include "../src/generated/combat_expect.hpp"   // pinned zone spot values

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
            t.assert(c.headZone, h.headZone, "creature headZone");
            t.assert(c.appendZone, h.appendZone, "creature appendZone");
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
            t.assert(combatCreatureHeadZone(i), h.headZone, "creature headZone accessor");
            t.assert(combatCreatureAppendZone(i), h.appendZone, "creature appendZone accessor");
        }
        // Migration A scaffold: slot 0 of every shipped creature is its lunge.
        t.assert(combatCreatureFirstAttack(combat_data::CREATURE_LUNGE), combat_data::ATTACK_LUNGE_LUNGE, "lunge creature first attack");
        t.assert(combatCreatureFirstAttack(combat_data::CREATURE_SWEEP), combat_data::ATTACK_SWEEP_LUNGE, "sweep creature first attack");
        t.assert(combatCreatureFirstAttack(combat_data::CREATURE_HEAVY), combat_data::ATTACK_HEAVY_BITE, "heavy creature first attack");
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
            t.assert(p.zoneFlags, h.zoneFlags, "profile zoneFlags");
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
        Test t("skeleton + zone records match combat_data.hpp");
        for (uint8_t i = 0; i < combat::SKELETONS_COUNT; i++) {
            const CombatSkeleton s = combatSkeletonRead(i);
            const combat_data::Skeleton &h = combat_data::SKELETONS[i];
            t.assert(s.firstAnchor, h.firstAnchor, "skeleton firstAnchor");
            t.assert(s.anchorCount, h.anchorCount, "skeleton anchorCount");
        }
        for (uint8_t i = 0; i < combat::ZONES_COUNT; i++) {
            const CombatZone p = combatZoneRead(i);
            const combat_data::Zone &h = combat_data::ZONES[i];
            t.assert(p.box.ox, h.box.ox, "zone box.ox");
            t.assert(p.box.oy, h.box.oy, "zone box.oy");
            t.assert(p.box.w, h.box.w, "zone box.w");
            t.assert(p.box.h, h.box.h, "zone box.h");
            t.assert(p.hp, h.hp, "zone hp");
            t.assert(p.dmgMul, h.dmgMul, "zone dmgMul");
            t.assert(p.bodyShare, h.bodyShare, "zone bodyShare");
            t.assert(p.breakTypes, h.breakTypes, "zone breakTypes");
            t.assert(p.staggerOnHit, h.staggerOnHit, "zone staggerOnHit");
            t.assert(p.brokenDmgMul, h.brokenDmgMul, "zone brokenDmgMul");
            t.assert(p.brokenFlags, h.brokenFlags, "zone brokenFlags");
            t.assert(p.unlockMask, h.unlockMask, "zone unlockMask");
        }
        suite.addTest(t);
    }

    {
        Test t("body box + spawn accessors match combat_data.hpp");
        // cgk: the ravager declares head + appendage. 4t4 added the heavy
        // appendage (long tail). 76y added the chicken's lunge head + legs
        // (appendage) zones.
        t.assert(combat::ZONES_COUNT, 5, "heavy tail + ravager + lunge head/legs zones");
        t.assert(combat::ATTACKS_COUNT, 8, "3x2 shipped + ravager bite/tail_sweep");
        t.assert(combat::WINDOWS_COUNT, 12, "3 single-window + ravager 2 + heavy bite 1 + tail_spin 4");
        t.assert(combat::PATTERNS_COUNT, 8, "ravager adds p_enraged; heavy swaps lunge/sweep for spin/bite");
        t.assert(combat::GUARDS_COUNT, 8, "one guard per pattern");
        for (uint8_t i = 0; i < combat::CREATURES_COUNT; i++) {
            const combat_data::Creature &h = combat_data::CREATURES[i];
            t.assert(combatCreatureSkeletonIdx(i), h.skeletonIdx, "creature skeletonIdx accessor");
            const CombatSpawn s = combatCreatureSpawnRead(i);
            t.assert(s.hp, h.hp, "creature spawn hp");
            t.assert(s.spd, h.spd, "creature spawn spd");
            t.assert(s.x, h.spawnX, "creature spawn x");
            t.assert(s.y, h.spawnY, "creature spawn y");
            CombatBox box = {0, 0, 0, 0};
            uint8_t headZone = COMBAT_NO_ZONE, appendZone = COMBAT_NO_ZONE;
            const bool ok = combatCreatureBodyBox(i, box, headZone, appendZone);
            t.assert(ok, 1, "body box found");
            t.assert(box.ox, 0, "body box ox");
            t.assert(box.oy, 0, "body box oy");
            t.assert(box.w, h.w, "body box w == creature w");
            t.assert(box.h, h.h, "body box h == creature h");
            t.assert(headZone, h.headZone, "body box head zone");
            t.assert(appendZone, h.appendZone, "body box append zone");
            const CombatBox cb = combatCreatureCollideBox(i);
            t.assert(cb.ox, h.collide.ox, "collide box ox");
            t.assert(cb.oy, h.collide.oy, "collide box oy");
            t.assert(cb.w, h.collide.w, "collide box w");
            t.assert(cb.h, h.collide.h, "collide box h");
        }
        // Default collide box is the body box; the chicken authors its legs-
        // only rect (9,11,12,13) so the hunter can overlap the raised body.
        CombatBox cbox = combatCreatureCollideBox(combat_data::CREATURE_SWEEP);
        t.assert(cbox.ox, 0, "sweep collide defaults to body ox");
        t.assert(cbox.oy, 0, "sweep collide defaults to body oy");
        t.assert(cbox.w, 28, "sweep collide body w");
        t.assert(cbox.h, 22, "sweep collide body h");
        cbox = combatCreatureCollideBox(combat_data::CREATURE_LUNGE);
        t.assert(cbox.ox, 9, "lunge legs collide ox");
        t.assert(cbox.oy, 11, "lunge legs collide oy");
        t.assert(cbox.w, 12, "lunge legs collide w");
        t.assert(cbox.h, 13, "lunge legs collide h");
        // Pinned shipped sizes (parity contract: LUNGE 32x24, SWEEP 28x22,
        // HEAVY 40x28).
        CombatBox box;
        t.assert(combatCreatureBodyBox(combat_data::CREATURE_LUNGE, box), 1, "lunge body box");
        t.assert(box.w, 32, "lunge 32x24 w");
        t.assert(box.h, 24, "lunge 32x24 h");
        t.assert(combatCreatureBodyBox(combat_data::CREATURE_SWEEP, box), 1, "sweep body box");
        t.assert(box.w, 28, "sweep 28x22 w");
        t.assert(box.h, 22, "sweep 28x22 h");
        t.assert(combatCreatureBodyBox(combat_data::CREATURE_HEAVY, box), 1, "heavy body box");
        t.assert(box.w, 40, "heavy 40x28 w");
        t.assert(box.h, 28, "heavy 40x28 h");
        box = CombatBox{9, 9, 9, 9};
        t.assert(combatCreatureBodyBox(99, box), 1, "bad id falls back to creature 0");
        t.assert(box.w, 40, "fallback body box heavy w");
        suite.addTest(t);
    }

    {
        Test t("combatResolveBodyHit: implicit body routes base damage");
        const uint8_t kinds[3] = {combat_data::CREATURE_LUNGE, combat_data::CREATURE_SWEEP, combat_data::CREATURE_HEAVY};
        for (uint8_t i = 0; i < 3; i++) {
            Game g;
            creatureLoad(g, kinds[i]);
            const CombatBodyHit r = combatResolveBodyHit(g, 12);
            t.assert(r.zone, COMBAT_NO_ZONE, "body hit has no zone");
            t.assert(r.mul, 100, "neutral multiplier");
            t.assert(r.dmg, 12, "body damage unchanged");
        }
        Game g;
        creatureLoad(g, combat_data::CREATURE_LUNGE);
        t.assert(combatResolveBodyHit(g, 0).dmg, 0, "zero base stays zero");
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
            t.assert(g.zonesBroken, h.zonesBroken, "guard zonesBroken");
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
        Test t("creatureLoad caches profile, resets zones, falls back on bad id");
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
        t.assert(g.combat.profile.zoneFlags, hp.zoneFlags, "cache zoneFlags");
        t.assert(g.combat.body.w, 32, "cache body box w");
        t.assert(g.combat.body.h, 24, "cache body box h");
        // 76y: the chicken ships a head and a legs (appendage) zone and a
        // legs-only collide box; both are cached at spawn.
        t.assert(g.combat.collide.ox, 9, "cache legs collide ox");
        t.assert(g.combat.collide.oy, 11, "cache legs collide oy");
        t.assert(g.combat.collide.w, 12, "cache legs collide w");
        t.assert(g.combat.collide.h, 13, "cache legs collide h");
        t.assert(g.combat.headZone, combat_data::ZONE_LUNGE_HEAD, "head zone seeded");
        t.assert(g.combat.appendZone, combat_data::ZONE_LUNGE_APPENDAGE, "appendage zone seeded");
        t.assert(g.combat.zone[COMBAT_ZONE_HEAD].hp, combat_expect::ZONE_LUNGE_HEAD_HP, "head pool seeded");
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp, combat_expect::ZONE_LUNGE_APPENDAGE_HP, "legs pool seeded");
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].dmgMul, combat_expect::ZONE_LUNGE_APPENDAGE_DMG_MUL, "legs dmgMul seeded");
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].bodyShare, combat_expect::ZONE_LUNGE_APPENDAGE_BODY_SHARE, "legs bodyShare seeded");
        t.assert(g.combat.zoneBroken, 0, "zones intact");
        t.assert(g.combat.patternIdx, COMBAT_NO_PATTERN, "pattern cursor reset");
        t.assert(g.combat.stepIdx, 0, "step cursor reset");
        t.assert(g.combat.stepT, 0, "step timer reset");
        t.assert(g.combat.stagger, 0, "stagger reset");
        t.assert(g.combat.attack.windup, 0, "attack cache cleared");

        g.combat.zoneBroken = 0xFF;   // dirty before reload
        const uint8_t fallback = creatureLoad(g, 99);
        const combat_data::Profile &hh = combat_data::PROFILES[combat_data::CREATURES[combat_data::CREATURE_HEAVY].profileIdx];
        t.assert(fallback, 0, "bad id falls back to creature 0");
        t.assert(g.combat.creature, 0, "fallback cache creature");
        t.assert(g.combat.profile.attackDist, hh.attackDist, "fallback profile");
        t.assert(g.combat.profile.zoneFlags, hh.zoneFlags, "fallback zoneFlags");
        t.assert(g.combat.zoneBroken, 0, "reload resets broken bits");
        suite.addTest(t);
    }

    {
        Test t("creatureLoad seeds the ravager zone cache (head + appendage)");
        Game g;
        creatureLoad(g, combat_data::CREATURE_RAVAGER);
        const CombatZone head = combatZoneRead(combat_data::ZONE_RAVAGER_HEAD);
        const CombatZone tail = combatZoneRead(combat_data::ZONE_RAVAGER_APPENDAGE);
        t.assert(g.combat.headZone, combat_data::ZONE_RAVAGER_HEAD, "head zone index");
        t.assert(g.combat.appendZone, combat_data::ZONE_RAVAGER_APPENDAGE, "appendage zone index");
        t.assert(g.combat.zone[0].hp, head.hp, "head pool seeded");
        t.assert(g.combat.zone[0].dmgMul, head.dmgMul, "head dmgMul seeded");
        t.assert(g.combat.zone[0].staggerOnHit, head.staggerOnHit, "head stagger seeded");
        t.assert(g.combat.zone[1].hp, tail.hp, "tail pool seeded");
        t.assert(g.combat.zone[1].dmgMul, tail.dmgMul, "tail dmgMul seeded");
        t.assert(g.combat.zone[1].bodyShare, tail.bodyShare, "tail bodyShare seeded");
        t.assert(g.combat.zone[1].breakTypes, PHYS_SLASH, "tail breakTypes seeded");
        t.assert(g.combat.zone[1].staggerOnHit, tail.staggerOnHit, "tail stagger seeded");
        t.assert(g.combat.zone[1].unlockMask, tail.unlockMask, "tail unlockMask seeded");
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
        t.assert(g.combat.attack.winIdx, combat_data::WINDOW_HEAVY_BITE_0, "fallback window idx");
        suite.addTest(t);
    }

    {
        Test t("pattern cursor + combatTick countdown (no reads)");
        Game g;
        creatureLoad(g, combat_data::CREATURE_HEAVY);
        patternStateSet(g, combat_data::PATTERN_HEAVY_P_SPIN, 1, 3);
        t.assert(g.combat.patternIdx, combat_data::PATTERN_HEAVY_P_SPIN, "pattern idx set");
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
        t.assert(g.combat.patternIdx, combat_data::PATTERN_HEAVY_P_SPIN, "cursor untouched");
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
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_SPIN, in), 1, "heavy spin dist 24 accepted");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_BITE, in), 0, "heavy bite band excludes 24");
        in.dist = 25;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_SPIN, in), 0, "heavy spin dist 25 rejected");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_BITE, in), 1, "heavy bite dist 25 accepted");

        creatureLoad(g, combat_data::CREATURE_SWEEP);
        in.dist = 0;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_SWEEP, in), 1, "sweep always matches 0");
        in.dist = 255;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_SWEEP, in), 1, "sweep always matches 255");
        t.assert(combatGuardPasses(g, 99, in), 0, "unknown pattern rejected");
        suite.addTest(t);
    }

    {
        Test t("guard clauses: inclusive integer pure helpers + zones mask");
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
        // zonesBroken mask: every listed zone must be broken.
        t.assert(combatGuardZonesOk(0, 0), 1, "no zone requirement");
        t.assert(combatGuardZonesOk(0, COMBAT_ZONE_HEAD_BIT), 1, "no requirement, broken head");
        t.assert(combatGuardZonesOk(COMBAT_ZONE_APPENDAGE_BIT, COMBAT_ZONE_APPENDAGE_BIT), 1, "required zone broken");
        t.assert(combatGuardZonesOk(COMBAT_ZONE_APPENDAGE_BIT, COMBAT_ZONE_HEAD_BIT), 0, "wrong zone broken");
        t.assert(combatGuardZonesOk(COMBAT_ZONE_HEAD_BIT | COMBAT_ZONE_APPENDAGE_BIT, COMBAT_ZONE_APPENDAGE_BIT), 0, "one of two missing");
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
        suite.addTest(t);
    }

    // ------------------------------------------------ cgk 3-zone resolve
    {
        Test t("ravager zone records: head + tail spot values");
        const CombatZone head = combatZoneRead(combat_data::ZONE_RAVAGER_HEAD);
        t.assert(head.box.ox, 20, "head ox");
        t.assert(head.box.oy, 4, "head oy");
        t.assert(head.box.w, 12, "head w");
        t.assert(head.box.h, 12, "head h");
        t.assert(head.dmgMul, 130, "head dmgMul");
        t.assert(head.hp, 40, "head pool hp");
        t.assert(head.bodyShare, 100, "head bodyShare");
        t.assert(head.breakTypes, PHYS_SLASH, "head breakTypes slash only");
        t.assert(head.staggerOnHit, 12, "head staggerOnHit");
        t.assert(head.brokenDmgMul, 130, "head broken override");
        t.assert(head.brokenFlags, COMBAT_BROKEN_HURT_OFF, "head broken hurtOff");

        const CombatZone tail = combatZoneRead(combat_data::ZONE_RAVAGER_APPENDAGE);
        t.assert(tail.box.ox, -14, "tail ox");
        t.assert(tail.box.oy, 8, "tail oy");
        t.assert(tail.box.w, 18, "tail w");
        t.assert(tail.box.h, 10, "tail h");
        t.assert(tail.dmgMul, 150, "tail dmgMul");
        t.assert(tail.hp, 60, "tail pool hp");
        t.assert(tail.bodyShare, 40, "tail bodyShare");
        t.assert(tail.breakTypes, PHYS_SLASH, "tail breakTypes slash only");
        t.assert(tail.staggerOnHit, 30, "tail staggerOnHit");
        t.assert(tail.brokenDmgMul, 200, "tail broken override 200");
        t.assert(tail.brokenFlags, COMBAT_BROKEN_HURT_OFF | COMBAT_BROKEN_CUE, "tail broken hurtOff + cue");
        t.assert(tail.unlockMask, static_cast<uint8_t>(1u << combat_data::ATTACK_RAVAGER_TAIL_SWEEP), "tail unlock disables tail_sweep");
        suite.addTest(t);
    }

    {
        Test t("zone hit resolve: containment, multipliers, pool drain, break");
        Game g;
        creatureLoad(g, combat_data::CREATURE_RAVAGER);
        Monster &m = g.monster;
        m.x = 100;
        m.y = 40;
        m.fx = 16;   // face east: head x 120..132 y 44..56; tail x 86..104 y 48..58
        m.fy = 0;

        // Body-only point: implicit body wins at neutral multiplier.
        CombatBodyHit r = combatZoneHitResolve(g, 10, PHYS_BLUNT, 102, 42);
        t.assert(r.zone, COMBAT_NO_ZONE, "body-only zone");
        t.assert(r.mul, 100, "body-only mul neutral");
        t.assert(r.dmg, 10, "body-only damage");

        // Head point: 10*130/100 = 13 out; share 100 -> 13 body.
        r = combatZoneHitResolve(g, 10, PHYS_BLUNT, 125, 48);
        t.assert(r.zone, COMBAT_ZONE_HEAD, "head wins forward");
        t.assert(r.mul, 130, "head multiplier");
        t.assert(r.dmg, 13, "head body share 100");
        t.assert(g.combat.zone[COMBAT_ZONE_HEAD].hp, 27, "head pool 40-13");

        // Blunt is not a head/hit break type? head breakTypes are SLASH: pool
        // drains but no break bit.
        t.assert(g.combat.zoneBroken & COMBAT_ZONE_HEAD_BIT, 0, "blunt head does not break");

        // Tail point: 10*150/100 = 15 out; share 40 -> 6 body.
        r = combatZoneHitResolve(g, 10, PHYS_BLUNT, 95, 52);
        t.assert(r.zone, COMBAT_ZONE_APPENDAGE, "tail wins behind");
        t.assert(r.mul, 150, "tail multiplier");
        t.assert(r.dmg, 6, "tail body share 40");
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp, 45, "tail pool 60-15");

        // Tie rule: an explicit 100 zone does not beat the implicit body.
        g.combat.zone[COMBAT_ZONE_HEAD].dmgMul = 100;
        r = combatZoneHitResolve(g, 10, PHYS_BLUNT, 125, 48);
        t.assert(r.zone, COMBAT_NO_ZONE, "body wins mul tie");
        // Head/appendage tie: overlapping boxes, equal muls -> head wins.
        g.combat.zone[COMBAT_ZONE_HEAD].dmgMul = 130;
        g.combat.zone[COMBAT_ZONE_APPENDAGE].box = g.combat.zone[COMBAT_ZONE_HEAD].box;
        g.combat.zone[COMBAT_ZONE_APPENDAGE].dmgMul = 130;
        r = combatZoneHitResolve(g, 10, PHYS_BLUNT, 125, 48);
        t.assert(r.zone, COMBAT_ZONE_HEAD, "head wins head/tail tie");
        suite.addTest(t);
    }

    {
        Test t("zone break: slash drains tail to 0, flips the broken bit");
        Game g;
        creatureLoad(g, combat_data::CREATURE_RAVAGER);
        Monster &m = g.monster;
        m.x = 100;
        m.y = 40;
        m.fx = 16;
        m.fy = 0;

        // 10*150/100 = 15 per slash hit; 60 -> 45 -> 30 -> 15 -> 0.
        for (int i = 0; i < 4; i++) {
            const CombatBodyHit r = combatZoneHitResolve(g, 10, PHYS_SLASH, 95, 52);
            t.assert(r.zone, COMBAT_ZONE_APPENDAGE, "slash hits tail while intact");
        }
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp, 0, "tail pool floors at 0");
        t.assert(g.combat.zoneBroken & COMBAT_ZONE_APPENDAGE_BIT, COMBAT_ZONE_APPENDAGE_BIT, "tail broken bit set");

        // Broken zone leaves the candidate set: a tail point now routes body.
        const CombatBodyHit r = combatZoneHitResolve(g, 10, PHYS_SLASH, 95, 52);
        t.assert(r.zone, COMBAT_NO_ZONE, "broken tail routes to body");
        t.assert(r.dmg, 10, "body takes the full hit");

        // A body/head point is unaffected by the broken tail.
        const CombatBodyHit h = combatZoneHitResolve(g, 10, PHYS_SLASH, 125, 48);
        t.assert(h.zone, COMBAT_ZONE_HEAD, "head still absorbs");
        suite.addTest(t);
    }

    {
        Test t("combatAttackDisabled: broken tail disables tail_sweep only");
        Game g;
        creatureLoad(g, combat_data::CREATURE_RAVAGER);
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_RAVAGER_TAIL_SWEEP), 0, "sweep enabled intact");
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_RAVAGER_BITE), 0, "bite enabled intact");
        g.combat.zoneBroken = COMBAT_ZONE_APPENDAGE_BIT;
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_RAVAGER_TAIL_SWEEP), 1, "broken tail disables sweep");
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_RAVAGER_BITE), 0, "broken tail keeps bite");
        // Head has no unlock mask: breaking it disables nothing.
        g.combat.zoneBroken = COMBAT_ZONE_HEAD_BIT;
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_RAVAGER_TAIL_SWEEP), 0, "broken head disables nothing");
        // Shipped 3 have no zones: the guard folds the whole path out.
        creatureLoad(g, combat_data::CREATURE_LUNGE);
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_LUNGE_LUNGE), 0, "no zones -> attack enabled");
        suite.addTest(t);
    }

    {
        Test t("ravager multi-window tail_sweep + enrage zones guard");
        t.assert(combatAttackWindowCount(combat_data::ATTACK_RAVAGER_TAIL_SWEEP), 2, "two tail_sweep windows");
        const CombatWindow w0 = combatWindowRead(combat_data::WINDOW_RAVAGER_TAIL_SWEEP_0);
        const CombatWindow w1 = combatWindowRead(combat_data::WINDOW_RAVAGER_TAIL_SWEEP_1);
        t.assert(w0.t0, 0, "w0 t0");
        t.assert(w0.t1, 5, "w0 t1");
        t.assert(w1.t0, 6, "w1 t0");
        t.assert(w1.t1, 11, "w1 t1");
        t.assert(w0.box.ox, -22, "w0 sweeps behind");
        t.assert(w1.box.ox, 20, "w1 sweeps front");
        t.assert(combatAttackFirstWindow(combat_data::ATTACK_RAVAGER_TAIL_SWEEP), combat_data::WINDOW_RAVAGER_TAIL_SWEEP_0, "first window idx");
        t.assert(combatStepRef(combat_data::STEP_RAVAGER_P_SWEEP_0), combat_data::ATTACK_RAVAGER_TAIL_SWEEP, "sweep step attack");

        // Enrage pattern is ordered first and guarded only on the tail broken bit.
        t.assertLessThan(combat_data::PATTERN_RAVAGER_P_ENRAGED, combat_data::PATTERN_RAVAGER_P_SWEEP, "enrage listed first");
        t.assert(combatStepRef(combat_data::STEP_RAVAGER_P_ENRAGED_0), combat_data::ATTACK_RAVAGER_BITE, "enrage uses bite");
        const CombatGuard enrage = combatGuardRead(combat_data::GUARD_RAVAGER_P_ENRAGED);
        t.assert(enrage.zonesBroken, COMBAT_ZONE_APPENDAGE_BIT, "enrage requires the broken appendage");

        Game g;
        creatureLoad(g, combat_data::CREATURE_RAVAGER);
        CombatGuardInput in = {0, 100, 0, 0, 0xFFFF, 0};
        t.assert(combatGuardPasses(g, combat_data::PATTERN_RAVAGER_P_ENRAGED, in), 0, "intact tail -> not enraged");
        in.dist = 255;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_RAVAGER_P_ENRAGED, in), 0, "enrage ignores distance while intact");
        g.combat.zoneBroken = COMBAT_ZONE_APPENDAGE_BIT;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_RAVAGER_P_ENRAGED, in), 1, "broken tail -> enraged");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_RAVAGER_P_SWEEP, in), 0, "p_sweep distance clause still applies");
        suite.addTest(t);
    }

    {
        Test t("heavy bite + 4-window tail_spin decode + facing lock");
        // nch.1: the longtail swaps lunge/sweep for bite + a 360 tail_spin
        // whose four contiguous windows whip behind -> side -> front -> side.
        t.assert(combatAttackWindowCount(combat_data::ATTACK_HEAVY_BITE), 1, "bite single window");
        t.assert(combatAttackWindowCount(combat_data::ATTACK_HEAVY_TAIL_SPIN), 4, "tail_spin four windows");
        t.assert(combatAttackFacing(combat_data::ATTACK_HEAVY_BITE), COMBAT_FACING_TRACK, "bite tracks");
        t.assert(combatAttackFacing(combat_data::ATTACK_HEAVY_TAIL_SPIN), COMBAT_FACING_LOCK_AWAY, "tail_spin locks away");
        t.assert(COMBAT_FACING_LOCK_AWAY, 2, "lock-away facing value");
        const CombatWindow bite = combatWindowRead(combat_data::WINDOW_HEAVY_BITE_0);
        t.assert(bite.t0, 0, "bite t0");
        t.assert(bite.t1, 8, "bite t1");
        t.assert(bite.box.ox, 14, "bite ox");
        t.assert(bite.box.oy, 0, "bite oy");
        t.assert(bite.box.w, 18, "bite w");
        t.assert(bite.box.h, 14, "bite h");
        const CombatWindow spin0 = combatWindowRead(combat_data::WINDOW_HEAVY_TAIL_SPIN_0);
        const CombatWindow spin1 = combatWindowRead(combat_data::WINDOW_HEAVY_TAIL_SPIN_1);
        const CombatWindow spin2 = combatWindowRead(combat_data::WINDOW_HEAVY_TAIL_SPIN_2);
        const CombatWindow spin3 = combatWindowRead(combat_data::WINDOW_HEAVY_TAIL_SPIN_3);
        t.assert(spin0.t0, 0, "spin0 t0");
        t.assert(spin0.t1, 5, "spin0 t1");
        t.assert(spin0.box.ox, -20, "spin0 whips behind");
        t.assert(spin0.box.oy, 0, "spin0 level");
        t.assert(spin1.t0, 6, "spin1 t0");
        t.assert(spin1.t1, 10, "spin1 t1");
        t.assert(spin1.box.ox, 0, "spin1 centred x");
        t.assert(spin1.box.oy, -22, "spin1 whips north");
        t.assert(spin2.t0, 11, "spin2 t0");
        t.assert(spin2.t1, 15, "spin2 t1");
        t.assert(spin2.box.ox, 22, "spin2 whips front");
        t.assert(spin3.t0, 16, "spin3 t0");
        t.assert(spin3.t1, 20, "spin3 t1");
        t.assert(spin3.box.oy, 22, "spin3 whips south");
        t.assert(combatAttackFirstWindow(combat_data::ATTACK_HEAVY_TAIL_SPIN), combat_data::WINDOW_HEAVY_TAIL_SPIN_0, "spin first window");
        // Source order is semantic: p_spin (<=24) is listed before p_bite (>=25).
        t.assertLessThan(combat_data::PATTERN_HEAVY_P_SPIN, combat_data::PATTERN_HEAVY_P_BITE, "spin pattern listed first");
        t.assert(combatStepRef(combat_data::STEP_HEAVY_P_SPIN_0), combat_data::ATTACK_HEAVY_TAIL_SPIN, "spin step attack");
        t.assert(combatStepRef(combat_data::STEP_HEAVY_P_BITE_0), combat_data::ATTACK_HEAVY_BITE, "bite step attack");
        suite.addTest(t);
    }

    {
        Test t("part art frame linkage (fxtail sheet)");
        t.assert(art_dims::tail_frames, 4, "tail sheet frames");
        t.assert(art_dims::tail_frame_w, 18, "tail frame w == zone box w");
        t.assert(art_dims::tail_frame_h, 10, "tail frame h == zone box h");
        t.assert(combatPartArtFrame(false, 0), 0, "east intact frame");
        t.assert(combatPartArtFrame(false, 1), 1, "east broken frame");
        t.assert(combatPartArtFrame(true, 0), 2, "west intact frame");
        t.assert(combatPartArtFrame(true, 1), 3, "west broken frame");
        for (uint8_t broken = 0; broken < 2; broken++)
            t.assert(combatPartArtFrame(broken != 0, broken) < art_dims::tail_frames, 1, "frame in range");
        suite.addTest(t);
    }

    {
        Test t("heavy appendage zone (heavy.json) + tail art linkage");
        // 4t4: the longtail gains a real tail zone whose overlay box is the
        // authored fxtail_heavy frame; the generator keeps the two in step.
        const CombatZone z = combatZoneRead(combat_data::ZONE_HEAVY_APPENDAGE);
        t.assert(z.box.ox, -24, "heavy tail ox");
        t.assert(z.box.oy, 0, "heavy tail oy");
        t.assert(z.box.w, 24, "heavy tail w");
        t.assert(z.box.h, 16, "heavy tail h");
        t.assert(z.hp, 60, "heavy tail hp");
        t.assert(z.dmgMul, 150, "heavy tail dmgMul");
        t.assert(z.bodyShare, 40, "heavy tail bodyShare");
        t.assert(z.breakTypes, PHYS_SLASH, "heavy tail breakTypes");
        t.assert(z.staggerOnHit, 30, "heavy tail staggerOnHit");
        t.assert(z.brokenDmgMul, 200, "heavy tail broken dmgMul");
        t.assert(z.brokenFlags, 0x03, "heavy tail broken hurtOff + cue");
        t.assert(z.unlockMask, static_cast<uint8_t>(1u << combat_data::ATTACK_HEAVY_TAIL_SPIN), "heavy tail disables tail_spin");
        t.assert(art_dims::tail_heavy_frame_w, z.box.w, "tail_heavy frame w == zone box w");
        t.assert(art_dims::tail_heavy_frame_h, z.box.h, "tail_heavy frame h == zone box h");
        t.assert(art_dims::tail_heavy_frames, 4, "tail_heavy frames");
        // nch.1: the spin overlay sheet is 4 x 24x24, body-centre anchored.
        t.assert(art_dims::tail_spin_frame_w, 24, "tail_spin frame w");
        t.assert(art_dims::tail_spin_frame_h, 24, "tail_spin frame h");
        t.assert(art_dims::tail_spin_frames, 4, "tail_spin frames (W/N/E/S)");
        // creatureLoad seeds the heavy appendage cache from the record.
        Game g;
        creatureLoad(g, combat_data::CREATURE_HEAVY);
        t.assert(g.combat.appendZone, combat_data::ZONE_HEAVY_APPENDAGE, "heavy append zone index");
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp, z.hp, "heavy tail pool seeded");
        // Broken tail disables tail_spin but leaves bite available.
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_HEAVY_TAIL_SPIN), 0, "spin enabled intact");
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_HEAVY_BITE), 0, "bite enabled intact");
        g.combat.zoneBroken = COMBAT_ZONE_APPENDAGE_BIT;
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_HEAVY_TAIL_SPIN), 1, "broken tail disables tail_spin");
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_HEAVY_BITE), 0, "broken tail keeps bite");
        suite.addTest(t);
    }

    {
        Test t("stagger meter: head hit feeds staggerOnHit and trips STAGGER");
        Game g;
        initGame(g, W_SWORD);
        initMonster(g, MON_RAVAGER);
        Monster &m = g.monster;
        m.x = 100;
        m.y = 40;
        m.fx = 16;
        m.fy = 0;
        t.assert(g.combat.profile.staggerMax, 60, "ravager stagger threshold");
        t.assert(g.combat.stagger, 0, "meter starts empty");
        // Head hit (125,48) -> +staggerOnHit 12.
        monsterOnHit(g, 10, 125, 48, 0, 0);
        t.assert(g.combat.stagger, 12, "head hit adds 12");
        // Tail hits add 30 each; 12 + 30 = 42, +30 = 72 -> trips.
        monsterOnHit(g, 10, 95, 52, 0, 0);
        t.assert(g.combat.stagger, 42, "tail hit adds 30");
        monsterOnHit(g, 10, 95, 52, 0, 0);
        t.assert(m.state, MS_STAGGER, "meter trips STAGGER");
        t.assert(m.t, g.combat.profile.staggerRecoverT, "STAGGER recovery timer");
        t.assert(g.combat.stagger, 0, "meter resets on trip");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
