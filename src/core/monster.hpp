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
// Migration C (bead monhun-ardu-ljj.5): the hardcoded FSM literals and the
// lunge/sweep split are gone. initMonster caches the whole creature profile
// (spawn timers included); updateMonster consumes only those RAM scalars and
// the ordered pattern list (first matching guard wins; attack steps load the
// attack through the combat loader, WAIT/after delays run in PURSUE, chance is
// tick-derived). A stagger meter (disabled while profile.staggerMax == 0) and
// a STAGGER state are wired but inert for the shipped 3. The observable
// interrupt order is verbatim: hitFlash-- -> dead return -> face/dist ->
// stun check -> state machine; pushApart/clamp/deflect-parry stuns stay
// native.
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
    case MON_RAVAGER:
        return combat::CREATURE_RAVAGER;
    default:
        return combat::CREATURE_LUNGE;
    }
}

// Load an attack's scalars + first window into the cache and record the stable
// identity on the monster. ~16 cart reads; the only attack-start read burst.
// Multi-window attacks (MULTI_WINDOW_ENABLED) track the pending windows;
// single-window data folds the bookkeeping away (winRemain stays 0).
static uint8_t monsterAttackSet(Game &g, uint8_t attackIdx) {
    const uint8_t loaded = attackLoad(g, attackIdx);
    g.monster.atkIdx = loaded;
    if (MULTI_WINDOW_ENABLED) {
        const uint8_t windows = combatAttackWindowCount(loaded);
        g.monster.winRemain = (windows > 0) ? static_cast<uint8_t>(windows - 1) : 0;
    }
    // else: single-window data never reads winRemain (refresh gated below).
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
// The body is implicit (build/zones-design.md): m.w/m.h are the creature w/h
// cached at spawn, m.x/m.y is the body anchor. Zones are tested at the landed
// hit point, so the target rect stays the body rect.
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

static void knockMonsterAway(Game &g, Monster &m, int16_t cx, int16_t cy, uint8_t amt) {
    const int16_t dx = static_cast<int16_t>((m.x + (m.w >> 1)) - cx);
    const int16_t dy = static_cast<int16_t>((m.y + (m.h >> 1)) - cy);
    const int8_t di = fp::dirIndexFromDelta(dx, dy);
    fp::addMove(m, fp::dir8X(di), fp::dir8Y(di), amt);
    clampMonster(g);
}

static void damageMonster(Game &g, int16_t dmg, int16_t hx, int16_t hy) {
    Monster &m = g.monster;
    if (m.state == MS_DEAD)
        return;
    const int16_t cx = static_cast<int16_t>(m.x + (m.w >> 1));
    const int16_t cy = static_cast<int16_t>(m.y + (m.h >> 1));
    // projection of the hit point onto the facing axis; >3 px on the head side
    // is a crit (x1.4, integer 14/10). Coords <= 256 and |fx|,|fy| <= 16 keep
    // the products inside int16 (max |proj| = 512*16*2 >> 4 = 1024).
    const int16_t proj = static_cast<int16_t>(((hx - cx) * m.fx + (hy - cy) * m.fy) >> 4);
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

// Stagger meter (docs section 7): accumulates a hit's stagger x part/stage
// mods; at threshold the creature cancels its pattern into STAGGER for
// profile.staggerRecoverT and the meter resets. Wired but inert on the shipped
// 3 (profile.staggerMax 0), so the interpreter and hit paths stay unchanged
// until a creature opts in.
static void monsterStaggerAdd(Game &g, uint8_t amount) {
    const uint16_t total = static_cast<uint16_t>(g.combat.stagger) + amount;
    g.combat.stagger = (total > 255) ? 255 : static_cast<uint8_t>(total);
    if (g.combat.stagger < g.combat.profile.staggerMax)
        return;
    g.combat.stagger = 0;
    g.combat.patternIdx = COMBAT_NO_PATTERN;   // cancel the active pattern
    g.combat.stepIdx = 0;
    g.combat.stepT = 0;
    Monster &m = g.monster;
    m.state = MS_STAGGER;
    m.t = static_cast<int16_t>(g.combat.profile.staggerRecoverT);
}

// Player hit physical type (ljj.6 part multipliers): the v1 weapon table has
// no phys column, so the weapon identity is the single mapping point. Sword
// slashes, flail blunts, gunshot/pointblank counts as SHOT (player attacks
// carry no element in v1).
static uint8_t playerPhys(const Game &g) {
    if (g.weapon == W_SWORD)
        return PHYS_SLASH;
    if (g.weapon == W_FLAIL)
        return PHYS_BLUNT;
    return PHYS_SHOT;
}

// Target::onHit — player hit landed: resolve the hurt part (multi-part
// creatures route through the cached part list + pools; the shipped 3 stay on
// the single-body path), then damage, stagger, trip stun, knockback.
static void monsterOnHit(Game &g, uint8_t dmg, int16_t hx, int16_t hy, uint8_t push, uint8_t effect) {
    Monster &m = g.monster;
    if (m.state == MS_DEAD)
        return;
    // 3-hitzone resolve: the body is implicit; optional head/appendage rects
    // replace it on a higher dmgMul, a drained zone pool flips its broken bit.
    // Images without zones use the body-only path.
    CombatBodyHit hit;
    if (ZONES_ENABLED)
        hit = combatZoneHitResolve(g, dmg, playerPhys(g), static_cast<int16_t>(hx), static_cast<int16_t>(hy));
    else
        hit = combatResolveBodyHit(g, dmg);
    damageMonster(g, static_cast<int16_t>(hit.dmg), static_cast<int16_t>(hx), static_cast<int16_t>(hy));
    if (m.state == MS_DEAD)
        return;
    // Stagger meter (docs section 7): a zone hit feeds the hit zone's
    // staggerOnHit. profile.staggerMax == 0 on the shipped 3 (STAGGER_ENABLED
    // false), so the guard folds the whole meter out; ZONES_ENABLED folds it
    // with the rest of the zone machinery in carved images.
    if (ZONES_ENABLED && STAGGER_ENABLED && g.combat.profile.staggerMax > 0) {
        const uint8_t amount = combatZoneStagger(g, hit.zone);
        if (amount)
            monsterStaggerAdd(g, amount);
    }
    if (effect == 1 && m.stun < 70)
        m.stun = 70;   // trip
    if (push)
        knockMonsterAway(g, m, hx, hy, static_cast<int16_t>(push));
}

// Target::onShove — gunshield shove: beast always gives way.
static void monsterOnShove(Game &g, int8_t dirX, int8_t dirY, uint8_t amount, uint8_t freeze) {
    fp::addMove(g.monster, dirX, dirY, amount);
    if (g.freeze < freeze)
        g.freeze = freeze;
}

// Target::onStun — deflect (28) / parry (60) freeze the beast.
static void monsterOnStun(Game &g, uint8_t ticks) {
    g.monster.stun = ticks;
}

// Spawn the hunt beast and wire it into Game::target. Call after initGame().
// kind selects the roster creature (monhun-ardu-6zb); the body box comes from
// the creature's skeleton body part, hp/spd/spawn + spawn timers from the
// creature record and cached profile (migrations B/C). Kind 0 is the legacy
// LUNGE beast: byte-for-byte the pre-roster spawn (all three records hold
// those values). The combat caches are reset to this creature's identity; the
// attack cache stays empty until the pattern interpreter loads one.
static void initMonster(Game &g, int8_t kind = 0) {
    if (kind < 0 || kind > MON_RAVAGER)
        kind = 0;
    g.monsterKind = kind;
    const uint8_t creatureId = creatureLoad(g, monsterCreatureId(kind));
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
    m.t = static_cast<int16_t>(g.combat.profile.spawnT);
    m.cd = static_cast<int16_t>(g.combat.profile.spawnCd);
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

// ------------------------------------------------- migration C interpreter
// The creature's ordered pattern list from the blob drives attack selection.
// Guards are evaluated through the combat loader (inclusive integer ranges,
// hp band, player flags, deterministic tick-derived chance; docs section 6).
// Source order is semantic: the first matching guard wins. profile.staggerMax
// == 0 (shipped 3) keeps the stagger meter inert.

// Guard probe for one pattern. Dist-only guards (SIMPLE_GUARDS,
// the shipped 3) are one u16 cart read + a min/max compare; complex guards
// (hp band, player flags, cooldown, part predicates, chance) fall back to the
// loader's full evaluator (~10 cart accesses, tick-derived chance). Generic
// path stays compiled when data uses it.
static bool patternGuardFull(Game &g, uint8_t patternIdx, uint8_t dist) {
    const Monster &m = g.monster;
    CombatGuardInput in;
    in.dist = dist;
    // hpPct needs a 32-bit divide; only computed when a shipped guard bands on
    // creature HP (combat::HAS_GUARD_HP), same folding convention as the rest.
    in.hpPct = 0;
    if (combat::HAS_GUARD_HP)
        in.hpPct = (m.hpMax > 0) ? static_cast<uint8_t>((static_cast<uint32_t>(m.hp) * 100u) / static_cast<uint16_t>(m.hpMax)) : 0;
    in.playerFlags = 0;   // player-state guards land with the T2 VM
    in.tick = static_cast<uint16_t>(g.tick);
    in.sinceUse = 0xFFFF;   // cooldown guards: profile cd gates decisions today
    in.stepIdx = 0;
    return combatGuardPasses(g, patternIdx, in);
}

static bool patternGuardOk(Game &g, uint8_t patternIdx, int16_t dist) {
    const uint8_t d = static_cast<uint8_t>(dist < 0 ? 0 : (dist > 255 ? 255 : dist));
    if (SIMPLE_GUARDS) {
        // chooseAttack only probes indices below the creature's pattern count,
        // so the guard record is always in range (generator-validated).
        const uint16_t range = combatPatternGuardRangeRead(patternIdx);
        return d >= static_cast<uint8_t>(range & 0xFF) && d <= static_cast<uint8_t>(range >> 8);
    }
    return patternGuardFull(g, patternIdx, d);
}

// Advance the active pattern cursor. Called once per PURSUE tick: stepT
// counts down (WAIT merges ticks+after into one delay; an ATK step's `after`
// is the PURSUE pause before the next step), then the step at stepIdx runs.
// ATK loads the attack through the combat loader and enters WINDUP; chance
// fail skips the step immediately; the last step clears the cursor. Bounded
// by stepCount: every iteration advances stepIdx. The delay / WAIT / chance
// branches fold away while the data declares no such records.
static void patternStepsGeneric(Game &g) {
    CombatState &c = g.combat;
    Monster &m = g.monster;
    for (;;) {
        if (c.patternIdx == COMBAT_NO_PATTERN)
            return;
        if (combat::HAS_WAIT_STEPS || combat::HAS_STEP_AFTER) {
            if (c.stepT > 0) {
                c.stepT--;
                if (c.stepT > 0)
                    return;
            }
        }
        if (c.stepIdx >= combatPatternStepCount(c.patternIdx)) {
            c.patternIdx = COMBAT_NO_PATTERN;
            return;
        }
        const CombatStep s = combatStepRead(static_cast<uint8_t>(combatPatternFirstStep(c.patternIdx) + c.stepIdx));
        const uint8_t stepIdx = c.stepIdx;
        c.stepIdx++;
        if (combat::HAS_WAIT_STEPS || combat::HAS_STEP_AFTER)
            c.stepT = s.after;
        if (combat::HAS_WAIT_STEPS && s.kind == STEP_WAIT) {
            const uint16_t wait = static_cast<uint16_t>(s.ref + s.after);
            c.stepT = (wait > 255) ? 255 : static_cast<uint8_t>(wait);
            return;
        }
        if (combat::HAS_STEP_CHANCE && !combatChancePasses(static_cast<uint16_t>(g.tick), c.creature, c.patternIdx, stepIdx, s.chance))
            continue;   // skip immediately; `after` still delays the next step
        // Part-stage attack gating (ljj.6): a crossed stage can disable an
        // attack (docs section 4); data with no parts/stages folds this out.
        if (ZONES_ENABLED && combatAttackDisabled(g, s.ref))
            continue;
        monsterAttackSet(g, s.ref);
        m.state = MS_WINDUP;
        m.t = static_cast<int16_t>(g.combat.attack.windup);
        m.windupMax = m.t;
        if (c.stepT == 0 && c.stepIdx >= combatPatternStepCount(c.patternIdx))
            c.patternIdx = COMBAT_NO_PATTERN;
        return;
    }
}

// Single-step, no-delay, always-hit patterns (the shipped 5): the pattern
// completes the moment its only step fires, so the cursor never survives the
// call (stepT stays 0, patternIdx clears; the next choice resets stepIdx).
static void patternStepsSingle(Game &g) {
    CombatState &c = g.combat;
    if (c.patternIdx == COMBAT_NO_PATTERN)
        return;
    const uint8_t ref = combatStepRef(combatPatternFirstStep(c.patternIdx));
    c.patternIdx = COMBAT_NO_PATTERN;
    c.stepT = 0;
    if (ZONES_ENABLED && combatAttackDisabled(g, ref))
        return;   // stage-disabled step: cursor cleared, decision retries
    monsterAttackSet(g, ref);
    Monster &m = g.monster;
    m.state = MS_WINDUP;
    m.t = static_cast<int16_t>(g.combat.attack.windup);
    m.windupMax = m.t;
}

static void patternSteps(Game &g) {
    if (!combat::HAS_MULTI_STEP && !combat::HAS_WAIT_STEPS && !combat::HAS_STEP_AFTER && !combat::HAS_STEP_CHANCE)
        patternStepsSingle(g);
    else
        patternStepsGeneric(g);
}

// Attack decision (PURSUE, cd <= 0 && dist < profile.attackDist): scan the
// creature's patterns in source order and run the first whose guard passes.
// No guard matching leaves the beast pursuing (cd stays <= 0, retried next
// tick); shipped guards cover every dist so this never fires today.
static void chooseAttack(Game &g, int16_t dist) {
    const uint16_t head = combatCreaturePatternHeadRead(g.combat.creature);
    const uint8_t first = static_cast<uint8_t>(head & 0xFF);
    const uint8_t count = static_cast<uint8_t>(head >> 8);
    for (uint8_t i = 0; i < count; i++) {
        const uint8_t patternIdx = static_cast<uint8_t>(first + i);
        if (!patternGuardOk(g, patternIdx, dist))
            continue;
        g.combat.patternIdx = patternIdx;
        if (combat::HAS_MULTI_STEP || combat::HAS_WAIT_STEPS || combat::HAS_STEP_AFTER || combat::HAS_STEP_CHANCE) {
            g.combat.stepIdx = 0;
            g.combat.stepT = 0;
        }   // single-step data: patternStepsSingle owns the cursor writes
        patternSteps(g);
        return;
    }
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

    const int16_t ox = static_cast<int16_t>((ax + aw - bx) < (bx + bw - ax) ? (ax + aw - bx) : (bx + bw - ax));
    const int16_t oy = static_cast<int16_t>((ay + ah - by) < (by + bh - ay) ? (ay + ah - by) : (by + bh - ay));
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
    const CombatProfile &pr = g.combat.profile;

    if (m.hitFlash > 0)
        m.hitFlash--;
    syncMonsterTarget(g);
    if (m.state == MS_DEAD)
        return;

    const int32_t dx = (p.x + (p.w >> 1)) - (m.x + (m.w >> 1));
    const int32_t dy = (p.y + (p.h >> 1)) - (m.y + (m.h >> 1));
    const int16_t dist = fp::isqrt(dx * dx + dy * dy);
    const int8_t di = fp::dirIndexFromDelta(dx, dy);
    m.fx = fp::dir8X(di);
    m.fy = fp::dir8Y(di);

    // Stagger meter decay (docs section 7). Shipped 3: fact false, folded out.
    if (STAGGER_ENABLED && g.combat.stagger > 0) {
        const uint8_t decay = pr.staggerDecay;
        g.combat.stagger = (g.combat.stagger > decay) ? static_cast<uint8_t>(g.combat.stagger - decay) : 0;
    }

    if (m.stun > 0) {
        m.stun--;
        if (m.stun == 0) {
            m.state = MS_RECOVER;
            m.t = static_cast<int16_t>(pr.stunRecoverT);
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
        if (dist > pr.engageDist) {
            fp::addMove(m, m.fx, m.fy, m.spd);
        } else if (dist < pr.keepDist) {
            fp::addMove(m, -m.fx, -m.fy, static_cast<int16_t>((m.spd * pr.retreatNum) / pr.retreatDen));
        } else {
            const int8_t si = static_cast<int8_t>((di + 2) & 7);   // perpendicular circle
            fp::addMove(m, static_cast<int16_t>(fp::dir8X(si) * m.circleDir), static_cast<int16_t>(fp::dir8Y(si) * m.circleDir), static_cast<int16_t>((m.spd * pr.circleNum) / pr.circleDen));
        }
        if (g.combat.patternIdx != COMBAT_NO_PATTERN)
            patternSteps(g);
        else if (m.cd <= 0 && dist < pr.attackDist)
            chooseAttack(g, dist);
        break;
    case MS_WINDUP:
        m.t--;
        if (m.t <= 0)
            startMonsterAttack(g);
        break;
    case MS_ATTACK: {
        // Every per-tick read comes from the RAM cache. The only mid-attack
        // cart access is the multi-window refresh (shipped attacks declare one
        // window, so it never fires today).
        const int16_t active = static_cast<int16_t>(g.combat.attack.active);
        const int16_t recover = static_cast<int16_t>(g.combat.attack.recover);
        m.t++;
        if (g.combat.attack.moveType == MOVE_LUNGE && m.t <= active)
            fp::addVel(m, m.lvx, m.lvy);
        if (MULTI_WINDOW_ENABLED)
            monsterWindowNext(g);
        const uint16_t t16 = static_cast<uint16_t>(m.t);
        if (t16 >= g.combat.attack.win.t0 && t16 <= g.combat.attack.win.t1 && monsterHitsPlayer(g)) {
            playerHurt(g, g.combat.attack.dmg, m.fx, m.fy);
            if (p.hp == 0) {
                if (g.over == OVER_NONE)
                    g.over = OVER_LOSE;
            }
        }
        if (m.t > active + recover) {
            m.state = MS_PURSUE;
            const uint16_t jitter = pr.cdJitter;
            m.cd = static_cast<int16_t>(pr.cdBase + (jitter ? static_cast<uint16_t>(g.tick) % jitter : 0));
            m.circleDir = (g.tick % 2) ? 1 : -1;
        }
        break;
    }
    case MS_RECOVER:
        m.t--;
        if (m.t <= 0) {
            m.state = MS_PURSUE;
            m.cd = static_cast<int16_t>(pr.cdBase);
        }
        break;
    case MS_STAGGER:
        // Inert on the shipped 3 (profile.staggerMax 0, STAGGER_ENABLED
        // false); same release as stun. Generic path returns with stagger data.
        if (!STAGGER_ENABLED)
            break;
        m.t--;
        if (m.t <= 0) {
            m.state = MS_PURSUE;
            m.cd = static_cast<int16_t>(pr.cdBase);
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
