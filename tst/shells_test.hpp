#pragma once
// Host unit tests for src/core/projectiles.hpp — shells, pellets, muzzle /
// damage-number effects, training pole and train DPS. Permanent, co-located
// with the repo tests. mock/game.js is the source of truth for every number
// (mock/game.test.js covers the same feel via the browser harness).
#include "test.hpp"
#include "../src/core/projectiles.hpp"

using namespace mh;

namespace {

const Input IDLE = Input{0, 0, false, false};
const Input HOLD_B = Input{0, 0, false, true};

// hold B until the gunshield guard stance is up
void holdGuard(Game &g) {
    for (int i = 0; i < HOLD_TICKS + 2; i++)
        stepWorld(g, HOLD_B);
}

// one A press while B (guard) is held; the shell fires this tick
void fireA(Game &g) {
    stepWorld(g, Input{0, 0, true, true});
}

// release A, keep holding B / guard
void releaseA(Game &g) {
    stepWorld(g, Input{0, 0, false, true});
}

void idleGuard(Game &g, int n) {
    for (int i = 0; i < n; i++)
        stepWorld(g, HOLD_B);
}

void idleTicks(Game &g, int n) {
    for (int i = 0; i < n; i++)
        stepWorld(g, IDLE);
}

// tap (not hold) B
void tapBranch(Game &g) {
    stepWorld(g, Input{0, 0, false, true});
    stepWorld(g, Input{0, 0, false, false});
}

// arm a shot record exactly as player.hpp fireShell() would, then spawn it
// without a game tick (checks spawn geometry / life before updateProjectiles)
void armShot(Game &g, int8_t shot, int16_t cx, int16_t cy, int16_t fx, int16_t fy) {
    g.lastShot = shot;
    g.lastShotX = cx;
    g.lastShotY = cy;
    g.lastShotFx = fx;
    g.lastShotFy = fy;
    spawnShot(g);
}

// hunt game with a parked, inert beast so shell tests are deterministic
void initHuntWorld(Game &g) {
    initMonster(g);
    g.monster.state = MS_RECOVER;
    g.monster.t = 9999;
    g.monster.cd = 9999;
    g.monster.x = 200;
    g.monster.y = 40;
    initWorld(g, MODE_HUNT);
}

}   // namespace

void ShellSuite(TestRunner &runner) {
    TestSuite suite("Shells, projectiles, effects + training pole (src/core/projectiles.hpp)");

    {
        Test t("shell tables: ball 1 pellet heavy, scatter 3 pellets");
        const ShellDef &ball = WEAPON_DEFS[W_GUN].shells[0];
        const ShellDef &scat = WEAPON_DEFS[W_GUN].shells[1];
        t.assert(ball.count, 2, "ball count");
        t.assert(ball.dmg, 28, "ball dmg");
        t.assert(ball.speedF, 35, "ball speedF");
        t.assert(ball.w, 7, "ball w");
        t.assert(ball.h, 6, "ball h");
        t.assert(ball.reload, 70, "ball reload");
        t.assert(ball.pellets, 1, "ball pellets");
        t.assert(scat.count, 5, "scatter count");
        t.assert(scat.pellets, 3, "scatter pellets");
        t.assert(scat.dmg, 7, "scatter dmg");
        t.assert(scat.speedF, 42, "scatter speedF");
        t.assert(scat.reload, 30, "scatter reload");
        t.assert(PROJ_LIFE, 90, "projectile life");
        suite.addTest(t);
    }

    {
        Test t("spawn geometry: centre + facing*13, 1/16 px velocity, life 90");
        Game g;
        initGame(g, W_GUN);
        initWorld(g, MODE_HUNT);
        armShot(g, 1, 104, 68, 16, 0);   // ball, facing E
        t.assert(g.projN, 1, "one ball projectile");
        t.assert(g.proj[0].x, 104, "spawn x = player centre");
        t.assert(g.proj[0].y, 68, "spawn y = player centre");
        t.assert(g.proj[0].subX, 13, "spawn subX = facing*13 (1/16 px)");
        t.assert(g.proj[0].subY, 0, "spawn subY");
        t.assert(g.proj[0].vx, 35, "ball vx = speedF (1/16 px/tick)");
        t.assert(g.proj[0].vy, 0, "ball vy");
        t.assert(g.proj[0].w, 7, "ball w");
        t.assert(g.proj[0].h, 6, "ball h");
        t.assert(g.proj[0].dmg, 28, "ball dmg");
        t.assert(g.proj[0].life, 90, "ball life");
        t.assert(g.proj[0].heavy, 1, "ball heavy flag");
        t.assert(g.fxN, 1, "muzzle effect");
        t.assert(g.fx[0].x, 114, "muzzle 10 px along facing");
        t.assert(g.fx[0].y, 68, "muzzle y");
        t.assert(g.fx[0].life, 5, "muzzle life");
        t.assert(g.fx[0].crit, 1, "muzzle crit flag");
        suite.addTest(t);
    }

    {
        Test t("scatter spawn: 3 pellets rotFp cos15 sin6, left/centre/right");
        Game g;
        initGame(g, W_GUN);
        initWorld(g, MODE_HUNT);
        armShot(g, 2, 104, 68, 16, 0);   // scatter, facing E
        t.assert(g.projN, 3, "three pellets");
        t.assert(g.proj[0].heavy, 0, "left pellet not heavy");
        t.assert(g.proj[1].heavy, 0, "centre pellet not heavy");
        t.assert(g.proj[2].heavy, 0, "right pellet not heavy");
        t.assert(g.proj[0].vx, 39, "left vx");
        t.assert(g.proj[0].vy, 15, "left vy (+22 deg)");
        t.assert(g.proj[0].subX, 12, "left subX");
        t.assert(g.proj[1].vx, 42, "centre vx = speedF");
        t.assert(g.proj[1].vy, 0, "centre vy");
        t.assert(g.proj[1].subX, 13, "centre subX = facing*13");
        t.assert(g.proj[2].vx, 39, "right vx");
        t.assert(g.proj[2].vy, -16, "right vy (-22 deg, arithmetic shift floors)");
        t.assert(g.proj[2].subX, 12, "right subX");
        for (int i = 0; i < 3; i++) {
            t.assert(g.proj[i].w, 4, "pellet w");
            t.assert(g.proj[i].h, 4, "pellet h");
            t.assert(g.proj[i].dmg, 7, "pellet dmg");
            t.assert(g.proj[i].life, 90, "pellet life");
        }
        suite.addTest(t);
    }

    {
        Test t("guard+A fires a ball, demo ammo stays full, reloads");
        Game g;
        initGame(g, W_GUN);
        initHuntWorld(g);
        holdGuard(g);
        t.assert(g.player.stance, ST_GUARD, "guard entered");
        fireA(g);
        t.assert(g.player.shells[0], 2, "demo: ammo unlimited (stays 2)");
        t.assert(g.player.reload, 70, "ball reload armed");
        t.assert(g.projN, 1, "one ball projectile");
        t.assert(g.proj[0].heavy, 1, "ball is heavy");
        t.assert(g.proj[0].w, 7, "ball w");
        t.assert(g.proj[0].h, 6, "ball h");
        t.assert(g.proj[0].dmg, 28, "ball dmg");
        t.assert(g.lastShot, 0, "shot record cleared");
        t.assert(g.fxN, 1, "muzzle effect");
        t.assert(g.fx[0].life, 5, "muzzle life");
        t.assert(g.fx[0].crit, 1, "muzzle crit flag");
        suite.addTest(t);
    }

    {
        Test t("reload timer blocks a second shot, ammo stays full");
        Game g;
        initGame(g, W_GUN);
        initHuntWorld(g);
        holdGuard(g);
        fireA(g);
        releaseA(g);
        fireA(g);
        t.assert(g.player.shells[0], 2, "no consume: ammo unlimited");
        t.assert(g.projN, 1, "no extra projectile");
        suite.addTest(t);
    }

    {
        Test t("demo unlimited ammo: shots stay paced by reload; empty clip still blocks");
        Game g;
        initGame(g, W_GUN);
        initHuntWorld(g);
        holdGuard(g);
        fireA(g);
        t.assert(g.projN, 1, "shot 1");
        t.assert(g.player.shells[0], 2, "ammo stays full after shot 1");
        releaseA(g);
        idleGuard(g, 70);   // reload 70 -> 0, guard held
        t.assert(g.player.reload, 0, "reload cleared");
        g.projN = 0;   // ignore in-flight culling; assert ammo/spawn only
        fireA(g);
        t.assert(g.player.shells[0], 2, "ammo stays full after shot 2");
        t.assert(g.projN, 1, "shot 2");
        releaseA(g);
        idleGuard(g, 70);
        g.projN = 0;
        fireA(g);
        t.assert(g.player.shells[0], 2, "ammo stays full after shot 3");
        t.assert(g.projN, 1, "shot 3");
        releaseA(g);
        idleGuard(g, 70);
        // Safety gate: a manually emptied clip still cannot fire.
        g.player.shells[0] = 0;
        g.projN = 0;
        fireA(g);
        t.assert(g.player.shells[0], 0, "manually emptied clip stays empty");
        t.assert(g.projN, 0, "empty clip cannot fire");
        suite.addTest(t);
    }

    {
        Test t("effect spark spawns with its life and expires");
        Game g;
        initGame(g, W_SWORD);
        initWorld(g, MODE_HUNT);
        g.tick = 0;
        addEffect(g, 77, 50, 26, true);
        t.assert(g.fxN, 1, "one effect");
        t.assert(g.fx[0].crit, 1, "crit flag stored");
        t.assert(g.fx[0].life, 26, "effect life");
        t.assert(g.fx[0].y, 50, "spawn y");
        updateEffects(g);
        t.assert(g.fx[0].t, 1, "effect ages");
        for (int i = 0; i < 25; i++)
            updateEffects(g);
        t.assert(g.fxN, 0, "expires after life ticks");
        suite.addTest(t);
    }

    {
        Test t("hunt mode: projectile collides with Game/Target rect, then culls");
        Game g;
        initGame(g, W_GUN);
        initHuntWorld(g);
        g.monster.x = 130;   // in the ball's path (player centre 104,68 facing E)
        g.monster.y = 52;
        const int16_t hp0 = g.monster.hp;
        holdGuard(g);
        fireA(g);
        t.assert(g.projN, 1, "ball away");
        idleTicks(g, 40);
        t.assertLessThan(g.monster.hp, hp0, "ball damages the beast");
        t.assert(g.projN, 0, "ball consumed on hit");
        suite.addTest(t);
    }

    {
        Test t("projectiles cull at world bounds and on life expiry");
        Game g;
        initGame(g, W_GUN);
        initHuntWorld(g);
        g.player.fx = fp::DIR8[4].x;   // face W so the ball leaves the world fast
        g.player.fy = fp::DIR8[4].y;
        holdGuard(g);
        fireA(g);
        t.assert(g.projN, 1, "ball away");
        idleTicks(g, 200);
        t.assert(g.projN, 0, "ball culled off-world / expired");
        suite.addTest(t);
    }

    {
        Test t("pointblank branch consumes a ball and arms reload 45");
        Game g;
        initGame(g, W_GUN);
        initHuntWorld(g);
        stepWorld(g, Input{0, 0, true, false});   // combo hit 1
        stepWorld(g, Input{0, 0, false, false});
        for (int i = 0; i < 40 && g.player.state != PS_IDLE; i++)
            idleTicks(g, 1);
        t.assert(g.player.chain, 1, "chain advanced");
        t.assert(g.player.chainLock, CHAIN_GAP, "HEAVY gap lock armed");
        idleTicks(g, CHAIN_GAP);   // lock counts down; window opens after it
        t.assertGreaterThan(g.player.chainWin, 0, "chain window open after the lock");
        const int16_t shells0 = g.player.shells[0];
        tapBranch(g);
        t.assert(g.player.state, PS_ATTACK, "pointblank started");
        t.assertNotNull(g.player.atk, "pointblank attack set");
        t.assert(g.player.atk->id, ATK_POINTBLANK, "pointblank id");
        t.assert(g.player.shells[0], shells0, "demo: ammo unlimited (no consume)");
        t.assert(g.player.reload, 45, "pointblank reload 45");
        t.assert(g.lastShot, 0, "pointblank spawns no projectile (mock parity)");
        t.assert(g.projN, 0, "no projectile from pointblank");
        suite.addTest(t);
    }

    {
        Test t("charged-ball codes inert: 3/4 spawn nothing after the prg.11 carve");
        // charge-lite removed the ynb charged ball, so the old shot codes 3/4
        // (and anything out of 1..2) are ignored by spawnShot.
        Game g;
        initGame(g, W_GUN);
        initWorld(g, MODE_HUNT);
        armShot(g, 3, 104, 68, 16, 0);
        t.assert(g.projN, 0, "code 3 spawns nothing");
        t.assert(g.fxN, 0, "code 3 spawns no muzzle effect");
        t.assert(g.lastShot, 0, "shot record cleared");

        Game g2;
        initGame(g2, W_GUN);
        initWorld(g2, MODE_HUNT);
        armShot(g2, 4, 104, 68, 16, 0);
        t.assert(g2.projN, 0, "code 4 spawns nothing");

        Game g3;
        initGame(g3, W_GUN);
        initWorld(g3, MODE_HUNT);
        armShot(g3, 5, 104, 68, 16, 0);
        t.assert(g3.projN, 0, "code 5 spawns nothing");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
