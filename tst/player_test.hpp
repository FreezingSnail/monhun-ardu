#pragma once
// Host unit tests for src/core/player.hpp — permanent, co-located with repo
// tests. Mirrors mock/game.test.js player-half expectations (mock is the
// source of truth for numbers).
#include "test.hpp"
#include "../src/core/player.hpp"

using namespace mh;

namespace {

struct Rec {
    int hits = 0, lastDmg = 0, lastPush = 0, lastEffect = 0;
    int shoves = 0, stuns = 0, lastFreeze = 0;
    void reset() {
        hits = 0;
        lastDmg = 0;
        lastPush = 0;
        lastEffect = 0;
        shoves = 0;
        stuns = 0;
        lastFreeze = 0;
    }
};
Rec rec;

void onHit(Game &, int dmg, int, int, int push, int effect) {
    rec.hits++;
    rec.lastDmg = dmg;
    rec.lastPush = push;
    rec.lastEffect = effect;
}
void onShove(Game &, int, int, int, int freeze) {
    rec.shoves++;
    rec.lastFreeze = freeze;
}
void onStun(Game &, int) {
    rec.stuns++;
}

// Place a monster-shaped hurt box (32x24) at (x,y); zq5/hrd will own this.
void armTarget(Game &g, int16_t x, int16_t y) {
    rec.reset();
    g.target.alive = true;
    g.target.rect = Rect{x, y, 32, 24};
    g.target.onHit = onHit;
    g.target.onShove = onShove;
    g.target.onStun = onStun;
}

void stepN(Game &g, int n, Input in = Input{0, 0, false, false}) {
    for (int i = 0; i < n; i++)
        stepPlayer(g, in);
}

// press A, release, run until the attack completes back to idle
void attackOnce(Game &g) {
    stepN(g, 1, Input{0, 0, true, false});
    stepN(g, 1, Input{0, 0, false, false});
    for (int i = 0; i < 40 && g.player.state != PS_IDLE; i++)
        stepN(g, 1);
}

// press A (second combo hit), then run into its recovery phase. bR must land
// before the 14-tick chainWin expires (A2 tick 13 -> chain still 1 -> stage 2).
void secondAttackToRecovery(Game &g) {
    stepN(g, 1, Input{0, 0, true, false});
    stepN(g, 1, Input{0, 0, false, false});
    stepN(g, 9);   // t=11; next tap lands bR at t=13, past every weapon's active window
}

// tap (not hold) B
void tapB(Game &g) {
    stepN(g, 1, Input{0, 0, false, true});
    stepN(g, 1, Input{0, 0, false, false});
}

// hold B until a stance is up
void holdToStance(Game &g) {
    stepN(g, 13, Input{0, 0, false, true});
}

}   // namespace

void PlayerSuite(TestRunner &runner) {
    TestSuite suite("Player FSM + weapons (src/core/player.hpp)");

    {
        Test t("WEAPON_DEFS tables match prototype numbers");
        t.assert(WEAPON_DEFS[W_SWORD].spd, 18, "sword spd");
        t.assert(WEAPON_DEFS[W_FLAIL].spd, 15, "flail spd");
        t.assert(WEAPON_DEFS[W_GUN].spd, 9, "gun spd");
        // sword combo + special
        t.assert(WEAPON_DEFS[W_SWORD].attacks[0].startup, 3, "s0 startup");
        t.assert(WEAPON_DEFS[W_SWORD].attacks[0].active, 5, "s0 active");
        t.assert(WEAPON_DEFS[W_SWORD].attacks[0].recover, 8, "s0 recover");
        t.assert(WEAPON_DEFS[W_SWORD].attacks[0].dmg, 9, "s0 dmg");
        t.assert(WEAPON_DEFS[W_SWORD].attacks[0].reach, 13, "s0 reach");
        t.assert(WEAPON_DEFS[W_SWORD].attacks[0].hw, 12, "s0 hw");
        t.assert(WEAPON_DEFS[W_SWORD].attacks[0].hh, 10, "s0 hh");
        t.assert(WEAPON_DEFS[W_SWORD].attacks[0].stam, 9, "s0 stam");
        t.assert(WEAPON_DEFS[W_SWORD].attacks[2].dmg, 17, "s2 dmg");
        t.assert(WEAPON_DEFS[W_SWORD].attacks[2].startup, 5, "s2 startup");
        t.assert(WEAPON_DEFS[W_SWORD].special.dmg, 24, "sword special dmg");
        t.assert(WEAPON_DEFS[W_SWORD].special.reach, 18, "sword special reach");
        // branches
        t.assert(WEAPON_DEFS[W_SWORD].branches[0].stage, 1, "stepslash stage");
        t.assert(WEAPON_DEFS[W_SWORD].branches[0].atk.lunge, 42, "stepslash lunge");
        t.assert(WEAPON_DEFS[W_SWORD].branches[0].atk.dmg, 12, "stepslash dmg");
        t.assert(WEAPON_DEFS[W_SWORD].branches[1].atk.id, ATK_SPINCUT, "spincut id");
        t.assert(WEAPON_DEFS[W_SWORD].branches[1].atk.hw, 28, "spincut hw");
        // flail
        t.assert(WEAPON_DEFS[W_FLAIL].branches[0].stance, ST_WHIRL, "flail branch1 stance");
        t.assert(WEAPON_DEFS[W_FLAIL].branches[0].autoT, 50, "flail whirl auto");
        t.assert(WEAPON_DEFS[W_FLAIL].branches[1].atk.id, ATK_TRIP, "trip id");
        t.assert(WEAPON_DEFS[W_FLAIL].branches[1].atk.effect, 1, "trip effect");
        // gunshield
        t.assert(WEAPON_DEFS[W_GUN].branches[0].atk.shell, true, "pointblank shell");
        t.assert(WEAPON_DEFS[W_GUN].branches[1].atk.push, 12, "guardbash push");
        t.assert(WEAPON_DEFS[W_GUN].shells[0].count, 2, "ball count");
        t.assert(WEAPON_DEFS[W_GUN].shells[0].dmg, 28, "ball dmg");
        t.assert(WEAPON_DEFS[W_GUN].shells[0].reload, 70, "ball reload");
        t.assert(WEAPON_DEFS[W_GUN].shells[1].count, 5, "scatter count");
        t.assert(WEAPON_DEFS[W_GUN].shells[1].pellets, 3, "scatter pellets");
        suite.addTest(t);
    }

    {
        Test t("chain advances on hit, window expires to reset");
        Game g;
        initGame(g, W_SWORD);
        attackOnce(g);
        t.assert(g.player.chain, 1, "chain after first attack");
        t.assert(g.player.chainWin, 14, "chain window 14 on completion");
        stepN(g, 15);   // window expires with no input
        t.assert(g.player.chain, 0, "chain resets when window expires");
        t.assert(g.player.chainWin, 0, "window closed");
        // combo advance: next attack inside a fresh window runs combo hit 2
        attackOnce(g);
        t.assert(g.player.chain, 1, "chain again after fresh attack");
        attackOnce(g);
        t.assert(g.player.chain, 1, "chain stays 1: 2nd combo outlives 14t window (mock semantics)");
        suite.addTest(t);
    }

    {
        Test t("sword A then B = stepslash branch (lunge)");
        Game g;
        initGame(g, W_SWORD);
        armTarget(g, g.player.x + 200, g.player.y);   // far away, no combo hit
        attackOnce(g);
        tapB(g);
        t.assert(g.player.state, PS_ATTACK, "branch attack state");
        t.assert(g.player.atk->id, ATK_STEPSLASH, "stepslash id");
        t.assert(g.player.atk->lunge, 42, "lunge distance in table");
        t.assertGreaterThan(g.player.vx, 0, "lunge velocity applied");
        t.assertLessThan(g.player.vx, 43, "lunge decays from 42 (13/16 per tick)");
        suite.addTest(t);
    }

    {
        Test t("sword second-hit recovery then B = spincut branch");
        Game g;
        initGame(g, W_SWORD);
        armTarget(g, g.player.x + 200, g.player.y);
        attackOnce(g);
        secondAttackToRecovery(g);
        tapB(g);
        t.assert(g.player.atk->id, ATK_SPINCUT, "spincut id");
        t.assert(g.player.atk->hw, 28, "spincut wide hitbox");
        suite.addTest(t);
    }

    {
        Test t("flail A then B = release into whirl stance (auto 50)");
        Game g;
        initGame(g, W_FLAIL);
        armTarget(g, g.player.x + 200, g.player.y);
        attackOnce(g);
        tapB(g);
        t.assert(g.player.stance, ST_WHIRL, "whirl entered");
        t.assert(g.player.stanceAuto, 49, "auto-release timer (50 set, entry tick decrements)");
        t.assert(g.player.state, PS_IDLE, "back to idle while whirling");
        suite.addTest(t);
    }

    {
        Test t("flail second-hit recovery then B = trip branch (effect)");
        Game g;
        initGame(g, W_FLAIL);
        armTarget(g, g.player.x + 200, g.player.y);
        attackOnce(g);
        secondAttackToRecovery(g);
        tapB(g);
        t.assert(g.player.atk->id, ATK_TRIP, "trip id");
        t.assert(g.player.atk->effect, 1, "trip effect flag");
        suite.addTest(t);
    }

    {
        Test t("gunshield A then B = pointblank shell branch");
        Game g;
        initGame(g, W_GUN);
        armTarget(g, g.player.x + 20, g.player.y);   // within reach 15
        attackOnce(g);
        rec.reset();
        tapB(g);
        t.assert(g.player.atk->id, ATK_POINTBLANK, "pointblank id");
        t.assert(g.player.shells[0], 1, "ball consumed 2 -> 1");
        t.assert(g.player.reload, 45, "branch reload set");
        stepN(g, 8);
        t.assert(rec.hits, 1, "pointblank connects");
        t.assert(rec.lastDmg, 22, "pointblank dmg");
        suite.addTest(t);
    }

    {
        Test t("gunshield second-hit recovery then B = guardbash branch (push)");
        Game g;
        initGame(g, W_GUN);
        armTarget(g, g.player.x + 200, g.player.y);
        attackOnce(g);
        secondAttackToRecovery(g);
        tapB(g);
        t.assert(g.player.atk->id, ATK_GUARDBASH, "guardbash id");
        t.assert(g.player.atk->push, 12, "guardbash push");
        suite.addTest(t);
    }

    {
        Test t("pointblank without shells falls back to shove");
        Game g;
        initGame(g, W_GUN);
        g.player.shells[0] = 0;
        attackOnce(g);
        tapB(g);
        t.assert(g.player.state, PS_SHOVE, "shove when out of shells");
        suite.addTest(t);
    }

    {
        Test t("hold-B enters parry/whirl/guard, release exits stance");
        Game g;
        initGame(g, W_SWORD);
        holdToStance(g);
        t.assert(g.player.stance, ST_PARRY, "sword hold -> parry");
        stepN(g, 1, Input{0, 0, false, false});   // release
        t.assert(g.player.stance, ST_NONE, "release exits parry");

        initGame(g, W_FLAIL);
        holdToStance(g);
        t.assert(g.player.stance, ST_WHIRL, "flail hold -> whirl");

        initGame(g, W_GUN);
        holdToStance(g);
        t.assert(g.player.stance, ST_GUARD, "gun hold -> guard");
        suite.addTest(t);
    }

    {
        Test t("whirl branch auto-exits after 50 ticks");
        Game g;
        initGame(g, W_FLAIL);
        armTarget(g, g.player.x + 200, g.player.y);
        attackOnce(g);
        tapB(g);
        stepN(g, 55);   // no input
        t.assert(g.player.stance, ST_NONE, "auto-release ran out");
        suite.addTest(t);
    }

    {
        Test t("parry caps at 34 ticks then exits locked");
        Game g;
        initGame(g, W_SWORD);
        stepN(g, 50, Input{0, 0, false, true});   // hold past cap
        t.assert(g.player.stance, ST_NONE, "parry expired by cap");
        t.assert(g.player.bLocked, true, "locked out of re-entry");
        suite.addTest(t);
    }

    {
        Test t("whirl drains stamina to empty and exits locked");
        Game g;
        initGame(g, W_FLAIL);
        g.player.stam = 12;
        g.player.stamSub = 0;
        stepN(g, 40, Input{0, 0, false, true});   // enters at t10; regen refilled to ~17 first
        t.assert(g.player.stance, ST_WHIRL, "still whirling at 40");
        t.assertLessThan(g.player.stam, 8, "draining hard");
        for (int i = 0; i < 100 && g.player.stance != ST_NONE; i++) {
            stepN(g, 1, Input{0, 0, false, true});
        }
        t.assert(g.player.stance, ST_NONE, "whirl ends on empty stamina");
        t.assert(g.player.bLocked, true, "locked after stamina break");
        suite.addTest(t);
    }

    {
        Test t("stamina regen +1 per 2 ticks up to max");
        Game g;
        initGame(g, W_SWORD);
        g.player.stam = 50;
        g.player.stamSub = 0;
        stepN(g, 100);
        t.assert(g.player.stam, 100, "regen back to max");
        t.assert(g.player.stamSub, 0, "sub accumulator clean");
        suite.addTest(t);
    }

    {
        Test t("sword tap-B dodges >10px in 10 ticks with i-frames");
        Game g;
        initGame(g, W_SWORD);
        stepN(g, 5, Input{1, 0, false, false});
        const int x0 = g.player.x;
        stepN(g, 1, Input{1, 0, false, true});
        stepN(g, 1, Input{1, 0, false, false});
        t.assert(g.player.state, PS_DODGE, "dodge state");
        t.assert(g.player.iT, 14, "i-frames 14");
        stepN(g, 10, Input{1, 0, false, false});
        t.assertGreaterThan(g.player.x, x0 + 10, "roll covers real distance");
        stepN(g, 6);
        t.assert(g.player.state, PS_IDLE, "dodge ends back to idle");
        suite.addTest(t);
    }

    {
        Test t("hold vs tap B semantics: tap dodges, hold stances");
        Game g;
        initGame(g, W_SWORD);
        tapB(g);
        t.assert(g.player.state, PS_DODGE, "short press = tap defense");

        initGame(g, W_SWORD);
        holdToStance(g);
        t.assert(g.player.stance, ST_PARRY, "long press = stance");
        stepN(g, 1, Input{0, 0, false, false});
        t.assert(g.player.stance, ST_NONE, "long release = stance exit");
        suite.addTest(t);
    }

    {
        Test t("canCancel: sword taps out of attack, flail cannot");
        Game g;
        initGame(g, W_SWORD);
        stepN(g, 1, Input{0, 0, true, false});
        stepN(g, 1, Input{0, 0, false, false});
        stepN(g, 2);   // mid startup/recovery
        tapB(g);
        t.assert(g.player.state, PS_DODGE, "sword cancels attack into dodge");

        initGame(g, W_FLAIL);
        stepN(g, 1, Input{0, 0, true, false});
        stepN(g, 1, Input{0, 0, false, false});
        stepN(g, 2);
        tapB(g);
        t.assert(g.player.state, PS_ATTACK, "flail cannot cancel attack");
        suite.addTest(t);
    }

    {
        Test t("guard blocks cost 22 stamina + chip, break -> stun 45");
        Game g;
        initGame(g, W_GUN);
        holdToStance(g);
        playerHurt(g, 10, -16, 0);
        t.assert(g.player.stam, 78, "block cost 22");
        t.assert(g.player.hp, 98, "chip 25% of 10 = 2");
        t.assert(g.player.state, PS_IDLE, "guard holds");

        initGame(g, W_GUN);
        holdToStance(g);
        g.player.stam = 20;
        playerHurt(g, 10, -16, 0);
        t.assert(g.player.stam, 0, "stamina bottomed");
        t.assert(g.player.state, PS_STUN, "guard break stuns");
        t.assert(g.player.t, 45, "stun length 45");
        t.assert(g.player.stance, ST_NONE, "stance dropped on break");
        t.assert(g.player.bLocked, true, "locked after break");
        suite.addTest(t);
    }

    {
        Test t("whirl stance hits every 16 ticks for 8 dmg + push 8");
        Game g;
        initGame(g, W_FLAIL);
        armTarget(g, g.player.x + 8, g.player.y - 8);   // overlapping whirl radius 24
        holdToStance(g);                                // whirl entered, whirlTick starts ticking
        stepN(g, 20, Input{0, 0, false, true});
        t.assert(rec.hits, 1, "one whirl hit fired");
        t.assert(rec.lastDmg, 8, "whirl dmg");
        t.assert(rec.lastPush, 8, "whirl knockback");
        suite.addTest(t);
    }

    {
        Test t("gunshield stance+A consumes shell and starts reload");
        Game g;
        initGame(g, W_GUN);
        holdToStance(g);
        stepN(g, 1, Input{0, 0, true, true});   // A press while holding B
        t.assert(g.player.shells[0], 1, "ball consumed 2 -> 1");
        t.assert(g.player.reload, 70, "ball reload started");
        t.assert(g.lastShot, 1, "shot recorded for hrd");
        stepN(g, 1, Input{0, 0, false, true});   // release A, reload ticks to 69
        stepN(g, 1, Input{0, 0, true, true});    // A again during reload
        t.assert(g.player.shells[0], 1, "no consume while reloading");
        suite.addTest(t);
    }

    {
        Test t("parry riposte and deflect response set riposteT / stun hook");
        Game g;
        initGame(g, W_SWORD);
        armTarget(g, g.player.x + 20, g.player.y);
        holdToStance(g);   // parry, stanceT 13 <= 20
        playerHurt(g, 10, 16, 0);
        t.assert(g.player.riposteT, 90, "parry riposte timer");
        t.assert(rec.stuns, 1, "monster stun requested (zq5)");

        initGame(g, W_FLAIL);
        armTarget(g, g.player.x + 20, g.player.y);
        tapB(g);   // deflect
        playerHurt(g, 10, 16, 0);
        t.assert(rec.stuns, 1, "deflect stun requested (zq5)");
        t.assert(g.player.state, PS_DEFLECT, "deflect absorbs hit");
        suite.addTest(t);
    }

    {
        Test t("flail stance special throws (throwCd) without hitting");
        Game g;
        initGame(g, W_FLAIL);
        armTarget(g, g.player.x + 200, g.player.y);
        holdToStance(g);   // whirl
        stepN(g, 1, Input{0, 0, true, true});
        t.assert(g.player.state, PS_SPECIAL, "throw special state");
        t.assert(g.player.throwCd, 50, "throw cooldown");
        suite.addTest(t);
    }

    {
        Test t("all weapons step 60 ticks without breaking");
        for (int8_t w = 0; w < 3; w++) {
            Game g;
            initGame(g, w);
            stepN(g, 60);
            t.assert(g.player.hp, 100, "no damage with no target");
        }
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}