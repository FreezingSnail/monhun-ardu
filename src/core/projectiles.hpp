#pragma once
// Projectiles + effects, ported from mock/game.js (source of truth):
// fireShell / updateProjectiles / updateEffects / activeTarget.
//
// The player FSM (player.hpp) still owns ammo + reload and only records the
// shot in Game::lastShot* (fireShell stub). This header turns that record into
// the muzzle effect + pellets and advances them, so host tests and the device
// run the exact same spawn offsets, spread and collision as the mock.
//
// Tick order matches mock step(): player, target update (monster),
// projectiles, effects. Camera / world slow-scroll (bead 0ny) and rendering
// (later beads) consume Game::mode / Game::proj / Game::fx and do not need
// changes here. No float, no Arduino.h. Header-only.

#include <stdint.h>
#include "monster.hpp"   // updateMonster / syncMonsterTarget / damageMonster
#include "zones.hpp"     // roomIsSafe (safe-room monster skip)
#include "../upgrade_state.hpp"

namespace mh {

// ---------------------------------------------------------------- lifecycle

// Clear hrd state. Call after initGame() + initMonster(); mock newGame(weapon,
// mode). `mode` is kept for the caller's shape but hunt is the only value
// (prg.8); the pole/train target install is gone.
static void initWorld(Game &g, int8_t mode) {
    g.mode = mode;
    g.lastShot = 0;
    g.lastShotX = g.lastShotY = 0;
    g.lastShotFx = g.lastShotFy = 0;
    g.projN = 0;
    for (int16_t i = 0; i < MAX_PROJECTILES; i++)
        g.proj[i] = Projectile{};
    g.fxN = 0;
    for (int16_t i = 0; i < MAX_EFFECTS; i++)
        g.fx[i] = Effect{};
}

// ---------------------------------------------------------------- effects

static void addEffect(Game &g, int16_t x, int16_t y, uint8_t life, bool crit) {
    if (g.fxN >= MAX_EFFECTS) {   // device cap: drop oldest, keep newest
        for (int16_t i = 1; i < MAX_EFFECTS; i++)
            g.fx[i - 1] = g.fx[i];
        g.fxN = MAX_EFFECTS - 1;
    }
    Effect &e = g.fx[g.fxN++];
    e.x = x;
    e.y = y;
    e.t = 0;
    e.life = life;
    e.crit = crit;
}

static void updateEffects(Game &g) {
    for (int16_t i = g.fxN - 1; i >= 0; i--) {
        g.fx[i].t++;
        if (g.fx[i].t >= g.fx[i].life) {
            for (int16_t j = i + 1; j < g.fxN; j++)
                g.fx[j - 1] = g.fx[j];
            g.fxN--;
        }
    }
}

// ---------------------------------------------------------------- projectiles

// Mock fireShell(): muzzle spark + 1 ball or 3 spread pellets, spawned 13 px
// along each direction from the player centre captured at fire time. Shot codes
// 1/2 are the normal shells (charge-lite prg.11 removed the ynb charged ball,
// codes 3/4).
static void spawnShot(Game &g) {
    const int8_t shot = g.lastShot;
    g.lastShot = 0;
    if (shot < 1 || shot > 2)
        return;
    const ShellDef *sh = weaponShell(&WEAPON_DEFS[g.weapon], static_cast<int16_t>(shot - 1));
    const int16_t cx = g.lastShotX;
    const int16_t cy = g.lastShotY;
    const int8_t fx = g.lastShotFx;
    const int8_t fy = g.lastShotFy;

    addEffect(g, static_cast<int16_t>(cx + ((fx * 10) >> 4)), static_cast<int16_t>(cy + ((fy * 10) >> 4)), 5, true);

    int8_t dirX[3], dirY[3];
    int8_t n = 1;
    if (shellPellets(sh) == 1) {
        dirX[0] = fx;
        dirY[0] = fy;
    } else {
        const fp::Dir8 left = fp::rotFp(fx, fy, 15, 6);     // ~22 deg left
        const fp::Dir8 right = fp::rotFp(fx, fy, 15, -6);   // ~22 deg right
        dirX[0] = static_cast<int8_t>(left.x);
        dirY[0] = static_cast<int8_t>(left.y);
        dirX[1] = fx;
        dirY[1] = fy;
        dirX[2] = static_cast<int8_t>(right.x);
        dirY[2] = static_cast<int8_t>(right.y);
        n = 3;
    }

    for (int8_t i = 0; i < n; i++) {
        if (g.projN >= MAX_PROJECTILES) {   // device cap: drop oldest
            for (int16_t j = 1; j < MAX_PROJECTILES; j++)
                g.proj[j - 1] = g.proj[j];
            g.projN = MAX_PROJECTILES - 1;
        }
        Projectile &pr = g.proj[g.projN++];
        pr.x = cx;
        pr.y = cy;
        pr.subX = static_cast<int16_t>((dirX[i] * 13) >> 4);   // spawn centre + dir*13
        pr.subY = static_cast<int16_t>((dirY[i] * 13) >> 4);
        const int16_t speedF = shellSpeedF(sh);
        pr.vx = static_cast<int16_t>((dirX[i] * speedF) >> 4);
        pr.vy = static_cast<int16_t>((dirY[i] * speedF) >> 4);
        pr.w = shellW(sh);
        pr.h = shellH(sh);
        // Smith tier damage (integer percent, truncating); captured at spawn so
        // the in-flight projectile carries the resolved hit value.
        pr.dmg = static_cast<uint8_t>(upgradeMul(shellDmg(sh), g.dmgMul));
        pr.life = PROJ_LIFE;
        pr.heavy = (shellPellets(sh) == 1);
    }
}

MH_NOINLINE static void removeProjectile(Game &g, int16_t i) {
    for (int16_t j = i + 1; j < g.projN; j++)
        g.proj[j - 1] = g.proj[j];
    g.projN--;
}

// Mock updateProjectiles(): move, age, hit active target, cull off-world.
static void updateProjectiles(Game &g) {
    for (int16_t i = g.projN - 1; i >= 0; i--) {
        Projectile &pr = g.proj[i];
        fp::addVel(pr, pr.vx, pr.vy);
        pr.life--;

        if (g.target.alive) {
            Rect r;
            r.x = pr.x - (pr.w >> 1);
            r.y = pr.y - (pr.h >> 1);
            r.w = pr.w;
            r.h = pr.h;
            if (r.overlaps(g.target.rect)) {
                const int16_t hx = static_cast<int16_t>(r.x + (r.w >> 1));
                const int16_t hy = static_cast<int16_t>(r.y + (r.h >> 1));
                monsterOnHit(g, pr.dmg, hx, hy, 0, 0);   // migration B: parts resolve
                removeProjectile(g, i);
                continue;
            }
        }
        // Mock culls on the 1/16 px field, so a shot still inside the last
        // sub-pixel of the margin survives one extra tick. Compare the same way.
        // int16 is enough: a shell is removed the tick it passes the room bound,
        // so |pr.x| stays under the bound + one tick of travel and (256+8)<<4 ==
        // 4224 is the largest value the comparison ever sees.
        const int16_t fpx = static_cast<int16_t>(pr.x * 16 + pr.subX);
        const int16_t fpy = static_cast<int16_t>(pr.y * 16 + pr.subY);
        if (pr.life <= 0 || fpx < -(8 << 4) || fpx > static_cast<int16_t>((roomBoundW(g) + 8) << 4) || fpy < -(8 << 4) || fpy > static_cast<int16_t>((roomBoundH(g) + 8) << 4)) {
            removeProjectile(g, i);
        }
    }
}

// ---------------------------------------------------------------- full tick

// Tick body, mock step() order from updatePlayer() through updateEffects().
// Edges are supplied by the caller so stepGame() can run them before the
// over/freeze gate (mock computes edges every tick, frozen or not).
static void stepWorldBody(Game &g, const Input &inp, bool aP, bool bP, bool bR) {
    // Safe room: the room record carries no monster, so the beast and target
    // are left untouched while the player controls / HUD / doors stay live.
    // Room bounds are carved out of the parity image, so this folds to the
    // pre-room single-branch dispatch there.
    const bool safe = roomIsSafe(g);
    if (!safe)
        syncMonsterTarget(g);
    else
        g.target.alive = false;
    updatePlayer(g, inp, aP, bP, bR);
    if (g.lastShot)
        spawnShot(g);
    if (!safe)
        updateMonster(g);
    if (!safe)
        syncMonsterTarget(g);
    updateProjectiles(g);
    updateEffects(g);
}

// Legacy entry: tick++ + edges + body, no over/freeze gate. Host unit tests
// drive sub-systems directly through this; the full loop uses stepGame()
// (world.hpp), which adds mock step()'s over/freeze gating.
static void stepWorld(Game &g, const Input &inp) {
    g.tick++;
    bool aP, bP, bR;
    inputEdges(inp, g.prevA, g.prevB, aP, bP, bR);
    stepWorldBody(g, inp, aP, bP, bR);
}

}   // namespace mh
