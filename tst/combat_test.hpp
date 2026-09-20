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
            // feel.6: creature enrage quad mirrors the host struct.
            t.assert(c.enrageHpPct, h.enrageHpPct, "creature enrageHpPct");
            t.assert(c.enrageSpdMul, h.enrageSpdMul, "creature enrageSpdMul");
            t.assert(c.enrageFaceHold, h.enrageFaceHold, "creature enrageFaceHold");
            t.assert(c.enrageCue, h.enrageCue, "creature enrageCue");
            t.assert(combatCreatureFirstAttack(i), h.firstAttack, "creature firstAttack accessor");
            t.assert(combatCreatureHeadZone(i), h.headZone, "creature headZone accessor");
            t.assert(combatCreatureAppendZone(i), h.appendZone, "creature appendZone accessor");
        }
        // Migration A scaffold: slot 0 of every shipped creature is its opener.
        t.assert(combatCreatureFirstAttack(combat_data::CREATURE_LUNGE), combat_data::ATTACK_LUNGE_PECK, "lunge creature first attack");
        t.assert(combatCreatureFirstAttack(combat_data::CREATURE_SWEEP), combat_data::ATTACK_SWEEP_STOMP, "sweep creature first attack");
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
            t.assert(p.faceHold, h.faceHold, "profile faceHold");
            t.assert(p.turnRate, h.turnRate, "profile turnRate");
            t.assert(p.cdBase, h.cdBase, "profile cdBase");
            t.assert(p.cdJitter, h.cdJitter, "profile cdJitter");
            t.assert(p.spawnT, h.spawnT, "profile spawnT");
            t.assert(p.spawnCd, h.spawnCd, "profile spawnCd");
            t.assert(p.stunRecoverT, h.stunRecoverT, "profile stunRecoverT");
            t.assert(p.staggerRecoverT, h.staggerRecoverT, "profile staggerRecoverT");
        }
        // nch.4: heavy commits its turn and holds ground at 12; feel.10 dropped
        // faceHold to 8, feel.15 raises it to 10 and authors turnRate 1 so the
        // spin can be out-ranged and the flank reached.
        const CombatProfile heavy = combatProfileRead(combatCreatureProfileIdx(combat_data::CREATURE_HEAVY));
        t.assert(heavy.faceHold, 10, "heavy faceHold 10 (feel.15)");
        t.assert(heavy.turnRate, 1, "heavy turnRate 1 (feel.15)");
        t.assert(heavy.keepDist, 12, "heavy keepDist 12");
        const CombatProfile lunge = combatProfileRead(combatCreatureProfileIdx(combat_data::CREATURE_LUNGE));
        t.assert(lunge.faceHold, 6, "chicken faceHold 6 (feel.15)");
        t.assert(lunge.turnRate, 1, "chicken turnRate 1 (feel.15)");
        t.assert(lunge.keepDist, 16, "chicken keepDist 16");
        t.assert(lunge.attackDist, 42, "chicken attackDist 42");
        t.assert(lunge.cdBase, 48, "chicken cdBase 48 (feel.8)");
        t.assert(lunge.cdJitter, 60, "chicken cdJitter 60 (feel.8)");
        t.assert(lunge.staggerMax, 30, "chicken staggerMax 30 (feel.8)");
        t.assert(lunge.staggerDecay, 2, "chicken staggerDecay 2 (feel.8)");
        t.assert(lunge.staggerRecoverT, 30, "chicken staggerRecoverT 30 (feel.8)");
        const CombatProfile sweep = combatProfileRead(combatCreatureProfileIdx(combat_data::CREATURE_SWEEP));
        t.assert(sweep.faceHold, combat_expect::PROFILE_SWEEP_FACE_HOLD, "bull faceHold 10");
        t.assert(sweep.turnRate, 1, "bull turnRate 1 (feel.15)");
        t.assert(sweep.keepDist, 18, "bull keepDist 18");
        t.assert(sweep.staggerMax, 40, "bull staggerMax 40 (feel.9)");
        t.assert(sweep.staggerDecay, 1, "bull staggerDecay 1 (feel.9)");
        t.assert(sweep.staggerRecoverT, 24, "bull staggerRecoverT 24 (feel.9)");
        const CombatProfile ravager = combatProfileRead(combatCreatureProfileIdx(combat_data::CREATURE_RAVAGER));
        t.assert(ravager.faceHold, 8, "ravager faceHold 8 (feel.15)");
        t.assert(ravager.turnRate, 2, "ravager turnRate 2 (feel.15)");
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
            t.assert(p.unlockMaskLo, h.unlockMaskLo, "zone unlockMaskLo");
            t.assert(p.unlockMaskHi, h.unlockMaskHi, "zone unlockMaskHi");
        }
        suite.addTest(t);
    }

    {
        Test t("body box + spawn accessors match combat_data.hpp");
        // cgk: the ravager declares head + appendage. 4t4 added the heavy
        // appendage (long tail). 76y added the chicken's lunge head + legs
        // (appendage) zones. nch.9 added the bull's head (horns) + appendage
        // (hooves) zones.
        // 6zb.9: each variant carries ONE whole-pole zone (the old per-variant
        // head zones are gone): plain head + 3 variant appendages = 4 pole zones.
        t.assert(combat::ZONES_COUNT, 11, "heavy tail + ravager + lunge + sweep + 4 pole zone records");
        t.assert(combat::CREATURES_COUNT, 8, "3 demo beasts + ravager + 4 static poles");
        t.assert(combat::SKELETONS_COUNT, 5, "bull/chicken/longtail/quad + pole");
        t.assert(combat::ATTACKS_COUNT, 11, "3x2 shipped + ravager bite/tail_sweep + chicken wing_beat + sweep rear_kick + heavy tail_slam (feel.10)");
        t.assert(combat::WINDOWS_COUNT, 16, "single-window attacks + ravager 2 + tail_spin 4 + sweep stomp 1/gore 2 + wing_beat + rear_kick + tail_slam");
        t.assert(combat::PATTERNS_COUNT, 15, "feel.10: heavy p_tail_slam + p_bite_spin; feel.9 sweep p_rear_kick/p_gore2; feel.8 chicken p_flank/p_leap2; ravager p_enraged; heavy spin/bite");
        t.assert(combat::GUARDS_COUNT, 15, "one guard per pattern");
        t.assert(combat::STEPS_COUNT, 19, "feel.10: heavy tail_slam step + bite_spin three steps");
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
        // only rect (9,11,12,13) and the bull (nch.9) its wide low hooves rect
        // (1,14,26,8) so the hunter can overlap the raised body.
        CombatBox cbox = combatCreatureCollideBox(combat_data::CREATURE_SWEEP);
        t.assert(cbox.ox, combat_expect::CREATURE_SWEEP_COLLIDE_OX, "sweep hooves collide ox");
        t.assert(cbox.oy, combat_expect::CREATURE_SWEEP_COLLIDE_OY, "sweep hooves collide oy");
        t.assert(cbox.w, combat_expect::CREATURE_SWEEP_COLLIDE_W, "sweep hooves collide w");
        t.assert(cbox.h, combat_expect::CREATURE_SWEEP_COLLIDE_H, "sweep hooves collide h");
        cbox = combatCreatureCollideBox(combat_data::CREATURE_RAVAGER);
        t.assert(cbox.ox, 0, "ravager collide defaults to body ox");
        t.assert(cbox.oy, 0, "ravager collide defaults to body oy");
        t.assert(cbox.w, 32, "ravager collide body w");
        t.assert(cbox.h, 24, "ravager collide body h");
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
        Test t("static prop records: pole skeleton, static/sheet/brokenBody decode");
        // The 4 pole records are static creatures (flags bit0) with an art sheet
        // id and an optional brokenBody; they carry no attacks/patterns.
        t.assert(combatCreatureStatic(combat_data::CREATURE_POLE), 1, "pole static");
        t.assert(combatCreatureStatic(combat_data::CREATURE_POLE_SEVER), 1, "sever static");
        t.assert(combatCreatureStatic(combat_data::CREATURE_POLE_BREAK), 1, "break static");
        t.assert(combatCreatureStatic(combat_data::CREATURE_POLE_CRACK), 1, "crack static");
        t.assert(combatCreatureStatic(combat_data::CREATURE_LUNGE), 0, "lunge not static");
        t.assert(combatCreatureSheet(combat_data::CREATURE_POLE), 1, "pole sheet 1");
        t.assert(combatCreatureSheet(combat_data::CREATURE_POLE_SEVER), 2, "sever sheet 2");
        t.assert(combatCreatureSheet(combat_data::CREATURE_POLE_BREAK), 3, "break sheet 3");
        t.assert(combatCreatureSheet(combat_data::CREATURE_POLE_CRACK), 4, "crack sheet 4");
        t.assert(combatCreatureSheet(combat_data::CREATURE_LUNGE), 0, "beast default sheet 0");
        // Whole-pole zones + stage art (bead monhun-ardu-6zb.9): no variant
        // resizes its hurt rect, so no brokenBody override ships.
        t.assert(combatCreatureBrokenW(combat_data::CREATURE_POLE_BREAK), 0, "break no brokenBody w");
        t.assert(combatCreatureBrokenH(combat_data::CREATURE_POLE_BREAK), 0, "break no brokenBody h");
        t.assert(combatCreatureBrokenW(combat_data::CREATURE_POLE), 0, "plain no brokenBody");
        for (uint8_t i = 0; i < combat::CREATURES_COUNT; i++) {
            const CombatCreature c = combatCreatureRead(i);
            t.assert(c.flags, combatCreatureFlags(i), "creature flags accessor");
            t.assert(c.sheet, combatCreatureSheet(i), "creature sheet accessor");
            t.assert(c.brokenW, combatCreatureBrokenW(i), "creature brokenW accessor");
            t.assert(c.brokenH, combatCreatureBrokenH(i), "creature brokenH accessor");
        }
        // Static pole carries no attacks/patterns; its profile is inert.
        const combat_data::Creature &h = combat_data::CREATURES[combat_data::CREATURE_POLE];
        t.assert(h.attackCount, 0, "pole no attacks");
        t.assert(h.patternCount, 0, "pole no patterns");
        const CombatProfile pp = combatProfileRead(h.profileIdx);
        t.assert(pp.cdBase, 0, "pole profile zeroed");
        t.assert(pp.circleDen, 1, "pole profile den 1");
        // Pole skeleton + anchors.
        const CombatSkeleton ps = combatSkeletonRead(combat_data::SKELETON_POLE);
        t.assert(ps.anchorCount, 2, "pole skeleton anchors");
        const CombatAnchor a0 = combatAnchorRead(ps.firstAnchor);
        const CombatAnchor a1 = combatAnchorRead(static_cast<uint8_t>(ps.firstAnchor + 1));
        t.assert(a0.ox, 0, "pole origin anchor ox");
        t.assert(a0.oy, 0, "pole origin anchor oy");
        t.assert(a1.ox, 10, "pole head anchor ox");
        t.assert(a1.oy, 8, "pole head anchor oy");
        suite.addTest(t);
    }

    {
        Test t("pole zone records: crit head, one part-locked breakable per variant");
        const CombatZone ph = combatZoneRead(combat_data::ZONE_POLE_HEAD);
        t.assert(ph.box.ox, -128, "plain head ox (x-independent band)");
        t.assert(ph.box.oy, 0, "plain head oy");
        t.assert(ph.box.w, 255, "plain head w");
        t.assert(ph.box.h, 16, "plain head h");
        t.assert(ph.dmgMul, 140, "plain head dmgMul");
        t.assert(ph.hp, 0, "plain head no pool");
        t.assert(ph.breakTypes, 0, "plain head unbreakable");
        const uint8_t anyPhys = PHYS_SLASH | PHYS_BLUNT | PHYS_SHOT;
        // Each variant ships exactly one appendage zone locked to its additive
        // part (cap/horn/collar), dmgMul 101 so a part hit beats the body tie.
        const CombatZone sa = combatZoneRead(combat_data::ZONE_POLE_SEVER_APPENDAGE);
        t.assert(sa.box.ox, -2, "sever cap ox");
        t.assert(sa.box.oy, 0, "sever cap oy");
        t.assert(sa.box.w, 24, "sever cap w");
        t.assert(sa.box.h, 20, "sever cap h");
        t.assert(sa.hp, 60, "sever cap pool");
        t.assert(sa.dmgMul, 101, "sever cap dmgMul (beats body tie, damage stays base)");
        t.assert(sa.bodyShare, 100, "sever cap body share");
        t.assert(sa.breakTypes, anyPhys, "sever any weapon breaks");
        t.assert(sa.brokenFlags, COMBAT_BROKEN_HURT_OFF, "sever broken hurtOff");
        const CombatZone ba = combatZoneRead(combat_data::ZONE_POLE_BREAK_APPENDAGE);
        t.assert(ba.box.ox, 4, "break horn ox");
        t.assert(ba.box.oy, 0, "break horn oy");
        t.assert(ba.box.w, 18, "break horn w");
        t.assert(ba.box.h, 20, "break horn h");
        t.assert(ba.hp, 40, "break horn pool");
        t.assert(ba.dmgMul, 101, "break horn dmgMul (beats body tie, damage stays base)");
        t.assert(ba.breakTypes, anyPhys, "break any weapon breaks");
        t.assert(ba.brokenFlags, COMBAT_BROKEN_HURT_OFF, "break broken hurtOff");
        const CombatZone ca = combatZoneRead(combat_data::ZONE_POLE_CRACK_APPENDAGE);
        t.assert(ca.box.ox, -2, "crack collar ox");
        t.assert(ca.box.oy, 12, "crack collar oy");
        t.assert(ca.box.w, 24, "crack collar w");
        t.assert(ca.box.h, 16, "crack collar h");
        t.assert(ca.hp, 30, "crack collar pool");
        t.assert(ca.dmgMul, 101, "crack collar dmgMul (beats body tie, damage stays base)");
        t.assert(ca.breakTypes, anyPhys, "crack any weapon breaks");
        suite.addTest(t);
    }

    {
        Test t("static prop resolve: east facing, x-independent head band, shared pool rule");
        Game g;
        creatureLoad(g, combat_data::CREATURE_POLE);
        t.assert(g.combat.isStatic, 1, "static flag cached");
        t.assert(g.combat.headZone, combat_data::ZONE_POLE_HEAD, "pole head zone seeded");
        // Anchor at the prop rect; the head band is hy < rect.y + 16 regardless
        // of hx (the legacy containment), so an hx far outside the 20 px body
        // still selects the head zone (mul 140 -> 14).
        CombatBodyHit r = combatZoneHitResolveAt(g, 10, PHYS_SLASH, 100, 50, 140, 40, 16, 0);
        t.assert(r.zone, COMBAT_ZONE_HEAD, "head selected with hx outside the body");
        t.assert(r.mul, 140, "head multiplier 140");
        t.assert(r.dmg, 14, "head damage x1.4");
        // Below the 16 px band -> body.
        r = combatZoneHitResolveAt(g, 10, PHYS_SLASH, 100, 70, 140, 40, 16, 0);
        t.assert(r.zone, COMBAT_NO_ZONE, "below band routes body");
        t.assert(r.dmg, 10, "body damage");
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
            t.assert(a.wallStun, h.wallStun, "attack wallStun");
            t.assert(a.firstWindow, h.firstWindow, "attack firstWindow");
            t.assert(a.windowCount, h.windowCount, "attack windowCount");
            t.assert(a.windup, h.windup, "attack windup");
            t.assert(a.active, h.active, "attack active");
            t.assert(a.recover, h.recover, "attack recover");
            t.assert(a.dmg, h.dmg, "attack dmg");
            t.assert(a.tell, h.tell, "attack tell");
            t.assert(combatAttackWindup(i), h.windup, "attack windup accessor");
            t.assert(combatAttackWallStun(i), h.wallStun, "attack wallStun accessor");
            t.assert(combatAttackFirstWindow(i), h.firstWindow, "attack firstWindow accessor");
            t.assert(combatAttackWindowCount(i), h.windowCount, "attack windowCount accessor");
            t.assert(combatAttackTell(i), h.tell, "attack tell accessor");
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
            t.assert(g.facing, h.facing, "guard facing");
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
        // feel.8: the chicken's first combo data. p_flank is the first pattern
        // (behind punish) and p_leap2 is the two-step enrage combo.
        Test t("chicken kit: p_flank + p_leap2 step shape (feel.8)");
        t.assert(combat_data::PATTERN_LUNGE_P_FLANK, combat_data::CREATURES[combat_data::CREATURE_LUNGE].firstPattern, "p_flank is the opener");
        t.assert(combatPatternStepCount(combat_data::PATTERN_LUNGE_P_FLANK), 1, "p_flank one step");
        t.assert(combatStepRef(combat_data::STEP_LUNGE_P_FLANK_0), combat_data::ATTACK_LUNGE_WING_BEAT, "p_flank uses wing_beat");
        const CombatGuard flank = combatGuardRead(combat_data::GUARD_LUNGE_P_FLANK);
        t.assert(flank.facing, GUARD_FACING_BEHIND, "p_flank guard behind");
        t.assert(flank.maxDist, combat_expect::PATTERN_LUNGE_P_FLANK_MAX_DIST, "p_flank guard reach");

        t.assert(combatPatternStepCount(combat_data::PATTERN_LUNGE_P_LEAP2), 2, "p_leap2 two steps");
        const CombatStep leap2a = combatStepRead(combat_data::STEP_LUNGE_P_LEAP2_0);
        const CombatStep leap2b = combatStepRead(combat_data::STEP_LUNGE_P_LEAP2_1);
        t.assert(leap2a.kind, STEP_ATK, "leap2 step0 atk");
        t.assert(leap2a.ref, combat_data::ATTACK_LUNGE_LEAP, "leap2 step0 leap");
        t.assert(leap2a.after, 10, "leap2 step0 after 10");
        t.assert(leap2b.kind, STEP_ATK, "leap2 step1 atk");
        t.assert(leap2b.ref, combat_data::ATTACK_LUNGE_LEAP, "leap2 step1 leap");
        t.assert(leap2b.chance, 70, "leap2 step1 chance 70");
        const CombatGuard leap2g = combatGuardRead(combat_data::GUARD_LUNGE_P_LEAP2);
        t.assert(leap2g.hpLo, 0, "leap2 hp lo 0");
        t.assert(leap2g.hpHi, 50, "leap2 hp hi 50");
        suite.addTest(t);
    }

    {
        // feel.9: the bull kit. stomp is a single body-centred ring window (the
        // RING tell draws the real hit area, so a sidestep alone is not safe);
        // gore is the committed charge with a LINE tell and a 70-tick wall stun;
        // rear_kick is the anti-flank ARC behind the body; p_gore2 doubles the
        // charge below 40% hp at every range, so breaking the hooves (stomp
        // disabled) does not make the fight easier.
        Test t("bull kit: ring stomp, gore wallStun/line, rear_kick arc, gore2 combo (feel.9)");
        t.assert(combatAttackWindowCount(combat_data::ATTACK_SWEEP_STOMP), 1, "stomp one window");
        const CombatWindow stomp0 = combatWindowRead(combat_data::WINDOW_SWEEP_STOMP_0);
        t.assert(stomp0.t0, 0, "stomp w0 t0");
        t.assert(stomp0.t1, 10, "stomp w0 t1");
        t.assert(stomp0.box.ox, 0, "stomp ring ox");
        t.assert(stomp0.box.oy, 0, "stomp ring oy");
        t.assert(stomp0.box.w, 36, "stomp ring w");
        t.assert(stomp0.box.h, 26, "stomp ring h");
        t.assert(combatAttackTell(combat_data::ATTACK_SWEEP_STOMP), TELL_RING, "stomp ring tell");
        t.assert(combatAttackFacing(combat_data::ATTACK_SWEEP_STOMP), COMBAT_FACING_TRACK, "stomp tracks");
        t.assert(combatAttackMoveType(combat_data::ATTACK_SWEEP_STOMP), MOVE_NONE, "stomp stationary");
        t.assert(combatAttackWindup(combat_data::ATTACK_SWEEP_STOMP), 34, "stomp windup 34");
        t.assert(combatAttackActive(combat_data::ATTACK_SWEEP_STOMP), 11, "stomp active 11");
        t.assert(combatAttackRecover(combat_data::ATTACK_SWEEP_STOMP), 46, "stomp recover 46");

        t.assert(combatAttackWindowCount(combat_data::ATTACK_SWEEP_GORE), 2, "gore two windows");
        t.assert(combatWindowRead(combat_data::WINDOW_SWEEP_GORE_0).box.ox, 16, "gore horns ox");
        t.assert(combatWindowRead(combat_data::WINDOW_SWEEP_GORE_0).t1, 6, "gore horns t1");
        t.assert(combatWindowRead(combat_data::WINDOW_SWEEP_GORE_1).box.ox, 12, "gore trample ox");
        t.assert(combatWindowRead(combat_data::WINDOW_SWEEP_GORE_1).t1, 12, "gore trample t1");
        t.assert(combatAttackFacing(combat_data::ATTACK_SWEEP_GORE), COMBAT_FACING_LOCK, "gore locks at windup");
        t.assert(combatAttackWallStun(combat_data::ATTACK_SWEEP_GORE), 70, "gore wallStun 70");
        t.assert(combatAttackTell(combat_data::ATTACK_SWEEP_GORE), TELL_LINE, "gore line tell");
        t.assert(combatAttackMoveSpeedF(combat_data::ATTACK_SWEEP_GORE), 40, "gore speedF 40");
        t.assert(combatAttackWindup(combat_data::ATTACK_SWEEP_GORE), 42, "gore windup 42");
        t.assert(combatAttackActive(combat_data::ATTACK_SWEEP_GORE), 14, "gore active 14");
        t.assert(combatAttackRecover(combat_data::ATTACK_SWEEP_GORE), 58, "gore recover 58");

        t.assert(combatAttackWindowCount(combat_data::ATTACK_SWEEP_REAR_KICK), 1, "rear_kick one window");
        const CombatWindow kick = combatWindowRead(combat_data::WINDOW_SWEEP_REAR_KICK_0);
        t.assert(kick.t0, 0, "rear_kick t0");
        t.assert(kick.t1, 4, "rear_kick t1");
        t.assert(kick.box.ox, -14, "rear_kick window behind the body");
        t.assert(kick.box.oy, 4, "rear_kick oy");
        t.assert(kick.box.w, 22, "rear_kick w");
        t.assert(kick.box.h, 14, "rear_kick h");
        t.assert(combatAttackFacing(combat_data::ATTACK_SWEEP_REAR_KICK), COMBAT_FACING_TRACK, "rear_kick tracks");
        t.assert(combatAttackTell(combat_data::ATTACK_SWEEP_REAR_KICK), TELL_ARC, "rear_kick arc tell");
        t.assert(combatAttackMoveType(combat_data::ATTACK_SWEEP_REAR_KICK), MOVE_NONE, "rear_kick stationary");
        t.assert(combatAttackWindup(combat_data::ATTACK_SWEEP_REAR_KICK), 14, "rear_kick windup 14");
        t.assert(combatAttackActive(combat_data::ATTACK_SWEEP_REAR_KICK), 4, "rear_kick active 4");
        t.assert(combatAttackRecover(combat_data::ATTACK_SWEEP_REAR_KICK), 30, "rear_kick recover 30");

        // Source order is semantic: rear_kick punishes flanks first, and gore2
        // must precede stomp so the low-HP enrage covers the close band even
        // after the hooves break disables stomp.
        t.assert(combat_data::PATTERN_SWEEP_P_REAR_KICK, combat_data::CREATURES[combat_data::CREATURE_SWEEP].firstPattern, "p_rear_kick is the opener");
        t.assertLessThan(combat_data::PATTERN_SWEEP_P_GORE2, combat_data::PATTERN_SWEEP_P_STOMP, "gore2 precedes stomp");
        t.assertLessThan(combat_data::PATTERN_SWEEP_P_STOMP, combat_data::PATTERN_SWEEP_P_GORE, "stomp precedes gore");

        t.assert(combatPatternStepCount(combat_data::PATTERN_SWEEP_P_REAR_KICK), 1, "p_rear_kick one step");
        t.assert(combatStepRef(combat_data::STEP_SWEEP_P_REAR_KICK_0), combat_data::ATTACK_SWEEP_REAR_KICK, "p_rear_kick uses rear_kick");
        const CombatGuard kickGuard = combatGuardRead(combat_data::GUARD_SWEEP_P_REAR_KICK);
        t.assert(kickGuard.facing, GUARD_FACING_BEHIND, "p_rear_kick guard behind");
        t.assert(kickGuard.maxDist, 30, "p_rear_kick guard reach 30 (feel.15)");

        t.assert(combatPatternStepCount(combat_data::PATTERN_SWEEP_P_GORE2), 2, "p_gore2 two steps");
        const CombatStep g2a = combatStepRead(combat_data::STEP_SWEEP_P_GORE2_0);
        const CombatStep g2b = combatStepRead(combat_data::STEP_SWEEP_P_GORE2_1);
        t.assert(g2a.kind, STEP_ATK, "gore2 step0 atk");
        t.assert(g2a.ref, combat_data::ATTACK_SWEEP_GORE, "gore2 step0 gore");
        t.assert(g2a.after, 18, "gore2 step0 after 18");
        t.assert(g2b.ref, combat_data::ATTACK_SWEEP_GORE, "gore2 step1 gore");
        t.assert(g2b.chance, 70, "gore2 step1 chance 70");
        const CombatGuard g2 = combatGuardRead(combat_data::GUARD_SWEEP_P_GORE2);
        t.assert(g2.hpLo, 0, "gore2 hp lo 0");
        t.assert(g2.hpHi, 40, "gore2 hp hi 40");
        t.assert(g2.minDist, 0, "gore2 reaches point blank");
        t.assert(g2.maxDist, 255, "gore2 reaches long range");
        suite.addTest(t);
    }

    {
        Test t("sweep enrage record + spawn cache (feel.9)");
        const CombatCreature c = combatCreatureRead(combat_data::CREATURE_SWEEP);
        t.assert(c.enrageHpPct, combat_expect::CREATURE_SWEEP_ENRAGE_HP_PCT, "sweep enrage hpPct 40");
        t.assert(c.enrageSpdMul, combat_expect::CREATURE_SWEEP_ENRAGE_SPD_MUL, "sweep enrage spdMul 130");
        t.assert(c.enrageFaceHold, combat_expect::CREATURE_SWEEP_ENRAGE_FACE_HOLD, "sweep enrage faceHold 6");
        t.assert(c.enrageCue, combat_expect::CREATURE_SWEEP_ENRAGE_CUE, "sweep enrage cue none");
        Game g;
        creatureLoad(g, combat_data::CREATURE_SWEEP);
        t.assert(g.combat.enrage.hpPct, 40, "enrage hpPct cached");
        t.assert(g.combat.enrage.spdMul, 130, "enrage spdMul cached");
        t.assert(g.combat.enrage.faceHold, 6, "enrage faceHold cached");
        t.assert(g.combat.enrage.fired, 0, "enrage latch clear at spawn");
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
        t.assert(g.combat.zone[COMBAT_ZONE_HEAD].hpMax, combat_expect::ZONE_LUNGE_HEAD_HP, "head hpMax cached");
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp, combat_expect::ZONE_LUNGE_APPENDAGE_HP, "legs pool seeded");
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hpMax, combat_expect::ZONE_LUNGE_APPENDAGE_HP, "legs hpMax cached");
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].dmgMul, combat_expect::ZONE_LUNGE_APPENDAGE_DMG_MUL, "legs dmgMul seeded");
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].bodyShare, combat_expect::ZONE_LUNGE_APPENDAGE_BODY_SHARE, "legs bodyShare seeded");
        t.assert(g.combat.zoneBroken, 0, "zones intact");
        t.assert(g.combat.patternIdx, COMBAT_NO_PATTERN, "pattern cursor reset");
        t.assert(g.combat.stepIdx, 0, "step cursor reset");
        t.assert(g.combat.stepT, 0, "step timer reset");
        t.assert(g.combat.stagger, 0, "stagger reset");
        t.assert(g.combat.enrage.hpPct, 0, "enrage hpPct reset");
        t.assert(g.combat.enrage.spdMul, 0, "enrage spdMul reset");
        t.assert(g.combat.enrage.faceHold, 0, "enrage faceHold reset");
        t.assert(g.combat.enrage.cue, 0, "enrage cue reset");
        t.assert(g.combat.enrage.fired, 0, "enrage latch cleared");
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
        t.assert(g.combat.zone[1].unlockMask, static_cast<uint16_t>(tail.unlockMaskLo) | (static_cast<uint16_t>(tail.unlockMaskHi) << 8), "tail unlockMask seeded");
        suite.addTest(t);
    }

    {
        Test t("attackLoad + attackWindowLoad cache lifecycle");
        Game g;
        creatureLoad(g, combat_data::CREATURE_LUNGE);
        const uint8_t atk = attackLoad(g, combat_data::ATTACK_LUNGE_PECK);
        t.assert(atk, combat_data::ATTACK_LUNGE_PECK, "load returns attack idx");
        t.assert(g.combat.attack.windup, 18, "cache windup");
        t.assert(g.combat.attack.active, 6, "cache active");
        t.assert(g.combat.attack.recover, 26, "cache recover");
        t.assert(g.combat.attack.dmg, 7, "cache dmg");
        t.assert(g.combat.attack.moveType, 1, "cache moveType lunge");
        t.assert(g.combat.attack.moveSpeedF, 20, "cache moveSpeedF");
        t.assert(g.combat.attack.facing, 0, "cache facing track");
        t.assert(g.combat.attack.wallStun, combat_expect::ATTACK_LUNGE_PECK_WALLSTUN, "cache wallStun");
        t.assert(g.combat.attack.tell, combat_expect::ATTACK_LUNGE_PECK_TELL, "cache tell");
        t.assert(g.combat.attack.winIdx, combat_data::WINDOW_LUNGE_PECK_0, "cache winIdx");
        t.assert(g.combat.attack.win.t0, 0, "cache window t0");
        t.assert(g.combat.attack.win.t1, 6, "cache window t1");
        t.assert(g.combat.attack.win.box.ox, 14, "cache window ox");
        t.assert(g.combat.attack.win.box.oy, -6, "cache window oy");
        t.assert(g.combat.attack.win.box.w, 12, "cache window w");
        t.assert(g.combat.attack.win.box.h, 10, "cache window h");
        t.assert(g.combat.attack.win.dmgMul, 100, "cache window dmgMul");

        attackWindowLoad(g, combat_data::WINDOW_LUNGE_LEAP_0);
        t.assert(g.combat.attack.winIdx, combat_data::WINDOW_LUNGE_LEAP_0, "window switch idx");
        t.assert(g.combat.attack.win.t1, 10, "window switch t1");
        t.assert(g.combat.attack.win.box.ox, 12, "window switch ox");
        t.assert(g.combat.attack.win.box.oy, -2, "window switch oy");
        t.assert(g.combat.attack.win.box.w, 18, "window switch w");
        t.assert(g.combat.attack.win.box.h, 16, "window switch h");

        // Attack scalars stay cached across a window switch (active phase).
        t.assert(g.combat.attack.dmg, 7, "scalars survive window switch");

        // feel.8: wing_beat is the third chicken attack — stationary, ARC tell,
        // a 26x18 window offset behind the body (ox -8) for the flank punish.
        attackLoad(g, combat_data::ATTACK_LUNGE_WING_BEAT);
        t.assert(g.combat.attack.windup, 16, "wing_beat windup");
        t.assert(g.combat.attack.active, 5, "wing_beat active");
        t.assert(g.combat.attack.recover, 34, "wing_beat recover");
        t.assert(g.combat.attack.dmg, 9, "wing_beat dmg");
        t.assert(g.combat.attack.moveType, 0, "wing_beat stationary");
        t.assert(g.combat.attack.moveSpeedF, 0, "wing_beat no speed");
        t.assert(g.combat.attack.facing, 0, "wing_beat tracks");
        t.assert(g.combat.attack.tell, TELL_ARC, "wing_beat arc tell");
        t.assert(g.combat.attack.winIdx, combat_data::WINDOW_LUNGE_WING_BEAT_0, "wing_beat window idx");
        t.assert(g.combat.attack.win.t0, 0, "wing_beat window t0");
        t.assert(g.combat.attack.win.t1, 5, "wing_beat window t1");
        t.assert(g.combat.attack.win.box.ox, -8, "wing_beat window behind");
        t.assert(g.combat.attack.win.box.oy, 0, "wing_beat window level");
        t.assert(g.combat.attack.win.box.w, 26, "wing_beat window w");
        t.assert(g.combat.attack.win.box.h, 18, "wing_beat window h");

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

        // Chicken (nch.7): p_peck 0..28, p_leap 28..255; both match at the
        // inclusive 28 boundary (source order picks peck).
        in.dist = 28;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_PECK, in), 1, "peck dist 28 accepted");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_LEAP, in), 1, "leap dist 28 accepted (peck wins order)");
        in.dist = 29;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_PECK, in), 0, "peck dist 29 rejected");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_LEAP, in), 1, "leap dist 29 accepted");
        in.dist = 0;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_PECK, in), 1, "peck dist 0 accepted");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_LEAP, in), 0, "leap dist 0 rejected");
        in.dist = 255;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_PECK, in), 0, "peck dist 255 rejected");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_LEAP, in), 1, "leap dist 255 accepted");

        // feel.8 p_flank: behind (facingDot < 0) and maxDist; feel.15 widens the
        // reach 26->32 so a hunter who circles behind at swing range is punished.
        // Source order puts it first, so a flanking hunter inside 32 takes the
        // wing beat.
        in.hpPct = 100;
        in.facingDot = -6;
        in.dist = 0;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_FLANK, in), 1, "flank behind accepted");
        in.dist = 32;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_FLANK, in), 1, "flank behind at 32 accepted");
        in.dist = 33;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_FLANK, in), 0, "flank behind past 32 rejected");
        in.dist = 0;
        in.facingDot = 0;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_FLANK, in), 0, "flank abeam rejected");
        in.facingDot = 6;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_FLANK, in), 0, "flank in front rejected");
        in.facingDot = 0;

        // feel.8 p_leap2: hpBand [0,50] enrage combo.
        in.dist = 33;
        in.hpPct = 100;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_LEAP2, in), 0, "leap2 above 50% hp rejected");
        in.hpPct = 51;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_LEAP2, in), 0, "leap2 at 51% rejected");
        in.hpPct = 50;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_LEAP2, in), 1, "leap2 at 50% accepted");
        in.hpPct = 0;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_LUNGE_P_LEAP2, in), 1, "leap2 at 0% accepted");
        in.hpPct = 100;

        creatureLoad(g, combat_data::CREATURE_HEAVY);
        // feel.10: source order is p_tail_slam (behind) then p_bite_spin
        // (0..20), p_spin (21..30) and p_bite (31..255). The three base bands are
        // exclusive, so tail_spin stays reachable <=30 and bite beyond; the slam
        // only answers a hunter who has circled behind at pounce range. feel.15
        // widens the behind band 20..60 -> 16..64.
        in.hpPct = 100;
        in.facingDot = 0;
        in.dist = 30;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_SPIN, in), 1, "heavy spin dist 30 accepted");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_BITE_SPIN, in), 0, "heavy bite_spin dist 30 rejected");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_BITE, in), 0, "heavy bite dist 30 rejected");
        in.dist = 20;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_BITE_SPIN, in), 1, "heavy bite_spin dist 20 accepted");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_SPIN, in), 0, "heavy spin dist 20 rejected");
        in.dist = 21;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_SPIN, in), 1, "heavy spin dist 21 accepted");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_BITE_SPIN, in), 0, "heavy bite_spin dist 21 rejected");
        in.dist = 31;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_SPIN, in), 0, "heavy spin dist 31 rejected");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_BITE, in), 1, "heavy bite dist 31 accepted");
        // tail_slam behind clause: 16..64 only, dot < 0.
        in.dist = 40;
        in.facingDot = -6;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_TAIL_SLAM, in), 1, "tail_slam behind at 40 accepted");
        in.dist = 16;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_TAIL_SLAM, in), 1, "tail_slam behind at 16 accepted");
        in.dist = 15;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_TAIL_SLAM, in), 0, "tail_slam behind below 16 rejected");
        in.dist = 64;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_TAIL_SLAM, in), 1, "tail_slam behind at 64 accepted");
        in.dist = 65;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_TAIL_SLAM, in), 0, "tail_slam behind past 64 rejected");
        in.dist = 40;
        in.facingDot = 6;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_TAIL_SLAM, in), 0, "tail_slam in front rejected");
        in.facingDot = 0;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_HEAVY_P_TAIL_SLAM, in), 0, "tail_slam abeam rejected");

        creatureLoad(g, combat_data::CREATURE_SWEEP);
        // Bull (feel.9): p_rear_kick (behind, first) is listed first, then
        // p_gore2 (hp <=40, every range), p_stomp (<=24) and p_gore (>=24, hp
        // 41..100). The hp bands are disjoint, so remaining HP swaps the gore
        // pattern; gore2 covers close range so breaking the hooves (stomp
        // disabled) does not open a free punish window during the enrage. feel.15
        // widens the kick band 24->30.
        in.hpPct = 100;
        in.facingDot = 0;
        in.dist = 24;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_REAR_KICK, in), 0, "rear_kick needs a behind flank");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_STOMP, in), 1, "stomp dist 24 accepted");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_GORE, in), 1, "gore dist 24 accepted");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_GORE2, in), 0, "gore2 above 40% hp rejected");
        in.facingDot = -6;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_REAR_KICK, in), 1, "rear_kick behind at 24 accepted");
        in.dist = 30;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_REAR_KICK, in), 1, "rear_kick behind at 30 accepted");
        in.dist = 31;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_REAR_KICK, in), 0, "rear_kick past 30 rejected");
        in.facingDot = 6;
        in.dist = 10;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_REAR_KICK, in), 0, "rear_kick in front rejected");
        in.facingDot = 0;
        in.dist = 25;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_STOMP, in), 0, "stomp dist 25 rejected");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_GORE, in), 1, "gore dist 25 accepted");
        in.dist = 0;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_STOMP, in), 1, "stomp dist 0 accepted");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_GORE, in), 0, "gore dist 0 rejected");
        in.dist = 255;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_STOMP, in), 0, "stomp dist 255 rejected");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_GORE, in), 1, "gore dist 255 accepted");
        // HP band swap at 40% (the enrage threshold): gore2 replaces gore.
        in.hpPct = 41;
        in.dist = 40;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_GORE2, in), 0, "gore2 at 41% hp rejected");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_GORE, in), 1, "gore at 41% hp accepted");
        in.hpPct = 40;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_GORE2, in), 1, "gore2 at 40% hp accepted");
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_GORE, in), 0, "gore at 40% hp rejected");
        in.dist = 255;
        t.assert(combatGuardPasses(g, combat_data::PATTERN_SWEEP_P_GORE2, in), 1, "gore2 reaches 255");
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
        Test t("guard facing: behind/front/edge + dot sign (west-facing beast)");
        // Any clause always passes, whatever the dot.
        t.assert(combatGuardFacingOk(GUARD_FACING_ANY, -5), 1, "any ignores behind");
        t.assert(combatGuardFacingOk(GUARD_FACING_ANY, 0), 1, "any ignores abeam");
        t.assert(combatGuardFacingOk(GUARD_FACING_ANY, 7), 1, "any ignores front");
        // Behind: negative dot only; front: positive only; dot 0 matches neither.
        t.assert(combatGuardFacingOk(GUARD_FACING_BEHIND, -1), 1, "behind accepts negative");
        t.assert(combatGuardFacingOk(GUARD_FACING_BEHIND, 0), 0, "behind rejects abeam");
        t.assert(combatGuardFacingOk(GUARD_FACING_BEHIND, 1), 0, "behind rejects front");
        t.assert(combatGuardFacingOk(GUARD_FACING_FRONT, 1), 1, "front accepts positive");
        t.assert(combatGuardFacingOk(GUARD_FACING_FRONT, 0), 0, "front rejects abeam");
        t.assert(combatGuardFacingOk(GUARD_FACING_FRONT, -1), 0, "front rejects behind");
        // Dot math on body centres: a west-facing beast (fx=-16, fy=0) sees a
        // player to its east (px > mx) as behind (negative), to its west as front
        // (positive); a player in the same column is dead abeam (0).
        t.assert(combatFacingDot(40, 20, 20, 20, -16, 0), -20, "west beast, east player behind");
        t.assert(combatFacingDot(0, 20, 20, 20, -16, 0), 20, "west beast, west player front");
        t.assert(combatFacingDot(20, 20, 20, 20, -16, 0), 0, "west beast, same column abeam");
        t.assert(combatFacingDot(40, 20, 20, 20, 16, 0), 20, "east beast, east player front");
        t.assert(combatFacingDot(40, 40, 20, 20, -16, 0), -20, "vertical offset ignored when facing west");
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
        t.assert(static_cast<uint16_t>(tail.unlockMaskLo) | (static_cast<uint16_t>(tail.unlockMaskHi) << 8), static_cast<uint16_t>(1u << combat_data::ATTACK_RAVAGER_TAIL_SWEEP),
                 "tail unlock disables tail_sweep");
        suite.addTest(t);
    }

    {
        Test t("bull zone records: horns head + hooves appendage (nch.9)");
        const CombatZone head = combatZoneRead(combat_data::ZONE_SWEEP_HEAD);
        t.assert(head.box.ox, 17, "bull head ox");
        t.assert(head.box.oy, -4, "bull head oy");
        t.assert(head.box.w, 12, "bull head w");
        t.assert(head.box.h, 10, "bull head h");
        t.assert(head.dmgMul, combat_expect::ZONE_SWEEP_HEAD_DMG_MUL, "bull head dmgMul");
        t.assert(head.hp, combat_expect::ZONE_SWEEP_HEAD_HP, "bull head pool hp");
        t.assert(head.bodyShare, combat_expect::ZONE_SWEEP_HEAD_BODY_SHARE, "bull head bodyShare");
        t.assert(head.breakTypes, PHYS_SLASH, "bull head breakTypes slash only");
        t.assert(head.staggerOnHit, 12, "bull head staggerOnHit");
        t.assert(head.brokenFlags, COMBAT_BROKEN_HURT_OFF, "bull head broken hurtOff");
        t.assert(static_cast<uint16_t>(head.unlockMaskLo) | (static_cast<uint16_t>(head.unlockMaskHi) << 8), 0, "bull head disables nothing");

        const CombatZone hooves = combatZoneRead(combat_data::ZONE_SWEEP_APPENDAGE);
        t.assert(hooves.box.ox, 4, "bull hooves ox");
        t.assert(hooves.box.oy, 12, "bull hooves oy");
        t.assert(hooves.box.w, 20, "bull hooves w");
        t.assert(hooves.box.h, 10, "bull hooves h");
        t.assert(hooves.dmgMul, combat_expect::ZONE_SWEEP_APPENDAGE_DMG_MUL, "bull hooves dmgMul");
        t.assert(hooves.hp, combat_expect::ZONE_SWEEP_APPENDAGE_HP, "bull hooves pool hp");
        t.assert(hooves.bodyShare, combat_expect::ZONE_SWEEP_APPENDAGE_BODY_SHARE, "bull hooves bodyShare");
        t.assert(hooves.breakTypes, PHYS_SLASH, "bull hooves breakTypes slash only");
        t.assert(hooves.staggerOnHit, 30, "bull hooves staggerOnHit");
        t.assert(hooves.brokenDmgMul, 200, "bull hooves broken override 200");
        t.assert(hooves.brokenFlags, COMBAT_BROKEN_HURT_OFF | COMBAT_BROKEN_CUE, "bull hooves broken hurtOff + cue");
        t.assert(static_cast<uint16_t>(hooves.unlockMaskLo) | (static_cast<uint16_t>(hooves.unlockMaskHi) << 8), static_cast<uint16_t>(1u << combat_data::ATTACK_SWEEP_STOMP),
                 "bull hooves disable stomp");
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
        // ... a 100 zone never outranks the body; the whole-pole props author
        // dmgMul 101 (base damage still truncates identically) so every landed
        // pole hit drains. (The head above still lost the same tie.)
        g.combat.zone[COMBAT_ZONE_HEAD].dmgMul = 130;
        g.combat.zone[COMBAT_ZONE_APPENDAGE].dmgMul = 101;
        r = combatZoneHitResolve(g, 10, PHYS_BLUNT, 95, 52);
        t.assert(r.zone, COMBAT_ZONE_APPENDAGE, "appendage 101 beats body");
        t.assert(r.dmg, 4, "101 mul truncates to base x share");
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
        // Chicken (nch.7): the legs' broken record disables the leap only.
        creatureLoad(g, combat_data::CREATURE_LUNGE);
        g.combat.zoneBroken = 0;
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_LUNGE_LEAP), 0, "intact legs -> leap enabled");
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_LUNGE_PECK), 0, "intact legs -> peck enabled");
        g.combat.zoneBroken = COMBAT_ZONE_APPENDAGE_BIT;
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_LUNGE_LEAP), 1, "broken legs disable leap");
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_LUNGE_PECK), 0, "broken legs keep peck");
        // Bull (nch.9): the hooves' broken record disables the stomp only.
        creatureLoad(g, combat_data::CREATURE_SWEEP);
        g.combat.zoneBroken = 0;
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_SWEEP_STOMP), 0, "intact hooves -> stomp enabled");
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_SWEEP_GORE), 0, "intact hooves -> gore enabled");
        g.combat.zoneBroken = COMBAT_ZONE_APPENDAGE_BIT;
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_SWEEP_STOMP), 1, "broken hooves disable stomp");
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_SWEEP_GORE), 0, "broken hooves keep gore");
        t.assert(combatAttackDisabled(g, combat_data::ATTACK_SWEEP_REAR_KICK), 0, "broken hooves keep rear_kick");
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
        Test t("heavy kit: bite + 4-window tail_spin + hop tail_slam + patterns");
        // nch.1/feel.10: the longtail keeps bite + a 360 tail_spin whose four
        // contiguous windows whip behind -> side -> front -> side, retimed to
        // active 18 with each long axis widened +4 px, plus the tail_slam pounce.
        t.assert(combatAttackWindowCount(combat_data::ATTACK_HEAVY_BITE), 1, "bite single window");
        t.assert(combatAttackWindowCount(combat_data::ATTACK_HEAVY_TAIL_SPIN), 4, "tail_spin four windows");
        t.assert(combatAttackFacing(combat_data::ATTACK_HEAVY_BITE), COMBAT_FACING_TRACK, "bite tracks");
        t.assert(combatAttackFacing(combat_data::ATTACK_HEAVY_TAIL_SPIN), COMBAT_FACING_LOCK_AWAY, "tail_spin locks away");
        t.assert(COMBAT_FACING_LOCK_AWAY, 2, "lock-away facing value");
        t.assert(combatAttackWindup(combat_data::ATTACK_HEAVY_TAIL_SPIN), 34, "spin windup 34");
        t.assert(combatAttackActive(combat_data::ATTACK_HEAVY_TAIL_SPIN), 18, "spin active 18");
        t.assert(combatAttackRecover(combat_data::ATTACK_HEAVY_TAIL_SPIN), 62, "spin recover 62");
        t.assert(combatAttackTell(combat_data::ATTACK_HEAVY_TAIL_SPIN), TELL_ARC, "spin arc tell");
        const CombatWindow bite = combatWindowRead(combat_data::WINDOW_HEAVY_BITE_0);
        t.assert(bite.t0, 0, "bite t0");
        t.assert(bite.t1, 8, "bite t1");
        t.assert(bite.box.ox, 14, "bite ox");
        t.assert(bite.box.oy, 0, "bite oy");
        t.assert(bite.box.w, 18, "bite w");
        t.assert(bite.box.h, 14, "bite h");
        t.assert(combatAttackTell(combat_data::ATTACK_HEAVY_BITE), TELL_LINE, "bite line tell");
        const CombatWindow spin0 = combatWindowRead(combat_data::WINDOW_HEAVY_TAIL_SPIN_0);
        const CombatWindow spin1 = combatWindowRead(combat_data::WINDOW_HEAVY_TAIL_SPIN_1);
        const CombatWindow spin2 = combatWindowRead(combat_data::WINDOW_HEAVY_TAIL_SPIN_2);
        const CombatWindow spin3 = combatWindowRead(combat_data::WINDOW_HEAVY_TAIL_SPIN_3);
        t.assert(spin0.t0, 0, "spin0 t0");
        t.assert(spin0.t1, 5, "spin0 t1");
        t.assert(spin0.box.ox, -20, "spin0 whips behind");
        t.assert(spin0.box.oy, 0, "spin0 level");
        t.assert(spin0.box.w, 28, "spin0 widened behind");
        t.assert(spin1.t0, 6, "spin1 t0");
        t.assert(spin1.t1, 10, "spin1 t1");
        t.assert(spin1.box.ox, 0, "spin1 centred x");
        t.assert(spin1.box.oy, -22, "spin1 whips north");
        t.assert(spin1.box.h, 28, "spin1 widened north");
        t.assert(spin2.t0, 11, "spin2 t0");
        t.assert(spin2.t1, 14, "spin2 t1");
        t.assert(spin2.box.ox, 22, "spin2 whips front");
        t.assert(spin2.box.w, 28, "spin2 widened front");
        t.assert(spin3.t0, 15, "spin3 t0");
        t.assert(spin3.t1, 18, "spin3 t1");
        t.assert(spin3.box.oy, 22, "spin3 whips south");
        t.assert(spin3.box.h, 28, "spin3 widened south");
        t.assert(combatAttackFirstWindow(combat_data::ATTACK_HEAVY_TAIL_SPIN), combat_data::WINDOW_HEAVY_TAIL_SPIN_0, "spin first window");
        // feel.10 tail_slam: hop pounce, lock-at-windup, ring tell, one behind
        // window covering the 20..60 decision band.
        t.assert(combatAttackMoveType(combat_data::ATTACK_HEAVY_TAIL_SLAM), MOVE_HOP, "tail_slam is a hop");
        t.assert(combatAttackFacing(combat_data::ATTACK_HEAVY_TAIL_SLAM), COMBAT_FACING_LOCK, "tail_slam locks at windup");
        t.assert(combatAttackTell(combat_data::ATTACK_HEAVY_TAIL_SLAM), TELL_RING, "tail_slam ring tell");
        t.assert(combatAttackWindup(combat_data::ATTACK_HEAVY_TAIL_SLAM), 26, "tail_slam windup");
        t.assert(combatAttackActive(combat_data::ATTACK_HEAVY_TAIL_SLAM), 8, "tail_slam active");
        t.assert(combatAttackRecover(combat_data::ATTACK_HEAVY_TAIL_SLAM), 44, "tail_slam recover");
        t.assert(combatAttackDmg(combat_data::ATTACK_HEAVY_TAIL_SLAM), 12, "tail_slam dmg");
        const CombatAttackValue slam = combatAttackRead(combat_data::ATTACK_HEAVY_TAIL_SLAM);
        t.assert(slam.moveDx, -56, "tail_slam hop dx");
        t.assert(slam.moveDy, 0, "tail_slam hop dy");
        const CombatWindow slamWin = combatWindowRead(combat_data::WINDOW_HEAVY_TAIL_SLAM_0);
        t.assert(slamWin.t0, 0, "tail_slam window t0");
        t.assert(slamWin.t1, 8, "tail_slam window t1");
        t.assert(slamWin.box.ox, -16, "tail_slam window behind");
        t.assert(slamWin.box.w, 36, "tail_slam window w");
        t.assert(slamWin.box.h, 28, "tail_slam window h");
        // Source order is semantic: p_tail_slam first, then p_bite_spin, p_spin,
        // p_bite. The three base bands are exclusive.
        t.assert(combat_data::CREATURES[combat_data::CREATURE_HEAVY].firstPattern, combat_data::PATTERN_HEAVY_P_TAIL_SLAM, "tail_slam pattern first");
        t.assertLessThan(combat_data::PATTERN_HEAVY_P_TAIL_SLAM, combat_data::PATTERN_HEAVY_P_BITE_SPIN, "slam before combo");
        t.assertLessThan(combat_data::PATTERN_HEAVY_P_BITE_SPIN, combat_data::PATTERN_HEAVY_P_SPIN, "combo before spin");
        t.assertLessThan(combat_data::PATTERN_HEAVY_P_SPIN, combat_data::PATTERN_HEAVY_P_BITE, "spin before bite");
        const CombatGuard slamGuard = combatGuardRead(combat_data::GUARD_HEAVY_P_TAIL_SLAM);
        t.assert(slamGuard.facing, GUARD_FACING_BEHIND, "tail_slam guard behind");
        t.assert(slamGuard.minDist, 16, "tail_slam guard minDist 16 (feel.15)");
        t.assert(slamGuard.maxDist, 64, "tail_slam guard maxDist 64 (feel.15)");
        t.assert(combatStepRef(combat_data::STEP_HEAVY_P_TAIL_SLAM_0), combat_data::ATTACK_HEAVY_TAIL_SLAM, "tail_slam step attack");
        // p_bite_spin combo: bite after 12, WAIT 16, tail_spin.
        t.assert(combatPatternStepCount(combat_data::PATTERN_HEAVY_P_BITE_SPIN), 3, "bite_spin three steps");
        const CombatStep bs0 = combatStepRead(combat_data::STEP_HEAVY_P_BITE_SPIN_0);
        const CombatStep bs1 = combatStepRead(combat_data::STEP_HEAVY_P_BITE_SPIN_1);
        const CombatStep bs2 = combatStepRead(combat_data::STEP_HEAVY_P_BITE_SPIN_2);
        t.assert(bs0.kind, STEP_ATK, "bite_spin step0 atk");
        t.assert(bs0.ref, combat_data::ATTACK_HEAVY_BITE, "bite_spin step0 bite");
        t.assert(bs0.after, 12, "bite_spin step0 after 12");
        t.assert(bs1.kind, STEP_WAIT, "bite_spin step1 wait");
        t.assert(bs1.ref, 16, "bite_spin step1 wait 16");
        t.assert(bs2.kind, STEP_ATK, "bite_spin step2 atk");
        t.assert(bs2.ref, combat_data::ATTACK_HEAVY_TAIL_SPIN, "bite_spin step2 tail_spin");
        const CombatGuard bsg = combatGuardRead(combat_data::GUARD_HEAVY_P_BITE_SPIN);
        t.assert(bsg.maxDist, 20, "bite_spin guard reach 20");
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
        t.assert(static_cast<uint16_t>(z.unlockMaskLo) | (static_cast<uint16_t>(z.unlockMaskHi) << 8), static_cast<uint16_t>(1u << combat_data::ATTACK_HEAVY_TAIL_SPIN),
                 "heavy tail disables tail_spin");
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
