#pragma once
// Player FSM + weapon moves, ported from mock/game.js updatePlayer() (source of
// truth). Tick order identical to mock: timers, B handling, A handling, state
// switch, stance update, clamp. No float, no Arduino.h. Header-only.
//
// Hit application resolves against Game::target (a rect + callback trio).
// zq5 (monster) and hrd (training pole) plug their handlers in later; the
// thresholds and timings here are the mock's, unchanged.

#include <stdint.h>
#include "game.hpp"
#include "items.hpp"   // gather nodes + herb use (feel.22)
#include "../upgrade_state.hpp"

namespace mh {

// Defined in projectiles.hpp (included after this header). The mock's damage
// handlers spawn a hit spark, so player/monster hit paths forward here.
static void addEffect(Game &g, int16_t x, int16_t y, uint8_t life, bool crit);

void Player::init(int8_t weapon) {
    (void)weapon;
    x = 96;
    y = 60;
    w = 16;
    h = 16;
    subX = 0;
    subY = 0;
    remX = 0;
    remY = 0;
    vx = 0;
    vy = 0;
    fx = fp::FP;
    fy = 0;   // face E
    hp = 100;
    hpMax = 100;
    stamMax = 100;
    stam = 100;
    stamSub = 0;
    state = PS_IDLE;
    t = 0;
    atk = nullptr;
    hitDone = false;
    chain = 0;
    chainWin = 0;
    finWin = false;
    aBuffer = 0;
    sheathed = false;
    sheatheLatch = false;
    chainLock = 0;
    bBuffer = 0;
    stance = ST_NONE;
    stanceT = 0;
    stanceAuto = 0;
    whirlTick = 0;
    throwCd = 0;
    riposteT = 0;
    bHeld = 0;
    bReady = false;
    bLocked = false;
    iT = 0;
    shell = 0;
    reload = 0;
    pA = false;
    aHold = 0;
    chargeT = 0;
    chargeArmed = false;
    aStowOk = false;
    dTapDir = -1;
    dTapT = 0;
    pDir = -1;
    itemNode = ITEM_NODE_NONE;
}

MH_NOINLINE void initGame(Game &g, int8_t weapon) {
    g.tick = 0;
    g.freeze = 0;
    g.over = OVER_NONE;
    g.weapon = weapon;
    g.prevA = g.prevB = false;
    g.playerMoved = false;
    // Legacy world extents + a live (non-safe) room; loadRoom overrides these
    // for a map room. Carved out of the parity image, whose extents stay the
    // legacy constants and which never calls loadRoom.
    if (ROOM_BOUNDS_ENABLED) {
        g.roomW = WORLD_W;
        g.roomH = WORLD_H;
        g.roomId = 0;
        g.roomMonsterKind = 0;   // MONSTER_LUNGE: a pre-room hunt is never safe
        g.roomFirstDoor = 0;
        g.roomDoorCount = 0;
        g.roomFirstHeal = 0;
        g.roomHealCount = 0;
        g.roomFirstProp = 0;
        g.roomPropCount = 0;
        g.roomFirstSmithy = 0;
        g.roomSmithyCount = 0;
        g.doorLatch = false;
        g.menuRequest = false;
        g.smithyRequest = false;
    }
    g.fade = 0;   // no transition wipe outside a room load
    g.player.init(weapon);
    g.target = Target{};
    g.lastShot = 0;
    g.lastShotX = 0;
    g.lastShotY = 0;
    // Quest accounting defaults off; the sketch arms it from the active quest
    // def after newGame() (bead monhun-ardu-me6).
    g.questTarget = -1;
    g.questNeed = 0;
    g.questProgress = 0;
    // Smith tiers default to identity; the sketch re-resolves from the save +
    // mhSmith cart at hunt start (bead monhun-ardu-4ug).
    g.dmgMul = UPGRADE_MUL_BASE;
    g.spdMul = UPGRADE_MUL_BASE;
    // Inventory + gather nodes are per-hunt (feel.22); loadRoom deliberately
    // leaves the node mask alone so a picked node stays picked across rooms.
    for (uint8_t i = 0; i < ITEM_COUNT; i++)
        g.items[i] = 0;
    g.gatherMask = 0;
    // Carve (prg.3): a fresh hunt has three carves and no live carcass interact.
    g.carvesDone = 0;
    g.carveHold = false;
    // Armor cache (arm.2): empty until the sketch arms it from the save
    // (armorApplyToGame); a default Game therefore keeps the base head/body.
    // armorFx (arm.3) is the same story: identity effects until armed.
    g.armor = ArmorAgg{};
    armorEffectsBase(g.armorFx);
    g.armorHead = 0;
}

static Rect meleeHitbox(const Player &p, const Attack *a) {
    const int16_t reach = attackReach(a);
    const int16_t hw = attackHw(a);
    const int16_t hh = attackHh(a);
    const int16_t cx = static_cast<int16_t>(p.x + (p.w >> 1) + ((p.fx * reach) >> 4));
    const int16_t cy = static_cast<int16_t>(p.y + (p.h >> 1) + ((p.fy * reach) >> 4));
    Rect r;
    r.x = static_cast<int16_t>(cx - (hw >> 1));
    r.y = static_cast<int16_t>(cy - (hh >> 1));
    r.w = hw;
    r.h = hh;
    return r;
}

// Clamp the hunter to the active-room extents (Game::roomW/roomH, legacy
// WORLD_W/H by default). Extents are passed explicitly so the helper stays a
// pure function of Player.
static void clampPlayer(Player &p, int16_t roomW, int16_t roomH) {
    if (p.x < 0)
        p.x = 0;
    if (p.x > roomW - p.w)
        p.x = roomW - p.w;
    if (p.y < 0)
        p.y = 0;
    if (p.y > roomH - p.h)
        p.y = roomH - p.h;
}

static void movePlayer(Player &p, int8_t mx, int8_t my, uint8_t spd, bool lockFacing) {
    const int8_t i = fp::dirIndexFromInput(mx, my);
    if (i < 0)
        return;
    const int8_t dx = static_cast<int8_t>(fp::dir8X(i));
    const int8_t dy = static_cast<int8_t>(fp::dir8Y(i));
    if (!lockFacing) {
        p.fx = dx;
        p.fy = dy;
    }
    // Lossless sub-pixel move (gun rework): the truncating fp::addMove dropped
    // each tick's dx*spd fraction, so diagonals ran slower than cardinals --
    // gun spd 7 lost 19%, guard strafe sp 2 lost 30%. The 1/256 px remainder
    // (remX/remY) carries across ticks, so 11/16 stays 11/16 on average.
    const int16_t ax = static_cast<int16_t>(p.remX) + static_cast<int16_t>(dx) * static_cast<int16_t>(spd);
    const int16_t ay = static_cast<int16_t>(p.remY) + static_cast<int16_t>(dy) * static_cast<int16_t>(spd);
    const int8_t qx = ax < 0 ? static_cast<int8_t>(-static_cast<int8_t>((-ax) >> 4)) : static_cast<int8_t>(ax >> 4);
    const int8_t qy = ay < 0 ? static_cast<int8_t>(-static_cast<int8_t>((-ay) >> 4)) : static_cast<int8_t>(ay >> 4);
    p.subX = static_cast<int8_t>(p.subX + qx);
    p.subY = static_cast<int8_t>(p.subY + qy);
    fp::fpCarry(p.subX, p.x);
    fp::fpCarry(p.subY, p.y);
    p.remX = static_cast<int8_t>(ax - static_cast<int16_t>(qx * fp::FP));
    p.remY = static_cast<int8_t>(ay - static_cast<int16_t>(qy * fp::FP));
}

// fixed-velocity decay; 13/16 per tick default, 14/16 for dodge/deflect
static void applyDrift(Player &p, uint8_t mult = 13) {
    p.subX += p.vx;
    p.subY += p.vy;
    // Same sub-pixel carry as addVel(): |vx|,|vy| <= 54 (Player velocity bound)
    // keeps subX/subY inside int8, so fpCarry's shift replaces tdiv + %FP and
    // drops two __divmodhi4 calls (measured -52 B whole-image).
    fp::fpCarry(p.subX, p.x);
    fp::fpCarry(p.subY, p.y);
    const int16_t avx = p.vx < 0 ? static_cast<int16_t>(-p.vx) : p.vx;
    const int16_t avy = p.vy < 0 ? static_cast<int16_t>(-p.vy) : p.vy;
    const int16_t ax = static_cast<int16_t>((avx * mult) / 16);
    const int16_t ay = static_cast<int16_t>((avy * mult) / 16);
    p.vx = p.vx < 0 ? -ax : ax;
    p.vy = p.vy < 0 ? -ay : ay;
    if (p.vx > -1 && p.vx < 1)
        p.vx = 0;
    if (p.vy > -1 && p.vy < 1)
        p.vy = 0;
}

// Shared lunge impulse (mock's `if (a.lunge)` block): convert the move's
// lunge magnitude into a fixed-velocity kick along the current facing.
static void applyLunge(Player &p, const Attack *a) {
    const int16_t lunge = attackLunge(a);
    if (lunge) {
        p.vx = (p.fx * lunge) >> 4;
        p.vy = (p.fy * lunge) >> 4;
    }
}

// Shared attack-entry tail: pay the move's stamina with the mock's clamp, then
// arm the attack. Callers keep their own guards and state clears around this.
static void beginAttack(Game &g, const Attack *a) {
    Game *gp = &g;   // hidden base: keeps the Player access displaced (measured -2 B)
    __asm__("" : "+r"(gp));
    Player &p = gp->player;
    const int16_t stam = attackStam(a);
    p.stam = (stam >= p.stam) ? 0 : static_cast<uint8_t>(p.stam - stam);
    p.state = PS_ATTACK;
    p.atk = a;
    p.t = 0;
    p.hitDone = false;
}

static void startAttack(Game &g, const WeaponDef *def, bool alt = false) {
    Player &p = g.player;
    if (p.sheathed)
        return;   // draw path clears the flag first
    if (p.chainLock > 0)
        return;   // debounce lock gates every attack entry
    // direction + A replaces combo hit 1 with the weapon's lunge opener; every
    // shipped WeaponDef carries an alt, so the mock's `def.alt` truthiness is
    // unconditional here (ROLL_ALT_ENABLED folds the selection out for parity).
    const Attack *a;
    if (ROLL_ALT_ENABLED && alt && p.chain == 0)
        a = weaponAlt(def);
    else
        a = weaponAttack(def, p.chain < 2 ? p.chain : 2);
    if (p.stam < 1)
        return;
    beginAttack(g, a);
    if (STAGE3_ENABLED)
        p.finWin = false;   // new attack clears the finisher window
    applyLunge(p, a);
    if (CHARGE_ENABLED)
        p.chargeArmed = true;   // hold A through this swing -> charge
}

// Held-A charge helper (monhun-ardu-ynb), ported from mock/game.js
// startChargeAttack(). Charge-lite (prg.11): a single level — slot 0 only, no
// CHARGE_L2 tier and no charged-ball release. Reached only from the PS_CHARGE
// release, which is itself gated by CHARGE_ENABLED.
static bool startChargeAttack(Game &g, const WeaponDef *def) {
    Player &p = g.player;
    const Attack *a = weaponCharge(def, 0);
    if (!a)
        return false;
    const int16_t stam = attackStam(a);
    if (p.stam < stam)
        return false;
    beginAttack(g, a);
    p.chain = 0;
    p.chainWin = 0;
    p.chainLock = 0;
    if (STAGE3_ENABLED)
        p.finWin = false;
    applyLunge(p, a);
    return true;
}

// Roll attack, ported from mock/game.js startRollAttack(): A out of a dodge// (or the flail deflect / gun evade-shove) cancels into the weapon's roll move;
// dodge i-frames keep ticking. Gun shield bash carries lunge 30 so it moves the
// hunter forward; sword/flail roll moves stop in place.
static bool startRollAttack(Game &g, const WeaponDef *def) {
    Player &p = g.player;
    const Attack *a = weaponRoll(def);
    const int16_t stam = attackStam(a);
    if (p.stam < stam)
        return false;
    beginAttack(g, a);
    // No finWin clear here: the mock's startRollAttack leaves the finisher
    // window alone (unlike startChargeAttack / branch entry).
    p.chain = 0;
    p.chainWin = 0;
    p.chainLock = 0;
    applyLunge(p, a);
    return true;
}

MH_NOINLINE static void exitStance(Player &p) {
    p.stance = ST_NONE;
    p.stanceT = 0;
    p.stanceAuto = 0;
}

static void enterStance(Game &g, const WeaponDef *def) {
    Player &p = g.player;
    if (p.stance != ST_NONE)
        return;
    if (p.sheathed) {
        p.bLocked = true;   // no stance while stowed
        return;
    }
    if (p.stam < 10) {
        p.bLocked = true;
        return;
    }
    if (p.state != PS_IDLE && !(p.state == PS_ATTACK && weaponCanCancel(def))) {
        p.bLocked = true;
        return;
    }
    const int8_t id = weaponId(def);
    p.stance = id == W_SWORD ? ST_PARRY : id == W_FLAIL ? ST_WHIRL : ST_GUARD;
    p.stanceT = 0;
    p.state = PS_IDLE;
    p.atk = nullptr;
    p.t = 0;
}

static void updateStance(Game &g, const WeaponDef *def) {
    (void)def;
    Player &p = g.player;
    if (p.stanceAuto > 0) {
        p.stanceAuto--;
        if (p.stanceAuto == 0) {
            exitStance(p);
            return;
        }
    }
    p.stanceT++;

    if (p.stance == ST_PARRY) {
        const bool ok = fp::drainStam(p, 2);   // ~0.12 per tick
        if (p.stanceT > 34 || !ok) {
            exitStance(p);
            p.bLocked = true;
        }
    } else if (p.stance == ST_WHIRL) {
        const bool ok = fp::drainStam(p, 8);   // 0.5 per tick
        p.whirlTick++;
        if (!ok) {
            exitStance(p);
            p.bLocked = true;
            return;
        }
        if (p.whirlTick % 16 == 0) {
            if (g.target.alive) {
                const int16_t cx = static_cast<int16_t>(p.x + (p.w >> 1));
                const int16_t cy = static_cast<int16_t>(p.y + (p.h >> 1));
                if (circleRectOverlap(cx, cy, 24, g.target.rect)) {
                    if (g.target.onHit)
                        g.target.onHit(g, static_cast<uint8_t>(attackMulFold(8, g.dmgMul, g.armorFx.dmgMul)), cx, cy, 8, 0);
                }
            }
        }
    } else if (p.stance == ST_GUARD) {
        const bool ok = fp::drainStam(p, 1);   // ~0.06 per tick
        if (!ok) {
            exitStance(p);
            p.bLocked = true;
        }
    }
}

static bool tryBranch(Game &g, const WeaponDef *def, const Input &inp) {
    (void)inp;
    Player &p = g.player;

    int stage = 0;
    if (p.state == PS_ATTACK && p.atk) {
        if (p.t < attackStartup(p.atk) + attackActive(p.atk))
            return false;                        // only from recovery
        stage = p.chain < 2 ? p.chain + 1 : 3;   // chain 2 recovery -> stage 3
    } else if (p.state == PS_IDLE && p.chainWin > 0) {
        stage = (STAGE3_ENABLED && p.finWin) ? 3 : p.chain;   // finisher done: B is stage 3
    } else {
        return false;
    }

    const Branch *br = nullptr;
    const int16_t branchCount = STAGE3_ENABLED ? 3 : 2;   // stage 3 folded out for parity
    for (int16_t i = 0; i < branchCount; i++) {
        const Branch *b = weaponBranch(def, i);
        if (branchStage(b) == stage) {
            br = b;
            break;
        }
    }
    if (!br)
        return false;

    const int8_t brStance = branchStance(br);
    if (brStance != ST_NONE) {
        p.whirlTick = 0;
        p.stance = static_cast<Stance>(brStance);
        p.stanceT = 0;
        p.stanceAuto = branchAutoT(br);
        p.state = PS_IDLE;
        p.atk = nullptr;
        p.t = 0;
        p.chain = 0;
        p.chainWin = 0;
        if (STAGE3_ENABLED)
            p.finWin = false;
        return true;
    }

    const Attack *atk = branchAtk(br);
    const int16_t atkStam = attackStam(atk);
    if (p.stam < atkStam)
        return false;
    beginAttack(g, atk);
    p.chain = 0;
    p.chainWin = 0;
    if (STAGE3_ENABLED)
        p.finWin = false;
    applyLunge(p, atk);
    if (CHARGE_ENABLED)
        p.chargeArmed = false;   // branch attacks do not charge
    return true;
}

// Shared entry gate for every tap-defense / roll action: no new move while an
// evade, stun or special is running, and out of an attack only when the weapon
// can cancel (feel.16/17/18). Mirrors the gate the B tap always had.
static bool tapDefenseReady(const Player &p, const WeaponDef *def) {
    if (p.state == PS_DODGE || p.state == PS_DEFLECT || p.state == PS_SHOVE || p.state == PS_STUN || p.state == PS_SPECIAL || p.state == PS_DRAW)
        return false;
    if (p.state == PS_ATTACK && !weaponCanCancel(def))
        return false;
    return true;
}

// Sword-style dodge roll, shared by the sword/stowed B tap (feel.16) and the
// universal double-tap roll (feel.18): facing set to (dx,dy), PS_DODGE for 16
// ticks with 14 i-frames, 3.4 px/t velocity, 14 stamina. Returns false without
// touching state when stamina is short.
static bool startDodgeRoll(Game &g, int16_t dx, int16_t dy) {
    Player &p = g.player;
    if (p.stam < 14)
        return false;
    p.fx = static_cast<int8_t>(dx);
    p.fy = static_cast<int8_t>(dy);
    p.stam -= 14;
    p.state = PS_DODGE;
    p.t = 16;
    p.iT = static_cast<uint8_t>(14 + g.armorFx.iT);   // EVADE_WINDOW extends dodge i-frames (arm.3)
    p.vx = (dx * 54) >> 4;                            // 3.4 px/t
    p.vy = (dy * 54) >> 4;
    exitStance(p);
    return true;
}

static void tapDefense(Game &g, const WeaponDef *def, const Input &inp) {
    Player &p = g.player;
    if (!tapDefenseReady(p, def))
        return;

    // roll toward move input if any, else current facing
    int16_t dx = p.fx;
    int16_t dy = p.fy;
    if (inp.mx || inp.my) {
        const int8_t di = fp::dirIndexFromInput(inp.mx, inp.my);
        dx = fp::dir8X(di);
        dy = fp::dir8Y(di);
        p.fx = static_cast<int8_t>(dx);
        p.fy = static_cast<int8_t>(dy);
    }

    const int8_t defId = weaponId(def);
    if (p.sheathed) {
        return;   // stowed B is inert: dodge roll is the double-tap input now (feel.23)
    }
    if (defId == W_SWORD) {
        startDodgeRoll(g, dx, dy);
    } else if (defId == W_FLAIL) {
        if (p.stam < 10)
            return;
        p.stam -= 10;
        p.state = PS_DEFLECT;
        p.t = 9;
        p.vx = (-(dx * 30)) >> 4;   // 1.9 px/t back step
        p.vy = (-(dy * 30)) >> 4;
        exitStance(p);
    } else {
        if (p.stam < 10)
            return;
        p.stam -= 10;
        p.state = PS_SHOVE;
        p.t = 10;
        // Bash step (feel.24): the shield thrust in drawPlayer carries a small
        // forward lean-in, riding the PS_SHOVE evade drift (14/16 per tick):
        // vx 24 = 1.5 px/t -> ~12 px total.
        p.vx = (dx * 24) >> 4;
        p.vy = (dy * 24) >> 4;
        exitStance(p);
        if (g.target.alive) {
            const Rect &m = g.target.rect;
            // int16 delta: both centres sit in a <= 256 px room with <= 128 px
            // bodies, so |mdx|,|mdy| <= 384. The squares still need int32 (384^2
            // overflows int16) but the 16x16->32 multiply is cheaper than the
            // 32x32 one; dot's products fit int16 (384*16 = 6144).
            const int16_t mdx = static_cast<int16_t>((m.x + (m.w >> 1)) - (p.x + (p.w >> 1)));
            const int16_t mdy = static_cast<int16_t>((m.y + (m.h >> 1)) - (p.y + (p.h >> 1)));
            const int16_t dist = fp::isqrt(static_cast<int32_t>(mdx) * mdx + static_cast<int32_t>(mdy) * mdy);
            const int16_t dot = static_cast<int16_t>((mdx * p.fx + mdy * p.fy) >> 4);   // px along facing
            if (dist > 0 && dist < 38 && dot * 5 > dist * 2) {
                const int8_t di = fp::dirIndexFromDelta(mdx, mdy);
                if (g.target.onShove)
                    g.target.onShove(g, fp::dir8X(di), fp::dir8Y(di), 10, 2);
            }
        }
    }
}

// Fires the stance verb for the active stance. Returns false when the verb is
// not available yet (state busy / nock / throw cooldown / stamina), so the A
// tap can be buffered instead of dropped.
static bool stanceSpecial(Game &g, const WeaponDef *def, const Input &inp) {
    Player &p = g.player;

    const Attack *special = weaponSpecial(def);
    const int8_t defId = weaponId(def);
    if (defId == W_SWORD) {
        const int16_t stam = attackStam(special);
        if (p.state != PS_IDLE)
            return false;
        if (p.stam < stam)
            return false;
        p.stam -= stam;
        p.state = PS_SPECIAL;
        p.atk = special;
        p.t = 0;
        p.hitDone = false;
        exitStance(p);
        p.bLocked = true;
    } else if (defId == W_FLAIL) {
        const int16_t stam = attackStam(special);
        if (p.state != PS_IDLE || p.throwCd > 0)
            return false;
        if (p.stam < stam)
            return false;
        p.stam -= stam;
        p.throwCd = 50;
        p.state = PS_SPECIAL;
        p.atk = special;
        p.t = 0;
        p.hitDone = false;
    } else {
        // Hitscan special: the long-reach `special` attack lands instantly at
        // reach (no projectile system); the tracer draw sells the flight. The
        // nock timer (p.reload) paces the shot and drives the HUD hint.
        if (p.reload > 0)
            return false;
        const int16_t stam = attackStam(special);
        if (p.state != PS_IDLE)
            return false;
        if (p.stam < stam)
            return false;
        p.stam -= stam;
        p.reload = ARROW_NOCK_TICKS;
        p.state = PS_SPECIAL;
        p.atk = special;
        p.t = 0;
        p.hitDone = false;
        // Guard stays up while B is held: the shot is a stance verb, not an
        // exit. The bR path still drops the stance on release, and a stance
        // broken by chip damage/stamina is unchanged. Firing off a non-held
        // stance keeps the old exit + lock.
        if (!inp.b) {
            exitStance(p);
            p.bLocked = true;
        }
        addEffect(g, static_cast<int16_t>(p.x + (p.w >> 1) + ((p.fx * 10) >> 4)), static_cast<int16_t>(p.y + (p.h >> 1) + ((p.fy * 10) >> 4)), 5, true);
    }
    return true;
}

// Damage taken by the player. Parry/deflect/guard responses live here so the
// guard break -> stun path matches the mock exactly; monster-side stun/effects
// are forwarded to Target::onStun for zq5.
static void playerHurt(Game &g, int16_t dmg, int16_t faceX, int16_t faceY) {
    Player &p = g.player;
    if (p.iT > 0)
        return;

    // Armor defense (arm.3): reduce the incoming hit before the guard/parry
    // branches, so a guard chip is computed off the reduced value. def 0 is
    // identity, and positive damage floors at 1 (never a free hit).
    dmg = armorReduce(dmg, g.armorFx.defense);

    if (p.state == PS_DEFLECT && p.t > 0) {
        if (g.target.onStun)
            g.target.onStun(g, 28);
        g.freeze = g.freeze > 5 ? g.freeze : 5;
        addEffect(g, static_cast<int16_t>(p.x + 8), static_cast<int16_t>(p.y + 8), 6, true);
        return;
    }
    if (p.stance == ST_PARRY && p.stanceT <= 20) {
        p.riposteT = 90;
        if (g.target.onStun)
            g.target.onStun(g, 60);
        g.freeze = g.freeze > 8 ? g.freeze : 8;
        addEffect(g, static_cast<int16_t>(p.x + 8), static_cast<int16_t>(p.y + 8), 8, true);
        return;
    }
    if (p.stance == ST_GUARD) {
        const int16_t chip = static_cast<int16_t>((dmg * 25) / 100);
        p.stam = (p.stam > 22) ? static_cast<uint8_t>(p.stam - 22) : 0;
        const int16_t chipDmg = chip < 1 ? 1 : chip;
        p.hp = (chipDmg >= p.hp) ? 0 : static_cast<uint8_t>(p.hp - chipDmg);
        p.vx = (faceX * 19) >> 4;
        p.vy = (faceY * 19) >> 4;
        g.freeze = g.freeze > 3 ? g.freeze : 3;
        if (p.stam <= 0) {
            exitStance(p);
            p.state = PS_STUN;
            p.t = 45;
            p.bLocked = true;
        }
        return;
    }

    p.hp = (dmg >= p.hp) ? 0 : static_cast<uint8_t>(p.hp - dmg);
    p.iT = 34;
    p.vx = (faceX * 35) >> 4;
    p.vy = (faceY * 35) >> 4;
    p.state = PS_IDLE;
    p.t = 0;
    p.atk = nullptr;
    exitStance(p);
    g.freeze = g.freeze > 6 ? g.freeze : 6;
    addEffect(g, static_cast<int16_t>(p.x + 8), static_cast<int16_t>(p.y + 8), 8, false);
}

// ---------------------------------------------------------------- sheathe
// Hold B and double-tap Down (feel.17). Only from idle (stance idle counts;
// the stance is dropped). Returns false when the state does not allow it, so
// the double-tap falls through to its normal roll.
static bool trySheathe(Player &p) {
    if (p.state != PS_IDLE)
        return false;
    if (p.stance != ST_NONE)
        exitStance(p);
    p.sheathed = true;
    p.chain = 0;
    p.chainWin = 0;
    p.chainLock = 0;   // stowing drops the pending combo recovery
    p.aBuffer = 0;
    // S2: the stow no longer rides a B press, so no sheathe latch is armed --
    // the stowed B verbs (herb hold, inert tap) stay live on the next press.
    p.sheatheLatch = false;
    return true;
}

static void updatePlayer(Game &g, const Input &inp, bool aP, bool bP, bool bR) {
    // Force Player access through a register base pointer. `g` is the single
    // global Game (650 B), so avr-gcc addresses every field absolutely: `lds`/
    // `sts` are 4 bytes each and this function touches p.* ~166 times. Hiding
    // the address in a register behind an empty asm barrier makes the compiler
    // emit 2-byte `ldd`/`std Y+q` displaced loads instead; Player is 53 B, so
    // every field is inside the q <= 63 displacement range. Measured -208 B.
    // Only worth it here and in updateMonster (measured -34 B there): the same
    // trick on drawPlayer grew the image (register pressure), so do not copy it
    // without measuring.
    Player *pp = &g.player;
    __asm__("" : "+r"(pp));
    Player &p = *pp;
    const WeaponDef *def = &WEAPON_DEFS[g.weapon];
    const int16_t x0 = p.x;
    const int16_t y0 = p.y;
    const int8_t sx0 = p.subX;
    const int8_t sy0 = p.subY;

    if (p.iT > 0)
        p.iT--;
    if (p.throwCd > 0)
        p.throwCd--;
    if (p.reload > 0)
        p.reload--;
    if (p.riposteT > 0)
        p.riposteT--;
    // Combo debounce (HEAVY): recovery lock first, then the combo window. The
    // window only ticks in idle, so a chained attack cannot expire its own chain.
    if (p.chainLock > 0) {
        p.chainLock--;
        if (p.chainLock == 0)
            p.chainWin = CHAIN_WIN;   // window opens after the recovery
    } else if (p.chainWin > 0 && p.state == PS_IDLE) {
        p.chainWin--;
        if (p.chainWin == 0) {
            p.chain = 0;
            if (STAGE3_ENABLED)
                p.finWin = false;   // window expired: the finisher offer is gone
        }
    }
    if (p.aBuffer > 0)
        p.aBuffer--;

    // A press/hold/release: a charge attack builds after a swing while A is
    // held (CHARGE_MIN), then release fires the single-level charge. Folded out
    // of the parity image by CHARGE_ENABLED (its scenes never hold A).
    bool aR = false;
    if (CHARGE_ENABLED) {
        aR = !inp.a && p.pA;
        p.pA = inp.a;
        p.aHold = inp.a ? static_cast<uint8_t>(p.aHold + 1 > 255 ? 255 : p.aHold + 1) : 0;
        if (aR)
            p.chargeArmed = false;
    }

    // Double-tap d-pad -> universal dodge roll toward the tapped direction
    // (feel.16, universal from feel.18): every weapon, sheathed, and in stance.
    // S2 moved the stow off the d-pad (hold A instead), so no direction is
    // stolen and a guard/parry/whirl can always roll out. startDodgeRoll
    // re-checks the stamina gate; tapDefenseReady keeps the same state gates
    // the B tap has. A press edge is a dir8 the pad did not carry last tick:
    // held directions never fire, and A/B are untouched.
    if (p.dTapT > 0)
        p.dTapT--;
    const int8_t dNow = fp::dirIndexFromInput(inp.mx, inp.my);
    if (dNow >= 0 && dNow != p.pDir) {
        if (p.dTapT > 0 && dNow == p.dTapDir) {
            p.dTapT = 0;   // second edge inside the window: fire and disarm
            if (tapDefenseReady(p, def))
                startDodgeRoll(g, fp::dir8X(dNow), fp::dir8Y(dNow));
        } else {
            p.dTapDir = dNow;
            p.dTapT = DTAP_WIN;
        }
    }
    p.pDir = dNow;

    const bool draining = p.stance == ST_WHIRL || p.stance == ST_GUARD;
    if (!draining && p.stam < p.stamMax) {
        p.stamSub += 8;   // 0.5 per tick
        if (p.stamSub >= 16) {
            p.stamSub -= 16;
            p.stam = p.stam + 1 > p.stamMax ? p.stamMax : p.stam + 1;
        }
    }

    // B: release speed picks tap defense vs hold stance. A tap inside attack
    // recovery or the debounce lock queues the A-B branch (B_BRANCH_BUFFER) so a
    // loose A A B still combos; it fires when the branch window opens.
    if (B_BRANCH_BUFFER_ENABLED && bP) {
        const Attack *a = p.atk;
        const bool inRecovery = p.state == PS_ATTACK && a && p.t >= attackStartup(a) + attackActive(a);
        const bool inLock = p.state == PS_IDLE && p.chainLock > 0 && (p.chain > 0 || (STAGE3_ENABLED && p.finWin));
        if (inRecovery || inLock)
            p.bBuffer = B_BRANCH_BUFFER;
    }
    if (bP) {
        p.bHeld = 0;
        p.bReady = true;
    }
    if (inp.b && p.bReady) {
        p.bHeld++;
        if (p.bHeld == HOLD_TICKS && p.stance == ST_NONE && !p.bLocked && !p.sheatheLatch) {
            // Stowed: the hold is the herb-use verb (feel.22), no stance. A
            // heal press on the same press sets bLocked (world.hpp tryHeal), so
            // the tent wins and this never double-acts.
            if (p.sheathed)
                startItemUse(g, p);
            else
                enterStance(g, def);
            p.bBuffer = 0;   // hold wins: drop any queued branch tap
        }
    }
    if (bR) {
        if (!p.sheatheLatch) {
            if (p.bHeld < HOLD_TICKS) {
                if (!tryBranch(g, def, inp) && p.bBuffer == 0)
                    tapDefense(g, def, inp);
            } else if (p.stance != ST_NONE) {
                exitStance(p);
                p.bBuffer = 0;
            }
        }
        p.bReady = false;
        p.bHeld = 0;
        p.bLocked = false;
        p.sheatheLatch = false;
    }
    if (B_BRANCH_BUFFER_ENABLED && p.bBuffer > 0 && !inp.b && !p.sheathed) {
        if (tryBranch(g, def, inp))
            p.bBuffer = 0;   // queued branch fired as soon as the window allowed
        else
            p.bBuffer--;
    }

    // A: attack / stance special (while stowed: draw into combo hit 1).
    // canAttackNow: the debounce profile attacks only from idle with no lock.
    const bool canAttackNow = p.chainLock == 0 && p.state == PS_IDLE;
    const bool altInput = ROLL_ALT_ENABLED && (inp.mx != 0 || inp.my != 0);
    // S2 stow latch: an armed press may hold-to-stow; the stowed draw press may
    // not (draw-and-keep-holding must not bounce straight back into the sheath).
    if (SHEATHE_ENABLED && aP)
        p.aStowOk = !p.sheathed;
    if (aP) {
        if (p.sheathed) {
            if (p.state == PS_IDLE) {
                // Sheathed A: a gather node under the hunter wins (feel.22);
                // otherwise commit to the rooted per-weapon draw windup
                // (feel.24) — combo hit 1 starts when the weapon is out.
                if (!tryStartGather(g, p)) {
                    p.sheathed = false;
                    p.chain = 0;
                    p.chainWin = 0;
                    p.aBuffer = 0;
                    p.state = PS_DRAW;
                    p.t = 0;
                    p.atk = nullptr;
                    p.hitDone = false;
                }
            }
        } else if (ROLL_ALT_ENABLED && (p.state == PS_DODGE || p.state == PS_DEFLECT || p.state == PS_SHOVE)) {
            startRollAttack(g, def);
        } else if (p.stance != ST_NONE) {
            // Guard can outlive its own verb: a shot keeps the stance while B is
            // held, so taps inside the recovery/nock re-arm the buffer (the shot
            // is a locked-out action, not a normal swing).
            if (!stanceSpecial(g, def, inp))
                p.aBuffer = A_BUFFER;
        } else if (canAttackNow) {
            startAttack(g, def, altInput);
        } else {
            p.aBuffer = A_BUFFER;   // buffered: fires when the debounce lock expires
        }
    }
    if (!p.sheathed && p.aBuffer > 0 && canAttackNow) {
        // A held stance keeps its verb for a buffered tap: a shot queued during
        // the previous shot's recovery fires the arrowshot, not a melee swing.
        if (p.stance != ST_NONE) {
            // Busy (nock/throw cd): leave the buffer to retry until it expires.
            if (stanceSpecial(g, def, inp))
                p.aBuffer = 0;
        } else {
            p.aBuffer = 0;
            startAttack(g, def, altInput);
        }
    }

    switch (p.state) {
    case PS_IDLE: {
        int8_t mx = inp.mx;
        int8_t my = inp.my;
        if (p.stance == ST_PARRY) {
            mx = 0;
            my = 0;
        }
        uint8_t sp = p.sheathed ? SHEATHE_SPD : static_cast<uint8_t>(weaponSpd(def));
        if (p.stance == ST_WHIRL)
            sp = (sp * 6) / 10;
        if (p.stance == ST_GUARD)
            sp = (sp * 4) / 10;
        // Smith tier speed (integer percent, truncating); 100 = unchanged.
        sp = static_cast<uint8_t>(upgradeMul(static_cast<int16_t>(sp), g.spdMul));
        // Guard: strafe with the shield up (facing locked); sheathed has no stance.
        movePlayer(p, mx, my, sp, p.stance == ST_GUARD);
        applyDrift(p);
        break;
    }
    case PS_ATTACK:
    case PS_SPECIAL: {
        const Attack *a = p.atk;
        const int16_t startup = attackStartup(a);
        const int16_t active = attackActive(a);
        const int16_t total = static_cast<int16_t>(startup + active + attackRecover(a));
        p.t++;
        if (p.t >= startup && p.t < startup + active && !p.hitDone) {
            const Rect hit = meleeHitbox(p, a);
            if (g.target.alive && hit.overlaps(g.target.rect)) {
                p.hitDone = true;
                const int16_t mult = (p.state == PS_SPECIAL && p.riposteT > 0) ? 2 : 1;
                const int16_t hx = static_cast<int16_t>(hit.x + hit.w / 2);
                const int16_t hy = static_cast<int16_t>(hit.y + hit.h / 2);
                if (g.target.onHit) {
                    // Smith tier + armor ATTACK_UP damage (integer percents,
                    // truncating at each step) applied before the riposte x2,
                    // then flows on through the existing Target::onHit ->
                    // monsterOnHit chain.
                    const int16_t dmg = attackMulFold(attackDmg(a), g.dmgMul, g.armorFx.dmgMul);
                    g.target.onHit(g, static_cast<uint8_t>(dmg * mult), hx, hy, attackPush(a), attackEffect(a));
                }
            }
        }
        if (p.t >= total) {
            if (p.state == PS_ATTACK) {
                p.state = PS_IDLE;
                const bool finisher = p.chain >= 2;
                p.chain = p.chain < 2 ? p.chain + 1 : 0;
                if (STAGE3_ENABLED)
                    p.finWin = finisher;   // next B in the window is the stage-3 branch
                p.chainLock = finisher ? COMBO_LOCK : CHAIN_GAP;
                p.chainWin = 0;   // window opens when the lock ends
            } else {
                p.state = PS_IDLE;
                p.chain = 0;
                p.chainWin = 0;
                p.chainLock = 0;
                if (STAGE3_ENABLED)
                    p.finWin = false;
            }
            p.t = 0;
            p.atk = nullptr;
        }
        applyDrift(p);
        break;
    }
    case PS_DODGE:
    case PS_DEFLECT:
    case PS_SHOVE: {
        // PS_SHOVE rides the evade drift (feel.24): the bash step armed in
        // tapDefense is a short lean-in (~12 px for vx 24 at 14/16 per tick).
        // PS_STUN keeps no drift.
        p.t--;
        applyDrift(p, 14);
        if (p.t <= 0)
            p.state = PS_IDLE;
        break;
    }
    case PS_STUN: {
        p.t--;
        if (p.t <= 0)
            p.state = PS_IDLE;
        break;
    }
    case PS_CHARGE: {
        // rooted windup; release fires the single-level charge (charge-lite,
        // prg.11). The whole body folds out of the parity image (CHARGE_ENABLED),
        // which never enters the state.
        if (CHARGE_ENABLED) {
            p.chargeT = static_cast<uint8_t>(p.chargeT + 1 > 255 ? 255 : p.chargeT + 1);
            // S2: hold past the charge window and the weapon goes away instead
            // (the flail's only stow path; the d-pad is roll-only now).
            if (inp.a && p.aHold >= CHARGE_MIN + STOW_HOLD_TICKS) {
                p.state = PS_IDLE;
                trySheathe(p);
                break;
            }
            if (aR) {
                const bool fired = weaponHasCharge(def) ? startChargeAttack(g, def) : false;
                if (!fired)
                    p.state = PS_IDLE;
                // chargeArmed was already cleared by the top-of-tick release.
            }
            applyDrift(p);
        }
        break;
    }
    case PS_DRAW: {
        // Rooted weapon draw (feel.24): no movement input handling, same
        // commitment class as PS_GATHER/PS_ITEM. When the windup elapses the
        // weapon is out and combo hit 1 starts; if startAttack refuses on the
        // lock/stamina gate the hunter just stands there with the weapon out.
        p.t++;
        if (p.t >= weaponDrawTicks(g.weapon)) {
            p.state = PS_IDLE;
            p.t = 0;
            startAttack(g, def);
        }
        break;
    }
    case PS_GATHER:
    case PS_ITEM: {
        // Rooted action window (feel.22). Any movement input or a landed hit
        // (playerHurt resets the state) cancels before the completion applies,
        // so a cancel never half-applies inventory or node state.
        if (inp.mx || inp.my) {
            p.state = PS_IDLE;
            p.t = 0;
            break;
        }
        p.t++;
        const bool gather = p.state == PS_GATHER;
        if (p.t >= (gather ? GATHER_TICKS : ITEM_USE_TICKS)) {
            if (gather)
                applyGather(g, p);
            else
                applyItemUse(g, p);
            p.state = PS_IDLE;
            p.t = 0;
        }
        break;
    }
    default:
        p.state = PS_IDLE;
    }

    // held A past the swing -> charge stance (weapons with melee charge data only)
    if (CHARGE_ENABLED && !p.sheathed && p.state == PS_IDLE && p.chargeArmed && inp.a && p.aHold >= CHARGE_MIN && weaponHasCharge(def)) {
        p.state = PS_CHARGE;
        p.chargeT = 0;
    }

    // S2 stow: hold A past STOW_HOLD_TICKS on an armed press puts the weapon
    // away (sword/gun: after the swing; flail's charge hold wins above and its
    // long hold stows from PS_CHARGE). Runs only from idle, so a running swing
    // or rooted verb is never interrupted.
    if (SHEATHE_ENABLED && !p.sheathed && p.aStowOk && p.state == PS_IDLE && inp.a && p.aHold >= STOW_HOLD_TICKS && !(CHARGE_ENABLED && p.chargeArmed && weaponHasCharge(def))) {
        trySheathe(p);
    }

    if (p.stance != ST_NONE)
        updateStance(g, def);
    clampPlayer(p, roomBoundW(g), roomBoundH(g));
    // Movement intent for pushApart (monster.hpp): any fixed-point change counts,
    // including a sub-pixel step with no pixel movement, so a hunter pressing
    // into a body keeps resolving on the player side instead of shoving it.
    // Carved out of the test_parity image (MH_PUSH_MOVE), whose scenes never
    // walk into the beast; that image keeps the pre-fix give-way rule.
    if (PUSH_MOVE_ENABLED)
        g.playerMoved = (p.x != x0 || p.y != y0 || p.subX != sx0 || p.subY != sy0);
}

// player half of mock step(): edge detect + updatePlayer. Monster / pole /
// projectile / effect updates live in the zq5 / hrd beads.
void stepPlayer(Game &g, const Input &inp) {
    g.tick++;
    bool aP, bP, bR;
    inputEdges(inp, g.prevA, g.prevB, aP, bP, bR);   // shared edge rule (input.hpp)
    updatePlayer(g, inp, aP, bP, bR);
}

}   // namespace mh