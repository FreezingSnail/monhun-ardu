#pragma once
// Effects, ported from mock/game.js (source of truth): addEffect /
// updateEffects. The shell/projectile system (spawnShot / updateProjectiles)
// was retired with the gun hitscan rework: the arrowshot resolves instantly
// through meleeHitbox at the authored reach, so no in-flight state exists.
//
// The player FSM owns the special-attack verb; this header owns the transient
// spark ring the hit/gather/carve paths share. Tick order matches mock step():
// player, target update (monster), effects. Camera / world slow-scroll (bead
// 0ny) and rendering (later beads) consume Game::mode / Game::fx and do not
// need changes here. No float, no Arduino.h. Header-only.

#include <stdint.h>
#include "monster.hpp"   // updateMonster / syncMonsterTarget / damageMonster
#include "zones.hpp"     // roomIsSafe (safe-room monster skip)
#include "../upgrade_state.hpp"

namespace mh {

// ---------------------------------------------------------------- lifecycle

// Clear hrd state. Call after initGame() + initMonster(); mock newGame(weapon,
// mode). `mode` is kept for the caller's shape but hunt is the only value
// (prg.8); the pole/train target install is gone. The shot/proj fields are
// retired but stay zeroed: legacy parity/perf fixtures still read them.
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
    if (!safe)
        updateMonster(g);
    if (!safe)
        syncMonsterTarget(g);
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
