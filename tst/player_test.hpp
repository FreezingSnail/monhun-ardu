#pragma once
// Host unit tests for src/core/player.hpp — permanent, co-located with repo
// tests. Mirrors mock/game.test.js player-half expectations (mock is the
// source of truth for numbers).
#include "test.hpp"
#include "../src/core/player.hpp"
#include "../src/core/projectiles.hpp"   // initWorld / stepWorld (charge-case world ticks)

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

void onHit(Game &, uint8_t dmg, int16_t, int16_t, uint8_t push, uint8_t effect) {
    rec.hits++;
    rec.lastDmg = dmg;
    rec.lastPush = push;
    rec.lastEffect = effect;
}
void onShove(Game &, int8_t, int8_t, uint8_t, uint8_t freeze) {
    rec.shoves++;
    rec.lastFreeze = freeze;
}
void onStun(Game &, uint8_t) {
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

// mock waitIdle(): run to idle
void waitIdle(Game &g) {
    for (int i = 0; i < 80 && g.player.state != PS_IDLE; i++)
        stepN(g, 1);
}

// press A, release, run until the attack starts (buffering through the HEAVY
// gap lock when needed), then until it completes back to idle
void attackOnce(Game &g) {
    waitIdle(g);
    stepN(g, 1, Input{0, 0, true, false});
    stepN(g, 1, Input{0, 0, false, false});
    for (int i = 0; i < 40 && g.player.state != PS_ATTACK; i++)
        stepN(g, 1);
    for (int i = 0; i < 40 && g.player.state != PS_IDLE; i++)
        stepN(g, 1);
}

// mock comboHit(): wait idle, tap A and run until an attack starts (buffering
// through the debounce lock). Returns true when an attack started.
bool comboHit(Game &g) {
    waitIdle(g);
    stepN(g, 1, Input{0, 0, true, false});
    for (int i = 0; i < 40 && g.player.state != PS_ATTACK; i++)
        stepN(g, 1);
    return g.player.state == PS_ATTACK;
}

// advance a running attack into its recovery (t >= startup + active)
void toRecovery(Game &g) {
    for (int i = 0; i < 40 && g.player.state == PS_ATTACK; i++) {
        const Attack *a = g.player.atk;
        if (a && g.player.t >= attackStartup(a) + attackActive(a))
            return;
        stepN(g, 1);
    }
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

// S2 stow: hold A on an armed press -- the swing plays out, then the weapon
// goes away at STOW_HOLD_TICKS (flail first enters PS_CHARGE and stows at
// CHARGE_MIN + STOW_HOLD_TICKS). Leaves A released, idle, and stowed.
void stowWeapon(Game &g) {
    stepN(g, 1, Input{0, 0, true, false});                                  // press A: swing starts
    stepN(g, CHARGE_MIN + STOW_HOLD_TICKS + 4, Input{0, 0, true, false});   // hold past the stow window
    stepN(g, 1, Input{0, 0, false, false});                                 // release A
}

// double-tap a d-pad direction (press, release, press) -- feel.16 roll input
void doubleTap(Game &g, int8_t mx, int8_t my) {
    stepN(g, 1, Input{mx, my, false, false});
    stepN(g, 1, Input{0, 0, false, false});
    stepN(g, 1, Input{mx, my, false, false});
}

}   // namespace

void PlayerSuite(TestRunner &runner) {
    TestSuite suite("Player FSM + weapons (src/core/player.hpp)");

    {
        Test t("WEAPON_DEFS tables match prototype numbers");
        t.assert(WEAPON_DEFS[W_SWORD].spd, 14, "sword spd");
        t.assert(WEAPON_DEFS[W_FLAIL].spd, 12, "flail spd");
        t.assert(WEAPON_DEFS[W_GUN].spd, 7, "gun spd");
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
        // stage 3 (7pw)
        t.assert(WEAPON_DEFS[W_SWORD].branches[2].stage, 3, "helmsplit stage");
        t.assert(WEAPON_DEFS[W_SWORD].branches[2].atk.dmg, 26, "helmsplit dmg");
        t.assert(WEAPON_DEFS[W_SWORD].branches[2].atk.reach, 16, "helmsplit reach");
        t.assert(WEAPON_DEFS[W_FLAIL].branches[2].stage, 3, "earthslam stage");
        t.assert(WEAPON_DEFS[W_FLAIL].branches[2].atk.dmg, 32, "earthslam dmg");
        t.assert(WEAPON_DEFS[W_FLAIL].branches[2].atk.push, 12, "earthslam push");
        t.assert(WEAPON_DEFS[W_FLAIL].branches[2].atk.effect, 1, "earthslam trip");
        t.assert(WEAPON_DEFS[W_GUN].branches[2].stage, 3, "cannonblast stage");
        t.assert(WEAPON_DEFS[W_GUN].branches[2].atk.dmg, 30, "cannonblast dmg");
        t.assert(WEAPON_DEFS[W_GUN].branches[2].atk.push, 16, "cannonblast push");
        t.assert(WEAPON_DEFS[W_GUN].shells[0].count, 2, "ball count");
        t.assert(WEAPON_DEFS[W_GUN].shells[0].dmg, 28, "ball dmg");
        t.assert(WEAPON_DEFS[W_GUN].shells[0].reload, 70, "ball reload");
        t.assert(WEAPON_DEFS[W_GUN].shells[1].count, 5, "scatter count");
        t.assert(WEAPON_DEFS[W_GUN].shells[1].pellets, 3, "scatter pellets");
        suite.addTest(t);
    }

    {
        Test t("chain advances on hit, gap lock gates the window");
        Game g;
        initGame(g, W_SWORD);
        attackOnce(g);
        t.assert(g.player.chain, 1, "chain after first attack");
        t.assert(g.player.chainLock, CHAIN_GAP, "HEAVY gap lock 9");
        t.assert(g.player.chainWin, 0, "window closed during the lock");
        stepN(g, CHAIN_GAP);   // lock counts down
        t.assert(g.player.chainLock, 0, "lock expired");
        t.assert(g.player.chainWin, CHAIN_WIN, "window opens after the lock");
        stepN(g, CHAIN_WIN);   // window expires with no input
        t.assert(g.player.chain, 0, "chain resets when the window expires");
        t.assert(g.player.chainWin, 0, "window closed");
        // fresh press after the window restarts the chain
        attackOnce(g);
        t.assert(g.player.chain, 1, "chain again after fresh attack");
        suite.addTest(t);
    }

    {
        Test t("sword A then B = stepslash branch (after the gap lock)");
        Game g;
        initGame(g, W_SWORD);
        armTarget(g, g.player.x + 200, g.player.y);   // far away, no combo hit
        attackOnce(g);
        stepN(g, CHAIN_GAP);   // window opens; a tap in the open window branches
        t.assert(g.player.chainWin, CHAIN_WIN, "chain window open");
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
        t.assert(comboHit(g) ? 1 : 0, 1, "second hit starts through the gap lock");
        toRecovery(g);
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
        stepN(g, CHAIN_GAP);
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
        t.assert(comboHit(g) ? 1 : 0, 1, "second hit starts through the gap lock");
        toRecovery(g);
        tapB(g);
        t.assert(g.player.atk->id, ATK_TRIP, "trip id");
        t.assert(g.player.atk->effect, 1, "trip effect flag");
        suite.addTest(t);
    }

    {
        Test t("gunshield A then B = pointblank branch (shell gate retired)");
        Game g;
        initGame(g, W_GUN);
        armTarget(g, g.player.x + 20, g.player.y);   // within reach 15
        attackOnce(g);
        rec.reset();
        stepN(g, CHAIN_GAP);
        tapB(g);
        t.assert(g.player.atk->id, ATK_POINTBLANK, "pointblank id");
        t.assert(g.player.reload, 0, "no reload timer (shells retired)");
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
        t.assert(comboHit(g) ? 1 : 0, 1, "second hit starts through the gap lock");
        toRecovery(g);
        tapB(g);
        t.assert(g.player.atk->id, ATK_GUARDBASH, "guardbash id");
        t.assert(g.player.atk->push, 12, "guardbash push");
        suite.addTest(t);
    }

    {
        Test t("pointblank branch fires with the shell gate retired");
        Game g;
        initGame(g, W_GUN);
        attackOnce(g);
        stepN(g, CHAIN_GAP);
        tapB(g);
        t.assert(g.player.atk->id, ATK_POINTBLANK, "pointblank branch still fires");
        t.assert(g.player.state, PS_ATTACK, "pointblank enters the attack state");
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
        stepN(g, CHAIN_GAP);
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
        Test t("gunshield stance+A = hitscan arrowshot (stam cost, no projectile)");
        Game g;
        initGame(g, W_GUN);
        holdToStance(g);
        const uint8_t stam0 = g.player.stam;
        stepN(g, 1, Input{0, 0, true, true});   // A press while holding B
        t.assert(g.player.state, PS_SPECIAL, "arrowshot enters PS_SPECIAL");
        t.assert(g.player.atk == weaponSpecial(&WEAPON_DEFS[W_GUN]) ? 1 : 0, 1, "arrowshot attack data");
        t.assert(g.player.stam < stam0, 1, "stam spent on the shot");
        t.assert(g.projN, 0, "no projectile spawned (hitscan)");
        t.assertGreaterThan(g.fxN, 0, "muzzle spark spawned");
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

    // ------------------------------------------------- sheathe + draw windup (gis)
    {
        Test t("sheathe S2: A starts a rooted per-weapon draw, then combo hit 1");
        // Per-weapon draw length: sword fastest, gun slowest, flail between.
        t.assert(weaponDrawTicks(W_SWORD), 6, "sword draw 6");
        t.assert(weaponDrawTicks(W_FLAIL), 10, "flail draw 10");
        t.assert(weaponDrawTicks(W_GUN), 16, "gun draw 16");
        t.assertLessThan(weaponDrawTicks(W_SWORD), weaponDrawTicks(W_FLAIL), "sword draws before flail");
        t.assertLessThan(weaponDrawTicks(W_FLAIL), weaponDrawTicks(W_GUN), "flail draws before gun");

        const int8_t weapons[3] = {W_SWORD, W_FLAIL, W_GUN};
        for (int i = 0; i < 3; i++) {
            const int8_t w = weapons[i];
            Game g;
            initGame(g, w);
            stowWeapon(g);
            t.assert(g.player.sheathed, true, "weapon stowed");

            const int16_t x0 = g.player.x;
            const int16_t y0 = g.player.y;
            // Draw press with East held: the draw is rooted, so no movement.
            stepN(g, 1, Input{1, 0, true, false});
            t.assert(g.player.sheathed, false, "A draws the weapon");
            t.assert(g.player.state, PS_DRAW, "draw press enters PS_DRAW");
            t.assert(g.player.atk == nullptr ? 1 : 0, 1, "no attack during the draw");
            t.assert(static_cast<int16_t>(g.player.x - x0) + static_cast<int16_t>(g.player.y - y0), 0, "rooted on the draw press");

            // Count the ticks until combo hit 1 starts. DRAW_TICKS counts the
            // press tick (the first PS_DRAW update), so the total is n + 1.
            int n = 0;
            while (n < 40 && g.player.state == PS_DRAW) {
                stepN(g, 1, Input{1, 0, false, false});   // direction still held
                n++;
            }
            t.assert(g.player.state, PS_ATTACK, "draw ends in an attack");
            t.assert(n + 1, weaponDrawTicks(w), "exact per-weapon draw length");
            t.assert(static_cast<int16_t>(g.player.x - x0) + static_cast<int16_t>(g.player.y - y0), 0, "rooted through the whole draw");
            // Combo hit 1 starts when the weapon is out: the attacks[0] data.
            const Attack *a0 = &WEAPON_DEFS[w].attacks[0];
            t.assert(attackStartup(g.player.atk), a0->startup, "draw attack = combo hit 1 startup");
            t.assert(attackDmg(g.player.atk), a0->dmg, "draw attack = combo hit 1 dmg");
        }

        // Damage mid-draw cancels to idle with the weapon out.
        Game h;
        initGame(h, W_GUN);
        stowWeapon(h);
        stepN(h, 1, Input{0, 0, true, false});   // draw press -> PS_DRAW
        t.assert(h.player.state, PS_DRAW, "gun draw running");
        playerHurt(h, 5, 1, 0);
        t.assert(h.player.state, PS_IDLE, "damage cancels the draw to idle");
        t.assert(h.player.sheathed, false, "weapon stays out after a draw cancel");
        t.assert(h.player.atk == nullptr ? 1 : 0, 1, "no attack left armed");

        // A B tap during the draw does not dodge (sword would roll).
        Game b;
        initGame(b, W_SWORD);
        stowWeapon(b);
        stepN(b, 1, Input{0, 0, true, false});   // draw press -> PS_DRAW
        tapB(b);
        t.assert(b.player.state, PS_DRAW, "B tap does not cancel the draw");
        t.assert(b.player.state != PS_DODGE ? 1 : 0, 1, "no dodge out of the draw");

        // Draw-and-keep-holding must not bounce straight back into the sheath.
        Game k;
        initGame(k, W_SWORD);
        stowWeapon(k);
        stepN(k, 1, Input{0, 0, true, false});                     // draw press
        stepN(k, STOW_HOLD_TICKS + 8, Input{0, 0, true, false});   // keep holding
        t.assert(k.player.sheathed, false, "draw hold does not re-stow");
        suite.addTest(t);
    }

    {
        Test t("sheathe S2: hold A through a stance special stows from the stance");
        Game g;
        initGame(g, W_SWORD);
        holdToStance(g);   // B held -> parry
        t.assert(g.player.stance, ST_PARRY, "stance up before the stow");
        stepN(g, 1, Input{0, 0, true, true});                     // A in stance -> riposte special
        stepN(g, STOW_HOLD_TICKS + 4, Input{0, 0, true, true});   // keep holding
        t.assert(g.player.sheathed, true, "stowed from stance");
        t.assert(g.player.stance, ST_NONE, "stance dropped");
        t.assert(g.player.sheatheLatch, false, "no stale latch (S2)");
        t.assert(g.player.state, PS_IDLE, "idle, no roll");
        suite.addTest(t);
    }

    {
        Test t("S2: d-pad double-tap rolls down without B and in stance");
        Game g;
        initGame(g, W_SWORD);
        doubleTap(g, 0, 1);
        t.assert(g.player.sheathed, false, "not stowed");
        t.assert(g.player.state, PS_DODGE, "rolls down");
        t.assertGreaterThan(g.player.vy, 0, "rolls south");

        Game h;
        initGame(h, W_SWORD);
        holdToStance(h);                         // B held -> parry; Down now rolls instead of stowing
        stepN(h, 1, Input{0, 1, false, true});   // tap 1 Down
        stepN(h, 1, Input{0, 0, false, true});
        stepN(h, 1, Input{0, 1, false, true});   // tap 2: roll out of the stance
        t.assert(h.player.sheathed, false, "stance down-tap does not stow");
        t.assert(h.player.state, PS_DODGE, "rolls down out of the stance");
        suite.addTest(t);
    }

    {
        Test t("S2: B-held double-tap of any direction rolls, never stows");
        Game g;
        initGame(g, W_SWORD);
        stepN(g, 1, Input{0, 0, false, true});   // B down
        stepN(g, 1, Input{1, 0, false, true});   // tap 1 East
        stepN(g, 1, Input{0, 0, false, true});
        stepN(g, 1, Input{1, 0, false, true});   // tap 2: rolls east
        t.assert(g.player.sheathed, false, "east double-tap does not stow");
        t.assert(g.player.state, PS_DODGE, "rolls east instead");
        suite.addTest(t);

        initGame(g, W_SWORD);
        stepN(g, 1, Input{0, 0, false, true});    // B down
        stepN(g, 1, Input{0, -1, false, true});   // tap 1 Up
        stepN(g, 1, Input{0, 0, false, true});
        stepN(g, 1, Input{0, -1, false, true});   // tap 2
        t.assert(g.player.sheathed, false, "north double-tap does not stow");
        t.assert(g.player.state, PS_DODGE, "rolls north instead");
        suite.addTest(t);
    }

    {
        Test t("S2: A-stow leaves the B verbs live (no stale latch)");
        Game g;
        initGame(g, W_SWORD);
        g.items[ITEM_HERB] = 1;
        stowWeapon(g);
        t.assert(g.player.sheatheLatch, false, "no latch at the stow");
        stepN(g, HOLD_TICKS + 2, Input{0, 0, false, true});
        t.assert(g.player.state, PS_ITEM, "stowed B hold still uses a herb");
        t.assert(g.player.sheathed, true, "still stowed");
        suite.addTest(t);
    }

    {
        Test t("sheathe feel.23: stowed B tap is inert, run speed 24");
        Game g;
        initGame(g, W_FLAIL);
        stowWeapon(g);
        stepN(g, 1);   // release B
        g.player.stam = 100;
        tapB(g);
        t.assert(g.player.state, PS_IDLE, "stowed B tap does nothing");
        t.assert(g.player.iT, 0, "no i-frames from a stowed B tap");
        t.assert(g.player.stam, 100, "no stamina spent");

        Game w;
        initGame(w, W_SWORD);
        stowWeapon(w);
        stepN(w, 1);
        const int x0 = w.player.x;
        stepN(w, 10, Input{1, 0, false, false});
        t.assert(w.player.x - x0, 15, "stowed run 24/16 px per tick (15 px in 10t)");
        suite.addTest(t);
    }

    {
        Test t("guard strafe: d-pad moves with locked facing (gun)");
        Game g;
        initGame(g, W_GUN);
        stepN(g, 4, Input{1, 0, false, false});   // face east
        t.assert(g.player.fx, 16, "facing east");
        holdToStance(g);   // hold B -> guard
        t.assert(g.player.stance, ST_GUARD, "guard up");
        const int x0 = g.player.x;
        stepN(g, 16, Input{-1, 0, false, true});   // strafe west, shield still east
        t.assert(g.player.fx, 16, "facing locked while strafing");
        t.assertLessThan(g.player.x, x0, "strafed west");
        // Diagonal strafe keeps the sub-pixel fraction: guard sp 2, axis step
        // 11*2 = 22/256 px/tick, so 8 ticks carry 176/256 = 11/16 px per axis
        // (the pre-fix truncating move dropped it to 8/16).
        const int16_t dx0 = g.player.x;
        const int8_t dsx0 = g.player.subX;
        stepN(g, 8, Input{1, 1, false, true});   // strafe SE
        t.assert(static_cast<int16_t>((g.player.x - dx0) * 16 + (g.player.subX - dsx0)), 11, "diagonal strafe carries 11/16 px per axis in 8t");
        suite.addTest(t);
    }

    {
        Test t("A buffer: press during the gap lock fires when it expires");
        Game g;
        initGame(g, W_SWORD);
        attackOnce(g);
        t.assert(g.player.chainLock, CHAIN_GAP, "lock armed");
        stepN(g, 1, Input{0, 0, true, false});   // press during the lock
        t.assert(g.player.state, PS_IDLE, "press during the lock is buffered");
        t.assertGreaterThan(g.player.aBuffer, 0, "aBuffer armed");
        for (int i = 0; i < 40 && g.player.state != PS_ATTACK; i++)
            stepN(g, 1);
        t.assert(g.player.state, PS_ATTACK, "buffered press fires after the lock");
        t.assert(g.player.aBuffer, 0, "buffer consumed");
        suite.addTest(t);
    }

    {
        Test t("debounce: finisher lock 24, an early press expires");
        Game g;
        initGame(g, W_SWORD);
        t.assert(comboHit(g) ? 1 : 0, 1, "hit 1");
        waitIdle(g);
        t.assert(comboHit(g) ? 1 : 0, 1, "hit 2");
        waitIdle(g);
        t.assert(comboHit(g) ? 1 : 0, 1, "hit 3 (finisher)");
        waitIdle(g);
        t.assert(g.player.chain, 0, "finisher resets the chain");
        t.assert(g.player.chainLock, COMBO_LOCK, "finisher lock 24");
        t.assert(g.player.chainWin, 0, "window closed");
        stepN(g, 1, Input{0, 0, true, false});   // early press (aBuffer 16 < 24)
        stepN(g, 30);
        t.assert(g.player.state, PS_IDLE, "early single press was dropped");
        t.assert(comboHit(g) ? 1 : 0, 1, "fresh press after the lock restarts");
        suite.addTest(t);
    }

    {
        Test t("B buffer: recovery tap queues the stage-2 branch through the lock");
        Game g;
        initGame(g, W_SWORD);
        armTarget(g, g.player.x + 200, g.player.y);
        t.assert(comboHit(g) ? 1 : 0, 1, "hit 1");
        waitIdle(g);
        t.assert(comboHit(g) ? 1 : 0, 1, "hit 2 running");
        toRecovery(g);
        stepN(g, 1, Input{0, 0, false, true});   // tap B in recovery
        for (int i = 0; i < 8; i++)
            stepN(g, 1, Input{0, 0, false, true});   // hold through completion
        stepN(g, 1);                                 // release during the lock
        t.assert(g.player.state, PS_IDLE, "no roll while the branch is queued");
        t.assertGreaterThan(g.player.bBuffer, 0, "B buffer still pending");
        for (int i = 0; i < 40 && g.player.state != PS_ATTACK; i++)
            stepN(g, 1);
        t.assert(g.player.state, PS_ATTACK, "queued branch fires when the window opens");
        t.assert(g.player.atk->id, ATK_SPINCUT, "stage-2 branch runs");
        suite.addTest(t);
    }

    // -------------------------------------- stage-3 finisher (7pw)
    {
        Test t("stage-3 finisher: A A A then B runs the finisher branch (all weapons)");
        const int8_t ws[3] = {W_SWORD, W_FLAIL, W_GUN};
        const int16_t fdmg[3] = {26, 32, 30};
        const int16_t freach[3] = {16, 24, 16};
        for (int k = 0; k < 3; k++) {
            Game g;
            initGame(g, ws[k]);
            t.assert(comboHit(g) ? 1 : 0, 1, "hit 1");
            waitIdle(g);
            t.assert(comboHit(g) ? 1 : 0, 1, "hit 2");
            waitIdle(g);
            t.assert(comboHit(g) ? 1 : 0, 1, "hit 3 (finisher)");
            waitIdle(g);
            t.assert(g.player.finWin, true, "finWin armed");
            t.assert(g.player.chain, 0, "chain reset for the finisher");
            stepN(g, 1, Input{0, 0, false, true});   // tap B: queues through the finisher lock
            stepN(g, 1);
            t.assertGreaterThan(g.player.bBuffer, 0, "finisher branch queued");
            for (int i = 0; i < 60 && g.player.state != PS_ATTACK; i++)
                stepN(g, 1);
            t.assert(g.player.state, PS_ATTACK, "finisher branch runs");
            t.assert(g.player.atk->dmg, fdmg[k], "finisher dmg");
            t.assert(g.player.atk->reach, freach[k], "finisher reach");
            t.assert(g.player.finWin, false, "branch clears finWin");
        }
        suite.addTest(t);
    }

    {
        Test t("stage-3 finWin: cleared on window expiry and by a fresh attack");
        Game g;
        initGame(g, W_SWORD);
        t.assert(comboHit(g) ? 1 : 0, 1, "hit 1");
        waitIdle(g);
        t.assert(comboHit(g) ? 1 : 0, 1, "hit 2");
        waitIdle(g);
        t.assert(comboHit(g) ? 1 : 0, 1, "hit 3 (finisher)");
        waitIdle(g);
        t.assert(g.player.finWin, true, "finWin armed");
        stepN(g, COMBO_LOCK);   // lock expires -> the combo window opens
        t.assert(g.player.chainWin, CHAIN_WIN, "window open after the lock");
        stepN(g, 1, Input{0, 0, true, false});   // A instead of B: fresh attack
        t.assert(g.player.state, PS_ATTACK, "fresh attack starts");
        t.assert(g.player.finWin, false, "startAttack clears finWin");

        Game h;
        initGame(h, W_SWORD);
        t.assert(comboHit(h) ? 1 : 0, 1, "hit 1");
        waitIdle(h);
        t.assert(comboHit(h) ? 1 : 0, 1, "hit 2");
        waitIdle(h);
        t.assert(comboHit(h) ? 1 : 0, 1, "hit 3 (finisher)");
        waitIdle(h);
        stepN(h, COMBO_LOCK);   // lock -> chainWin = CHAIN_WIN
        t.assert(h.player.finWin, true, "finWin armed in the window");
        stepN(h, CHAIN_WIN);   // let the window expire in idle
        t.assert(h.player.chainWin, 0, "window expired");
        t.assert(h.player.finWin, false, "expiry clears finWin");
        suite.addTest(t);
    }

    {
        Test t("B branch buffer: early B in startup still dodge-cancels");
        Game g;
        initGame(g, W_SWORD);
        armTarget(g, g.player.x + 200, g.player.y);
        t.assert(comboHit(g) ? 1 : 0, 1, "hit 1 running");
        stepN(g, 1, Input{0, 0, false, true});   // startup: no branch, no buffer
        stepN(g, 1);
        t.assert(g.player.state, PS_DODGE, "sword dodge-cancel preserved");
        t.assert(g.player.bBuffer, 0, "nothing queued");
        suite.addTest(t);
    }

    // -------------------------------- roll attack + direction+A alt (8xx)
    {
        Test t("roll attack: A out of the evade runs the weapon roll move");
        // sword dodge -> rollslash
        Game g;
        initGame(g, W_SWORD);
        tapB(g);
        t.assert(g.player.state, PS_DODGE, "sword evade state");
        stepN(g, 1, Input{0, 0, true, false});
        t.assert(g.player.state, PS_ATTACK, "sword roll attack started");
        t.assert(g.player.atk->id, ATK_NONE, "sword roll id");
        t.assert(g.player.atk->reach, 15, "sword roll reach");
        t.assert(g.player.atk->hw, 16, "sword roll hw");
        t.assert(g.player.atk->hh, 14, "sword roll hh");
        t.assert(g.player.atk->dmg, 12, "sword roll dmg");
        t.assert(g.player.atk->stam, 10, "sword roll stam");
        t.assert(meleeHitbox(g.player, g.player.atk).w, 16, "sword roll box w");
        t.assert(meleeHitbox(g.player, g.player.atk).h, 14, "sword roll box h");
        t.assertGreaterThan(g.player.iT, 0, "dodge i-frames keep ticking");

        // flail deflect -> rollsweep
        initGame(g, W_FLAIL);
        tapB(g);
        t.assert(g.player.state, PS_DEFLECT, "flail evade state");
        stepN(g, 1, Input{0, 0, true, false});
        t.assert(g.player.state, PS_ATTACK, "flail roll attack started");
        t.assert(g.player.atk->reach, 20, "flail roll reach");
        t.assert(g.player.atk->hw, 24, "flail roll hw");
        t.assert(g.player.atk->hh, 16, "flail roll hh");
        t.assert(g.player.atk->dmg, 15, "flail roll dmg");
        t.assert(g.player.atk->stam, 10, "flail roll stam");

        // gun shove -> shieldbash
        initGame(g, W_GUN);
        tapB(g);
        t.assert(g.player.state, PS_SHOVE, "gun evade state");
        stepN(g, 1, Input{0, 0, true, false});
        t.assert(g.player.state, PS_ATTACK, "gun roll attack started");
        t.assert(g.player.atk->reach, 14, "gun roll reach");
        t.assert(g.player.atk->hw, 16, "gun roll hw");
        t.assert(g.player.atk->hh, 14, "gun roll hh");
        t.assert(g.player.atk->dmg, 8, "gun roll dmg");
        t.assert(g.player.atk->push, 10, "gun roll push");
        t.assert(g.player.atk->stam, 8, "gun roll stam");
        suite.addTest(t);
    }

    {
        Test t("roll attack: shield bash lunges the hunter forward (lunge 30)");
        Game g;
        initGame(g, W_GUN);
        tapB(g);
        t.assert(g.player.state, PS_SHOVE, "shove state");
        const int x0 = g.player.x;
        stepN(g, 1, Input{0, 0, true, false});
        t.assertGreaterThan(g.player.vx, 0, "forward velocity applied");
        stepN(g, 8);
        t.assertGreaterThan(g.player.x, x0, "bash carried the hunter forward");
        suite.addTest(t);
    }

    {
        Test t("shove bash: small forward step, shield thrust stays (feel.24)");
        Game g;
        initGame(g, W_GUN);
        const int x0 = g.player.x;
        tapB(g);
        t.assert(g.player.state, PS_SHOVE, "shove state");
        t.assertGreaterThan(g.player.vx, 0, "bash arms a forward velocity");
        for (int i = 0; i < 14 && g.player.state == PS_SHOVE; i++)
            stepN(g, 1);
        t.assert(g.player.state, PS_IDLE, "shove ends");
        t.assertGreaterThan(g.player.x, x0, "bash stepped the hunter forward");
        t.assertLessThan(g.player.x - x0, 20, "the step stays small (~12 px)");
        suite.addTest(t);
    }

    {
        Test t("direction + A: thrust opener replaces combo hit 1");
        Game g;
        initGame(g, W_SWORD);
        stepN(g, 4, Input{1, 0, false, false});   // face east, moving
        stepN(g, 1, Input{1, 0, true, false});
        t.assert(g.player.state, PS_ATTACK, "thrust runs");
        t.assert(g.player.atk->reach, 22, "thrust reach (alt data)");
        t.assert(g.player.atk->hw, 10, "thrust hw");
        t.assert(g.player.atk->hh, 10, "thrust hh");
        t.assert(g.player.atk->dmg, 14, "thrust dmg");
        t.assertGreaterThan(g.player.vx, 0, "thrust lunges forward");
        suite.addTest(t);
    }

    {
        Test t("direction + A: mid-combo hits keep the normal combo data");
        Game g;
        initGame(g, W_SWORD);
        stepN(g, 4, Input{1, 0, false, false});
        stepN(g, 1, Input{1, 0, true, false});   // alt hit 1
        t.assert(g.player.atk->reach, 22, "alt hit 1");
        waitIdle(g);
        stepN(g, CHAIN_GAP);                     // lock counts down, chain 1
        stepN(g, 1, Input{1, 0, true, false});   // chain 1 -> normal combo hit 2
        t.assert(g.player.state, PS_ATTACK, "hit 2 started");
        t.assert(g.player.atk->reach, 13, "normal combo hit 2 data");
        suite.addTest(t);
    }

    {
        Test t("debounce lock still gates attack entry (alt cannot bypass it)");
        Game g;
        initGame(g, W_SWORD);
        attackOnce(g);
        t.assert(g.player.chainLock, CHAIN_GAP, "lock armed");
        stepN(g, 1, Input{1, 0, true, false});   // direction+A during the lock
        t.assert(g.player.state, PS_IDLE, "no attack mid-lock");
        t.assertGreaterThan(g.player.aBuffer, 0, "press buffered, not lost");
        suite.addTest(t);
    }

    // ---------------------------------------- charge attacks (ynb, mock game.test.js)
    {
        Test t("charge: flail hold A past the swing -> single-level chargeslam1");
        // charge-lite (prg.11): one level, slot 0 only.
        Game g;
        initGame(g, W_FLAIL);
        for (int i = 0; i < 80 && g.player.state != PS_CHARGE; i++)
            stepPlayer(g, Input{0, 0, true, false});
        t.assert(g.player.state, PS_CHARGE, "charge entered after the swing");
        t.assert(g.player.chargeT, 0, "meter starts at 0");
        stepPlayer(g, Input{0, 0, false, false});   // release right away
        t.assert(g.player.state, PS_ATTACK, "charge swing runs");
        t.assert(g.player.atk->dmg, 24, "chargeslam1 dmg");
        t.assert(g.player.atk->reach, 26, "chargeslam1 reach");
        t.assert(g.player.atk->hw, 28, "chargeslam1 hw");
        t.assert(g.player.atk->hh, 18, "chargeslam1 hh");
        t.assert(g.player.atk->stam, 14, "chargeslam1 stam");

        // Holding into the charge window then releasing still fires the single
        // level: no chargeslam2 tier.
        Game g2;
        initGame(g2, W_FLAIL);
        for (int i = 0; i < 80 && g2.player.state != PS_CHARGE; i++)
            stepPlayer(g2, Input{0, 0, true, false});
        for (int i = 0; i < 10; i++)
            stepPlayer(g2, Input{0, 0, true, false});
        t.assertGreaterThan(g2.player.chargeT, 5, "held inside the charge window");
        stepPlayer(g2, Input{0, 0, false, false});
        t.assert(g2.player.state, PS_ATTACK, "charged swing runs");
        t.assert(g2.player.atk->dmg, 24, "hold still chargeslam1");
        t.assert(g2.player.atk->stam, 14, "hold still chargeslam1 stam");

        // S2: holding past CHARGE_MIN + STOW_HOLD stows instead.
        Game g3;
        initGame(g3, W_FLAIL);
        for (int i = 0; i < 80 && g3.player.state != PS_CHARGE; i++)
            stepPlayer(g3, Input{0, 0, true, false});
        for (int i = 0; i < STOW_HOLD_TICKS + 4; i++)
            stepPlayer(g3, Input{0, 0, true, false});
        t.assert(g3.player.sheathed, 1, "flail long hold stows");
        t.assert(g3.player.state, PS_IDLE, "flail stow settles at idle");
        suite.addTest(t);
    }

    {
        Test t("charge: gun never enters PS_CHARGE (charged ball removed, prg.11)");
        Game g;
        initGame(g, W_GUN);
        initWorld(g, MODE_HUNT);
        for (int i = 0; i < 80; i++) {
            stepWorld(g, Input{0, 0, true, false});
            if (g.player.state == PS_CHARGE)
                break;
        }
        t.assert(g.player.state != PS_CHARGE ? 1 : 0, 1, "gun has no charge stance");
        t.assert(weaponHasCharge(&WEAPON_DEFS[W_GUN]), 0, "gun has no melee charge");
        t.assert(g.player.sheathed, 1, "gun long hold stows (S2)");
        // A long hold + release fires no charged ball; the shot record stays clear.
        stepWorld(g, Input{0, 0, false, false});
        t.assert(g.projN, 0, "no charged ball spawned");
        t.assert(g.lastShot, 0, "no charged shot queued");
        suite.addTest(t);
    }

    {
        Test t("charge: sword never enters PS_CHARGE (no charge data)");
        Game g;
        initGame(g, W_SWORD);
        for (int i = 0; i < 40; i++) {
            stepPlayer(g, Input{0, 0, true, false});
            if (g.player.state == PS_CHARGE)
                break;
        }
        t.assert(g.player.state != PS_CHARGE ? 1 : 0, 1, "sword has no charge");
        t.assert(weaponHasCharge(&WEAPON_DEFS[W_SWORD]), 0, "no melee charge");
        t.assert(g.player.sheathed, 1, "sword long hold stows (S2)");
        suite.addTest(t);
    }

    {
        Test t("charge: a tap does not charge (chargeArmed cleared on release)");
        Game g;
        initGame(g, W_FLAIL);
        stepPlayer(g, Input{0, 0, true, false});   // press: starts the swing
        t.assert(g.player.chargeArmed, 1, "swing arms the charge");
        stepPlayer(g, Input{0, 0, false, false});   // release mid-swing
        t.assert(g.player.chargeArmed, 0, "release clears the latch");
        for (int i = 0; i < 40; i++)
            stepPlayer(g, Input{0, 0, false, false});
        t.assert(g.player.state, PS_IDLE, "no charge after a tap");
        suite.addTest(t);
    }

    // ------------------------------- double-tap d-pad roll (feel.16)
    // feel.18: the double-tap roll is universal — sword, flail and gun all use
    // the sword dodge numbers. Only the B tap stays weapon-specific.
    {
        Test t("double-tap E rolls PS_DODGE (iT 14, stam 14) for every weapon");
        const int8_t ws[3] = {W_SWORD, W_FLAIL, W_GUN};
        for (int k = 0; k < 3; k++) {
            Game g;
            initGame(g, ws[k]);
            g.player.stam = 100;
            g.player.stamSub = 0;
            doubleTap(g, 1, 0);
            t.assert(g.player.state, PS_DODGE, "double-tap dodge state");
            t.assert(g.player.iT, 14, "i-frames 14");
            t.assert(g.player.t, 15, "dodge length (16 set, same-tick ticks once)");
            t.assert(g.player.stam, 86, "roll cost 14");
            t.assertGreaterThan(g.player.vx, 0, "rolls east");
        }
        suite.addTest(t);
    }

    {
        Test t("double-tap rolls the tapped direction (non-sword) + facing");
        Game g;
        initGame(g, W_FLAIL);
        g.player.stam = 100;
        g.player.stamSub = 0;
        doubleTap(g, 0, 1);   // south
        t.assert(g.player.state, PS_DODGE, "flail rolls, not deflects");
        t.assertGreaterThan(g.player.vy, 0, "south velocity");
        t.assert(g.player.fy, 16, "facing south");

        initGame(g, W_GUN);
        g.player.stam = 100;
        g.player.stamSub = 0;
        doubleTap(g, -1, 0);   // west
        t.assert(g.player.state, PS_DODGE, "gun rolls, not shoves");
        t.assertLessThan(g.player.vx, 0, "west velocity");
        t.assert(g.player.fx, -16, "facing west");
        suite.addTest(t);
    }

    {
        Test t("B tap stays weapon-specific (sword dodge, flail deflect, gun shove)");
        Game g;
        initGame(g, W_SWORD);
        tapB(g);
        t.assert(g.player.state, PS_DODGE, "sword B tap dodge");

        initGame(g, W_FLAIL);
        tapB(g);
        t.assert(g.player.state, PS_DEFLECT, "flail B tap deflect");

        initGame(g, W_GUN);
        tapB(g);
        t.assert(g.player.state, PS_SHOVE, "gun B tap shove");
        suite.addTest(t);
    }

    {
        Test t("double-tap timing: single, expired window and different dir do not roll");
        Game g;
        initGame(g, W_SWORD);
        stepN(g, 1, Input{1, 0, false, false});
        stepN(g, 1, Input{0, 0, false, false});
        t.assert(g.player.state, PS_IDLE, "single tap does not roll");

        initGame(g, W_SWORD);
        stepN(g, 1, Input{1, 0, false, false});
        stepN(g, 1, Input{0, 0, false, false});
        stepN(g, DTAP_WIN + 1);
        stepN(g, 1, Input{1, 0, false, false});
        stepN(g, 1, Input{0, 0, false, false});
        t.assert(g.player.state, PS_IDLE, "second tap after the window does not roll");

        initGame(g, W_SWORD);
        stepN(g, 1, Input{1, 0, false, false});
        stepN(g, 1, Input{0, 0, false, false});
        stepN(g, 1, Input{-1, 0, false, false});
        stepN(g, 1, Input{0, 0, false, false});
        t.assert(g.player.state, PS_IDLE, "different direction does not roll");
        suite.addTest(t);
    }

    {
        Test t("held direction does not roll (no press edge)");
        Game g;
        initGame(g, W_SWORD);
        stepN(g, 30, Input{1, 0, false, false});
        t.assert(g.player.state, PS_IDLE, "held east stays idle");
        suite.addTest(t);
    }

    {
        Test t("double-tap gated: zero stamina and non-cancelable attack do not roll");
        Game g;
        initGame(g, W_SWORD);
        g.player.stam = 0;
        g.player.stamSub = 0;
        doubleTap(g, 1, 0);
        t.assert(g.player.state, PS_IDLE, "no stamina, no roll");

        initGame(g, W_FLAIL);
        stepN(g, 1, Input{0, 0, true, false});   // swing: flail cannot cancel
        t.assert(g.player.state, PS_ATTACK, "flail attack running");
        doubleTap(g, 1, 0);
        t.assert(g.player.state, PS_ATTACK, "cannot roll out of a flail attack");
        suite.addTest(t);
    }

    {
        Test t("sheathed double-tap rolls with the stowed numbers (stam 14, vx 54)");
        Game g;
        initGame(g, W_FLAIL);
        stowWeapon(g);
        stepN(g, 1);   // release B, clears the sheathe latch
        g.player.stam = 100;
        g.player.stamSub = 0;
        doubleTap(g, 1, 0);
        t.assert(g.player.sheathed, true, "still stowed");
        t.assert(g.player.state, PS_DODGE, "stowed roll state");
        t.assert(g.player.iT, 14, "stowed roll i-frames");
        t.assert(g.player.stam, 86, "stowed roll cost 14");
        t.assertGreaterThan(g.player.vx, 40, "stowed roll velocity from 54");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
