#pragma once
// Host unit tests for src/core/monster.hpp — permanent, co-located with repo
// tests. Mirrors mock/game.test.js monster-half expectations (mock is the
// source of truth for numbers): tell timing, recovery windows, push rule,
// crit zone, stun->recover, knockback ints, and the player-hit routing.
#include "test.hpp"
#include "../src/core/monster.hpp"
#include "../src/core/world.hpp"                // newGame / withWeapon / resetHunt
#include "../src/generated/combat_expect.hpp"   // bull collide box pins

using namespace mh;

namespace {

// initGame + spawn the beast, wired into Game::target.
void newHunt(Game &g, int8_t weapon = W_SWORD) {
    initGame(g, weapon);
    initMonster(g);
}

// Full hunt ticks (player then monster).
void hunt(Game &g, int n, Input in = Input{0, 0, false, false}) {
    for (int i = 0; i < n; i++)
        stepHunt(g, in);
}

// Raw beast ticks (no player update, no tick advance).
void beast(Game &g, int n) {
    for (int i = 0; i < n; i++)
        updateMonster(g);
}

}   // namespace

void MonsterSuite(TestRunner &runner) {
    TestSuite suite("Monster FSM + hit resolution (src/core/monster.hpp)");

    {
        Test t("MONSTER_ATTACKS + init fields match prototype");
        t.assert(MONSTER_ATTACKS[0].kind, MK_LUNGE, "lunge kind");
        t.assert(MONSTER_ATTACKS[0].windup, 40, "lunge windup");
        t.assert(MONSTER_ATTACKS[0].active, 10, "lunge active");
        t.assert(MONSTER_ATTACKS[0].recover, 55, "lunge recover");
        t.assert(MONSTER_ATTACKS[0].speedF, 34, "lunge speedF");
        t.assert(MONSTER_ATTACKS[0].dmg, 12, "lunge dmg");
        t.assert(MONSTER_ATTACKS[0].reach, 12, "lunge reach");
        t.assert(MONSTER_ATTACKS[0].hw, 24, "lunge hw");
        t.assert(MONSTER_ATTACKS[0].hh, 22, "lunge hh");
        t.assert(MONSTER_ATTACKS[1].kind, MK_SWEEP, "sweep kind");
        t.assert(MONSTER_ATTACKS[1].windup, 48, "sweep windup");
        t.assert(MONSTER_ATTACKS[1].active, 12, "sweep active");
        t.assert(MONSTER_ATTACKS[1].recover, 60, "sweep recover");
        t.assert(MONSTER_ATTACKS[1].dmg, 9, "sweep dmg");
        t.assert(MONSTER_ATTACKS[1].reach, 17, "sweep reach");
        t.assert(MONSTER_ATTACKS[1].hw, 32, "sweep hw");
        t.assert(MONSTER_ATTACKS[1].hh, 24, "sweep hh");

        Game g;
        newHunt(g);
        t.assert(g.monster.w, 32, "monster width");
        t.assert(g.monster.h, 24, "monster height");
        t.assert(g.monster.hp, 200, "monster hp");
        t.assert(g.monster.hpMax, 200, "monster hpMax");
        t.assert(g.monster.state, MS_IDLE, "starts idle");
        t.assert(g.monster.t, 90, "idle timer 90");
        t.assert(g.monster.cd, 140, "cooldown 140");
        t.assert(g.monster.fx, -fp::FP, "starts facing W");
        t.assert(g.monster.fy, 0, "starts facing flat");
        t.assert(g.monster.circleDir, 1, "circle dir right");
        t.assert(g.monster.spd, 6, "pursue speed 6/16");
        suite.addTest(t);
    }

    {
        Test t("chooseAttack: chicken pecks inside 28, leaps 29..41 (cache identity)");
        Game g;
        newHunt(g);
        chooseAttack(g, 33);
        t.assert(g.monster.atkIdx, combat::ATTACK_LUNGE_LEAP, "dist 33 -> leap");
        t.assert(g.combat.attack.moveType, MOVE_LUNGE, "leap cache moveType");
        t.assert(g.monster.state, MS_WINDUP, "windup state");
        t.assert(g.monster.t, 30, "leap tell 30");
        t.assert(g.monster.windupMax, 30, "windupMax recorded");
        chooseAttack(g, 28);
        t.assert(g.monster.atkIdx, combat::ATTACK_LUNGE_PECK, "dist 28 -> peck");
        t.assert(g.combat.attack.moveType, MOVE_LUNGE, "peck cache moveType");
        t.assert(g.monster.t, 18, "peck tell 18");
        suite.addTest(t);
    }

    {
        Test t("MONSTER_DEFS roster matches the demo contract");
        t.assert(MONSTER_DEFS[0].kind, MON_LUNGE, "lunge kind");
        t.assert(MONSTER_DEFS[0].w, 32, "lunge w");
        t.assert(MONSTER_DEFS[0].h, 24, "lunge h");
        t.assert(MONSTER_DEFS[0].hp, 200, "lunge hp");
        t.assert(MONSTER_DEFS[0].spd, 5, "lunge spd");
        t.assert(MONSTER_DEFS[0].atkDist, 32, "lunge atkDist");
        t.assert(MONSTER_DEFS[1].kind, MON_SWEEP, "sweep kind");
        t.assert(MONSTER_DEFS[1].w, 28, "sweep w");
        t.assert(MONSTER_DEFS[1].h, 22, "sweep h");
        t.assert(MONSTER_DEFS[1].hp, 150, "sweep hp");
        t.assert(MONSTER_DEFS[1].spd, 7, "sweep spd");
        t.assert(MONSTER_DEFS[1].atkDist, -1, "sweep atkDist never lunges");
        t.assert(MONSTER_DEFS[2].kind, MON_HEAVY, "heavy kind");
        t.assert(MONSTER_DEFS[2].w, 40, "heavy w");
        t.assert(MONSTER_DEFS[2].h, 28, "heavy h");
        t.assert(MONSTER_DEFS[2].hp, 320, "heavy hp");
        t.assert(MONSTER_DEFS[2].spd, 3, "heavy spd");
        t.assert(MONSTER_DEFS[2].atkDist, 24, "heavy atkDist");
        suite.addTest(t);
    }

    {
        Test t("initMonster(kind): box + stats from blob (migration B), legacy fields intact");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT, MON_SWEEP);
        Monster &m = g.monster;
        t.assert(g.monsterKind, MON_SWEEP, "kind recorded");
        t.assert(m.w, 28, "sweep width");
        t.assert(m.h, 22, "sweep height");
        t.assert(g.combat.body.w, 28, "sweep cached box w");
        t.assert(g.combat.body.h, 22, "sweep cached box h");
        t.assert(g.combat.body.ox, 0, "sweep cached box ox");
        t.assert(g.combat.body.oy, 0, "sweep cached box oy");
        t.assert(m.hp, 150, "sweep hp");
        t.assert(m.hpMax, 150, "sweep hpMax");
        t.assert(m.spd, 7, "sweep pursue speed");
        t.assert(m.x, 200, "spawn x");
        t.assert(m.y, 40, "spawn y");
        t.assert(m.t, 90, "idle timer 90");
        t.assert(m.cd, 140, "cooldown 140");
        t.assert(m.fx, -fp::FP, "faces W");
        t.assert(m.fy, 0, "facing flat");
        t.assert(m.circleDir, 1, "circle dir right");
        t.assert(m.state, MS_IDLE, "starts idle");
        // nch.9: the bull authors a legs/hooves collide box (1,14,26,8), so the
        // synced hurt box is that rect, not the body box.
        t.assert(g.target.rect.w, combat_expect::CREATURE_SWEEP_COLLIDE_W, "hurt box synced from collide");
        t.assert(g.target.rect.h, combat_expect::CREATURE_SWEEP_COLLIDE_H, "hurt box height from collide");
        t.assert(g.target.rect.x, m.x + combat_expect::CREATURE_SWEEP_COLLIDE_OX, "hurt box x at collide origin");
        t.assert(g.target.rect.y, m.y + combat_expect::CREATURE_SWEEP_COLLIDE_OY, "hurt box y at collide origin");

        Game g2;
        newGame(g2, W_SWORD, MODE_HUNT, MON_HEAVY);
        t.assert(g2.monsterKind, MON_HEAVY, "heavy kind recorded");
        t.assert(g2.monster.w, 40, "heavy width");
        t.assert(g2.monster.h, 28, "heavy height");
        t.assert(g2.combat.body.w, 40, "heavy cached box w");
        t.assert(g2.combat.body.h, 28, "heavy cached box h");
        t.assert(g2.monster.hp, 320, "heavy hp");
        t.assert(g2.monster.spd, 5, "heavy speed");

        Game g3;
        newGame(g3, W_SWORD, MODE_HUNT);   // default kind 0 = legacy beast
        t.assert(g3.monsterKind, MON_LUNGE, "default kind 0");
        t.assert(g3.monster.w, 32, "legacy width");
        t.assert(g3.combat.body.w, 32, "legacy cached box w");
        t.assert(g3.combat.body.h, 24, "legacy cached box h");
        t.assert(g3.monster.hp, 200, "legacy hp");
        t.assert(g3.monster.spd, 6, "legacy speed");
        suite.addTest(t);
    }

    {
        Test t("body box is creature w/h; player hits route through it");
        Game g;
        newHunt(g);
        // Cached box == the blob's creature w/h at origin for this creature.
        CombatBox expected;
        t.assert(combatCreatureBodyBox(monsterCreatureId(MON_LUNGE), expected), 1, "lunge body box readable");
        t.assert(g.combat.body.w, expected.w, "cached box w from creature");
        t.assert(g.combat.body.h, expected.h, "cached box h from creature");
        t.assert(g.combat.body.ox, 0, "cached box ox at origin");
        t.assert(g.combat.body.oy, 0, "cached box oy at origin");
        // A landed melee hit resolves the implicit body; the body multiplier is
        // always 100, so the damage equals the raw attack damage.
        const CombatBodyHit r = combatResolveBodyHit(g, 12);
        t.assert(r.zone, COMBAT_NO_ZONE, "resolved body (no zone)");
        t.assert(r.mul, 100, "body multiplier neutral");
        t.assert(r.dmg, 12, "body damage unchanged");
        suite.addTest(t);
    }

    {
        Test t("chooseAttack variants: BULL stomps/gores, HEAVY spins inside 30, CHICKEN pecks/leaps");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT, MON_SWEEP);
        Monster &m = g.monster;
        chooseAttack(g, 24);
        t.assert(m.atkIdx, combat::ATTACK_SWEEP_STOMP, "bull stomps at 24");
        chooseAttack(g, 0);
        t.assert(m.atkIdx, combat::ATTACK_SWEEP_STOMP, "bull stomps at 0");
        chooseAttack(g, 25);
        t.assert(m.atkIdx, combat::ATTACK_SWEEP_GORE, "bull gores at 25");
        chooseAttack(g, 41);
        t.assert(m.atkIdx, combat::ATTACK_SWEEP_GORE, "bull gores at 41");
        Game g2;
        newGame(g2, W_SWORD, MODE_HUNT, MON_HEAVY);
        Monster &h = g2.monster;
        Player &p2 = g2.player;
        h.x = 100;
        h.y = 40;
        h.fx = -fp::FP;   // faces W
        h.fy = 0;
        p2.y = static_cast<int16_t>(h.y + (h.h >> 1) - (p2.h >> 1));
        // feel.10 bands in front (west of the beast): 0..20 opens the bite/spin
        // combo, 21..30 the pure spin, 31+ the bite.
        p2.x = static_cast<int16_t>(h.x - 60);
        h.fx = -fp::FP;
        chooseAttack(g2, 41);
        t.assert(h.atkIdx, combat::ATTACK_HEAVY_BITE, "heavy bites at 41 (front)");
        h.fx = -fp::FP;
        chooseAttack(g2, 31);
        t.assert(h.atkIdx, combat::ATTACK_HEAVY_BITE, "heavy bites at 31 (front)");
        h.fx = -fp::FP;
        chooseAttack(g2, 30);
        t.assert(h.atkIdx, combat::ATTACK_HEAVY_TAIL_SPIN, "heavy spins at 30 (front)");
        h.fx = -fp::FP;
        chooseAttack(g2, 21);
        t.assert(h.atkIdx, combat::ATTACK_HEAVY_TAIL_SPIN, "heavy spins at 21 (front)");
        h.fx = -fp::FP;
        chooseAttack(g2, 20);
        t.assert(h.atkIdx, combat::ATTACK_HEAVY_BITE, "heavy opens bite_spin at 20 (front)");
        t.assert(g2.combat.patternIdx, combat::PATTERN_HEAVY_P_BITE_SPIN, "bite_spin pattern selected");
        h.fx = -fp::FP;
        chooseAttack(g2, 0);
        t.assert(h.atkIdx, combat::ATTACK_HEAVY_BITE, "heavy opens bite_spin at 0 (front)");
        // Behind (east of the beast): the slam pounces at 16..64 (feel.15), else
        // the base bands still apply.
        p2.x = static_cast<int16_t>(h.x + 60);
        h.fx = -fp::FP;
        chooseAttack(g2, 40);
        t.assert(h.atkIdx, combat::ATTACK_HEAVY_TAIL_SLAM, "heavy slams a flank at 40");
        h.fx = -fp::FP;
        chooseAttack(g2, 64);
        t.assert(h.atkIdx, combat::ATTACK_HEAVY_TAIL_SLAM, "heavy slams a flank at 64");
        h.fx = -fp::FP;
        chooseAttack(g2, 65);
        t.assert(h.atkIdx, combat::ATTACK_HEAVY_BITE, "heavy bites past the slam band");
        h.fx = -fp::FP;
        chooseAttack(g2, 16);
        t.assert(h.atkIdx, combat::ATTACK_HEAVY_TAIL_SLAM, "heavy slams at the new floor 16");
        h.fx = -fp::FP;
        chooseAttack(g2, 15);
        t.assert(h.atkIdx, combat::ATTACK_HEAVY_BITE, "heavy bites inside the slam floor");
        Game g3;
        newGame(g3, W_SWORD, MODE_HUNT, MON_LUNGE);
        chooseAttack(g3, 41);
        t.assert(g3.monster.atkIdx, combat::ATTACK_LUNGE_LEAP, "chicken leaps at 41");
        chooseAttack(g3, 29);
        t.assert(g3.monster.atkIdx, combat::ATTACK_LUNGE_LEAP, "chicken leaps at 29");
        chooseAttack(g3, 28);
        t.assert(g3.monster.atkIdx, combat::ATTACK_LUNGE_PECK, "chicken pecks at 28");
        chooseAttack(g3, 0);
        t.assert(g3.monster.atkIdx, combat::ATTACK_LUNGE_PECK, "chicken pecks at 0");
        suite.addTest(t);
    }

    {
        // feel.9: the bull's rear_kick (behind guard, first pattern) answers a
        // close flank; the low-HP gore2 combo arms a second charge at any range.
        Test t("chooseAttack bull (feel.9): rear_kick flank, gore2 enrage combo");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT, MON_SWEEP);
        Monster &m = g.monster;
        Player &p = g.player;
        m.x = 100;
        m.y = 40;
        m.hpMax = 150;
        m.hp = 150;
        m.fx = fp::FP;
        m.fy = 0;
        p.y = static_cast<int16_t>(m.y + (m.h >> 1) - (p.h >> 1));
        // Close and in front: stomp.
        p.x = static_cast<int16_t>(m.x + 20);
        chooseAttack(g, 10);
        t.assert(m.atkIdx, combat::ATTACK_SWEEP_STOMP, "bull stomps a close front");
        // Close and behind: the anti-flank kick wins source order.
        p.x = static_cast<int16_t>(m.x - 20);
        chooseAttack(g, 10);
        t.assert(m.atkIdx, combat::ATTACK_SWEEP_REAR_KICK, "bull rear_kicks a close flank");
        // At exactly 40% hp the combo opens at every range: close front now
        // gores (covering the band the broken hooves disable) and arms step 1.
        m.hp = 60;
        p.x = static_cast<int16_t>(m.x + 20);
        chooseAttack(g, 10);
        t.assert(m.atkIdx, combat::ATTACK_SWEEP_GORE, "enraged bull opens the gore combo");
        t.assert(g.combat.patternIdx, combat::PATTERN_SWEEP_P_GORE2, "gore2 pattern selected");
        t.assert(g.combat.stepIdx, 1, "combo armed its second step");
        t.assert(g.combat.stepT, 18, "combo waits 18 ticks before step 1");
        // 41% hp falls back to the single gore / stomp bands.
        m.hp = 62;
        chooseAttack(g, 10);
        t.assert(m.atkIdx, combat::ATTACK_SWEEP_STOMP, "41% hp keeps the close stomp");
        chooseAttack(g, 40);
        t.assert(m.atkIdx, combat::ATTACK_SWEEP_GORE, "41% hp gores at range");
        suite.addTest(t);
    }

    {
        // nch.4/feel.10/feel.15: heavy profile.faceHold 10 commits the tracked
        // facing; the hunter can cross behind and a from-behind hit lands the
        // appendage/tail. turnRate is pinned to 0 here so this test isolates the
        // faceHold cadence; the shipped turnRate 1 is exercised by the
        // "turnRate: real-data chicken" reachability test below.
        Test t("heavy faceHold: facing stale for faceHold ticks, flank hit lands the tail");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT, MON_HEAVY);
        Monster &m = g.monster;
        Player &p = g.player;
        t.assert(g.combat.profile.faceHold, 10, "heavy faceHold 10 (feel.15)");
        t.assert(g.combat.appendZone != COMBAT_NO_ZONE, true, "heavy appendage zone loaded");
        g.combat.profile.turnRate = 0;   // isolate the faceHold cadence
        // Beast parked in PURSUE (never chooses), hunter due east -> facing E.
        m.state = MS_PURSUE;
        m.cd = 30000;
        m.x = 80;
        m.y = 40;
        p.x = 110;
        p.y = static_cast<int16_t>(m.y + (m.h >> 1) - (p.h >> 1));
        updateMonster(g);
        t.assert(m.fx, fp::FP, "facing E after the first refresh");
        t.assert(m.fy, 0, "level E");
        t.assert(m.faceT, 9, "faceHold countdown armed (10 set, decremented)");
        // Hunter crosses behind (west); facing stays E for the rest of the hold.
        p.x = 40;
        p.y = static_cast<int16_t>(m.y + (m.h >> 1) - (p.h >> 1));
        for (int i = 0; i < 9; i++)
            updateMonster(g);
        t.assert(m.fx, fp::FP, "facing stale through the full hold");
        t.assert(m.faceT, 0, "countdown reached zero");
        // From-behind hit under the stale E facing: the tail box (ox -24)
        // rotates to the west side and wins the higher multiplier.
        const int16_t hx = static_cast<int16_t>(m.x - 12);
        const int16_t hy = static_cast<int16_t>(m.y + (m.h >> 1));
        const CombatBodyHit behind = combatZoneHitResolveAt(g, 10, PHYS_SLASH, hx, hy, m.x, m.y, m.fx, m.fy);
        t.assert(behind.zone, COMBAT_ZONE_APPENDAGE, "from-behind hit lands the tail zone");
        // The next tick refreshes facing W, rotating the tail back in front.
        updateMonster(g);
        t.assert(m.fx, -fp::FP, "facing refreshed W after faceHold ticks");
        const CombatBodyHit front = combatZoneHitResolveAt(g, 10, PHYS_SLASH, hx, hy, m.x, m.y, m.fx, m.fy);
        t.assert(front.zone, COMBAT_NO_ZONE, "same world point is body once the tail rotates");
        suite.addTest(t);
    }

    {
        // feel.14: profile.turnRate bounds how far the refreshed tracked facing
        // may rotate each faceHold window. 0 snaps (legacy), N steps at most N
        // DIR8 steps along the shortest arc (mod 8). feel.15 authors real rates,
        // so this test pins the shipped heavy value then drives 0/1 synthetically.
        Test t("turnRate: 0 snaps, 1 rotates 45 deg per refresh and wraps");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT, MON_HEAVY);
        Monster &m = g.monster;
        Player &p = g.player;
        t.assert(g.combat.profile.turnRate, 1, "shipped heavy turnRate 1 (feel.15)");
        m.state = MS_PURSUE;
        m.cd = 30000;
        m.spd = 0;   // frozen position: only facing under test
        m.x = 100;
        m.y = 40;
        const int16_t cx = static_cast<int16_t>(m.x + (m.w >> 1));
        const int16_t cy = static_cast<int16_t>(m.y + (m.h >> 1));
        // (a) turnRate 0 snaps the full way to the desired index.
        g.combat.profile.turnRate = 0;
        m.fx = 11;
        m.fy = -11;   // NE
        p.x = static_cast<int16_t>(cx + 60 - (p.w >> 1));
        p.y = static_cast<int16_t>(cy - (p.h >> 1));
        m.faceT = 0;
        updateMonster(g);
        t.assert(m.fx, fp::FP, "turnRate 0 snaps E x");
        t.assert(m.fy, 0, "turnRate 0 snaps E y");
        // (b) turnRate 1 steps one DIR8 notch per refresh toward the player.
        g.combat.profile.turnRate = 1;
        m.fx = fp::FP;
        m.fy = 0;   // E
        p.x = static_cast<int16_t>(cx - (p.w >> 1));
        p.y = static_cast<int16_t>(cy + 60 - (p.h >> 1));   // due south -> desired S (2)
        m.faceT = 0;
        updateMonster(g);
        t.assert(m.fx, 11, "one 45-deg step toward S: SE x");
        t.assert(m.fy, 11, "one 45-deg step toward S: SE y");
        // The faceHold cadence is unchanged: a non-refresh tick does not rotate.
        updateMonster(g);
        t.assert(m.fx, 11, "non-refresh tick holds SE x");
        t.assert(m.fy, 11, "non-refresh tick holds SE y");
        m.faceT = 0;
        updateMonster(g);
        t.assert(m.fx, 0, "second step reaches S x");
        t.assert(m.fy, 16, "second step reaches S y");
        // Wrap the short way across index 0/7: NE (7) -> E (0) is +1, not -7.
        m.fx = 11;
        m.fy = -11;   // NE (7)
        p.x = static_cast<int16_t>(cx + 60 - (p.w >> 1));
        p.y = static_cast<int16_t>(cy - (p.h >> 1));   // E (0)
        m.faceT = 0;
        updateMonster(g);
        t.assert(m.fx, fp::FP, "NE wraps forward to E x");
        t.assert(m.fy, 0, "NE wraps forward to E y");
        // And the other way: E (0) -> NE (7) is -1, not +7.
        m.fx = fp::FP;
        m.fy = 0;   // E (0)
        p.x = static_cast<int16_t>(cx + 40 - (p.w >> 1));
        p.y = static_cast<int16_t>(cy - 40 - (p.h >> 1));   // NE (7)
        m.faceT = 0;
        updateMonster(g);
        t.assert(m.fx, 11, "E wraps backward to NE x");
        t.assert(m.fy, -11, "E wraps backward to NE y");
        // (c) desired == current leaves the facing untouched.
        m.fx = fp::FP;
        m.fy = 0;   // E
        p.x = static_cast<int16_t>(cx + 60 - (p.w >> 1));
        p.y = static_cast<int16_t>(cy - (p.h >> 1));   // E (0)
        m.faceT = 0;
        updateMonster(g);
        t.assert(m.fx, fp::FP, "no-op keeps E x");
        t.assert(m.fy, 0, "no-op keeps E y");
        suite.addTest(t);
    }

    {
        // feel.15 real-data reachability: chicken faceHold 6 / turnRate 1 means
        // the tracked heading only rotates one 45-deg notch per 6-tick window, so
        // a hunter who circles the beast at ~16-20 px can out-run the turn and
        // reach behind (facingDot < 0) well inside a 24-tick approach.
        Test t("turnRate: chicken faceHold 6 / turnRate 1 lets a circling hunter reach behind");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT, MON_LUNGE);
        Monster &m = g.monster;
        Player &p = g.player;
        t.assert(g.combat.profile.faceHold, 6, "chicken faceHold 6 (feel.15)");
        t.assert(g.combat.profile.turnRate, 1, "chicken turnRate 1 (feel.15)");
        m.state = MS_PURSUE;
        m.cd = 30000;
        m.spd = 0;   // frozen beast: only the facing track is under test
        m.x = 100;
        m.y = 40;
        const int16_t cx = static_cast<int16_t>(m.x + (m.w >> 1));
        const int16_t cy = static_cast<int16_t>(m.y + (m.h >> 1));
        const int16_t R = 18;   // circling radius: inside the 16-20 px target
        m.fx = fp::FP;
        m.fy = 0;   // start facing E, hunter due east (in front)
        m.faceT = 0;
        int8_t idx = 0;
        int behindTick = -1;
        for (int tick = 0; tick < 24; tick++) {
            const int16_t ox = static_cast<int16_t>((fp::dir8X(idx) * R) >> 4);
            const int16_t oy = static_cast<int16_t>((fp::dir8Y(idx) * R) >> 4);
            p.x = static_cast<int16_t>(cx + ox - (p.w >> 1));
            p.y = static_cast<int16_t>(cy + oy - (p.h >> 1));
            updateMonster(g);
            const int16_t dot = combatFacingDot(static_cast<int16_t>(cx + ox), static_cast<int16_t>(cy + oy), cx, cy, m.fx, m.fy);
            if (behindTick < 0 && dot < 0)
                behindTick = tick;
            idx = static_cast<int8_t>((idx + 1) & 7);
        }
        t.assert(behindTick >= 0, true, "circling hunter reaches behind (dot < 0)");
        t.assert(behindTick, 3, "behind reached at tick 3 of the circle");
        t.assertLessThan(behindTick, 24, "behind reached inside 24 ticks");
        suite.addTest(t);
    }

    {
        // feel.14: with faceHold 0 the facing recomputes every tick, so a bounded
        // turnRate also applies every tick (still one step per refresh).
        Test t("turnRate: faceHold 0 still bounds the per-tick rotation");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT, MON_HEAVY);
        Monster &m = g.monster;
        Player &p = g.player;
        m.state = MS_PURSUE;
        m.cd = 30000;
        m.spd = 0;
        m.x = 100;
        m.y = 40;
        g.combat.profile.faceHold = 0;
        g.combat.profile.turnRate = 1;
        m.fx = fp::FP;
        m.fy = 0;   // E
        p.x = static_cast<int16_t>(m.x + (m.w >> 1) - (p.w >> 1));
        p.y = static_cast<int16_t>(m.y + (m.h >> 1) + 60 - (p.h >> 1));   // S (2)
        updateMonster(g);
        t.assert(m.fx, 11, "faceHold 0 one step toward S: SE x");
        t.assert(m.fy, 11, "faceHold 0 one step toward S: SE y");
        updateMonster(g);
        t.assert(m.fx, 0, "next tick steps again to S x");
        t.assert(m.fy, 16, "next tick steps again to S y");
        suite.addTest(t);
    }

    {
        // feel.14: a lock-at-windup attack freezes its entry facing even when the
        // profile has a turn rate (the hunter can out-circle it).
        Test t("turnRate: lock-at-windup attack still freezes its facing");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT, MON_SWEEP);
        Monster &m = g.monster;
        Player &p = g.player;
        g.combat.profile.turnRate = 2;
        chooseAttack(g, 40);   // gore (lock-at-windup) at range
        t.assert(m.atkIdx, combat::ATTACK_SWEEP_GORE, "gore selected");
        t.assert(g.combat.attack.facing, COMBAT_FACING_LOCK, "gore is lock-at-windup");
        m.fx = fp::FP;
        m.fy = 0;                               // windup-entry facing E
        p.x = static_cast<int16_t>(m.x - 60);   // hunter crosses behind
        p.y = m.y;
        m.t = 1;
        beast(g, 1);   // release into attack
        t.assert(m.state, MS_ATTACK, "released into attack");
        t.assert(m.fx, fp::FP, "locked facing holds through windup");
        t.assert(m.fy, 0, "locked facing holds flat");
        beast(g, 1);
        t.assert(m.fx, fp::FP, "locked facing holds through attack");
        t.assert(m.fy, 0, "locked facing holds through attack (flat)");
        suite.addTest(t);
    }

    {
        Test t("heavy tail_spin: lock-away turns the back at windup, frozen after");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT, MON_HEAVY);
        Monster &m = g.monster;
        Player &p = g.player;
        // Hunter due east of the beast centre: the tracked vector is +E, then
        // lock-away negates it once at windup entry so the tail (window 0 behind
        // the turned-away back) points at the hunter. feel.10: the pure spin now
        // owns 21..30 px (the <=20 band opens the bite/spin combo). feel.15:
        // heavy turnRate 1 would take four windows to swing W->E, so start the
        // facing already on the hunter (front) and let the lock do the work.
        m.x = 80;
        m.y = 40;
        m.fx = fp::FP;
        m.fy = 0;
        p.x = 120;
        p.y = static_cast<int16_t>(m.y + (m.h >> 1) - (p.h >> 1));
        p.iT = 0;
        m.state = MS_PURSUE;
        m.cd = 0;
        updateMonster(g);
        t.assert(m.atkIdx, combat::ATTACK_HEAVY_TAIL_SPIN, "spin selected inside 21..30");
        t.assert(m.state, MS_WINDUP, "windup entered");
        t.assert(m.fx, -fp::FP, "turned away from the hunter");
        t.assert(m.fy, 0, "level turn-away");

        // Release into attack, then move the hunter behind the beast: the
        // lock-away facing stays put through WINDUP + ATTACK.
        m.t = 1;
        beast(g, 1);
        t.assert(m.state, MS_ATTACK, "released into attack");
        p.x = 0;
        p.y = 0;
        beast(g, 1);
        t.assert(m.fx, -fp::FP, "facing frozen through attack");
        suite.addTest(t);
    }

    {
        Test t("heavy tail_spin: lock-away window hit knocks the hunter radially away");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT, MON_HEAVY);
        Monster &m = g.monster;
        Player &p = g.player;
        m.x = 100;
        m.y = 40;   // centre (120,54)
        m.fx = -fp::FP;
        m.fy = 0;   // turned away; window 0 (ox -20) rotates onto the hunter side
        monsterAttackSet(g, combat::ATTACK_HEAVY_TAIL_SPIN);
        m.state = MS_ATTACK;
        m.t = 0;
        p.x = 130;
        p.y = 46;
        p.iT = 0;
        const uint8_t hp0 = p.hp;
        updateMonster(g);
        t.assertLessThan(p.hp, hp0, "tail window hit lands");
        t.assertGreaterThan(p.vx, 0, "radial knock pushes east, away from the beast");
        suite.addTest(t);
    }

    {
        Test t("withWeapon/resetHunt preserve the chosen beast kind");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT, MON_HEAVY);
        withWeapon(g, W_FLAIL);
        t.assert(g.monsterKind, MON_HEAVY, "swap keeps kind");
        t.assert(g.monster.w, 40, "swap keeps heavy body");
        t.assert(g.monster.hp, 320, "swap keeps heavy hp");
        resetHunt(g);
        t.assert(g.monsterKind, MON_HEAVY, "reset keeps kind");
        t.assert(g.monster.hp, 320, "reset keeps heavy hp");
        suite.addTest(t);
    }

    {
        Test t("chooseAttack caches the shipped window scalars (migration A)");
        Game g;
        newHunt(g);
        chooseAttack(g, 33);
        const CombatAttackCache &ac = g.combat.attack;
        t.assert(g.monster.winRemain, 0, "leap declares one window");
        t.assert(ac.windup, 30, "cache windup");
        t.assert(ac.active, 12, "cache active");
        t.assert(ac.recover, 52, "cache recover");
        t.assert(ac.dmg, 13, "cache dmg");
        t.assert(ac.moveSpeedF, 48, "cache speedF");
        t.assert(ac.winIdx, combat::WINDOW_LUNGE_LEAP_0, "cache first window");
        t.assert(ac.win.t0, 0, "window t0");
        t.assert(ac.win.t1, 10, "window t1");
        t.assert(ac.win.box.ox, 12, "window ox");
        t.assert(ac.win.box.oy, -2, "window oy");
        t.assert(ac.win.box.w, 18, "window w");
        t.assert(ac.win.box.h, 16, "window h");
        t.assert(ac.win.dmgMul, 100, "window dmgMul");

        chooseAttack(g, 28);
        t.assert(g.combat.attack.winIdx, combat::WINDOW_LUNGE_PECK_0, "peck window cached");
        t.assert(g.combat.attack.win.box.ox, 14, "peck window ox");
        t.assert(g.combat.attack.win.box.oy, -6, "peck window oy");
        t.assert(g.combat.attack.win.box.w, 12, "peck window w");
        t.assert(g.combat.attack.win.box.h, 10, "peck window h");
        t.assert(g.combat.attack.dmg, 7, "peck dmg cached");
        suite.addTest(t);
    }

    {
        Test t("hit test consumes the cached window (switch flips miss to hit)");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        monsterAttackSet(g, combat::ATTACK_LUNGE_PECK);
        m.x = 0;
        m.y = 0;
        m.fx = 16;
        m.fy = 0;   // centre (16,12); peck box (14,-6) -> x24..36
        g.player.x = 36;
        g.player.y = 4;
        g.player.w = 16;
        g.player.h = 16;
        t.assert(monsterHitsPlayer(g), 0, "peck window misses at x36");
        attackWindowLoad(g, combat::WINDOW_LUNGE_LEAP_0);   // leap box (12,-2) -> x19..37
        t.assert(monsterHitsPlayer(g), 1, "wider cached window hits at x36");
        suite.addTest(t);
    }

    {
        Test t("multi-window refresh path: next contiguous window reloads once");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        monsterAttackSet(g, combat::ATTACK_LUNGE_PECK);
        t.assert(m.winRemain, 0, "single-window attack has no pending window");
        m.winRemain = 1;   // synthetic multi-window state
        m.t = 11;          // past the peck window t1 == 6
        monsterWindowNext(g);
        t.assert(g.combat.attack.winIdx, combat::WINDOW_LUNGE_LEAP_0, "next window loaded");
        t.assert(g.combat.attack.win.box.ox, 12, "next window box swapped");
        t.assert(m.winRemain, 0, "pending window consumed");
        monsterWindowNext(g);
        t.assert(g.combat.attack.winIdx, combat::WINDOW_LUNGE_LEAP_0, "no further reload");
        suite.addTest(t);
    }

    {
        Test t("windup tell counts down 30 ticks, then attack starts");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        monsterAttackSet(g, combat::ATTACK_LUNGE_LEAP);
        m.state = MS_WINDUP;
        m.t = 30;
        m.windupMax = 30;
        beast(g, 29);
        t.assert(m.state, MS_WINDUP, "still winding up at 29");
        t.assert(m.t, 1, "one tell tick left");
        beast(g, 1);
        t.assert(m.state, MS_ATTACK, "tell released into attack");
        t.assert(m.t, 0, "attack starts at t 0");
        t.assert(m.fx, -fp::FP, "faced west at release");
        t.assert(m.lvx, -48, "leap velocity int (-16*48)>>4");
        t.assert(m.lvy, 0, "flat leap");
        suite.addTest(t);
    }

    {
        Test t("peck recovery window closes, cd = 48 + tick%60, circle flips");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        monsterAttackSet(g, combat::ATTACK_LUNGE_PECK);
        m.state = MS_ATTACK;
        m.t = 0;
        m.x = 10;
        m.y = 10;      // far from player: no contact
        hunt(g, 32);   // peck active 6 + recover 26
        t.assert(m.state, MS_ATTACK, "still active+recovering at 32");
        hunt(g, 1);
        t.assert(m.state, MS_PURSUE, "attack releases to pursue");
        t.assert(m.cd, 48 + (g.tick % 60), "post-attack cooldown");
        t.assert(m.circleDir, (g.tick % 2) ? 1 : -1, "circle direction flips");
        suite.addTest(t);
    }

    {
        Test t("recovery state releases to pursue with cd 55");
        Game g;
        newHunt(g);
        g.monster.state = MS_RECOVER;
        g.monster.t = 3;
        beast(g, 3);
        t.assert(g.monster.state, MS_PURSUE, "recover -> pursue");
        t.assert(g.monster.cd, 48, "short cooldown after recover");
        suite.addTest(t);
    }

    {
        Test t("stun ends into recover 24, then pursue cd 55");
        Game g;
        newHunt(g);
        g.monster.stun = 5;
        beast(g, 5);
        t.assert(g.monster.stun, 0, "stun drained");
        t.assert(g.monster.state, MS_RECOVER, "stun -> recover");
        t.assert(g.monster.t, 24, "recover window 24");
        beast(g, 24);
        t.assert(g.monster.state, MS_PURSUE, "recover -> pursue after 24");
        t.assert(g.monster.cd, 48, "recover cooldown 48");
        suite.addTest(t);
    }

    {
        Test t("crit zone: facing-side hit is x1.4 (integer 14/10)");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        m.x = 200;
        m.y = 40;
        m.fx = -16;
        m.fy = 0;   // center (216,52), facing W
        m.hp = 200;
        g.freeze = 0;
        damageMonster(g, 10, 210, 52);   // west of center: proj +6
        t.assert(m.hp, 186, "crit 10 -> 14");
        t.assert(m.hitFlash, 4, "hit flash 4");
        t.assert(g.freeze, 6, "crit freeze 6");

        m.hp = 200;
        g.freeze = 0;
        damageMonster(g, 10, 220, 52);   // east of center: proj -4
        t.assert(m.hp, 190, "no crit 10 -> 10");
        t.assert(g.freeze, 4, "normal freeze 4");

        m.fx = 16;
        m.hp = 200;   // facing E, same side flips
        damageMonster(g, 10, 222, 52);
        t.assert(m.hp, 186, "head side of new facing crits");

        m.fx = -16;
        m.hp = 200;
        damageMonster(g, 0, 220, 52);   // floor at 1 even for 0 dmg
        t.assert(m.hp, 199, "damage floors at 1");
        suite.addTest(t);
    }

    {
        Test t("hp <= 0 -> dead + over win + long freeze");
        Game g;
        newHunt(g);
        g.monster.hp = 5;
        damageMonster(g, 10, 220, 52);
        t.assert(g.monster.hp, 0, "hp clamped to 0");
        t.assert(g.monster.state, MS_DEAD, "dead state");
        t.assert(g.over, OVER_WIN, "hunt won");
        t.assert(g.freeze, 12, "death freeze 12");
        suite.addTest(t);
    }

    {
        Test t("lunge velocities are integer from 8-dir facing");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        monsterAttackSet(g, combat::ATTACK_LUNGE_LEAP);
        m.fx = 16;
        m.fy = 0;
        startMonsterAttack(g);
        t.assert(m.state, MS_ATTACK, "attack entered");
        t.assert(m.lvx, 48, "E leap 48");
        t.assert(m.lvy, 0, "E leap flat");
        m.fx = 11;
        m.fy = 11;
        startMonsterAttack(g);
        t.assert(m.lvx, 33, "SE leap integer trunc (11*48)>>4");
        t.assert(m.lvy, 33, "SE leap y");
        monsterAttackSet(g, combat::ATTACK_HEAVY_TAIL_SPIN);
        m.fx = 16;
        m.fy = 0;
        startMonsterAttack(g);
        t.assert(m.lvx, 0, "stationary attack (tail_spin)");
        t.assert(m.lvy, 0, "stationary attack y");
        suite.addTest(t);
    }

    {
        Test t("lunge moves via addVel with sub-pixel carry");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        monsterAttackSet(g, combat::ATTACK_LUNGE_LEAP);
        m.state = MS_ATTACK;
        m.t = 0;
        m.lvx = 34;
        m.lvy = 0;
        m.x = 10;
        m.y = 10;
        m.subX = 0;
        m.subY = 0;
        updateMonster(g);
        t.assert(m.x, 12, "34/16 px moved");
        t.assert(m.subX, 2, "sub-pixel remainder carried");
        suite.addTest(t);
    }

    {
        // feel.7/feel.10: move.type hop commits a face-relative (forward, lateral)
        // velocity at release, rotated into the world frame by the current
        // facing. heavy.tail_slam ships hop (the real-data release is tested
        // below); this test installs synthetic move scalars in the RAM cache and
        // calls the release directly to pin the rotation math.
        Test t("hop: release rotates the face-relative dx/dy into lvx/lvy");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        g.combat.attack.moveType = MOVE_HOP;
        g.combat.attack.moveDx = 12;   // forward 0.75 px/tick
        g.combat.attack.moveDy = 6;    // lateral 0.375 px/tick
        m.fx = 16;
        m.fy = 0;   // E: forward +x, lateral +y
        startMonsterAttack(g);
        t.assert(m.state, MS_ATTACK, "hop enters attack");
        t.assert(m.lvx, 12, "E hop forward x");
        t.assert(m.lvy, 6, "E hop lateral y");
        m.fx = 0;
        m.fy = 16;   // S: forward +y, lateral -x
        startMonsterAttack(g);
        t.assert(m.lvx, -6, "S hop lateral x");
        t.assert(m.lvy, 12, "S hop forward y");
        suite.addTest(t);
    }

    {
        // The hop fires m.lvx/lvy through addVel on t <= active only: the
        // velocity stops at the active boundary and the sub-pixel accumulator
        // freezes through recovery (no drift).
        Test t("hop: moves along the vector while active, no drift after");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        g.combat.attack.moveType = MOVE_HOP;
        g.combat.attack.moveDx = 12;
        g.combat.attack.moveDy = 0;
        g.combat.attack.active = 3;
        g.combat.attack.recover = 5;
        m.fx = 16;
        m.fy = 0;
        startMonsterAttack(g);
        m.x = 100;
        m.y = 20;
        m.subX = 0;
        m.subY = 0;
        g.player.x = 10;
        g.player.y = 10;   // clear of the beast: no shove/hit interference
        beast(g, 3);
        t.assert(m.x, 102, "12*3/16 = 2 px travelled");
        t.assert(m.subX, 4, "sub-pixel remainder after the active phase");
        t.assert(m.state, MS_ATTACK, "still in attack after active");
        beast(g, 5);   // recovery ticks = active + recover
        t.assert(m.x, 102, "no drift after the last active tick");
        t.assert(m.subX, 4, "remainder frozen through recovery");
        t.assert(m.y, 20, "no lateral movement");
        t.assert(m.state, MS_ATTACK, "still attack exactly at active+recover");
        // South facing: the same forward velocity (12,0) rotates to (0,12).
        m.fx = 0;
        m.fy = 16;
        startMonsterAttack(g);
        m.x = 100;
        m.y = 20;
        m.subX = 0;
        m.subY = 0;
        beast(g, 3);
        t.assert(m.y, 22, "S 12*3/16 = 2 px travelled");
        t.assert(m.subY, 4, "S sub-pixel remainder");
        t.assert(m.x, 100, "S no sideways movement");
        suite.addTest(t);
    }

    {
        // Regression guard: the none/lunge release paths are unchanged by hop.
        Test t("hop: none and lunge release paths unchanged");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        g.combat.attack.moveType = MOVE_NONE;
        m.fx = 16;
        m.fy = 0;
        startMonsterAttack(g);
        t.assert(m.state, MS_ATTACK, "none enters attack");
        t.assert(m.lvx, 0, "none lvx zero");
        t.assert(m.lvy, 0, "none lvy zero");
        monsterAttackSet(g, combat::ATTACK_LUNGE_LEAP);
        m.fx = 16;
        m.fy = 0;
        startMonsterAttack(g);
        t.assert(m.lvx, 48, "lunge leap 48 unchanged");
        t.assert(m.lvy, 0, "lunge leap flat unchanged");
        suite.addTest(t);
    }

    {
        // feel.10: heavy.tail_slam is the first shipped hop. With lock-at-windup
        // facing away from a flanking hunter, the face-relative dx -56 releases
        // backward (toward the hunter) and travels 8 active ticks * 3.5 px = 28 px.
        Test t("tail_slam hop: backward vector pounces 28 px toward the flank");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT, MON_HEAVY);
        Monster &m = g.monster;
        monsterAttackSet(g, combat::ATTACK_HEAVY_TAIL_SLAM);
        m.fx = 16;
        m.fy = 0;   // facing E, away from the hunter
        startMonsterAttack(g);
        t.assert(m.state, MS_ATTACK, "tail_slam enters attack");
        t.assert(m.lvx, -56, "E facing releases backward -56");
        t.assert(m.lvy, 0, "no lateral hop");
        m.x = 100;
        m.y = 20;
        m.subX = 0;
        m.subY = 0;
        g.player.x = 10;
        g.player.y = 10;   // clear of the beast: no shove/hit interference
        beast(g, 8);       // active phase
        t.assert(m.x, 72, "8 active ticks * -56/16 = -28 px pounce");
        beast(g, 44);   // recovery: no drift
        t.assert(m.x, 72, "hop frozen through recovery");
        suite.addTest(t);
    }

    {
        // feel.4: a committed moving attack that reaches a room bound self-stuns
        // for the attack's wallStun ticks, then the shared MS_STAGGER release
        // returns the beast to PURSUE (cdBase). The synthetic tests below set
        // the RAM cache directly; the bull's gore authors wallStun 70 (feel.9).
        Test t("attack wallStun: lunge into a bound staggers for wallStun ticks");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        monsterAttackSet(g, combat::ATTACK_LUNGE_PECK);
        g.combat.attack.wallStun = 7;
        patternStateSet(g, combat::PATTERN_LUNGE_P_PECK, 1, 3);   // active cursor to cancel
        m.state = MS_ATTACK;
        m.t = 0;
        m.x = 0;   // flush against the west bound (clamp pins x at 0)
        m.y = 40;
        m.subX = 0;
        m.subY = 0;
        m.lvx = -16;   // committed west lunge: clamp displaces -1 -> 0
        m.lvy = 0;
        updateMonster(g);
        t.assert(m.state, MS_STAGGER, "bound contact self-stuns");
        t.assert(m.t, 7, "stagger length equals wallStun");
        t.assert(g.combat.patternIdx, COMBAT_NO_PATTERN, "pattern cursor cancelled");
        t.assert(g.combat.stepIdx, 0, "step idx reset");
        t.assert(g.combat.stepT, 0, "step timer reset");
        beast(g, 6);
        t.assert(m.state, MS_STAGGER, "still staggered after wallStun-1 ticks");
        t.assert(m.t, 1, "one stun tick left");
        beast(g, 1);
        t.assert(m.state, MS_PURSUE, "released to pursue after wallStun ticks");
        t.assert(m.cd, 48, "stagger release uses cdBase 48");
        suite.addTest(t);
    }

    {
        Test t("attack wallStun: lunge in the open does not stagger");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        monsterAttackSet(g, combat::ATTACK_LUNGE_PECK);
        g.combat.attack.wallStun = 7;
        m.state = MS_ATTACK;
        m.t = 0;
        m.x = 128;   // mid-arena: clamp never fires
        m.y = 40;
        m.subX = 0;
        m.subY = 0;
        m.lvx = -16;
        m.lvy = 0;
        updateMonster(g);
        t.assert(m.state, MS_ATTACK, "open-field movement stays in attack");
        t.assert(m.x, 127, "lunge moved one pixel west");
        suite.addTest(t);
    }

    {
        Test t("attack wallStun: a second bound contact does not re-trigger or stack");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        monsterAttackSet(g, combat::ATTACK_LUNGE_PECK);
        g.combat.attack.wallStun = 7;
        m.state = MS_ATTACK;
        m.t = 0;
        m.x = 0;
        m.y = 40;
        m.lvx = -16;
        m.lvy = 0;
        updateMonster(g);
        t.assert(m.state, MS_STAGGER, "first contact self-stuns");
        t.assert(m.t, 7, "stun armed");
        // Jam the beast into the bound through the stun: the state change is the
        // one-trigger latch, so ticks drain the timer instead of re-arming it.
        m.x = 0;
        m.lvx = -16;
        beast(g, 1);
        t.assert(m.t, 6, "timer drains, not re-armed");
        m.x = 0;
        m.lvx = -16;
        beast(g, 1);
        t.assert(m.t, 5, "second bound contact does not stack");
        t.assert(m.state, MS_STAGGER, "still the first stun");
        suite.addTest(t);
    }

    {
        // feel.6: the enrage phase is cached at spawn (CombatEnrage) and applied
        // once when HP crosses hpPct. These tests drive synthetic values into the
        // RAM cache; the bull authors the first real phase (feel.9, below).
        Test t("enrage: crossing the threshold applies spdMul truncating + faceHold once");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        t.assert(g.combat.enrage.hpPct, 0, "shipped enrage disabled");
        t.assert(g.combat.enrage.fired, 0, "latch starts clear");
        m.state = MS_PURSUE;
        m.cd = 30000;
        m.hpMax = 100;
        m.spd = 7;
        g.combat.profile.faceHold = 0;
        g.combat.enrage.hpPct = 50;
        g.combat.enrage.spdMul = 150;   // 7 * 150 / 100 = 10 (truncating)
        g.combat.enrage.faceHold = 8;
        m.hp = 51;   // above the 50% threshold
        updateMonster(g);
        t.assert(g.combat.enrage.fired, 0, "no fire above threshold");
        t.assert(m.spd, 7, "spd unchanged above threshold");
        t.assert(g.combat.profile.faceHold, 0, "faceHold unchanged above threshold");
        m.hp = 50;   // exactly 50%: 50*100 <= 100*50 -> fires
        updateMonster(g);
        t.assert(g.combat.enrage.fired, 1, "fires at the threshold");
        t.assert(m.spd, 10, "spdMul 150 applied with truncation");
        t.assert(g.combat.profile.faceHold, 8, "profile cache faceHold replaced");
        suite.addTest(t);
    }

    {
        Test t("enrage: never re-fires after further damage (one-shot latch)");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        m.state = MS_PURSUE;
        m.cd = 30000;
        m.hpMax = 100;
        m.spd = 6;
        g.combat.enrage.hpPct = 80;
        g.combat.enrage.spdMul = 200;
        g.combat.enrage.faceHold = 4;
        m.hp = 80;
        updateMonster(g);
        t.assert(g.combat.enrage.fired, 1, "fires at 80%");
        t.assert(m.spd, 12, "200% of 6");
        // More damage below the threshold: spd/faceHold must not re-apply or grow.
        m.hp = 20;
        updateMonster(g);
        t.assert(m.spd, 12, "spd stays after further damage");
        t.assert(g.combat.profile.faceHold, 4, "faceHold stays");
        suite.addTest(t);
    }

    {
        Test t("enrage: hpPct 0 disabled, and a tiny spdMul floors at 1");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        m.state = MS_PURSUE;
        m.cd = 30000;
        m.hpMax = 100;
        m.hp = 0;
        m.hp = 1;
        // hpPct 0 (default): below/at zero does not fire.
        updateMonster(g);
        t.assert(g.combat.enrage.fired, 0, "hpPct 0 inert");
        t.assert(m.spd, 6, "spd untouched while disabled");
        // Tiny multiplier: 5 * 3 / 100 = 0 truncates, floored to 1.
        g.combat.enrage.hpPct = 100;
        g.combat.enrage.spdMul = 3;
        updateMonster(g);
        t.assert(g.combat.enrage.fired, 1, "fires once enabled");
        t.assert(m.spd, 1, "tiny spdMul floors at 1");
        suite.addTest(t);
    }

    {
        // feel.9: the bull is the first creature to author stats.enrage; the
        // loader caches the real 40% / 130% / faceHold 6 phase at spawn and the
        // FSM applies it once when HP crosses the threshold.
        Test t("enrage: the bull's authored 40% phase caches and fires (feel.9)");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT, MON_SWEEP);
        Monster &m = g.monster;
        t.assert(g.combat.enrage.hpPct, 40, "bull enrage hpPct 40");
        t.assert(g.combat.enrage.spdMul, 130, "bull enrage spdMul 130");
        t.assert(g.combat.enrage.faceHold, 6, "bull enrage faceHold 6");
        t.assert(g.combat.enrage.fired, 0, "latch starts clear");
        t.assert(m.spd, 7, "pre-enrage spd 7");
        t.assert(g.combat.profile.faceHold, 10, "pre-enrage faceHold 10");
        m.state = MS_PURSUE;
        m.cd = 30000;
        m.hp = 61;   // just above 40% (61 * 100 > 150 * 40)
        updateMonster(g);
        t.assert(g.combat.enrage.fired, 0, "no fire above 40%");
        t.assert(m.spd, 7, "spd unchanged above the threshold");
        m.hp = 60;   // exactly 40%: 60*100 <= 150*40 -> fires
        updateMonster(g);
        t.assert(g.combat.enrage.fired, 1, "fires at the 40% threshold");
        t.assert(m.spd, 9, "7 * 130 / 100 truncates to 9");
        t.assert(g.combat.profile.faceHold, 6, "profile faceHold swaps to 6");
        suite.addTest(t);
    }

    {
        Test t("attack wallStun: 0 at a bound is inert (shipped behavior)");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        monsterAttackSet(g, combat::ATTACK_LUNGE_PECK);
        t.assert(g.combat.attack.wallStun, 0, "shipped wallStun is 0");
        m.state = MS_ATTACK;
        m.t = 0;
        m.x = 0;
        m.y = 40;
        m.subX = 0;
        m.subY = 0;
        m.lvx = -16;
        m.lvy = 0;
        updateMonster(g);
        t.assert(m.state, MS_ATTACK, "no stun with wallStun 0");
        t.assert(m.x, 0, "clamp still pins to the bound");
        suite.addTest(t);
    }

    {
        Test t("push rule: beast gives way, idle player never shoved");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        m.state = MS_PURSUE;
        m.cd = 30000;
        m.x = g.player.x + 8;
        m.y = g.player.y + 4;
        const int px = g.player.x;
        const int py = g.player.y;
        const int mx0 = m.x;
        beast(g, 40);
        t.assert(g.player.x, px, "player x untouched");
        t.assert(g.player.y, py, "player y untouched");
        t.assert(g.player.hp, 100, "player unhurt");
        t.assert(m.x != mx0 || m.y != g.player.y + 4, true, "beast moved aside");
        suite.addTest(t);
    }

    {
        // 76y: the chicken's collide rect is the legs only (9,11,12,13), so the
        // hunter can overlap the raised body and only the legs shove/block.
        Test t("push rule: legs collide box shoves; body overlap passes under");
        // (a) body overlap (+8,+4), legs clear: no shove, beast holds.
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        m.state = MS_WINDUP;
        monsterAttackSet(g, combat::ATTACK_LUNGE_PECK);
        m.t = 30000;
        m.x = g.player.x + 8;
        m.y = g.player.y + 4;
        const int px = g.player.x;
        const int py = g.player.y;
        const int mx0 = m.x;
        updateMonster(g);
        t.assert(g.player.x, px, "hunter walks under the raised body");
        t.assert(g.player.y, py, "player y unchanged");
        t.assert(m.x, mx0, "beast holds ground");
        // (b) legs overlapping (+4,0): the windup beast shoves the hunter west.
        Game g2;
        newHunt(g2);
        Monster &m2 = g2.monster;
        m2.state = MS_WINDUP;
        monsterAttackSet(g2, combat::ATTACK_LUNGE_PECK);
        m2.t = 30000;
        m2.x = g2.player.x + 4;
        m2.y = g2.player.y;
        const int px2 = g2.player.x;
        const int mx20 = m2.x;
        updateMonster(g2);
        t.assert(g2.player.x, px2 - 3, "legs shove the hunter west");
        t.assert(g2.player.y, py, "player y unchanged on legs shove");
        t.assert(m2.x, mx20, "beast holds ground while attacking");
        suite.addTest(t);
    }

    {
        // Walking hunter cannot push the beast (bug fix): when the player moved
        // this tick the overlap resolves on the player side; a stationary
        // hunter still lets the beast give way (previous test).
        Test t("push rule: walking hunter cannot push the beast");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        m.state = MS_RECOVER;
        m.t = 30000;
        m.cd = 30000;
        m.x = g.player.x + 1;
        m.y = g.player.y + 4;
        Rect pr{g.player.x, g.player.y, g.player.w, g.player.h};
        const Rect mr0 = monsterCollideRect(g);
        t.assert(mr0.overlaps(pr), true, "hunter starts inside the legs collide box");
        const int mx0 = m.x;
        const int my0 = m.y;
        stepHunt(g, Input{1, 0, false, false});   // hunt east into the beast
        const Rect mr1 = monsterCollideRect(g);
        pr.x = g.player.x;
        pr.y = g.player.y;
        t.assert(m.x, mx0, "beast holds x");
        t.assert(m.y, my0, "beast holds y");
        t.assert(!pr.overlaps(mr1), true, "hunter resolved out of the collide box");
        suite.addTest(t);
    }

    {
        Test t("monster leap attack damages player (mock parity)");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        m.x = g.player.x + 20;
        m.y = g.player.y;
        m.state = MS_ATTACK;
        monsterAttackSet(g, combat::ATTACK_LUNGE_LEAP);
        m.t = 0;
        hunt(g, 10);
        t.assert(g.player.hp, 87, "leap 13 dmg lands once");
        t.assertGreaterThan(g.player.iT, 0, "i-frames started");
        suite.addTest(t);
    }

    {
        Test t("player attack damages monster through Game::target");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        m.state = MS_RECOVER;   // parked, no movement
        m.t = 30000;
        // 76y: the chicken's target rect is the legs (9,11,12,13); the hunter
        // stands in sword reach of the legs and the melee centre lands on the
        // body, so the front crit still routes base 9 * 14/10 = 12.
        m.x = g.player.x + 14;
        m.y = g.player.y;
        hunt(g, 1, Input{0, 0, true, false});
        hunt(g, 1, Input{0, 0, false, false});
        hunt(g, 18);
        t.assert(m.hp, 188, "sword hit crit for 12");
        suite.addTest(t);
    }

    {
        Test t("playerHit routing: parry/deflect stun, guard chip, iT gate");
        // parry: stun 60 + riposte 90
        Game g;
        newHunt(g, W_SWORD);
        g.monster.state = MS_RECOVER;
        g.monster.t = 30000;
        hunt(g, 13, Input{0, 0, false, true});
        t.assert(g.player.stance, ST_PARRY, "parry up");
        g.monster.stun = 0;
        playerHurt(g, 10, -16, 0);
        t.assert(g.monster.stun, 60, "parry stuns monster 60");
        t.assert(g.player.riposteT, 90, "parry riposte timer");

        // deflect: stun 28
        Game g2;
        newHunt(g2, W_FLAIL);
        g2.monster.state = MS_RECOVER;
        g2.monster.t = 30000;
        hunt(g2, 1, Input{0, 0, false, true});
        hunt(g2, 1, Input{0, 0, false, false});
        t.assert(g2.player.state, PS_DEFLECT, "deflect up");
        g2.monster.stun = 0;
        playerHurt(g2, 10, -16, 0);
        t.assert(g2.monster.stun, 28, "deflect stuns monster 28");
        t.assert(g2.player.state, PS_DEFLECT, "deflect absorbs");

        // guard: chip 25%, no monster stun
        Game g3;
        newHunt(g3, W_GUN);
        g3.monster.state = MS_RECOVER;
        g3.monster.t = 30000;
        hunt(g3, 13, Input{0, 0, false, true});
        t.assert(g3.player.stance, ST_GUARD, "guard up");
        g3.monster.stun = 0;
        playerHurt(g3, 10, -16, 0);
        t.assert(g3.player.hp, 98, "guard chip 2");
        t.assert(g3.player.stam, 78, "guard cost 22");
        t.assert(g3.monster.stun, 0, "guard does not stun monster");

        // normal hit: 35 knockback int + 34 i-frames gate repeats
        Game g4;
        newHunt(g4);
        g4.monster.state = MS_RECOVER;
        g4.monster.t = 30000;
        playerHurt(g4, 10, -16, 0);
        t.assert(g4.player.hp, 90, "normal hit 10");
        t.assert(g4.player.iT, 34, "i-frames 34");
        t.assert(g4.player.vx, -35, "knockback -35 fixed");
        playerHurt(g4, 10, -16, 0);
        t.assert(g4.player.hp, 90, "i-frames block repeat");
        suite.addTest(t);
    }

    {
        Test t("idle hunt runs: beast engages, hunter survives");
        Game g;
        newHunt(g);
        hunt(g, 240);
        t.assert(g.over, OVER_NONE, "no game over");
        t.assert(g.monster.state != MS_IDLE, true, "beast engaged");
        t.assertGreaterThan(g.player.hp, 0, "hunter survived 240 idle ticks");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
