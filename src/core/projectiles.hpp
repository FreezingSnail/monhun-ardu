#pragma once
// Projectiles, effects, training pole + train DPS, ported from
// mock/game.js (source of truth): fireShell / updateProjectiles /
// updateEffects / updatePole / damagePole / trainDps / activeTarget.
//
// The player FSM (player.hpp) still owns ammo + reload and only records the
// shot in Game::lastShot* (fireShell stub). This header turns that record into
// the muzzle effect + pellets and advances them, so host tests and the device
// run the exact same spawn offsets, spread and collision as the mock.
//
// Tick order matches mock step(): player, target update (pole|monster),
// projectiles, effects. Camera / world slow-scroll (bead 0ny) and rendering
// (later beads) consume Game::mode / Game::pole / Game::proj / Game::fx and do
// not need changes here. No float, no Arduino.h. Header-only.

#include <stdint.h>
#include "monster.hpp"   // updateMonster / syncMonsterTarget / damageMonster
#include "../upgrade_state.hpp"

namespace mh {

static void syncPoleTarget(Game &g);
static void poleBreakBurst(Game &g, int16_t cx, int16_t cy);

// ---------------------------------------------------------------- lifecycle

// Select the prop creature record for a PoleKind. The four poles are static
// creatures in the combat blob (data/creatures/pole*.json), so their body box,
// zones and art sheet all ride the shared creature pipeline: no flash tables.
static uint8_t poleCreatureId(int8_t kind) {
    switch (kind) {
    case POLE_SEVER:
        return combat::CREATURE_POLE_SEVER;
    case POLE_BREAK:
        return combat::CREATURE_POLE_BREAK;
    case POLE_CRACK:
        return combat::CREATURE_POLE_CRACK;
    default:
        return combat::CREATURE_POLE;
    }
}

// Install a pole variant: load the static creature through the shared loader
// (body box + zones + profile caches) and derive the hurt rect from its body
// stats + spawn. Broken state is the shared zone cache, so it resets with the
// creature load. Called for the train default (initWorld) and by the menu /
// weapon / reset tools.
static void initPoleKind(Game &g, int8_t kind) {
    if (kind < 0 || kind > POLE_CRACK)
        kind = POLE_PLAIN;
    g.pole.kind = kind;
    const uint8_t cid = creatureLoad(g, poleCreatureId(kind));
    const CombatSpawn spawn = combatCreatureSpawnRead(cid);
    g.pole.rect = Rect{static_cast<int16_t>(spawn.x), static_cast<int16_t>(spawn.y), static_cast<int16_t>(g.combat.body.w), static_cast<int16_t>(g.combat.body.h)};
    g.pole.hitFlash = 0;
    if (g.mode == MODE_TRAIN)
        syncPoleTarget(g);
}

// Clear hrd state and pick the active target: hunt (beast) or train (pole).
// Call after initGame() + initMonster(); mock newGame(weapon, mode).
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
    g.pole.rect = Rect{140, 40, 20, 36};
    g.pole.hitFlash = 0;
    g.pole.kind = POLE_PLAIN;
    g.train.total = 0;
    g.train.last = 0;
    g.train.head = 0;
    g.train.count = 0;
    for (int16_t i = 0; i < MAX_TRAIN_EVENTS; i++)
        g.train.ev[i] = TrainEvent{};
    if (mode == MODE_TRAIN)
        initPoleKind(g, POLE_PLAIN);
}

// ---------------------------------------------------------------- effects

static void addEffect(Game &g, int16_t x, int16_t y, uint8_t life, bool crit, int16_t text) {
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
    e.text = text;
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

// ---------------------------------------------------------------- train pole

static void syncPoleTarget(Game &g) {
    g.target.alive = true;
    g.target.rect = g.pole.rect;
}

// Install a pole variant is defined above with the lifecycle (it loads the
// static prop creature through the shared loader).

static void updatePole(Game &g) {
    if (g.pole.hitFlash > 0)
        g.pole.hitFlash--;
}

// Damage-stage selection for the breakable pole art (bead monhun-ardu-6zb.7).
// Stage 0 intact, 1 damaged (pool at or below half), 2 broken; the authored
// variant sheet is stage*2 + flash. hpMax == 0 marks a pole with no breakable
// zone (PLAIN), which keeps its own 2-frame normal/flash sheet. Pure integer
// math so the render path and the host frame-selection test share one source.
inline uint8_t poleDamageStage(uint8_t broken, uint8_t hp, uint8_t hpMax) {
    if (broken)
        return 2;
    return (hpMax != 0 && static_cast<uint16_t>(hp) * 2 <= hpMax) ? 1 : 0;
}
inline uint8_t poleStageFrame(uint8_t broken, uint8_t hp, uint8_t hpMax, uint8_t flash) {
    return static_cast<uint8_t>(poleDamageStage(broken, hp, hpMax) * 2 + (flash ? 1 : 0));
}

static void trainAdd(TrainStats &t, int32_t tick, int16_t dmg) {
    t.ev[t.head] = TrainEvent{tick, dmg};
    t.head = static_cast<int16_t>((t.head + 1) % MAX_TRAIN_EVENTS);
    if (t.count < MAX_TRAIN_EVENTS)
        t.count++;
}

// Mock trainDps(): sum events inside the trailing 600-tick window, /10,
// Math.round (round half up for the non-negative sums this produces).
static int16_t trainDps(const Game &g) {
    const int32_t cutoff = g.tick - 600;
    int32_t sum = 0;
    for (int16_t i = 0; i < g.train.count; i++) {
        const int16_t idx = static_cast<int16_t>((g.train.head - 1 - i + MAX_TRAIN_EVENTS * 2) % MAX_TRAIN_EVENTS);
        if (g.train.ev[idx].tick > cutoff)
            sum += g.train.ev[idx].dmg;
    }
    return static_cast<int16_t>((sum + 5) / 10);
}

// Mock damagePole(), now resolved by the shared 3-hitzone code: the head zone
// (plain/variant) carries dmgMul 140 so a head hit multiplies x1.4 through the
// same path a beast uses; a breakable zone drains its pool and flips the shared
// zoneBroken bit. hitFlash 4, freeze crit 5 / body 4, rising damage number
// (life 26). The prop is static, so the resolver runs with an explicit east
// facing at the pole rect anchor and never reads g.monster.fx/fy. Returns the
// exact total applied so train stats and the pool drain agree.
static int16_t damagePole(Game &g, uint8_t dmg, int16_t hx, int16_t hy) {
    Pole &pole = g.pole;
    const uint8_t brokenBefore = g.combat.zoneBroken;
    CombatBodyHit hit;
    if (ZONES_ENABLED)
        hit = combatZoneHitResolveAt(g, dmg, playerPhys(g), hx, hy, pole.rect.x, pole.rect.y, fp::FP, 0, true);
    else
        hit = combatResolveBodyHit(g, dmg);
    const int16_t total = static_cast<int16_t>(hit.dmg);
    pole.hitFlash = 4;
    const bool crit = hit.zone == COMBAT_ZONE_HEAD;
    const uint8_t fr = crit ? 5 : 4;
    if (g.freeze < fr)
        g.freeze = fr;
    g.train.total += total;
    g.train.last = total;
    trainAdd(g.train, g.tick, total);
    addEffect(g, hx, static_cast<int16_t>(hy - 6), 26, crit, total);

    // Fresh break: burst at the zone that flipped, refresh the target rect.
    if (g.combat.zoneBroken != brokenBefore) {
        const uint8_t slot = (g.combat.zoneBroken & COMBAT_ZONE_HEAD_BIT) ? COMBAT_ZONE_HEAD : COMBAT_ZONE_APPENDAGE;
        const CombatBox &zb = g.combat.zone[slot].box;
        poleBreakBurst(g, static_cast<int16_t>(pole.rect.x + zb.ox + (zb.w >> 1)), static_cast<int16_t>(pole.rect.y + zb.oy + (zb.h >> 1)));
        const uint8_t bw = combatCreatureBrokenW(g.combat.creature);
        const uint8_t bh = combatCreatureBrokenH(g.combat.creature);
        if (bw != 0 && bh != 0 && (pole.rect.w != bw || pole.rect.h != bh)) {
            pole.rect.w = bw;
            pole.rect.h = bh;
        }
        syncPoleTarget(g);   // BREAK: target rect refresh on the break tick
    }
    return total;
}

// ZQ5-style break juice: a small spread of the existing fxspark frames at the
// zone centre + a 6-tick freeze. Uses crit=false so the audio detector's
// critSpark scan (text==0 && crit) is not tripped by the break burst.
static void poleBreakBurst(Game &g, int16_t cx, int16_t cy) {
    addEffect(g, cx, cy, 6, false, 0);
    addEffect(g, static_cast<int16_t>(cx - 5), static_cast<int16_t>(cy + 3), 7, false, 0);
    addEffect(g, static_cast<int16_t>(cx + 5), static_cast<int16_t>(cy - 3), 7, false, 0);
    if (g.freeze < 6)
        g.freeze = 6;
}

// Target::onHit — pole is static and takes no knockback/trip. The shared zone
// resolve (damagePole) does the crit/gate/pool/break work; the pole callback
// only forwards the landed point.
static void poleOnHit(Game &g, uint8_t dmg, int16_t hx, int16_t hy, uint8_t push, uint8_t effect) {
    (void)push;
    (void)effect;
    damagePole(g, dmg, hx, hy);
}
static void poleOnShove(Game &, int8_t, int8_t, uint8_t, uint8_t) {
}   // pole never moves
static void poleOnStun(Game &, uint8_t) {
}

static void armPoleTarget(Game &g) {
    syncPoleTarget(g);
    g.target.onHit = poleOnHit;
    g.target.onShove = poleOnShove;
    g.target.onStun = poleOnStun;
}

// ---------------------------------------------------------------- projectiles

// Mock fireShell(): muzzle spark + 1 ball or 3 spread pellets, spawned 13 px
// along each direction from the player centre captured at fire time.
static void spawnShot(Game &g) {
    const int8_t shot = g.lastShot;
    g.lastShot = 0;
    if (shot < 1 || shot > 2)
        return;
    const ShellDef *sh = weaponShell(&WEAPON_DEFS[g.weapon], shot - 1);
    const int16_t cx = g.lastShotX;
    const int16_t cy = g.lastShotY;
    const int8_t fx = g.lastShotFx;
    const int8_t fy = g.lastShotFy;

    addEffect(g, static_cast<int16_t>(cx + ((fx * 10) >> 4)), static_cast<int16_t>(cy + ((fy * 10) >> 4)), 5, true, 0);

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

static void removeProjectile(Game &g, int16_t i) {
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
                if (g.mode == MODE_TRAIN)
                    poleOnHit(g, pr.dmg, hx, hy, 0, 0);
                else
                    monsterOnHit(g, pr.dmg, hx, hy, 0, 0);   // migration B: parts resolve
                removeProjectile(g, i);
                continue;
            }
        }
        // Mock culls on the 1/16 px field, so a shot still inside the last
        // sub-pixel of the margin survives one extra tick. Compare the same way.
        const int32_t fpx = pr.x * 16 + pr.subX;
        const int32_t fpy = pr.y * 16 + pr.subY;
        if (pr.life <= 0 || fpx < -(8 << 4) || fpx > (WORLD_W + 8) << 4 || fpy < -(8 << 4) || fpy > (WORLD_H + 8) << 4) {
            removeProjectile(g, i);
        }
    }
}

// ---------------------------------------------------------------- full tick

// Tick body, mock step() order from updatePlayer() through updateEffects().
// Edges are supplied by the caller so stepGame() can run them before the
// over/freeze gate (mock computes edges every tick, frozen or not).
static void stepWorldBody(Game &g, const Input &inp, bool aP, bool bP, bool bR) {
    if (g.mode == MODE_TRAIN)
        armPoleTarget(g);
    else
        syncMonsterTarget(g);
    updatePlayer(g, inp, aP, bP, bR);
    if (g.lastShot)
        spawnShot(g);
    if (g.mode == MODE_TRAIN)
        updatePole(g);
    else
        updateMonster(g);
    if (g.mode == MODE_TRAIN)
        armPoleTarget(g);
    else
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
