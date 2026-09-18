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
        t.assert(g.fx[0].text, 0, "muzzle is not a damage number");
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
        Test t("pole head zone x1.4, body x1.0, hitFlash 4");
        Game g;
        initGame(g, W_SWORD);
        initWorld(g, MODE_TRAIN);
        g.pole.rect = Rect{140, 40, 20, 36};
        g.tick = 0;
        damagePole(g, 10, 100, 100);   // body: hy >= pole.y + 16
        t.assert(g.train.last, 10, "body damage x1.0");
        t.assert(g.train.total, 10, "total accumulates");
        t.assert(g.pole.hitFlash, 4, "hitFlash set");
        t.assert(g.fx[0].crit, 0, "body effect not crit");
        damagePole(g, 10, 100, 50);   // head: hy < pole.y + 16
        t.assert(g.train.last, 14, "head damage x1.4 (integer 14/10)");
        t.assert(g.train.total, 24, "total piles up");
        t.assert(g.fx[1].crit, 1, "head effect is crit");
        t.assert(g.train.count, 2, "two events recorded");
        suite.addTest(t);
    }

    {
        Test t("damage number spawns at hy-6 with life 26 and expires");
        Game g;
        initGame(g, W_SWORD);
        initWorld(g, MODE_TRAIN);
        g.tick = 0;
        damagePole(g, 10, 77, 50);
        t.assert(g.fxN, 1, "one damage number");
        t.assert(g.fx[0].text, 14, "text is the total (head x1.4)");
        t.assert(g.fx[0].life, 26, "damage number life");
        t.assert(g.fx[0].y, 44, "spawns hy-6 (rises in render)");
        updateEffects(g);
        t.assert(g.fx[0].t, 1, "effect ages");
        for (int i = 0; i < 25; i++)
            updateEffects(g);
        t.assert(g.fxN, 0, "expires after life ticks");
        suite.addTest(t);
    }

    {
        Test t("pole kinds: initPoleKind loads the prop record; pools live in the zone cache");
        Game g;
        initGame(g, W_SWORD);
        initWorld(g, MODE_TRAIN);
        t.assert(g.pole.kind, POLE_PLAIN, "default plain");
        t.assert(g.combat.zone[COMBAT_ZONE_HEAD].hp, 0, "plain head pool 0");
        t.assert(g.combat.zone[COMBAT_ZONE_HEAD].hpMax, 0, "plain head hpMax 0");
        t.assert(g.pole.rect.w, 20, "plain rect w");
        initPoleKind(g, POLE_SEVER);
        t.assert(g.pole.kind, POLE_SEVER, "sever kind");
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp, 60, "sever whole-pole pool 60");
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hpMax, 60, "sever pool hpMax cached");
        t.assert(g.combat.headZone, COMBAT_NO_ZONE, "sever has no crit head zone");
        t.assert(g.pole.rect.w, 20, "sever rect w");
        initPoleKind(g, POLE_BREAK);
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp, 40, "break whole-pole pool 40");
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hpMax, 40, "break pool hpMax cached");
        t.assert(g.combat.headZone, COMBAT_NO_ZONE, "break has no crit head zone");
        t.assert(g.pole.rect.w, 20, "break rect w 20");
        t.assert(g.target.rect.w, 20, "break target rect refreshed");
        initPoleKind(g, POLE_CRACK);
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp, 30, "crack whole-pole pool 30");
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hpMax, 30, "crack pool hpMax cached");
        t.assert(g.pole.rect.h, 36, "crack rect h");
        initPoleKind(g, 99);
        t.assert(g.pole.kind, POLE_PLAIN, "out-of-range kind clamps to plain");
        suite.addTest(t);
    }

    {
        Test t("pole render frame: stage from broken + hp/hpMax, flash adds 1");
        // Intact: full pool.
        t.assert(poleDamageStage(0, 60, 60), 0, "full pool intact stage");
        t.assert(poleStageFrame(0, 60, 60, 0), 0, "intact frame");
        t.assert(poleStageFrame(0, 60, 60, 1), 1, "intact flash frame");
        // Damaged at or below half; above half stays intact.
        t.assert(poleDamageStage(0, 31, 60), 0, "above half intact");
        t.assert(poleDamageStage(0, 30, 60), 1, "half pool damaged");
        t.assert(poleDamageStage(0, 1, 60), 1, "low pool damaged");
        t.assert(poleStageFrame(0, 30, 60, 0), 2, "damaged frame");
        t.assert(poleStageFrame(0, 30, 60, 1), 3, "damaged flash frame");
        // Broken wins even with an intact pool (broken bit set).
        t.assert(poleDamageStage(1, 60, 60), 2, "broken stage");
        t.assert(poleStageFrame(1, 60, 60, 0), 4, "broken frame");
        t.assert(poleStageFrame(1, 60, 60, 1), 5, "broken flash frame");
        // PLAIN: no breakable zone (hpMax 0) stays stage 0 on its 2-frame sheet.
        t.assert(poleDamageStage(0, 0, 0), 0, "no zone stage intact");
        t.assert(poleStageFrame(0, 0, 0, 0), 0, "no zone frame normal");
        t.assert(poleStageFrame(0, 0, 0, 1), 1, "no zone frame flash");
        suite.addTest(t);
    }

    {
        Test t("SEVER: whole-pole zone drains anywhere on the post, no head crit");
        Game g;
        initGame(g, W_SWORD);
        initWorld(g, MODE_TRAIN);
        initPoleKind(g, POLE_SEVER);
        const int16_t hx = static_cast<int16_t>(g.pole.rect.x + 10);
        const int16_t hy = static_cast<int16_t>(g.pole.rect.y + 22);   // mid-post play path
        poleOnHit(g, 10, hx, hy, 0, 0);                                // body-mul zone: 10, pool 60 -> 50
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp, 50, "sever pool drains by the hit total");
        t.assert(g.combat.zoneBroken & COMBAT_ZONE_APPENDAGE_BIT, 0, "not broken yet");
        t.assert(g.train.last, 10, "mid-post hit x1.0 (no head crit)");
        for (int i = 0; i < 5; i++)
            poleOnHit(g, 10, hx, hy, 0, 0);
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp, 0, "pool drained");
        t.assert(g.combat.zoneBroken & COMBAT_ZONE_APPENDAGE_BIT, COMBAT_ZONE_APPENDAGE_BIT, "sever broken");
        t.assert(g.pole.rect.w, 20, "sever rect unchanged");
        poleOnHit(g, 10, hx, hy, 0, 0);   // broken: zone leaves the candidate set
        t.assert(g.train.last, 10, "body takes the full hit after break");
        suite.addTest(t);
    }

    {
        Test t("BREAK: flail drains the whole pole, rect stays 20, burst + freeze");
        Game g;
        initGame(g, W_FLAIL);
        initWorld(g, MODE_TRAIN);
        initPoleKind(g, POLE_BREAK);
        t.assert(g.pole.rect.w, 20, "break rect starts 20");
        const int16_t hx = static_cast<int16_t>(g.pole.rect.x + 10);
        const int16_t hy = static_cast<int16_t>(g.pole.rect.y + 22);   // mid-post play path
        g.fxN = 0;
        poleOnHit(g, 20, hx, hy, 0, 0);   // blast body-mul zone: pool 40 -> 20
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp, 20, "break pool drains on blunt");
        t.assert(g.combat.zoneBroken & COMBAT_ZONE_APPENDAGE_BIT, 0, "not broken yet");
        g.fxN = 0;
        poleOnHit(g, 20, hx, hy, 0, 0);   // pool 20 -> 0 -> break
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp, 0, "pool drained");
        t.assert(g.combat.zoneBroken & COMBAT_ZONE_APPENDAGE_BIT, COMBAT_ZONE_APPENDAGE_BIT, "break broken");
        t.assert(g.pole.rect.w, 20, "break rect stays 20 (horn is a stage, not a resize)");
        t.assert(g.target.rect.w, 20, "target rect unchanged");
        t.assert(g.freeze, 6, "break freeze 6");
        uint8_t sparks = 0;
        for (int16_t i = 0; i < g.fxN; i++)
            if (g.fx[i].text == 0)
                sparks++;
        t.assert(sparks, 3, "three burst sparks (+1 damage number)");
        suite.addTest(t);
    }

    {
        Test t("all weapons drain + break every breakable pole variant");
        // Owner direction (6zb.8): no weapon gate. Each variant carries ONE
        // whole-pole appendage zone, so a heavy hit at the natural mid-post
        // centre drains the pool and flips the broken bit for any weapon.
        const int8_t kinds[3] = {POLE_SEVER, POLE_BREAK, POLE_CRACK};
        const uint8_t weapons[3] = {W_SWORD, W_FLAIL, W_GUN};
        for (int ki = 0; ki < 3; ki++) {
            for (int wi = 0; wi < 3; wi++) {
                Game g;
                initGame(g, weapons[wi]);
                initWorld(g, MODE_TRAIN);
                initPoleKind(g, kinds[ki]);
                const int16_t hx = static_cast<int16_t>(g.pole.rect.x + 10);
                const int16_t hy = static_cast<int16_t>(g.pole.rect.y + 22);
                poleOnHit(g, 100, hx, hy, 0, 0);
                t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp, 0, "pool drained by any weapon");
                t.assert(g.combat.zoneBroken & COMBAT_ZONE_APPENDAGE_BIT, COMBAT_ZONE_APPENDAGE_BIT, "broken by any weapon");
                t.assert(g.train.total > 0, true, "damage lands");
            }
        }
        suite.addTest(t);
    }

    {
        Test t("play path: mid-post hit drains + breaks every variant for all weapons");
        // Acceptance 6zb.9: the landed melee point is the attack-box centre at
        // mid height (rect.x+10, rect.y+22), NOT the old part boxes. Every
        // landed hit must drain, then break after ceil(pool/dmg) hits.
        const int8_t kinds[3] = {POLE_SEVER, POLE_BREAK, POLE_CRACK};
        const uint8_t pools[3] = {60, 40, 30};
        const uint8_t weapons[3] = {W_SWORD, W_FLAIL, W_GUN};
        for (int ki = 0; ki < 3; ki++) {
            const uint8_t hits = static_cast<uint8_t>((pools[ki] + 9) / 10);   // dmg 10
            for (int wi = 0; wi < 3; wi++) {
                Game g;
                initGame(g, weapons[wi]);
                initWorld(g, MODE_TRAIN);
                initPoleKind(g, kinds[ki]);
                const int16_t hx = static_cast<int16_t>(g.pole.rect.x + 10);
                const int16_t hy = static_cast<int16_t>(g.pole.rect.y + 22);
                uint8_t n = 0;
                while (n < hits && !(g.combat.zoneBroken & COMBAT_ZONE_APPENDAGE_BIT)) {
                    poleOnHit(g, 10, hx, hy, 0, 0);
                    n++;
                }
                t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp, 0, "mid-post hit drains the pool");
                t.assert(g.combat.zoneBroken & COMBAT_ZONE_APPENDAGE_BIT, COMBAT_ZONE_APPENDAGE_BIT, "breaks after enough hits");
                t.assert(g.train.total, static_cast<int16_t>(hits) * 10, "every landed hit counted");
            }
        }
        suite.addTest(t);
    }

    {
        Test t("CRACK: gun shot drains the whole pole and breaks it");
        Game g;
        initGame(g, W_GUN);
        initWorld(g, MODE_TRAIN);
        initPoleKind(g, POLE_CRACK);
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp, 30, "crack pool 30");
        const int16_t hx = static_cast<int16_t>(g.pole.rect.x + 10);
        const int16_t hy = static_cast<int16_t>(g.pole.rect.y + 22);   // mid-post play path
        g.fxN = 0;
        poleOnHit(g, 10, hx, hy, 0, 0);   // 30 -> 20
        poleOnHit(g, 10, hx, hy, 0, 0);   // 20 -> 10
        g.fxN = 0;
        poleOnHit(g, 10, hx, hy, 0, 0);   // 10 -> 0 -> break
        t.assert(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp, 0, "crack pool drained");
        t.assert(g.combat.zoneBroken & COMBAT_ZONE_APPENDAGE_BIT, COMBAT_ZONE_APPENDAGE_BIT, "crack broken");
        t.assert(g.pole.rect.w, 20, "crack rect unchanged");
        uint8_t sparks = 0;
        for (int16_t i = 0; i < g.fxN; i++)
            if (g.fx[i].text == 0)
                sparks++;
        t.assert(sparks, 3, "break burst spawned");
        suite.addTest(t);
    }

    {
        Test t("plain pole ignores break routing byte-identically");
        Game g;
        initGame(g, W_SWORD);
        initWorld(g, MODE_TRAIN);
        g.tick = 0;
        poleOnHit(g, 10, 100, 50, 0, 0);   // hx outside the 20 px box: still head band
        t.assert(g.train.last, 14, "plain head x1.4 (hx-independent)");
        t.assert(g.combat.zoneBroken, 0, "plain never breaks");
        t.assert(g.combat.zone[COMBAT_ZONE_HEAD].hp, 0, "plain pool stays 0");
        t.assert(g.pole.rect.w, 20, "plain rect unchanged");
        suite.addTest(t);
    }

    {
        Test t("train DPS sums the trailing 600-tick window, sum/10 rounded");
        Game g;
        initGame(g, W_SWORD);
        initWorld(g, MODE_TRAIN);
        g.tick = 100;
        damagePole(g, 100, 100, 100);   // body -> 100
        g.tick = 200;
        damagePole(g, 50, 100, 100);   // body -> 50
        g.tick = 500;
        t.assert(trainDps(g), 15, "both events in window (150/10)");
        g.tick = 750;
        t.assert(trainDps(g), 5, "first event aged out (50/10)");
        g.tick = 850;
        t.assert(trainDps(g), 0, "window empty");
        g.tick = 800;
        damagePole(g, 7, 100, 100);   // 7 -> rounds to 1
        g.tick = 801;
        t.assert(trainDps(g), 1, "round half up (7+5)/10");
        suite.addTest(t);
    }

    {
        Test t("train mode: pole is targetable, monster stays frozen");
        Game g;
        initGame(g, W_SWORD);
        initMonster(g);
        initWorld(g, MODE_TRAIN);
        g.pole.rect = Rect{static_cast<int16_t>(g.player.x + 20), g.player.y, 20, 36};
        const int16_t mx = g.monster.x, my = g.monster.y;
        stepWorld(g, Input{0, 0, true, false});
        for (int i = 0; i < 20 && g.player.state != PS_IDLE; i++)
            idleTicks(g, 1);
        t.assertGreaterThan(g.train.total, 0, "pole takes player damage");
        t.assertGreaterThan(g.train.last, 0, "last hit recorded");
        t.assertGreaterThan(g.fxN, 0, "damage number spawned");
        idleTicks(g, 100);
        t.assert(g.monster.x, mx, "monster must not move in train");
        t.assert(g.monster.y, my, "monster must not move in train");
        t.assert(g.player.hp, 100, "nothing hurts the player in train");
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
        t.assertGreaterThan(g.player.chainWin, 0, "chain window open");
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

    runner.addTestSuite(suite);
}
