#pragma once
// Host unit tests for src/core/monster.hpp — permanent, co-located with repo
// tests. Mirrors mock/game.test.js monster-half expectations (mock is the
// source of truth for numbers): tell timing, recovery windows, push rule,
// crit zone, stun->recover, knockback ints, and the player-hit routing.
#include "test.hpp"
#include "../src/core/monster.hpp"
#include "../src/core/world.hpp"   // newGame / withWeapon / resetHunt

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
        t.assert(g.monster.spd, 5, "pursue speed 5/16");
        suite.addTest(t);
    }

    {
        Test t("chooseAttack: lunge beyond 32px, sweep inside (cache identity)");
        Game g;
        newHunt(g);
        chooseAttack(g, 33);
        t.assert(g.monster.atkIdx, combat::ATTACK_LUNGE_LUNGE, "dist 33 -> lunge");
        t.assert(g.combat.attack.moveType, MOVE_LUNGE, "lunge cache moveType");
        t.assert(g.monster.state, MS_WINDUP, "windup state");
        t.assert(g.monster.t, 40, "lunge tell 40");
        t.assert(g.monster.windupMax, 40, "windupMax recorded");
        chooseAttack(g, 32);
        t.assert(g.monster.atkIdx, combat::ATTACK_LUNGE_SWEEP, "dist 32 -> sweep");
        t.assert(g.combat.attack.moveType, MOVE_NONE, "sweep cache moveType");
        t.assert(g.monster.t, 48, "sweep tell 48");
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
        t.assert(g.target.rect.w, 28, "hurt box synced from box");
        t.assert(g.target.rect.h, 22, "hurt box height from box");
        t.assert(g.target.rect.x, m.x, "hurt box x at box origin");
        t.assert(g.target.rect.y, m.y, "hurt box y at box origin");

        Game g2;
        newGame(g2, W_SWORD, MODE_HUNT, MON_HEAVY);
        t.assert(g2.monsterKind, MON_HEAVY, "heavy kind recorded");
        t.assert(g2.monster.w, 40, "heavy width");
        t.assert(g2.monster.h, 28, "heavy height");
        t.assert(g2.combat.body.w, 40, "heavy cached box w");
        t.assert(g2.combat.body.h, 28, "heavy cached box h");
        t.assert(g2.monster.hp, 320, "heavy hp");
        t.assert(g2.monster.spd, 3, "heavy speed");

        Game g3;
        newGame(g3, W_SWORD, MODE_HUNT);   // default kind 0 = legacy beast
        t.assert(g3.monsterKind, MON_LUNGE, "default kind 0");
        t.assert(g3.monster.w, 32, "legacy width");
        t.assert(g3.combat.body.w, 32, "legacy cached box w");
        t.assert(g3.combat.body.h, 24, "legacy cached box h");
        t.assert(g3.monster.hp, 200, "legacy hp");
        t.assert(g3.monster.spd, 5, "legacy speed");
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
        Test t("chooseAttack variants: SWEEP never lunges, HEAVY spins inside 24 else bites");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT, MON_SWEEP);
        Monster &m = g.monster;
        for (int32_t d = 10; d <= 41; d += 11) {
            chooseAttack(g, d);
            t.assert(m.atkIdx, combat::ATTACK_SWEEP_SWEEP, "sweep def never lunges");
        }
        Game g2;
        newGame(g2, W_SWORD, MODE_HUNT, MON_HEAVY);
        Monster &h = g2.monster;
        chooseAttack(g2, 41);
        t.assert(h.atkIdx, combat::ATTACK_HEAVY_BITE, "heavy bites at 41");
        chooseAttack(g2, 30);
        t.assert(h.atkIdx, combat::ATTACK_HEAVY_BITE, "heavy bites at 30");
        chooseAttack(g2, 25);
        t.assert(h.atkIdx, combat::ATTACK_HEAVY_BITE, "heavy bites at 25");
        chooseAttack(g2, 24);
        t.assert(h.atkIdx, combat::ATTACK_HEAVY_TAIL_SPIN, "heavy spins at 24");
        Game g3;
        newGame(g3, W_SWORD, MODE_HUNT, MON_LUNGE);
        chooseAttack(g3, 33);
        t.assert(g3.monster.atkIdx, combat::ATTACK_LUNGE_LUNGE, "legacy lunges at 33");
        chooseAttack(g3, 32);
        t.assert(g3.monster.atkIdx, combat::ATTACK_LUNGE_SWEEP, "legacy sweeps at 32");
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
        t.assert(g.monster.winRemain, 0, "lunge declares one window");
        t.assert(ac.windup, 40, "cache windup");
        t.assert(ac.active, 10, "cache active");
        t.assert(ac.recover, 55, "cache recover");
        t.assert(ac.dmg, 12, "cache dmg");
        t.assert(ac.moveSpeedF, 34, "cache speedF");
        t.assert(ac.winIdx, combat::WINDOW_LUNGE_LUNGE_0, "cache first window");
        t.assert(ac.win.t0, 0, "window t0");
        t.assert(ac.win.t1, 10, "window t1");
        t.assert(ac.win.box.ox, 12, "window ox");
        t.assert(ac.win.box.oy, 0, "window oy");
        t.assert(ac.win.box.w, 24, "window w");
        t.assert(ac.win.box.h, 22, "window h");
        t.assert(ac.win.dmgMul, 100, "window dmgMul");

        chooseAttack(g, 32);
        t.assert(g.combat.attack.winIdx, combat::WINDOW_LUNGE_SWEEP_0, "sweep window cached");
        t.assert(g.combat.attack.win.box.ox, 17, "sweep window ox");
        t.assert(g.combat.attack.win.box.w, 32, "sweep window w");
        t.assert(g.combat.attack.win.box.h, 24, "sweep window h");
        t.assert(g.combat.attack.dmg, 9, "sweep dmg cached");
        suite.addTest(t);
    }

    {
        Test t("hit test consumes the cached window (switch flips miss to hit)");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        monsterAttackSet(g, combat::ATTACK_LUNGE_LUNGE);
        m.x = 0;
        m.y = 0;
        m.fx = 16;
        m.fy = 0;   // centre (16,12), lunge box x16..40
        g.player.x = 41;
        g.player.y = 4;
        g.player.w = 16;
        g.player.h = 16;
        t.assert(monsterHitsPlayer(g), 0, "lunge window misses at x41");
        attackWindowLoad(g, combat::WINDOW_LUNGE_SWEEP_0);   // sweep box x17..49
        t.assert(monsterHitsPlayer(g), 1, "widened cached window hits at x41");
        suite.addTest(t);
    }

    {
        Test t("multi-window refresh path: next contiguous window reloads once");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        monsterAttackSet(g, combat::ATTACK_LUNGE_LUNGE);
        t.assert(m.winRemain, 0, "single-window attack has no pending window");
        m.winRemain = 1;   // synthetic multi-window state
        m.t = 11;          // past the lunge window t1 == 10
        monsterWindowNext(g);
        t.assert(g.combat.attack.winIdx, combat::WINDOW_LUNGE_SWEEP_0, "next window loaded");
        t.assert(g.combat.attack.win.box.ox, 17, "next window box swapped");
        t.assert(m.winRemain, 0, "pending window consumed");
        monsterWindowNext(g);
        t.assert(g.combat.attack.winIdx, combat::WINDOW_LUNGE_SWEEP_0, "no further reload");
        suite.addTest(t);
    }

    {
        Test t("windup tell counts down 40 ticks, then attack starts");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        monsterAttackSet(g, combat::ATTACK_LUNGE_LUNGE);
        m.state = MS_WINDUP;
        m.t = 40;
        m.windupMax = 40;
        beast(g, 39);
        t.assert(m.state, MS_WINDUP, "still winding up at 39");
        t.assert(m.t, 1, "one tell tick left");
        beast(g, 1);
        t.assert(m.state, MS_ATTACK, "tell released into attack");
        t.assert(m.t, 0, "attack starts at t 0");
        t.assert(m.fx, -fp::FP, "faced west at release");
        t.assert(m.lvx, -34, "lunge velocity int (-16*34)>>4");
        t.assert(m.lvy, 0, "flat lunge");
        suite.addTest(t);
    }

    {
        Test t("sweep recovery window closes, cd = 55 + tick%40, circle flips");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        monsterAttackSet(g, combat::ATTACK_LUNGE_SWEEP);
        m.state = MS_ATTACK;
        m.t = 0;
        m.x = 10;
        m.y = 10;   // far from player: no contact
        hunt(g, 72);
        t.assert(m.state, MS_ATTACK, "still active+recovering at 72");
        hunt(g, 1);
        t.assert(m.state, MS_PURSUE, "attack releases to pursue");
        t.assert(m.cd, 55 + (g.tick % 40), "post-attack cooldown");
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
        t.assert(g.monster.cd, 55, "short cooldown after recover");
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
        t.assert(g.monster.cd, 55, "recover cooldown 55");
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
        monsterAttackSet(g, combat::ATTACK_LUNGE_LUNGE);
        m.fx = 16;
        m.fy = 0;
        startMonsterAttack(g);
        t.assert(m.state, MS_ATTACK, "attack entered");
        t.assert(m.lvx, 34, "E lunge 34");
        t.assert(m.lvy, 0, "E lunge flat");
        m.fx = 11;
        m.fy = 11;
        startMonsterAttack(g);
        t.assert(m.lvx, 23, "SE lunge integer trunc (11*34)>>4");
        t.assert(m.lvy, 23, "SE lunge y");
        monsterAttackSet(g, combat::ATTACK_LUNGE_SWEEP);
        m.fx = 16;
        m.fy = 0;
        startMonsterAttack(g);
        t.assert(m.lvx, 0, "sweep stationary");
        t.assert(m.lvy, 0, "sweep stationary y");
        suite.addTest(t);
    }

    {
        Test t("lunge moves via addVel with sub-pixel carry");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        monsterAttackSet(g, combat::ATTACK_LUNGE_LUNGE);
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
        monsterAttackSet(g, combat::ATTACK_LUNGE_LUNGE);
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
        monsterAttackSet(g2, combat::ATTACK_LUNGE_LUNGE);
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
        Test t("monster sweep attack damages player (mock parity)");
        Game g;
        newHunt(g);
        Monster &m = g.monster;
        m.x = g.player.x + 20;
        m.y = g.player.y;
        m.state = MS_ATTACK;
        monsterAttackSet(g, combat::ATTACK_LUNGE_SWEEP);
        m.t = 0;
        hunt(g, 10);
        t.assert(g.player.hp, 91, "sweep 9 dmg lands once");
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
