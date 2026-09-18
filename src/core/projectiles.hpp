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

// ------------------------------------------------- pole variants (6zb.5)
// Hardcoded PROGMEM table keyed by PoleKind (legacy MONSTER_DEFS precedent; no
// JSON pipeline). Fields are byte-sized so AVR reads are single LPM bytes.
//   w/h            hurt rect size (BREAK carries a 28x36 side-arm rect)
//   zox/zoy/zw/zh  zone box relative to the rect ((zw==0) = no breakable zone)
//   pool           zone pool in damage points
//   breakTypes     Phys mask required to drain the pool (wrong weapon: no drain)
//   brokenW/H      rect size once broken (SEVER/CRACK unchanged, BREAK shrinks)
//   brokenFlags    bit0 = head crit x1.4 disabled while broken (SEVER)
struct PoleDef {
    uint8_t w, h;
    int8_t zox, zoy;
    uint8_t zw, zh;
    uint8_t pool;
    uint8_t breakTypes;
    uint8_t brokenW, brokenH;
    uint8_t brokenFlags;
};

// Phys bits mirror combat.hpp (SLASH/BLUNT/SHOT); game.hpp cannot include it.
enum PoleBreak : uint8_t {
    POLEBT_SLASH = 0x01,
    POLEBT_BLUNT = 0x02,
    POLEBT_SHOT = 0x04
};

MH_PROGMEM const PoleDef POLE_DEFS[4] = {
    {20, 36, 0, 0, 0, 0, 0, 0, 20, 36, 0},                 // PLAIN: no zone, no break
    {20, 36, 0, 0, 20, 16, 60, POLEBT_SLASH, 20, 36, 1},   // SEVER: top block, crit lost
    {28, 36, 20, 8, 8, 12, 40, POLEBT_BLUNT, 20, 36, 0},   // BREAK: side arm, rect shrinks
    {20, 36, 0, 18, 20, 10, 30, POLEBT_SHOT, 20, 36, 0},   // CRACK: mid band
};

inline uint8_t poleDefW(const PoleDef *d) {
    return mhPgmReadU8(&d->w);
}
inline uint8_t poleDefH(const PoleDef *d) {
    return mhPgmReadU8(&d->h);
}
inline int16_t poleDefZox(const PoleDef *d) {
    return mhPgmReadI8(&d->zox);
}
inline int16_t poleDefZoy(const PoleDef *d) {
    return mhPgmReadI8(&d->zoy);
}
inline uint8_t poleDefZw(const PoleDef *d) {
    return mhPgmReadU8(&d->zw);
}
inline uint8_t poleDefZh(const PoleDef *d) {
    return mhPgmReadU8(&d->zh);
}
inline uint8_t poleDefPool(const PoleDef *d) {
    return mhPgmReadU8(&d->pool);
}
inline uint8_t poleDefBreakTypes(const PoleDef *d) {
    return mhPgmReadU8(&d->breakTypes);
}
inline uint8_t poleDefBrokenW(const PoleDef *d) {
    return mhPgmReadU8(&d->brokenW);
}
inline uint8_t poleDefBrokenH(const PoleDef *d) {
    return mhPgmReadU8(&d->brokenH);
}
inline uint8_t poleDefBrokenFlags(const PoleDef *d) {
    return mhPgmReadU8(&d->brokenFlags);
}

// ---------------------------------------------------------------- lifecycle

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
    g.pole.hp = 0;
    g.pole.broken = 0;
    g.pole.kind = POLE_PLAIN;
    g.train.total = 0;
    g.train.last = 0;
    g.train.head = 0;
    g.train.count = 0;
    for (int16_t i = 0; i < MAX_TRAIN_EVENTS; i++)
        g.train.ev[i] = TrainEvent{};
    if (mode == MODE_TRAIN)
        syncPoleTarget(g);
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

// Install a pole variant: rect/hp from POLE_DEFS, broken cleared. Called after
// newGame() by the menu (menuStart) and by the weapon/reset tools. Kind 0 is
// the legacy plain pole (no zone, pool 0) so a default Game stays identical.
static void initPoleKind(Game &g, int8_t kind) {
    if (kind < 0 || kind > POLE_CRACK)
        kind = POLE_PLAIN;
    const PoleDef *d = &POLE_DEFS[kind];
    g.pole.kind = kind;
    g.pole.rect = Rect{140, 40, static_cast<int16_t>(poleDefW(d)), static_cast<int16_t>(poleDefH(d))};
    g.pole.hitFlash = 0;
    g.pole.hp = poleDefPool(d);
    g.pole.broken = 0;
    if (g.mode == MODE_TRAIN)
        syncPoleTarget(g);
}

static void updatePole(Game &g) {
    if (g.pole.hitFlash > 0)
        g.pole.hitFlash--;
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

// Mock damagePole(): head zone is the top 16 px (crit x1.4 as integer 14/10),
// hitFlash 4, freeze crit 5 / body 4, rising damage number (life 26). Returns
// the exact total applied so the zone pool drains by the same number the train
// stats recorded. SEVER loses its head crit once the top block is broken.
static int16_t damagePole(Game &g, uint8_t dmg, int16_t hx, int16_t hy) {
    Pole &pole = g.pole;
    bool crit = hy < pole.rect.y + POLE_HEAD;
    if (crit && pole.broken && (poleDefBrokenFlags(&POLE_DEFS[pole.kind]) & 1u))
        crit = false;   // SEVER: severed top -> body damage only
    int32_t total = (dmg * (crit ? 14 : 10)) / 10;
    if (total < 1)
        total = 1;
    pole.hitFlash = 4;
    const uint8_t fr = crit ? 5 : 4;
    if (g.freeze < fr)
        g.freeze = fr;
    g.train.total += total;
    g.train.last = static_cast<int16_t>(total);
    trainAdd(g.train, g.tick, static_cast<int16_t>(total));
    addEffect(g, hx, static_cast<int16_t>(hy - 6), 26, crit, static_cast<int16_t>(total));
    return static_cast<int16_t>(total);
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

// Target::onHit — pole is static and takes no knockback/trip. A landed hit on a
// breakable variant's zone box drains its pool only when the player's phys is
// in breakTypes (wrong weapon: damage lands, pool does not drain). Pool 0 ->
// broken: rect refresh (BREAK shrinks), burst + freeze. The zone test is
// half-open and facing-independent (the pole never turns).
static void poleOnHit(Game &g, uint8_t dmg, int16_t hx, int16_t hy, uint8_t push, uint8_t effect) {
    (void)push;
    (void)effect;
    const int16_t total = damagePole(g, dmg, hx, hy);
    Pole &pole = g.pole;
    if (pole.kind == POLE_PLAIN || pole.broken)
        return;
    const PoleDef *d = &POLE_DEFS[pole.kind];
    const uint8_t zw = poleDefZw(d);
    if (zw == 0)
        return;
    const int16_t zx = static_cast<int16_t>(pole.rect.x + poleDefZox(d));
    const int16_t zy = static_cast<int16_t>(pole.rect.y + poleDefZoy(d));
    if (hx < zx || hx >= zx + zw || hy < zy || hy >= zy + poleDefZh(d))
        return;
    if (!(playerPhys(g) & poleDefBreakTypes(d)))
        return;
    if (total < pole.hp) {
        pole.hp = static_cast<uint8_t>(pole.hp - total);
        return;
    }
    pole.hp = 0;
    pole.broken = 1;
    poleBreakBurst(g, static_cast<int16_t>(zx + (zw >> 1)), static_cast<int16_t>(zy + (poleDefZh(d) >> 1)));
    if (pole.rect.w != poleDefBrokenW(d) || pole.rect.h != poleDefBrokenH(d)) {
        pole.rect.w = poleDefBrokenW(d);
        pole.rect.h = poleDefBrokenH(d);
    }
    syncPoleTarget(g);   // BREAK: target rect refresh on the break tick
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
