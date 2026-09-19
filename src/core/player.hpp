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
#include "../upgrade_state.hpp"

namespace mh {

// Defined in projectiles.hpp (included after this header). The mock's damage
// handlers spawn a hit spark, so player/monster hit paths forward here.
static void addEffect(Game &g, int16_t x, int16_t y, uint8_t life, bool crit, int16_t text);

void Player::init(int8_t weapon) {
    (void)weapon;
    x = 96;
    y = 60;
    w = 16;
    h = 16;
    subX = 0;
    subY = 0;
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
    aBuffer = 0;
    sheathed = false;
    sheatheLatch = false;
    seqT = 0;
    seq2 = false;
    chordT = 0;
    pMy = false;
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
    shells[0] = shellCount(weaponShell(&WEAPON_DEFS[W_GUN], 0));
    shells[1] = shellCount(weaponShell(&WEAPON_DEFS[W_GUN], 1));
}

void initGame(Game &g, int8_t weapon) {
    g.tick = 0;
    g.freeze = 0;
    g.over = OVER_NONE;
    g.weapon = weapon;
    g.prevA = g.prevB = false;
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

static void clampPlayer(Player &p) {
    if (p.x < 0)
        p.x = 0;
    if (p.x > WORLD_W - p.w)
        p.x = WORLD_W - p.w;
    if (p.y < 0)
        p.y = 0;
    if (p.y > WORLD_H - p.h)
        p.y = WORLD_H - p.h;
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
    fp::addMove(p, dx, dy, spd);
}

// fixed-velocity decay; 13/16 per tick default, 14/16 for dodge/deflect
static void applyDrift(Player &p, uint8_t mult = 13) {
    p.subX += p.vx;
    p.subY += p.vy;
    p.x += fp::tdiv(p.subX, fp::FP);
    p.y += fp::tdiv(p.subY, fp::FP);
    p.subX %= fp::FP;
    p.subY %= fp::FP;
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

static void startAttack(Game &g, const WeaponDef *def) {
    Player &p = g.player;
    if (p.sheathed)
        return;   // draw path clears the flag first
    if (p.chainLock > 0)
        return;   // debounce lock gates every attack entry
    const Attack *a = weaponAttack(def, p.chain < 2 ? p.chain : 2);
    if (p.stam < 1)
        return;
    const int16_t stam = attackStam(a);
    p.stam = (stam >= p.stam) ? 0 : static_cast<uint8_t>(p.stam - stam);
    p.state = PS_ATTACK;
    p.atk = a;
    p.t = 0;
    p.hitDone = false;
}

static void exitStance(Player &p) {
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
                        g.target.onHit(g, static_cast<uint8_t>(upgradeMul(8, g.dmgMul)), cx, cy, 8, 0);
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
        stage = p.chain < 2 ? p.chain + 1 : 3;   // stage 3 has no branch (finisher out of scope)
    } else if (p.state == PS_IDLE && p.chainWin > 0) {
        stage = p.chain;   // finWin stage-3 branch is out of scope; chain only
    } else {
        return false;
    }

    const Branch *br = nullptr;
    for (int16_t i = 0; i < 2; i++) {
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
        return true;
    }

    const Attack *atk = branchAtk(br);
    const int16_t atkStam = attackStam(atk);
    if (p.stam < atkStam)
        return false;
    if (attackShell(atk)) {
        if (p.shells[0] <= 0)
            return false;
        // Demo: ammo unlimited (no decrement here either); reload still arms.
        p.reload = 45;
    }
    p.stam -= atkStam;
    p.state = PS_ATTACK;
    p.atk = atk;
    p.t = 0;
    p.hitDone = false;
    p.chain = 0;
    p.chainWin = 0;
    const int16_t lunge = attackLunge(atk);
    if (lunge) {
        p.vx = (p.fx * lunge) >> 4;
        p.vy = (p.fy * lunge) >> 4;
    }
    return true;
}

static void tapDefense(Game &g, const WeaponDef *def, const Input &inp) {
    Player &p = g.player;
    if (p.state == PS_DODGE || p.state == PS_DEFLECT || p.state == PS_SHOVE || p.state == PS_STUN || p.state == PS_SPECIAL)
        return;
    if (p.state == PS_ATTACK && !weaponCanCancel(def))
        return;

    // roll toward move input if any, else current facing
    int16_t dx = p.fx;
    int16_t dy = p.fy;
    if (inp.mx || inp.my) {
        const int8_t di = fp::dirIndexFromInput(inp.mx, inp.my);
        dx = fp::dir8X(di);
        dy = fp::dir8Y(di);
        p.fx = dx;
        p.fy = dy;
    }

    const int8_t defId = weaponId(def);
    if (p.sheathed) {
        // stowed: every weapon rolls with the sword dodge numbers (MH-style run +
        // evade while sheathed)
        if (p.stam < 14)
            return;
        p.stam -= 14;
        p.state = PS_DODGE;
        p.t = 16;
        p.iT = 14;
        p.vx = (dx * 54) >> 4;
        p.vy = (dy * 54) >> 4;
        exitStance(p);
    } else if (defId == W_SWORD) {
        if (p.stam < 14)
            return;
        p.stam -= 14;
        p.state = PS_DODGE;
        p.t = 16;
        p.iT = 14;
        p.vx = (dx * 54) >> 4;   // 3.4 px/t
        p.vy = (dy * 54) >> 4;
        exitStance(p);
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
        exitStance(p);
        if (g.target.alive) {
            const Rect &m = g.target.rect;
            const int32_t mdx = (m.x + (m.w >> 1)) - (p.x + (p.w >> 1));
            const int32_t mdy = (m.y + (m.h >> 1)) - (p.y + (p.h >> 1));
            const int32_t dist = fp::isqrt(mdx * mdx + mdy * mdy);
            const int32_t dot = (mdx * p.fx + mdy * p.fy) >> 4;   // px along facing
            if (dist > 0 && dist < 38 && dot * 5 > dist * 2) {
                const int8_t di = fp::dirIndexFromDelta(mdx, mdy);
                if (g.target.onShove)
                    g.target.onShove(g, fp::dir8X(di), fp::dir8Y(di), 10, 2);
            }
        }
    }
}

static void fireShell(Game &g, const Player &p, const ShellDef *sh) {
    (void)sh;
    // Projectiles + muzzle effects are owned by hrd; record the shot so hrd can
    // spawn them from the same facing / spawn point the mock used.
    g.lastShot = p.shell + 1;   // 1 ball, 2 scatter
    g.lastShotX = p.x + (p.w >> 1);
    g.lastShotY = p.y + (p.h >> 1);
    g.lastShotFx = p.fx;   // facing at fire time (hrd spawns before post-fire drift)
    g.lastShotFy = p.fy;
}

static void stanceSpecial(Game &g, const WeaponDef *def) {
    Player &p = g.player;

    const Attack *special = weaponSpecial(def);
    const int8_t defId = weaponId(def);
    if (defId == W_SWORD) {
        const int16_t stam = attackStam(special);
        if (p.state != PS_IDLE)
            return;
        if (p.stam < stam)
            return;
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
            return;
        if (p.stam < stam)
            return;
        p.stam -= stam;
        p.throwCd = 50;
        p.state = PS_SPECIAL;
        p.atk = special;
        p.t = 0;
        p.hitDone = false;
    } else {
        if (p.reload > 0)
            return;
        const ShellDef *sh = weaponShell(def, p.shell);
        const int16_t stam = shellStam(sh);
        if (p.shells[p.shell] <= 0 || p.stam < stam)
            return;
        p.stam -= stam;
        // Demo: ammo is unlimited (the magazine count stays at max); the
        // per-shot reload timer still paces the gun. The shell-count check
        // above stays as a safety gate for manually emptied mags.
        p.reload = shellReload(sh);
        fireShell(g, p, sh);
    }
}

// Damage taken by the player. Parry/deflect/guard responses live here so the
// guard break -> stun path matches the mock exactly; monster-side stun/effects
// are forwarded to Target::onStun for zq5.
static void playerHurt(Game &g, int16_t dmg, int16_t faceX, int16_t faceY) {
    Player &p = g.player;
    if (p.iT > 0)
        return;

    if (p.state == PS_DEFLECT && p.t > 0) {
        if (g.target.onStun)
            g.target.onStun(g, 28);
        g.freeze = g.freeze > 5 ? g.freeze : 5;
        addEffect(g, static_cast<int16_t>(p.x + 8), static_cast<int16_t>(p.y + 8), 6, true, 0);
        return;
    }
    if (p.stance == ST_PARRY && p.stanceT <= 20) {
        p.riposteT = 90;
        if (g.target.onStun)
            g.target.onStun(g, 60);
        g.freeze = g.freeze > 8 ? g.freeze : 8;
        addEffect(g, static_cast<int16_t>(p.x + 8), static_cast<int16_t>(p.y + 8), 8, true, 0);
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
    addEffect(g, static_cast<int16_t>(p.x + 8), static_cast<int16_t>(p.y + 8), 8, false, 0);
}

// ------------------------------------------------------------ sheathe combo
// Ported from mock/game.js trySheathe()/sheatheCombo(), device variant ddab:
// double-tap Down then an A+B chord (3t grace). Only from idle (stance idle
// counts; the stance is dropped). Returns false when the state does not allow it,
// so the press pair falls through to its normal attack/dodge/stance meaning.
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
    p.seqT = 0;
    p.seq2 = false;
    p.sheatheLatch = true;   // suppress roll/stance until B is released
    return true;
}

// Evaluate the active variant (device ships ddab only). Returns true only when
// the toggle actually fired; the caller then consumes the A/B press this tick.
static bool sheatheCombo(Player &p, bool aP, const Input &inp) {
    if (p.sheathed)
        return false;   // stowed: A draws, combos do nothing
    const bool chordA = aP && (inp.b || p.chordT > 0);
    return chordA && p.seq2 && trySheathe(p);
}

static void updatePlayer(Game &g, const Input &inp, bool aP, bool bP, bool bR) {
    Player &p = g.player;
    const WeaponDef *def = &WEAPON_DEFS[g.weapon];

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
        if (p.chainWin == 0)
            p.chain = 0;
    }
    if (p.aBuffer > 0)
        p.aBuffer--;

    // Sheathe prototype (ddab): double-tap Down then an A+B chord within the
    // grace, then the combo check (consumes the press pair so it cannot also
    // attack/dodge/stance). SHEATHE_ENABLED folds this whole path out of the
    // parity image, whose scenes never stow (host suite covers it).
    bool sheatheConsumed = false;
    if (SHEATHE_ENABLED) {
        if (p.chordT > 0)
            p.chordT--;
        if (p.seqT > 0) {
            p.seqT--;
            if (p.seqT == 0)
                p.seq2 = false;
        }
        const bool myNow = inp.my > 0;
        if (myNow && !p.pMy) {   // down press edge
            if (p.seqT > 0)
                p.seq2 = true;   // second tap inside the window: combo armed
            p.seqT = SHEATHE_SEQ_WIN;
        }
        p.pMy = myNow;

        sheatheConsumed = sheatheCombo(p, aP, inp);
        if (!sheatheConsumed) {
            if (aP && !inp.b)
                p.chordT = CHORD_WIN;
            if (bP && !inp.a)
                p.chordT = CHORD_WIN;
        }
    }

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
    if (B_BRANCH_BUFFER_ENABLED && bP && !sheatheConsumed) {
        const Attack *a = p.atk;
        const bool inRecovery = p.state == PS_ATTACK && a && p.t >= attackStartup(a) + attackActive(a);
        const bool inLock = p.state == PS_IDLE && p.chain > 0 && p.chainLock > 0;
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
    if (aP && !sheatheConsumed) {
        if (p.sheathed) {
            if (p.state == PS_IDLE) {
                p.sheathed = false;
                p.chain = 0;
                p.chainWin = 0;
                p.aBuffer = 0;
                startAttack(g, def);
            }
        } else if (p.stance != ST_NONE) {
            stanceSpecial(g, def);
        } else if (canAttackNow) {
            startAttack(g, def);
        } else {
            p.aBuffer = A_BUFFER;   // buffered: fires when the debounce lock expires
        }
    }
    if (!p.sheathed && p.aBuffer > 0 && canAttackNow) {
        p.aBuffer = 0;
        startAttack(g, def);
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
                    // Smith tier damage (integer percent, truncating) applied
                    // before the riposte x2, then flows on through the existing
                    // Target::onHit -> monsterOnHit chain.
                    const int16_t dmg = upgradeMul(attackDmg(a), g.dmgMul);
                    g.target.onHit(g, static_cast<uint8_t>(dmg * mult), hx, hy, attackPush(a), attackEffect(a));
                }
            }
        }
        if (p.t >= total) {
            if (p.state == PS_ATTACK) {
                p.state = PS_IDLE;
                const bool finisher = p.chain >= 2;
                p.chain = p.chain < 2 ? p.chain + 1 : 0;
                p.chainLock = finisher ? COMBO_LOCK : CHAIN_GAP;
                p.chainWin = 0;   // window opens when the lock ends
            } else {
                p.state = PS_IDLE;
                p.chain = 0;
                p.chainWin = 0;
                p.chainLock = 0;
            }
            p.t = 0;
            p.atk = nullptr;
        }
        applyDrift(p);
        break;
    }
    case PS_DODGE:
    case PS_DEFLECT: {
        p.t--;
        applyDrift(p, 14);
        if (p.t <= 0)
            p.state = PS_IDLE;
        break;
    }
    case PS_SHOVE:
    case PS_STUN: {
        p.t--;
        if (p.t <= 0)
            p.state = PS_IDLE;
        break;
    }
    default:
        p.state = PS_IDLE;
    }

    if (p.stance != ST_NONE)
        updateStance(g, def);
    clampPlayer(p);
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