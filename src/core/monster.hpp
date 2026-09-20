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

// Windup entry facing (nch.2): a lock-away attack turns the beast's back to the
// hunter by negating the tracked vector just computed this tick from the player
// delta. Called right after monsterAttackSet in both pattern runners; track and
// lock-at-windup attacks are byte-identical.
static void monsterFacingWindup(Game &g) {
    if (g.combat.attack.facing != COMBAT_FACING_LOCK_AWAY)
        return;
    Monster &m = g.monster;
    m.fx = static_cast<int16_t>(-m.fx);
    m.fy = static_cast<int16_t>(-m.fy);
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
// hit point, so the target rect is the body-collision box. When the creature
// authors a `collide` box (epic monhun-ardu-nch: the chicken's legs) that rect
// drives body collision instead, so the hunter can overlap the raised body and
// only the legs push/block; every creature without one uses the body box, so
// the shipped 3 keep their exact rect.
static Rect monsterCollideRect(const Game &g) {
    const Monster &m = g.monster;
    const CombatBox &c = g.combat.collide;
    Rect r;
    if (c.w == 0 || c.h == 0) {
        r.x = m.x;
        r.y = m.y;
        r.w = m.w;
        r.h = m.h;
        return r;
    }
    r.x = static_cast<int16_t>(m.x + c.ox);
    r.y = static_cast<int16_t>(m.y + c.oy);
    r.w = c.w;
    r.h = c.h;
    return r;
}

static void syncMonsterTarget(Game &g) {
    Monster &m = g.monster;
    g.target.alive = (m.state != MS_DEAD);
    g.target.rect = monsterCollideRect(g);
}

static void clampMonster(Game &g) {
    Monster &m = g.monster;
    const int16_t rw = roomBoundW(g);
    const int16_t rh = roomBoundH(g);
    if (m.x < 0)
        m.x = 0;
    if (m.x > rw - m.w)
        m.x = rw - m.w;
    if (m.y < 0)
        m.y = 0;
    if (m.y > rh - m.h)
        m.y = rh - m.h;
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
    // int16: dmg is a weapon/upgrade value well under 2340, so dmg*14 cannot
    // overflow and the 32-bit divide helper stays out of the image.
    int16_t total = static_cast<int16_t>((dmg * (crit ? 14 : 10)) / 10);
    if (total < 1)
        total = 1;
    m.hp -= total;
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
        // Quest kill accounting (bead monhun-ardu-me6): count this hunt's kill
        // when the beast matches the active quest's target kind. progress is
        // committed to the save at hunt end by the sketch, never mid-hunt.
        if (g.questTarget >= 0 && g.monsterKind == g.questTarget && g.questProgress < 255)
            g.questProgress++;
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
    // Hidden register base (same trick as updatePlayer/updateMonster): spawn is
    // cold, so the ldd/std addressing is a pure size win here. Measured -44 B
    // whole-image; the callees still take `g` directly.
    Game *gp = &g;
    __asm__("" : "+r"(gp));
    if (kind < 0 || kind > MON_RAVAGER)
        kind = 0;
    gp->monsterKind = kind;
    const uint8_t creatureId = creatureLoad(g, monsterCreatureId(kind));
    const CombatSpawn spawn = combatCreatureSpawnRead(creatureId);
    Monster &m = gp->monster;
    m.x = static_cast<int16_t>(spawn.x);
    m.y = static_cast<int16_t>(spawn.y);
    m.w = gp->combat.body.w;
    m.h = gp->combat.body.h;
    m.subX = 0;
    m.subY = 0;
    m.hp = static_cast<int16_t>(spawn.hp);
    m.hpMax = m.hp;
    m.state = MS_IDLE;
    m.t = static_cast<int16_t>(gp->combat.profile.spawnT);
    m.cd = static_cast<int16_t>(gp->combat.profile.spawnCd);
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
    m.faceT = 0;   // nch.4: refresh facing on the first update tick
    gp->over = OVER_NONE;
    gp->target.onHit = monsterOnHit;
    gp->target.onShove = monsterOnShove;
    gp->target.onStun = monsterOnStun;
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
    // Facing clause (HAS_GUARD_FACING): player-centre offset projected on the
    // beast's 1/16 facing vector, body centres mirroring the dist math above.
    // Decision-time only; folds out while no shipped guard declares a facing.
    in.facingDot = 0;
    if (combat::HAS_GUARD_FACING) {
        const int16_t px = static_cast<int16_t>(g.player.x + (g.player.w >> 1));
        const int16_t py = static_cast<int16_t>(g.player.y + (g.player.h >> 1));
        const int16_t mx = static_cast<int16_t>(m.x + (m.w >> 1));
        const int16_t my = static_cast<int16_t>(m.y + (m.h >> 1));
        in.facingDot = combatFacingDot(px, py, mx, my, m.fx, m.fy);
    }
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
        monsterFacingWindup(g);
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
    monsterFacingWindup(g);
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

// Attack release: lunge velocity comes from the cached move scalars; a hop
// commits the face-relative dx/dy vector rotated by the current facing; every
// other move type stays native/stationary in migration A.
static void startMonsterAttack(Game &g) {
    Monster &m = g.monster;
    m.state = MS_ATTACK;
    m.t = 0;
    const uint8_t moveType = g.combat.attack.moveType;
    if (moveType == MOVE_LUNGE) {
        const int16_t speedF = g.combat.attack.moveSpeedF;
        m.lvx = (m.fx * speedF) >> 4;
        m.lvy = (m.fy * speedF) >> 4;
    } else if (moveType == MOVE_HOP) {
        // Hop (feel.7): dx/dy are face-relative (forward, lateral) velocities in
        // 1/16 px per tick, committed once at release. combatFacePoint rotates
        // the offset into the world frame with the same math attack windows use.
        int16_t hx, hy;
        combatFacePoint(m.fx, m.fy, g.combat.attack.moveDx, g.combat.attack.moveDy, hx, hy);
        m.lvx = static_cast<int8_t>(hx);
        m.lvy = static_cast<int8_t>(hy);
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
    int16_t dx, dy;
    combatFaceOffset(m.fx, m.fy, w.box, dx, dy);
    const int16_t cx = static_cast<int16_t>(m.x + (m.w >> 1) + dx);   // body box centre (migration B)
    const int16_t cy = static_cast<int16_t>(m.y + (m.h >> 1) + dy);
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
    // Body collision uses the creature's collide box (legs-only for the chicken,
    // the body box otherwise); the monster anchor m.x/m.y shifts rigidly with it.
    const Rect mr = monsterCollideRect(g);
    if (!pr.overlaps(mr))
        return;

    // Pole never moves; an attacking/windup beast shoves the player. Otherwise
    // the overlap resolves on the side that moved into it: a hunter who moved
    // this tick is pushed back (it can never shove the beast), a stationary
    // hunter lets the beast give way, so idle players are never shoved. The
    // player-move gate is carved out of the test_parity image (MH_PUSH_MOVE),
    // where the pre-fix give-way rule is behavior-identical for its scenes.
    const bool shovePlayer = (m.state == MS_ATTACK || m.state == MS_WINDUP) || (PUSH_MOVE_ENABLED && g.playerMoved);
    const Rect &ar = shovePlayer ? pr : mr;
    const Rect &br = shovePlayer ? mr : pr;

    const int16_t ox = static_cast<int16_t>((ar.x + ar.w - br.x) < (br.x + br.w - ar.x) ? (ar.x + ar.w - br.x) : (br.x + br.w - ar.x));
    const int16_t oy = static_cast<int16_t>((ar.y + ar.h - br.y) < (br.y + br.h - ar.y) ? (ar.y + ar.h - br.y) : (br.y + br.h - ar.y));
    if (ox < oy) {
        const int16_t d = static_cast<int16_t>((ar.x + (ar.w >> 1)) < (br.x + (br.w >> 1)) ? -ox : ox);
        if (shovePlayer)
            p.x += d;
        else
            m.x += d;
    } else {
        const int16_t d = static_cast<int16_t>((ar.y + (ar.h >> 1)) < (br.y + (br.h >> 1)) ? -oy : oy);
        if (shovePlayer)
            p.y += d;
        else
            m.y += d;
    }
}

static void updateMonster(Game &g) {
    // See updatePlayer: a hidden register base turns absolute 4-byte lds/sts on
    // the 650 B global Game into 2-byte ldd/std. Measured -34 B here (most of
    // the win is respent on base maintenance -- see the spike numbers).
    Game *gp = &g;
    __asm__("" : "+r"(gp));
    Monster &m = gp->monster;
    Player &p = gp->player;
    const CombatProfile &pr = gp->combat.profile;

    if (m.hitFlash > 0)
        m.hitFlash--;
    syncMonsterTarget(g);
    if (m.state == MS_DEAD)
        return;

    // Creature enrage phase (feel.6): one-shot escalation when HP first crosses
    // the authored threshold. The four scalars are cached at spawn
    // (CombatEnrage, no per-tick cart reads); hpPct 0 is the shipped default, so
    // the branch is inert until a creature authors stats.enrage. The fired latch
    // makes it fire exactly once. Applied before the facing block and the FSM
    // switch, so the tick it fires already uses the new speed and faceHold.
    {
        CombatEnrage &en = gp->combat.enrage;
        if (!en.fired && en.hpPct > 0 && static_cast<int32_t>(m.hp) * 100 <= static_cast<int32_t>(m.hpMax) * en.hpPct) {
            en.fired = 1;
            const uint16_t spd = static_cast<uint16_t>((static_cast<uint16_t>(m.spd) * en.spdMul) / 100);
            m.spd = static_cast<uint8_t>(spd < 1 ? 1 : spd);
            gp->combat.profile.faceHold = en.faceHold;
        }
    }

    const int32_t dx = (p.x + (p.w >> 1)) - (m.x + (m.w >> 1));
    const int32_t dy = (p.y + (p.h >> 1)) - (m.y + (m.h >> 1));
    const int16_t dist = fp::isqrt(dx * dx + dy * dy);
    const int8_t di = fp::dirIndexFromDelta(dx, dy);
    // Facing (docs section 7): track attacks recompute the unit vector from the
    // player delta every tick. A lock attack (heavy's tail_spin) freezes the
    // windup-start facing through WINDUP + ATTACK; lock-away additionally turned
    // the vector away at windup entry. Every shipped lunge/sweep is track, so
    // parity stays byte-identical. dist/di still feed PURSUE movement and the
    // circle step while locked.
    //
    // Turn commitment (nch.4): profile.faceHold > 0 refreshes the tracked vector
    // only every faceHold ticks (faceT counts down from faceHold to 0, then the
    // vector refreshes and faceT re-arms). faceHold 0 recomputes every tick, so
    // the shipped lunge/sweep stay byte-identical. Lock modes still freeze.
    //
    // Turn-rate limit (feel.14): profile.turnRate bounds the rotation applied on
    // a refresh. 0 keeps the legacy direct snap (byte-identical shipped data);
    // 1..8 steps the cached DIR8 facing toward the desired index along the
    // shortest arc (mod 8) by at most turnRate 45-degree steps. The faceHold
    // cadence and the lock freezing are unchanged. combat::HAS_TURN_RATE is
    // generated for the data-fact ledger but not folded here: host tests drive
    // synthetic turnRate values before any kit authors one, so the stepping
    // path must stay compiled (see output.md).
    const bool facingLocked = m.atkIdx != COMBAT_NO_ATTACK && combatFacingLockV(gp->combat.attack.facing) && (m.state == MS_WINDUP || m.state == MS_ATTACK);
    if (!facingLocked) {
        bool refresh;
        if (pr.faceHold == 0) {
            refresh = true;
        } else {
            refresh = (m.faceT == 0);
            if (refresh)
                m.faceT = pr.faceHold;
            m.faceT--;
        }
        if (refresh) {
            const uint8_t rate = pr.turnRate;
            if (rate == 0) {
                m.fx = fp::dir8X(di);
                m.fy = fp::dir8Y(di);
            } else {
                const int8_t cur = fp::dirIndexFromDelta(m.fx, m.fy);
                int8_t step = static_cast<int8_t>(di - cur);
                if (step > 4)
                    step = static_cast<int8_t>(step - 8);
                else if (step < -4)
                    step = static_cast<int8_t>(step + 8);
                if (step > static_cast<int8_t>(rate))
                    step = static_cast<int8_t>(rate);
                else if (step < -static_cast<int8_t>(rate))
                    step = static_cast<int8_t>(-static_cast<int8_t>(rate));
                if (step != 0) {
                    const int8_t next = static_cast<int8_t>((cur + step) & 7);
                    m.fx = fp::dir8X(next);
                    m.fy = fp::dir8Y(next);
                }
            }
        }
    }

    // Stagger meter decay (docs section 7). Shipped 3: fact false, folded out.
    if (STAGGER_ENABLED && gp->combat.stagger > 0) {
        const uint8_t decay = pr.staggerDecay;
        gp->combat.stagger = (gp->combat.stagger > decay) ? static_cast<uint8_t>(gp->combat.stagger - decay) : 0;
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
        if (gp->combat.patternIdx != COMBAT_NO_PATTERN)
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
        const int16_t active = static_cast<int16_t>(gp->combat.attack.active);
        const int16_t recover = static_cast<int16_t>(gp->combat.attack.recover);
        m.t++;
        if ((gp->combat.attack.moveType == MOVE_LUNGE || gp->combat.attack.moveType == MOVE_HOP) && m.t <= active)
            fp::addVel(m, m.lvx, m.lvy);
        if (MULTI_WINDOW_ENABLED)
            monsterWindowNext(g);
        const uint16_t t16 = static_cast<uint16_t>(m.t);
        if (t16 >= gp->combat.attack.win.t0 && t16 <= gp->combat.attack.win.t1 && monsterHitsPlayer(g)) {
            // Knockback direction: legacy/track attacks push along the facing
            // vector; a lock-away tail hit pushes the hunter radially away from
            // the beast (the turned-away facing would pull them inward).
            int16_t kx = m.fx;
            int16_t ky = m.fy;
            if (gp->combat.attack.facing == COMBAT_FACING_LOCK_AWAY) {
                kx = fp::dir8X(di);
                ky = fp::dir8Y(di);
            }
            playerHurt(g, gp->combat.attack.dmg, kx, ky);
            if (p.hp == 0) {
                if (gp->over == OVER_NONE)
                    gp->over = OVER_LOSE;
            }
        }
        if (m.t > active + recover) {
            m.state = MS_PURSUE;
            const uint16_t jitter = pr.cdJitter;
            m.cd = static_cast<int16_t>(pr.cdBase + (jitter ? static_cast<uint16_t>(gp->tick) % jitter : 0));
            m.circleDir = (gp->tick % 2) ? 1 : -1;
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

    // Pre-clamp body anchor (feel.4): the state switch has applied this tick's
    // movement, so a post-clamp delta means the attack's move hit a room bound.
    const int16_t preClampX = m.x;
    const int16_t preClampY = m.y;
    clampMonster(g);
    // Attack wall stun (feel.4): a committed moving attack whose clamp reaches
    // a room bound self-stuns for the attack's wallStun ticks, opening the
    // punish window. The branch is inert while wallStun is 0 in all shipped
    // data. The state change to MS_STAGGER is the one-trigger latch: the beast
    // leaves MS_ATTACK, so a sustained wall contact cannot re-trigger or stack,
    // and the shared MS_STAGGER release (PURSUE + cdBase) already exists.
    if (m.state == MS_ATTACK && gp->combat.attack.wallStun > 0 && gp->combat.attack.moveType != MOVE_NONE && (m.x != preClampX || m.y != preClampY)) {
        gp->combat.patternIdx = COMBAT_NO_PATTERN;
        gp->combat.stepIdx = 0;
        gp->combat.stepT = 0;
        m.state = MS_STAGGER;
        m.t = static_cast<int16_t>(gp->combat.attack.wallStun);
    }
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
