#pragma once
// Shared game structs + weapon tables, ported from mock/game.js (source of truth).
// No float, no Arduino.h — ints and fp.hpp only. Header-only; host tests and
// device share the exact same tables and constants.
//
// WEAPON_DEFS is a byte-for-byte port of mock/game.js WEAPON_DEFS. Do not
// retune numbers here.

#include <stdint.h>
#include "progmem.hpp"
#include "fp.hpp"
#include "input.hpp"
#include "fxmem.hpp"                      // FX cart offsets + mhFxRead* field readers (identity on host)
#include "../generated/combat_meta.hpp"   // data facts (HAS_ZONES) gate the zone caches

// Per-image zones carve (mirrors MH_AUDIO in src/audio.hpp): the on-device
// perf bench and parity scenes run only MON_LUNGE/SWEEP/HEAVY, which never run a
// breakable-zone, multi-window, stagger or zones-guard path, so those images
// compile the ravager machinery out with -DMH_COMBAT_PARTS=0 and keep their
// flash headroom. Shipping and test_combat keep the generated facts (default
// 1). The generated data facts stay authoritative; these effective flags only
// fold optional machinery for a build whose scene does not exercise it, and for
// the shipped 3 they are behavior-identical (single-window attacks, dist-only
// guards, staggerMax 0).
#ifndef MH_COMBAT_PARTS
#define MH_COMBAT_PARTS 1
#endif

namespace mh {

constexpr bool ZONES_ENABLED = combat::HAS_ZONES && MH_COMBAT_PARTS;
constexpr bool MULTI_WINDOW_ENABLED = combat::HAS_MULTI_WINDOW && MH_COMBAT_PARTS;
constexpr bool STAGGER_ENABLED = combat::HAS_STAGGER && MH_COMBAT_PARTS;
constexpr bool GUARD_ZONES_ENABLED = combat::HAS_GUARD_ZONES && MH_COMBAT_PARTS;
// A carved image only has dist-only guards, so the fast path is correct there.
constexpr bool SIMPLE_GUARDS = combat::HAS_SIMPLE_GUARDS || !MH_COMBAT_PARTS;

constexpr int16_t HOLD_TICKS = 11;        // B held this long -> stance (~180ms)
constexpr int16_t CHAIN_WIN = 14;         // chain follow-up window after a combo hit
constexpr uint8_t CHAIN_GAP = 9;          // HEAVY debounce: lock after a non-finisher hit
constexpr uint8_t COMBO_LOCK = 24;        // HEAVY debounce: lock after the finisher (chain >= 2)
constexpr uint8_t A_BUFFER = 16;          // attack input buffer in ticks (covers the gap lock)
constexpr uint8_t B_BRANCH_BUFFER = 36;   // B branch tap buffer: bridges recovery + lock
// Sheathe combo (ddab only): double-tap Down then an A+B chord within a 3t grace.
constexpr uint8_t SHEATHE_SEQ_WIN = 18;   // d-pad tap -> chord window (~300ms)
constexpr uint8_t CHORD_WIN = 3;          // A/B chord grace (~50ms)
constexpr uint8_t SHEATHE_SPD = 24;       // 1/16 px per tick while stowed (1.5 px/t run)
// Parity carve facts (same pattern as MH_COMBAT_PARTS): the test_parity scenes
// never sheathe and never queue a B branch through recovery/lock, so those input
// paths fold out of that image; the host suite keeps covering them.
#ifndef MH_SHEATHE
#define MH_SHEATHE 1
#endif
#ifndef MH_B_BRANCH_BUFFER
#define MH_B_BRANCH_BUFFER 1
#endif
constexpr bool SHEATHE_ENABLED = MH_SHEATHE;
constexpr bool B_BRANCH_BUFFER_ENABLED = MH_B_BRANCH_BUFFER;
// Roll-attack + direction+A opener carve (same pattern as MH_SHEATHE): no
// parity scene rolls into an A press or presses direction+A, so the A-out-of-
// evade branch and the alt table selection fold out of the test_parity image;
// the host suite keeps covering both paths.
#ifndef MH_ROLL_ALT
#define MH_ROLL_ALT 1
#endif
constexpr bool ROLL_ALT_ENABLED = MH_ROLL_ALT;
// Stage-3 finisher carve (same pattern as MH_SHEATHE): no test_parity scene
// taps B after a finisher (A A A then B), so the finWin writes, the idle
// stage-3 mapping and the bBuffer inLock extension fold out of that image;
// the host suite keeps covering the finisher path.
#ifndef MH_STAGE3
#define MH_STAGE3 1
#endif
constexpr bool STAGE3_ENABLED = MH_STAGE3;
// Charge carve (monhun-ardu-ynb, same pattern as MH_SHEATHE): no test_parity
// scene holds A past a swing, so the A-hold counter, the PS_CHARGE stance entry
// and the charge release fold out of that image; the host suite (and the
// shipping build, MH_CHARGE default 1) keeps covering them.
#ifndef MH_CHARGE
#define MH_CHARGE 1
#endif
constexpr bool CHARGE_ENABLED = MH_CHARGE;
// Player-move carve (push-rule bug fix 2026-09-19): no test_parity scene walks
// the hunter into the beast, so the per-tick player-move flag folds out of that
// image (its budget is at the board limit); the host suite and the shipping
// build keep it and pushApart defaults to the pre-fix give-way rule there.
#ifndef MH_PUSH_MOVE
#define MH_PUSH_MOVE 1
#endif
constexpr bool PUSH_MOVE_ENABLED = MH_PUSH_MOVE;
constexpr int16_t CHARGE_MIN = 14;   // A held this long past the swing -> charge stance
constexpr int16_t CHARGE_L2 = 20;    // extra charge ticks for level 2 (bar flashes white)
constexpr int16_t WORLD_W = 256;
constexpr int16_t WORLD_H = 112;

// hrd: projectiles / effects / training pole. Caps are device-sized ring
// buffers: the mock uses unbounded JS arrays, the device overrides the oldest
// entry when full (documented in src/core/projectiles.hpp).
constexpr int16_t MAX_PROJECTILES = 12;
constexpr int16_t MAX_EFFECTS = 12;
constexpr int16_t MAX_TRAIN_EVENTS = 24;
constexpr int16_t PROJ_LIFE = 90;   // ticks, mock fireShell()

enum Mode : int8_t {
    MODE_HUNT = 0,
    MODE_TRAIN = 1
};

enum WeaponId : int8_t {
    W_SWORD = 0,
    W_FLAIL = 1,
    W_GUN = 2
};
enum PState : int8_t {
    PS_IDLE = 0,
    PS_ATTACK,
    PS_SPECIAL,
    PS_DODGE,
    PS_DEFLECT,
    PS_SHOVE,
    PS_STUN,
    PS_CHARGE   // appended (ynb): existing 0..6 values must not move
};
enum Stance : int8_t {
    ST_NONE = 0,
    ST_PARRY,
    ST_WHIRL,
    ST_GUARD
};
enum AtkId : int8_t {
    ATK_NONE = 0,
    ATK_STEPSLASH,
    ATK_SPINCUT,
    ATK_TRIP,
    ATK_POINTBLANK,
    ATK_GUARDBASH
};

struct Rect {
    int16_t x, y, w, h;
    bool overlaps(const Rect &o) const {
        const int32_t ax = x, aw = w, ox = o.x, ow = o.w;
        const int32_t ay = y, ah = h, oy = o.y, oh = o.h;
        return ax < ox + ow && ax + aw > ox && ay < oy + oh && ay + ah > oy;
    }
};

// prototype circleRectOverlap: clamp circle center into rect, compare radius²
inline bool circleRectOverlap(int16_t cx, int16_t cy, int16_t r, const Rect &rect) {
    const int32_t nx = cx < rect.x ? rect.x : (cx > rect.x + rect.w ? rect.x + rect.w : cx);
    const int32_t ny = cy < rect.y ? rect.y : (cy > rect.y + rect.h ? rect.y + rect.h : cy);
    const int32_t dx = cx - nx;
    const int32_t dy = cy - ny;
    return dx * dx + dy * dy <= static_cast<int32_t>(r) * r;
}

struct Attack {
    int16_t startup, active, recover, dmg, reach, hw, hh, stam;
    int16_t lunge;   // branch lunge velocity, 0 = none
    int16_t push;    // branch knockback push distance, 0 = none
    int8_t effect;   // 0 none, 1 trip
    bool shell;      // consumes a ball shell
    int8_t id;       // AtkId (ATK_NONE for plain combo attacks)
};

struct Branch {
    int8_t stage;
    int8_t stance;   // ST_WHIRL for stance branches, ST_NONE for attack branches
    int16_t autoT;   // stanceAuto ticks when entering a stance branch
    Attack atk;
};

struct ShellDef {
    int16_t count, dmg, speedF, w, h, reload, stam;
    int8_t pellets;   // 1 = ball (heavy), 3 = scatter
};

struct WeaponDef {
    int8_t id;
    int16_t spd;   // 1/16 px per tick
    Attack attacks[3];
    Attack special;
    Branch branches[3];
    bool canCancel;   // may tap-B out of an attack into dodge
    ShellDef shells[2];
    Attack roll;                // A out of dodge/deflect/shove (bead monhun-ardu-8xx)
    Attack alt;                 // direction+A opener, replaces combo hit 1 at chain 0
    Attack charge[2];           // held-A release melee (ynb); zero = no charge data
    ShellDef chargeShells[2];   // held-A release shells (ynb); zero = no charge data
};

// On AVR the table lives on the FX cart as one packed 987 B blob (bead
// monhun-ardu-ynb: 759 B base + 3 x 76 B charge/chargeShells per weapon): the shim below exposes the same `WEAPON_DEFS[i]` /
// `&WEAPON_DEFS[i]` syntax, but every element is a fake 16-bit pointer into
// the cart's address space (the fxdata.h blob offset). Nothing dereferences it
// on MCU; the accessors read fields through mhFxRead*. The host keeps the
// plain array so player_test.hpp / shells_test.hpp field reads stay unchanged.
#if defined(__AVR__)
static_assert(sizeof(Attack) == 23, "Attack must match packed FX blob size");
static_assert(sizeof(Branch) == 27, "Branch must match packed FX blob size");
static_assert(sizeof(ShellDef) == 15, "ShellDef must match packed FX blob size");
static_assert(sizeof(WeaponDef) == 329, "WeaponDef must match packed FX blob size");

struct FxWeaponDefsRom {
    const WeaponDef &operator[](int16_t i) const {
        return *reinterpret_cast<const WeaponDef *>(static_cast<uint16_t>(MH_FX_WEAPON_DEFS_ADDR + sizeof(WeaponDef) * i));
    }
};
constexpr FxWeaponDefsRom WEAPON_DEFS = {};
#else
MH_PROGMEM const WeaponDef WEAPON_DEFS[3] = {
    // sword: fast taps, dodge (i-frames), parry stance + riposte special
    {
        W_SWORD,
        18,
        {{3, 5, 8, 9, 13, 12, 10, 9, 0, 0, 0, false, ATK_NONE}, {3, 5, 8, 10, 13, 12, 10, 9, 0, 0, 0, false, ATK_NONE}, {5, 6, 14, 17, 16, 18, 14, 15, 0, 0, 0, false, ATK_NONE}},
        {4, 6, 16, 24, 18, 20, 16, 20, 0, 0, 0, false, ATK_NONE},
        {{1, ST_NONE, 0, {3, 5, 12, 12, 18, 14, 12, 10, 42, 0, 0, false, ATK_STEPSLASH}},
         {2, ST_NONE, 0, {5, 7, 15, 20, 12, 28, 26, 16, 0, 0, 0, false, ATK_SPINCUT}},
         {3, ST_NONE, 0, {8, 4, 20, 26, 16, 20, 22, 18, 0, 0, 0, false, ATK_NONE}}},
        true,
        {{0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}},
        {4, 5, 10, 12, 15, 16, 14, 10, 0, 0, 0, false, ATK_NONE},                                                   // rollslash
        {6, 4, 12, 14, 22, 10, 10, 12, 20, 0, 0, false, ATK_NONE},                                                  // thrust (lunge 20)
        {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, ATK_NONE}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, ATK_NONE}},   // no charge
        {{0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}},                                                       // no charge shells
    },
    // flail: slow momentum chain, deflect step, whirl stance + ball throw
    {
        W_FLAIL,
        15,
        {{8, 6, 9, 14, 19, 20, 16, 13, 0, 0, 0, false, ATK_NONE}, {6, 6, 9, 17, 21, 22, 16, 12, 0, 0, 0, false, ATK_NONE}, {5, 7, 15, 25, 24, 24, 20, 17, 0, 0, 0, false, ATK_NONE}},
        {4, 8, 14, 27, 32, 14, 18, 22, 0, 0, 0, false, ATK_NONE},
        {{1, ST_WHIRL, 50, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, ATK_NONE}},
         {2, ST_NONE, 0, {5, 6, 16, 12, 22, 22, 14, 14, 0, 0, 1, false, ATK_TRIP}},
         {3, ST_NONE, 0, {10, 6, 24, 32, 24, 32, 24, 24, 0, 12, 1, false, ATK_NONE}}},
        false,
        {{0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}},
        {4, 6, 13, 15, 20, 24, 16, 10, 0, 0, 0, false, ATK_NONE},     // rollsweep
        {6, 6, 14, 18, 22, 30, 14, 14, 0, 0, 0, false, ATK_NONE},     // widesweep
        {{4, 6, 14, 24, 26, 28, 18, 14, 0, 0, 0, false, ATK_NONE},    // chargeslam1
         {5, 8, 20, 36, 28, 34, 24, 22, 0, 0, 1, false, ATK_NONE}},   // chargeslam2 (trip)
        {{0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}},         // no charge shells
    },
    // gunshield: slow walk, shove, guard stance + gun (ball / scatter)
    {
        W_GUN,
        9,
        {{5, 4, 11, 6, 11, 14, 12, 8, 0, 0, 0, false, ATK_NONE}, {5, 4, 11, 7, 11, 14, 12, 8, 0, 0, 0, false, ATK_NONE}, {7, 5, 15, 11, 13, 16, 14, 13, 0, 0, 0, false, ATK_NONE}},
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, ATK_NONE},   // gunshield fires shells, no melee special
        {{1, ST_NONE, 0, {4, 5, 16, 22, 15, 18, 16, 6, 0, 0, 0, true, ATK_POINTBLANK}},
         {2, ST_NONE, 0, {4, 4, 12, 9, 14, 16, 14, 8, 0, 12, 0, false, ATK_GUARDBASH}},
         {3, ST_NONE, 0, {6, 3, 20, 30, 16, 24, 18, 16, 0, 16, 0, false, ATK_NONE}}},
        true,
        {{2, 28, 35, 7, 6, 70, 6, 1}, {5, 7, 42, 4, 4, 30, 5, 3}},
        {3, 4, 12, 8, 14, 16, 14, 8, 30, 10, 0, false, ATK_NONE},                                                   // shieldbash (lunge 30, push 10)
        {4, 5, 14, 10, 15, 18, 16, 9, 18, 14, 0, false, ATK_NONE},                                                  // shieldcharge (lunge 18, push 14)
        {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, ATK_NONE}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, ATK_NONE}},   // no melee charge
        {{0, 34, 45, 7, 6, 70, 12, 1}, {0, 46, 55, 8, 8, 70, 18, 1}},                                               // charge ball L1/L2
    },
};
#endif   // __AVR__

// ------------------------------------------------ WEAPON_DEFS cart accessors
// Every field read goes through one of these, so the same core code works with
// the table in host RAM (make test) or on the FX cart (AVR). Sub-objects are
// addressed as `&d->attacks[i]` etc.; per-field readers then load exactly one
// value (plain deref on host, a cart read via mhFxRead* on AVR).

inline int8_t weaponId(const WeaponDef *d) {
    return mhFxReadI8(&d->id);
}
inline int16_t weaponSpd(const WeaponDef *d) {
    return mhFxReadI16(&d->spd);
}
inline bool weaponCanCancel(const WeaponDef *d) {
    return mhFxReadBool(&d->canCancel);
}
inline const Attack *weaponAttack(const WeaponDef *d, int16_t i) {
    return &d->attacks[i];
}
inline const Attack *weaponSpecial(const WeaponDef *d) {
    return &d->special;
}
inline const Attack *weaponRoll(const WeaponDef *d) {
    return &d->roll;
}
inline const Attack *weaponAlt(const WeaponDef *d) {
    return &d->alt;
}
inline const Branch *weaponBranch(const WeaponDef *d, int16_t i) {
    return &d->branches[i];
}
inline const ShellDef *weaponShell(const WeaponDef *d, int16_t i) {
    return &d->shells[i];
}
// Held-A release data (ynb). A weapon "has charge data" when its level-1 entry
// is non-zero (the mock's `def.charge` / `def.chargeShells` truthiness).
inline const Attack *weaponCharge(const WeaponDef *d, int16_t i) {
    return &d->charge[i];
}
inline const ShellDef *weaponChargeShell(const WeaponDef *d, int16_t i) {
    return &d->chargeShells[i];
}
// "Has charge data" tests: the mock checks the truthiness of def.charge /
// def.chargeShells (absent for weapons that cannot charge). The packed table
// always carries the slots, so a zeroed level-1 entry stands in for absence;
// the level-1 dmg is the single read that distinguishes the shipped sets
// (flail charge dmg 24, gun chargeShell dmg 34, all others 0).
inline bool weaponHasCharge(const WeaponDef *d) {
    return mhFxReadI16(&d->charge[0].dmg) != 0;
}
inline bool weaponHasChargeShells(const WeaponDef *d) {
    return mhFxReadI16(&d->chargeShells[0].dmg) != 0;
}

inline int16_t attackStartup(const Attack *a) {
    return mhFxReadI16(&a->startup);
}
inline int16_t attackActive(const Attack *a) {
    return mhFxReadI16(&a->active);
}
inline int16_t attackRecover(const Attack *a) {
    return mhFxReadI16(&a->recover);
}
inline int16_t attackDmg(const Attack *a) {
    return mhFxReadI16(&a->dmg);
}
inline int16_t attackReach(const Attack *a) {
    return mhFxReadI16(&a->reach);
}
inline int16_t attackHw(const Attack *a) {
    return mhFxReadI16(&a->hw);
}
inline int16_t attackHh(const Attack *a) {
    return mhFxReadI16(&a->hh);
}
inline int16_t attackStam(const Attack *a) {
    return mhFxReadI16(&a->stam);
}
inline int16_t attackLunge(const Attack *a) {
    return mhFxReadI16(&a->lunge);
}
inline int16_t attackPush(const Attack *a) {
    return mhFxReadI16(&a->push);
}
inline int8_t attackEffect(const Attack *a) {
    return mhFxReadI8(&a->effect);
}
inline bool attackShell(const Attack *a) {
    return mhFxReadBool(&a->shell);
}
inline int8_t attackId(const Attack *a) {
    return mhFxReadI8(&a->id);
}

inline int8_t branchStage(const Branch *b) {
    return mhFxReadI8(&b->stage);
}
inline int8_t branchStance(const Branch *b) {
    return mhFxReadI8(&b->stance);
}
inline int16_t branchAutoT(const Branch *b) {
    return mhFxReadI16(&b->autoT);
}
inline const Attack *branchAtk(const Branch *b) {
    return &b->atk;
}

inline int16_t shellCount(const ShellDef *s) {
    return mhFxReadI16(&s->count);
}
inline int16_t shellDmg(const ShellDef *s) {
    return mhFxReadI16(&s->dmg);
}
inline int16_t shellSpeedF(const ShellDef *s) {
    return mhFxReadI16(&s->speedF);
}
inline int16_t shellW(const ShellDef *s) {
    return mhFxReadI16(&s->w);
}
inline int16_t shellH(const ShellDef *s) {
    return mhFxReadI16(&s->h);
}
inline int16_t shellReload(const ShellDef *s) {
    return mhFxReadI16(&s->reload);
}
inline int16_t shellStam(const ShellDef *s) {
    return mhFxReadI16(&s->stam);
}
inline int8_t shellPellets(const ShellDef *s) {
    return mhFxReadI8(&s->pellets);
}

struct Game;

// Hurt-box target + hit-resolution indirection. zq5 (monster) and hrd (pole)
// plug their damage/knockback/trip handlers in here; this bead only resolves
// the overlap and forwards the exact mock numbers.
struct Target {
    Rect rect;   // hurt box (monster body or training pole)
    bool alive;
    void (*onHit)(Game &, uint8_t dmg, int16_t hx, int16_t hy, uint8_t push, uint8_t effect);
    void (*onShove)(Game &, int8_t dirX, int8_t dirY, uint8_t amount, uint8_t freeze);
    void (*onStun)(Game &, uint8_t ticks);   // deflect / parry response
};

// hrd: shot / spark / damage-number state, mirroring mock/game.js
// projectiles[] + effects[]. Positions are integer px with a 1/16 px
// remainder in subX/subY (fp::FpBody), so a projectile moves speedF/16 px
// per tick through fp::addVel. The mock stores pr.x pre-multiplied by 16 and
// then runs it through the pixel-domain addVel, a latent double-scaling bug
// that made shots ~1/16 speed (never exercised by mock/game.test.js); this
// port keeps the published numbers (spawn centre + facing*13, speedF, life
// 90) with the intended fixed-point motion.
struct Projectile : fp::FpBody {
    int8_t vx, vy;   // 1/16 px per tick: |dir| <= 16, speedF <= 42 -> |v| <= 42
    uint8_t w, h;    // collision size (px): generated shell w/h <= 7
    uint8_t dmg;     // generated shell dmg <= 28
    uint8_t life;    // PROJ_LIFE 90, culled at 0
    bool heavy;      // ball (render: big core) vs scatter pellet
};

// text == 0: spark / muzzle effect. text != 0: rising damage number.
struct Effect {
    int16_t x, y;
    uint8_t t, life;   // life <= 26 (spark/damage-number), t ages to life
    bool crit;
    int16_t text;
};

// Training-pole variants (bead monhun-ardu-6zb.6; part-locked zones 6zb.10).
// Kind selects the static creature record loaded through the shared combat
// loader; PLAIN carries a crit head zone, each breakable variant ONE part-locked
// appendage zone (cap/horn/collar, body-mul 101 so a part hit beats the body
// tie). The Pole itself only owns its world rect, hit flash timer and the
// selected variant kind: pool/broken live in the shared Game::combat zone
// caches (zoneBroken, zone[..].hp) exactly like a beast's, so there is no
// pole-specific drain/break code.
enum PoleKind : int8_t {
    POLE_PLAIN = 0,
    POLE_SEVER = 1,   // sword: cap pool -> cap shears off
    POLE_BREAK = 2,   // flail: horn pool -> horn snaps off
    POLE_CRACK = 3    // gun: collar pool -> collar splits
};

struct Pole {
    Rect rect;          // hurt box: every pole 20x36 at (140,40) (no resize)
    uint8_t hitFlash;   // 4 on hit, decays in updatePole()
    int8_t kind;        // PoleKind (selects the prop creature record)
};

struct TrainEvent {
    int32_t tick;
    int16_t dmg;
};

// Rolling window of landed pole hits. total / last are unbounded/latest,
// events backs trainDps() over the trailing 600 ticks.
struct TrainStats {
    int32_t total;
    int16_t last;
    TrainEvent ev[MAX_TRAIN_EVENTS];
    uint8_t head;    // next write slot, 0..MAX_TRAIN_EVENTS-1
    uint8_t count;   // 0..MAX_TRAIN_EVENTS
};

// Player inherits the fp bodies so addMove/addVel/drainStam work directly on
// it (p.x/p.subX and p.stam/p.stamSub are the fp-owned fields).
struct Player : fp::FpBody, fp::FpStam {
    uint8_t w, h;        // hurt box size (px): 16x16
    int8_t vx, vy;       // 1/16 px per tick, |v| <= 54
    int8_t fx, fy;       // 1/16 unit facing vector, -16..16
    uint8_t hp, hpMax;   // 0..100, clamped at 0 on the landing tick
    uint8_t stamMax;
    PState state;
    uint8_t t;   // state timer, <= ~200
    const Attack *atk;
    bool hitDone;
    uint8_t chain, chainWin, aBuffer;   // chain 0..2, windows <= CHAIN_WIN/A_BUFFER
    bool finWin;                        // combo finisher done: the next B is the stage-3 branch
    // Sheathe combo (ddab): stowed flag + latch (suppresses B until release),
    // double-tap Down tracker (seqT alive, seq2 armed on the second press) and
    // the A/B chord grace; combo debounce lock + B branch tap buffer.
    bool sheathed, sheatheLatch;
    uint8_t seqT;
    bool seq2;
    uint8_t chordT;
    bool pMy;
    uint8_t chainLock;
    uint8_t bBuffer;
    Stance stance;
    uint8_t stanceT, stanceAuto, whirlTick;
    uint8_t throwCd, riposteT;
    int16_t bHeld;
    bool bReady, bLocked;
    uint8_t iT;
    int8_t shell;   // 0 ball, 1 scatter
    uint8_t reload;
    uint8_t shells[2];
    // Charge attack (ynb): A-hold counter + previous-A edge, the charge stance
    // timer and the "swing is chargeable" latch (set by startAttack, cleared on
    // A release). Appended last so existing hashed/state fields do not move.
    bool pA;
    uint8_t aHold;
    uint8_t chargeT;
    bool chargeArmed;

    void init(int8_t weapon);
};

// --------------------------------------- combat loader caches (ljj.2)
// Live RAM caches for src/core/combat.hpp. The cache types live here (not in
// combat.hpp) because Game stores them by value and combat.hpp includes
// game.hpp; keeping them next to Player/Monster/Target is the same layering.
// Sizes are the design contract (docs/creature-framework.md section 9):
// profile 23 B + attack 21 B + runtime 7 B = 51 B on AVR, no bulk table loads.
// Compact/packed by construction on AVR (byte fields, uint16 alignment 1).

struct CombatBox {
    int8_t ox, oy;
    uint8_t w, h;
};

// Fixed zone slots (build/zones-design.md): slot 0 head, slot 1 appendage.
// combat.hpp's COMBAT_ZONE_COUNT/bit constants mirror this order.
constexpr uint8_t COMBAT_ZONE_SLOTS = 2;

// Full profile record mirror (17 fields, blob ABI order). Read whole at spawn
// and cached; the interpreter consumes the cache at decision time. zoneFlags
// carries the per-creature zone presence bits (build/zones-design.md). faceHold
// (nch.4) is the turn-commitment cadence: 0 recomputes facing every tick, >0
// refreshes it only every faceHold ticks.
struct CombatProfile {
    uint8_t engageDist, keepDist, attackDist;
    uint8_t circleNum, circleDen, retreatNum, retreatDen;
    uint8_t staggerMax, staggerDecay, zoneFlags;
    uint8_t faceHold;
    uint16_t cdBase, cdJitter, spawnT, spawnCd, stunRecoverT, staggerRecoverT;
};

// One hit window (blob ABI order minus the reserved flags byte, which the
// loader does not cache: always 0 today and never read).
struct CombatWindow {
    uint16_t t0, t1;
    CombatBox box;
    uint8_t dmgMul;
};

// Face-relative box offset (docs section 3): the box centre sits at (ox, oy)
// in the facing frame, so its world offset is the DIR8 rotation of (ox, oy).
// With oy == 0 this is exactly the legacy scalar reach projection
// (((fx * reach) >> 4) / ((fy * reach) >> 4)), so shipped windows stay
// identical. combatFacePoint is the same rotation for arbitrary (not int8)
// offsets: part boxes rotate their centre, which can exceed the int8 range.
inline void combatFacePoint(int16_t fx, int16_t fy, int32_t ox, int32_t oy, int32_t &dx, int32_t &dy) {
    dx = ((static_cast<int32_t>(fx) * ox) - (static_cast<int32_t>(fy) * oy)) >> 4;
    dy = ((static_cast<int32_t>(fy) * ox) + (static_cast<int32_t>(fx) * oy)) >> 4;
}

inline void combatFaceOffset(int16_t fx, int16_t fy, const CombatBox &b, int32_t &dx, int32_t &dy) {
    combatFacePoint(fx, fy, b.ox, b.oy, dx, dy);
}

// Attack scalar cache + the currently loaded window. Read once at attack start
// (~16 FX reads), refreshed only when the interpreter switches windows;
// per-tick code consumes this cache and performs no cart reads.
struct CombatAttackCache {
    uint16_t windup, active, recover, dmg;
    uint8_t moveType, moveSpeedF;
    uint8_t facing;
    uint8_t winIdx;   // index of the cached window in the WINDOWS section
    CombatWindow win;
};

// Live scalars of one zone record (build/zones-design.md). Box + multipliers +
// the remaining pool + the single broken-record fields the runtime needs. The
// record's brokenDmgMul/brokenFlags are data-only (a broken zone leaves the
// candidate set), so they are not cached.
struct CombatZoneCache {
    CombatBox box;   // 4 B AVR: face-relative zone origin + size
    uint8_t hp;      // remaining zone pool
    uint8_t hpMax;   // pool at load (render damage stage); 0 = no zone
    uint8_t dmgMul;
    uint8_t bodyShare;
    uint8_t breakTypes;
    uint8_t staggerOnHit;
    uint8_t unlockMask;   // attacks disabled while this zone is broken
};

// Combat runtime state: which creature was loaded, the implicit body box (the
// creature w/h at (0,0)), the body-collision box (authored `collide` box, or
// the body box when absent), the two optional zone slots (slot 0 head, slot 1
// appendage; headZone/appendZone hold the global ZONES index or COMBAT_NO_ZONE)
// and the single broken bit per zone. The pattern step cursor
// (stepIdx + 256-tick countdown stepT) and stagger meter are unchanged.
struct CombatState {
    CombatProfile profile;                     // 23 B AVR
    CombatAttackCache attack;                  // 21 B AVR
    CombatBox body;                            // 4 B AVR
    CombatBox collide;                         // 4 B AVR: body-collision rect
    CombatZoneCache zone[COMBAT_ZONE_SLOTS];   // 20 B AVR
    uint8_t headZone;                          // global ZONE index or COMBAT_NO_ZONE
    uint8_t appendZone;
    uint8_t creature;     // index into CREATURES
    uint8_t zoneBroken;   // bit0 head, bit1 appendage
    uint8_t isStatic;     // creature record flags bit0: static prop (pole)
    uint8_t patternIdx;
    uint8_t stepIdx;
    uint8_t stepT;     // 8-bit countdown: step `after`/WAIT ticks cap at 255
    uint8_t stagger;   // stagger meter accumulator (profile.staggerMax = 0 -> unused)
};

// Monster attack table — byte-for-byte port of mock/game.js MONSTER_ATTACKS.
// speedF only applies to the lunge; sweep is stationary.
enum MKind : int8_t {
    MK_LUNGE = 0,
    MK_SWEEP = 1
};
struct MonsterAttack {
    int8_t kind;
    int16_t windup, active, recover, speedF, dmg, reach, hw, hh;
};

// On AVR the table lives on the FX cart as one packed 34 B blob (bead
// monhun-ardu-42n.1), addressed through the same fake-pointer shim as
// WEAPON_DEFS. monster_test.hpp still reads the plain host array
// field-by-field; core code uses the accessors below.
#if defined(__AVR__)
static_assert(sizeof(MonsterAttack) == 17, "MonsterAttack must match packed FX blob size");

struct FxMonsterAttacksRom {
    const MonsterAttack &operator[](int16_t i) const {
        return *reinterpret_cast<const MonsterAttack *>(static_cast<uint16_t>(MH_FX_MONSTER_ATTACKS_ADDR + sizeof(MonsterAttack) * i));
    }
};
constexpr FxMonsterAttacksRom MONSTER_ATTACKS = {};
#else
MH_PROGMEM const MonsterAttack MONSTER_ATTACKS[2] = {
    {MK_LUNGE, 40, 10, 55, 34, 12, 12, 24, 22},
    {MK_SWEEP, 48, 12, 60, 0, 9, 17, 32, 24},
};
#endif   // __AVR__

// --------------------------------------------- MONSTER_ATTACKS cart accessors
inline int8_t monsterAttackKind(const MonsterAttack *a) {
    return mhFxReadI8(&a->kind);
}
inline int16_t monsterAttackWindup(const MonsterAttack *a) {
    return mhFxReadI16(&a->windup);
}
inline int16_t monsterAttackActive(const MonsterAttack *a) {
    return mhFxReadI16(&a->active);
}
inline int16_t monsterAttackRecover(const MonsterAttack *a) {
    return mhFxReadI16(&a->recover);
}
inline int16_t monsterAttackSpeedF(const MonsterAttack *a) {
    return mhFxReadI16(&a->speedF);
}
inline int16_t monsterAttackDmg(const MonsterAttack *a) {
    return mhFxReadI16(&a->dmg);
}
inline int16_t monsterAttackReach(const MonsterAttack *a) {
    return mhFxReadI16(&a->reach);
}
inline int16_t monsterAttackHw(const MonsterAttack *a) {
    return mhFxReadI16(&a->hw);
}
inline int16_t monsterAttackHh(const MonsterAttack *a) {
    return mhFxReadI16(&a->hh);
}

// Monster roster (bead monhun-ardu-6zb.1): the three demo beast variants.
// Index 0 (LUNGE) is the legacy parity default: its def reproduces the values
// initMonster() used to hardcode. atkDist is the lunge/sweep split distance;
// a negative value means "never lunge" (SWEEP always sweeps).
enum MonsterKind : int8_t {
    MON_LUNGE = 0,
    MON_SWEEP = 1,
    MON_HEAVY = 2,
    MON_RAVAGER = 3   // ljj.6: first breakable-part creature (data/creatures/ravager.json)
};
struct MonsterDef {
    int8_t kind;
    int16_t w, h, hp, spd, atkDist;
};

// On AVR the roster lives on the FX cart as one packed 33 B blob (bead
// monhun-ardu-6zb.1), addressed through the same fake-pointer shim as
// WEAPON_DEFS / MONSTER_ATTACKS.
#if defined(__AVR__)
static_assert(sizeof(MonsterDef) == 11, "MonsterDef must match packed FX blob size");

struct FxMonsterDefsRom {
    const MonsterDef &operator[](int16_t i) const {
        return *reinterpret_cast<const MonsterDef *>(static_cast<uint16_t>(MH_FX_MONSTER_DEFS_ADDR + sizeof(MonsterDef) * i));
    }
};
constexpr FxMonsterDefsRom MONSTER_DEFS = {};
#else
MH_PROGMEM const MonsterDef MONSTER_DEFS[4] = {
    {MON_LUNGE, 32, 24, 200, 5, 32},
    {MON_SWEEP, 28, 22, 150, 7, -1},
    {MON_HEAVY, 40, 28, 320, 3, 24},
    {MON_RAVAGER, 32, 24, 260, 6, 24},
};
#endif   // __AVR__

// ----------------------------------------------- MONSTER_DEFS cart accessors
inline int8_t monsterDefKind(const MonsterDef *d) {
    return mhFxReadI8(&d->kind);
}
inline int16_t monsterDefW(const MonsterDef *d) {
    return mhFxReadI16(&d->w);
}
inline int16_t monsterDefH(const MonsterDef *d) {
    return mhFxReadI16(&d->h);
}
inline int16_t monsterDefHp(const MonsterDef *d) {
    return mhFxReadI16(&d->hp);
}
inline int16_t monsterDefSpd(const MonsterDef *d) {
    return mhFxReadI16(&d->spd);
}
inline int16_t monsterDefAtkDist(const MonsterDef *d) {
    return mhFxReadI16(&d->atkDist);
}

enum MState : int8_t {
    MS_IDLE = 0,
    MS_PURSUE,
    MS_WINDUP,
    MS_ATTACK,
    MS_RECOVER,
    MS_DEAD,
    MS_STAGGER   // appended: parity fixtures hash the legacy 0..5 values
};
enum Over : int8_t {
    OVER_NONE = 0,
    OVER_WIN,
    OVER_LOSE
};

// FSM fields mirror the mock monster object; face is a fixed 1/16 unit vector
// (never a normalized float).
struct Monster : fp::FpBody {
    uint8_t w, h;        // hurt box size (px): generated creature w/h <= 40
    int16_t hp, hpMax;   // generated creature hp <= 320
    MState state;
    int16_t t, cd;   // 30000 sentinel set by the parity fixture setup; kept int16
    int8_t fx, fy;   // 1/16 unit facing vector, -16..16
    // Attack identity + cache link (migration A): the global attack index the
    // combat loader cached in Game::combat.attack, COMBAT_NO_ATTACK when none.
    // Replaces the old MONSTER_ATTACKS pointer; render/debug read this plus the
    // cached window scalars, never a table pointer.
    uint8_t atkIdx;
    uint8_t winRemain;   // windows remaining after the cached one (multi-window)
    int8_t lvx, lvy;     // lunge velocity (1/16 px per tick), |v| <= 34
    uint8_t windupMax, hitFlash, stun;
    int8_t circleDir;
    uint8_t spd;   // generated creature spd <= 7
    // Turn-commitment countdown (nch.4): profile.faceHold 0 = facing recomputed
    // every tick; >0 = faceT counts down and facing refreshes at 0. Lock modes
    // (windup/attack of a lock attack) still freeze facing regardless.
    uint8_t faceT;
};

struct Game {
    int16_t tick;
    uint8_t freeze;   // hitstop ticks, 0..12
    int8_t weapon;
    int8_t over;          // Over: 0 none, 1 win, 2 lose
    int8_t mode;          // Mode: hunt or train (hrd)
    int8_t monsterKind;   // MonsterKind: chosen demo beast variant (6zb)
    uint8_t camX, camY;   // camera top-left in world px: 0..CAM_MAX_X/Y
    bool prevA, prevB;
    // Set by updatePlayer(): the player's fixed-point state changed this tick
    // (input move, drift, knockback). pushApart uses it to resolve a body
    // overlap on the player side, so a walking hunter cannot shove the beast.
    bool playerMoved;
    Player player;
    Monster monster;
    Target target;
    int8_t lastShot;                 // 1 ball, 2 scatter; cleared by spawnShot (hrd)
    uint8_t lastShotX, lastShotY;    // player centre at fire time: 8..248
    int8_t lastShotFx, lastShotFy;   // facing at fire time, -16..16
    uint8_t projN;                   // 0..MAX_PROJECTILES
    Projectile proj[MAX_PROJECTILES];
    uint8_t fxN;   // 0..MAX_EFFECTS
    Effect fx[MAX_EFFECTS];
    Pole pole;
    TrainStats train;
    CombatState combat;   // combat loader caches (ljj.2, 50 B AVR)
    // Quest kill accounting (bead monhun-ardu-me6): the sketch sets questTarget
    // from the active quest's def at hunt start (-1 = none), restores progress
    // from the save, and every monster death of that kind bumps questProgress.
    // The hunt-end commit writes questProgress back to the save. Not part of the
    // deterministic sim hash: a default Game (questTarget -1) never counts.
    int8_t questTarget;   // MonsterKind to count, -1 = no active quest
    uint8_t questNeed;
    uint8_t questProgress;
    // Smith upgrade multipliers (bead monhun-ardu-4ug): resolved from the
    // mhSmith cart table + the save tiers at hunt start (100 = no upgrade).
    // player.hpp scales melee damage + move speed, projectiles.hpp scales shell
    // damage. Not part of the parity state hash; initGame defaults them to
    // 100/100 so a default Game keeps the sim byte-identical.
    uint8_t dmgMul;
    uint8_t spdMul;
};

}   // namespace mh