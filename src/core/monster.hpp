#pragma once
// Monster FSM + hit resolution, ported from mock/game.js (source of truth):
// updateMonster / chooseAttack / startMonsterAttack / monsterHitsPlayer /
// playerHit / damageMonster / knockMonsterAway / pushApart.
//
// Migration A (bead monhun-ardu-ljj.3): attack selection loads the attack's
// scalars + current window from the combat blob into Game::combat.attack
// (Monster::atkIdx is the identity) and the per-tick FSM consumes only the RAM
// cache, so windup/active ticks issue zero cart reads. The window box is the
// single source for the hit test and (in render.hpp) the telegraph.
//
// Migration B (bead monhun-ardu-ljj.4): body geometry comes from the combat
// blob. initMonster reads the creature's skeleton body part into
// Game::combat.body and the creature record's hp/spd/spawn scalars; m.w/m.h,
// Target::rect and the collide box all mirror that cached box. Landed player
// hits resolve the creature's hurtbox list via combatResolveBodyHit (single
// body part on the shipped 3; all multipliers 100, so damage is unchanged).
//
// The beast plugs into Game::target so the player FSM resolves melee against it
// (and the training-pole bead, hrd, plugs in the same way). Tick order mirrors
// the mock: player first, then monster, then the push-apart correction.
// No float, no Arduino.h. Header-only.

#include <stdint.h>
#include "player.hpp"
#include "combat.hpp"   // attack/window cache loaders (migration A)

namespace mh {

// Roster variant -> creature record. The blob is sorted by creature id, so the
// demo roster order (LUNGE/SWEEP/HEAVY) and the creature indices differ.
static uint8_t monsterCreatureId(int8_t kind) {
    switch (kind) {
    case MON_SWEEP:
        return combat::CREATURE_SWEEP;
    case MON_HEAVY:
        return combat::CREATURE_HEAVY;
    default:
        return combat::CREATURE_LUNGE;
    }
}

// Load an attack's scalars + first window into the cache and record the stable
// identity on the monster. ~16 cart reads; the only attack-start read burst.
static uint8_t monsterAttackSet(Game &g, uint8_t attackIdx) {
    const uint8_t loaded = attackLoad(g, attackIdx);
    g.monster.atkIdx = loaded;
    const uint8_t windows = combatAttackWindowCount(loaded);
    g.monster.winRemain = (windows > 0) ? static_cast<uint8_t>(windows - 1) : 0;
    return loaded;
}

// Multi-window attacks (docs section 5): once the cached window's t1 is past,
// refresh the next contiguous window. Shipped attacks declare one window, so
// winRemain stays 0 and this path never issues a cart read.
static void monsterWindowNext(Game &g) {
    Monster &m = g.monster;
    if (m.winRemain > 0 && static_cast<uint16_t>(m.t) > g.combat.attack.win.t1) {
        attackWindowLoad(g, static_cast<uint8_t>(g.combat.attack.winIdx + 1));
        m.winRemain--;
    }
}

// Keep Game::target (the live hurt box + callbacks) in step with the beast.
// Migration B: m.w/m.h are the cached skeleton body box (initMonster), so the
// hurt rect mirrors the blob-loaded geometry; m.x/m.y is the body anchor.
static void syncMonsterTarget(Game &g) {
    Monster &m = g.monster;
    g.target.alive = (m.state != MS_DEAD);
    g.target.rect.x = m.x;
    g.target.rect.y = m.y;
    g.target.rect.w = m.w;
    g.target.rect.h = m.h;
}

static void clampMonster(Game &g) {
    Monster &m = g.monster;
    if (m.x < 0)
        m.x = 0;
    if (m.x > WORLD_W - m.w)
        m.x = WORLD_W - m.w;
    if (m.y < 0)
        m.y = 0;
    if (m.y > WORLD_H - m.h)
        m.y = WORLD_H - m.h;
}

static void knockMonsterAway(Game &g, Monster &m, int32_t cx, int32_t cy, int16_t amt) {
    const int32_t dx = (m.x + (m.w >> 1)) - cx;
    const int32_t dy = (m.y + (m.h >> 1)) - cy;
    const int8_t di = fp::dirIndexFromDelta(dx, dy);
    fp::addMove(m, fp::dir8X(di), fp::dir8Y(di), amt);
    clampMonster(g);
}

static void damageMonster(Game &g, int16_t dmg, int16_t hx, int16_t hy) {
    Monster &m = g.monster;
    if (m.state == MS_DEAD)
        return;
    const int32_t cx = m.x + (m.w >> 1);
    const int32_t cy = m.y + (m.h >> 1);
    // projection of the hit point onto the facing axis; >3 px on the head side
    // is a crit (x1.4, integer 14/10).
    const int32_t proj = ((hx - cx) * m.fx + (hy - cy) * m.fy) >> 4;
    const bool crit = proj > 3;
    int32_t total = (dmg * (crit ? 14 : 10)) / 10;
    if (total < 1)
        total = 1;
    m.hp -= static_cast<int16_t>(total);
    m.hitFlash = 4;
    const int16_t fr = crit ? 6 : 4;
    if (g.freeze < fr)
        g.freeze = fr;
    addEffect(g, hx, hy, 7, crit, 0);
    if (m.hp <= 0) {
        m.hp = 0;
        m.state = MS_DEAD;
        g.over = OVER_WIN;
        g.freeze = 12;
    }
}

// Target::onHit — player melee landed: resolve the part (migration B), then
// damage, trip stun, knockback. The part multiplier chain is all-100 on the
// shipped data, so the routed number equals the raw attack damage exactly.
static void monsterOnHit(Game &g, int dmg, int hx, int hy, int push, int effect) {
    Monster &m = g.monster;
    if (m.state == MS_DEAD)
        return;
    const CombatBodyHit hit = combatResolveBodyHit(g, dmg);
    if (hit.partIdx == COMBAT_NO_PART)
        return;
    damageMonster(g, static_cast<int16_t>(hit.dmg), static_cast<int16_t>(hx), static_cast<int16_t>(hy));
    if (m.state == MS_DEAD)
        return;
    if (effect == 1 && m.stun < 70)
        m.stun = 70;   // trip
    if (push)
        knockMonsterAway(g, m, hx, hy, static_cast<int16_t>(push));
}

// Target::onShove — gunshield shove: beast always gives way.
static void monsterOnShove(Game &g, int dirX, int dirY, int amount, int freeze) {
    fp::addMove(g.monster, static_cast<int16_t>(dirX), static_cast<int16_t>(dirY), static_cast<int16_t>(amount));
    if (g.freeze < freeze)
        g.freeze = static_cast<int16_t>(freeze);
}

// Target::onStun — deflect (28) / parry (60) freeze the beast.
static void monsterOnStun(Game &g, int ticks) {
    g.monster.stun = static_cast<int16_t>(ticks);
}

// Spawn the hunt beast and wire it into Game::target. Call after initGame().
// kind selects MONSTER_DEFS[3] (monhun-ardu-6zb roster); the body box comes
// from the creature's skeleton body part and hp/spd/spawn from the creature
// record in the combat blob (migration B). Kind 0 is the legacy LUNGE beast:
// byte-for-byte the pre-roster spawn (all three records hold those values).
// The combat caches are reset to this creature's identity; the attack cache
// stays empty until chooseAttack loads one.
static void initMonster(Game &g, int8_t kind = 0) {
    if (kind < 0 || kind > 2)
        kind = 0;
    g.monsterKind = kind;
    creatureCacheReset(g, monsterCreatureId(kind));
    const uint8_t creatureId = g.combat.creature;
    combatCreatureBodyBox(creatureId, g.combat.body, g.combat.bodyFirst, g.combat.bodyCount);
    const CombatSpawn spawn = combatCreatureSpawnRead(creatureId);
    Monster &m = g.monster;
    m.x = static_cast<int16_t>(spawn.x);
    m.y = static_cast<int16_t>(spawn.y);
    m.w = g.combat.body.w;
    m.h = g.combat.body.h;
    m.subX = 0;
    m.subY = 0;
    m.hp = static_cast<int16_t>(spawn.hp);
    m.hpMax = m.hp;
    m.state = MS_IDLE;
    m.t = 90;
    m.cd = 140;
    m.fx = -fp::FP;
    m.fy = 0;   // face W
    m.atkIdx = COMBAT_NO_ATTACK;
    m.winRemain = 0;
    m.lvx = 0;
    m.lvy = 0;
    m.windupMax = 0;
    m.hitFlash = 0;
    m.stun = 0;
    m.circleDir = 1;
    m.spd = spawn.spd;
    g.over = OVER_NONE;
    g.target.onHit = monsterOnHit;
    g.target.onShove = monsterOnShove;
    g.target.onStun = monsterOnStun;
    syncMonsterTarget(g);
}

// Lunge/sweep split still comes from the roster def (migration C replaces this
// with pattern guards). The chosen attack is the creature's authored list entry
// (slot 0 lunge, slot 1 sweep for all three shipped beasts), loaded through the
// combat loader: attack scalars + first window land in the RAM cache.
static void chooseAttack(Game &g, int32_t dist) {
    Monster &m = g.monster;
    const int16_t atkDist = monsterDefAtkDist(&MONSTER_DEFS[g.monsterKind]);
    const uint8_t slot = (atkDist >= 0 && dist > atkDist) ? 0 : 1;   // lunge / sweep
    monsterAttackSet(g, static_cast<uint8_t>(combatCreatureFirstAttack(g.combat.creature) + slot));
    m.state = MS_WINDUP;
    m.t = g.combat.attack.windup;
    m.windupMax = m.t;
}

// Attack release: lunge velocity comes from the cached move scalars; every
// other move type stays native/stationary in migration A.
static void startMonsterAttack(Game &g) {
    Monster &m = g.monster;
    m.state = MS_ATTACK;
    m.t = 0;
    if (g.combat.attack.moveType == MOVE_LUNGE) {
        const int16_t speedF = g.combat.attack.moveSpeedF;
        m.lvx = (m.fx * speedF) >> 4;
        m.lvy = (m.fy * speedF) >> 4;
    } else {
        m.lvx = 0;
        m.lvy = 0;
    }
}

// Hit-window overlap from the cached window (docs section 5): the box centre is
// the body box centre plus the face-relative offset, size is the window box.
// The window is tested inclusive [t0, t1] with t 1-based (incremented before
// tests, spike 1c contract).
static bool monsterHitsPlayer(const Game &g) {
    const Monster &m = g.monster;
    const CombatWindow &w = g.combat.attack.win;
    int32_t dx, dy;
    combatFaceOffset(m.fx, m.fy, w.box, dx, dy);
    const int32_t cx = m.x + (m.w >> 1) + dx;   // body box centre (migration B)
    const int32_t cy = m.y + (m.h >> 1) + dy;
    Rect r;
    r.x = static_cast<int16_t>(cx - (w.box.w >> 1));
    r.y = static_cast<int16_t>(cy - (w.box.h >> 1));
    r.w = w.box.w;
    r.h = w.box.h;
    const Player &p = g.player;
    Rect pr;
    pr.x = p.x;
    pr.y = p.y;
    pr.w = p.w;
    pr.h = p.h;
    return r.overlaps(pr);
}

static void pushApart(Game &g) {
    Player &p = g.player;
    Monster &m = g.monster;
    if (m.state == MS_DEAD)
        return;

    Rect pr;
    pr.x = p.x;
    pr.y = p.y;
    pr.w = p.w;
    pr.h = p.h;
    Rect mr;
    mr.x = m.x;
    mr.y = m.y;
    mr.w = m.w;   // skeleton body box == collide box (migration B)
    mr.h = m.h;
    if (!pr.overlaps(mr))
        return;

    // Pole never moves; an attacking/windup beast shoves the player; otherwise the
    // beast gives way, so idle players are never shoved (mock bug fix).
    const bool shovePlayer = (m.state == MS_ATTACK || m.state == MS_WINDUP);
    const int16_t ax = shovePlayer ? p.x : m.x;
    const int16_t ay = shovePlayer ? p.y : m.y;
    const int16_t aw = shovePlayer ? p.w : m.w;
    const int16_t ah = shovePlayer ? p.h : m.h;
    const int16_t bx = shovePlayer ? m.x : p.x;
    const int16_t by = shovePlayer ? m.y : p.y;
    const int16_t bw = shovePlayer ? m.w : p.w;
    const int16_t bh = shovePlayer ? m.h : p.h;

    const int32_t ox = (ax + aw - bx) < (bx + bw - ax) ? (ax + aw - bx) : (bx + bw - ax);
    const int32_t oy = (ay + ah - by) < (by + bh - ay) ? (ay + ah - by) : (by + bh - ay);
    if (ox < oy) {
        const int16_t d = static_cast<int16_t>((ax + (aw >> 1)) < (bx + (bw >> 1)) ? -ox : ox);
        if (shovePlayer)
            p.x += d;
        else
            m.x += d;
    } else {
        const int16_t d = static_cast<int16_t>((ay + (ah >> 1)) < (by + (bh >> 1)) ? -oy : oy);
        if (shovePlayer)
            p.y += d;
        else
            m.y += d;
    }
}

static void updateMonster(Game &g) {
    Monster &m = g.monster;
    Player &p = g.player;

    if (m.hitFlash > 0)
        m.hitFlash--;
    syncMonsterTarget(g);
    if (m.state == MS_DEAD)
        return;

    const int32_t dx = (p.x + (p.w >> 1)) - (m.x + (m.w >> 1));
    const int32_t dy = (p.y + (p.h >> 1)) - (m.y + (m.h >> 1));
    const int32_t dist = fp::isqrt(dx * dx + dy * dy);
    const int8_t di = fp::dirIndexFromDelta(dx, dy);
    m.fx = fp::dir8X(di);
    m.fy = fp::dir8Y(di);

    if (m.stun > 0) {
        m.stun--;
        if (m.stun == 0) {
            m.state = MS_RECOVER;
            m.t = 24;
        }
        return;
    }

    switch (m.state) {
    case MS_IDLE:
        m.t--;
        if (m.t <= 0)
            m.state = MS_PURSUE;
        break;
    case MS_PURSUE:
        m.cd--;
        if (dist > 36) {
            fp::addMove(m, m.fx, m.fy, m.spd);
        } else if (dist < 24) {
            fp::addMove(m, -m.fx, -m.fy, (m.spd * 6) / 10);
        } else {
            const int8_t si = static_cast<int8_t>((di + 2) & 7);   // perpendicular circle
            fp::addMove(m, static_cast<int16_t>(fp::dir8X(si) * m.circleDir), static_cast<int16_t>(fp::dir8Y(si) * m.circleDir), (m.spd * 8) / 10);
        }
        if (m.cd <= 0 && dist < 42)
            chooseAttack(g, dist);
        break;
    case MS_WINDUP:
        m.t--;
        if (m.t <= 0)
            startMonsterAttack(g);
        break;
    case MS_ATTACK: {
        // Migration A: every per-tick read comes from the RAM cache. The only
        // mid-attack cart access is the multi-window refresh (shipped attacks
        // declare one window, so it never fires today).
        const int16_t active = static_cast<int16_t>(g.combat.attack.active);
        const int16_t recover = static_cast<int16_t>(g.combat.attack.recover);
        m.t++;
        if (g.combat.attack.moveType == MOVE_LUNGE && m.t <= active)
            fp::addVel(m, m.lvx, m.lvy);
        monsterWindowNext(g);
        const uint16_t t16 = static_cast<uint16_t>(m.t);
        if (t16 >= g.combat.attack.win.t0 && t16 <= g.combat.attack.win.t1 && monsterHitsPlayer(g)) {
            playerHurt(g, g.combat.attack.dmg, m.fx, m.fy);
            if (p.hp <= 0) {
                p.hp = 0;
                if (g.over == OVER_NONE)
                    g.over = OVER_LOSE;
            }
        }
        if (m.t > active + recover) {
            m.state = MS_PURSUE;
            m.cd = static_cast<int16_t>(55 + (g.tick % 40));
            m.circleDir = (g.tick % 2) ? 1 : -1;
        }
        break;
    }
    case MS_RECOVER:
        m.t--;
        if (m.t <= 0) {
            m.state = MS_PURSUE;
            m.cd = 55;
        }
        break;
    default:
        m.state = MS_PURSUE;
    }

    clampMonster(g);
    pushApart(g);
}

// Raw beast update (mock updateMonster).
static void stepMonster(Game &g) {
    updateMonster(g);
}

// One full hunt tick in mock step() order: player, then monster. The target
// rect is synced first so melee sees the beast's current position.
static void stepHunt(Game &g, const Input &inp) {
    syncMonsterTarget(g);
    stepPlayer(g, inp);
    updateMonster(g);
}

}   // namespace mh
