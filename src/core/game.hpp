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
#include "../generated/combat_meta.hpp"   // data facts (HAS_PARTS) size the part caches

namespace mh {

constexpr int16_t HOLD_TICKS = 11;   // B held this long -> stance (~180ms)
constexpr int16_t CHAIN_WIN = 14;    // chain follow-up window after a combo hit
constexpr int16_t A_BUFFER = 10;     // attack input buffer in ticks
constexpr int16_t WORLD_W = 256;
constexpr int16_t WORLD_H = 112;

// hrd: projectiles / effects / training pole. Caps are device-sized ring
// buffers: the mock uses unbounded JS arrays, the device overrides the oldest
// entry when full (documented in src/core/projectiles.hpp).
constexpr int16_t MAX_PROJECTILES = 12;
constexpr int16_t MAX_EFFECTS = 12;
constexpr int16_t MAX_TRAIN_EVENTS = 24;
constexpr int16_t PROJ_LIFE = 90;   // ticks, mock fireShell()
constexpr int16_t POLE_HEAD = 16;   // head zone = top 16 px (x1.4)

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
    PS_STUN
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
    Branch branches[2];
    bool canCancel;   // may tap-B out of an attack into dodge
    ShellDef shells[2];
};

// On AVR the table lives on the FX cart as one packed 540 B blob (bead
// monhun-ardu-42n.1): the shim below exposes the same `WEAPON_DEFS[i]` /
// `&WEAPON_DEFS[i]` syntax, but every element is a fake 16-bit pointer into
// the cart's address space (the fxdata.h blob offset). Nothing dereferences it
// on MCU; the accessors read fields through mhFxRead*. The host keeps the
// plain array so player_test.hpp / shells_test.hpp field reads stay unchanged.
#if defined(__AVR__)
static_assert(sizeof(Attack) == 23, "Attack must match packed FX blob size");
static_assert(sizeof(Branch) == 27, "Branch must match packed FX blob size");
static_assert(sizeof(ShellDef) == 15, "ShellDef must match packed FX blob size");
static_assert(sizeof(WeaponDef) == 180, "WeaponDef must match packed FX blob size");

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
        {{1, ST_NONE, 0, {3, 5, 12, 12, 18, 14, 12, 10, 42, 0, 0, false, ATK_STEPSLASH}}, {2, ST_NONE, 0, {5, 7, 15, 20, 12, 28, 26, 16, 0, 0, 0, false, ATK_SPINCUT}}},
        true,
        {{0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}},
    },
    // flail: slow momentum chain, deflect step, whirl stance + ball throw
    {
        W_FLAIL,
        15,
        {{8, 6, 9, 14, 19, 20, 16, 13, 0, 0, 0, false, ATK_NONE}, {6, 6, 9, 17, 21, 22, 16, 12, 0, 0, 0, false, ATK_NONE}, {5, 7, 15, 25, 24, 24, 20, 17, 0, 0, 0, false, ATK_NONE}},
        {4, 8, 14, 27, 32, 14, 18, 22, 0, 0, 0, false, ATK_NONE},
        {{1, ST_WHIRL, 50, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, ATK_NONE}}, {2, ST_NONE, 0, {5, 6, 16, 12, 22, 22, 14, 14, 0, 0, 1, false, ATK_TRIP}}},
        false,
        {{0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}},
    },
    // gunshield: slow walk, shove, guard stance + gun (ball / scatter)
    {
        W_GUN,
        9,
        {{5, 4, 11, 6, 11, 14, 12, 8, 0, 0, 0, false, ATK_NONE}, {5, 4, 11, 7, 11, 14, 12, 8, 0, 0, 0, false, ATK_NONE}, {7, 5, 15, 11, 13, 16, 14, 13, 0, 0, 0, false, ATK_NONE}},
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, ATK_NONE},   // gunshield fires shells, no melee special
        {{1, ST_NONE, 0, {4, 5, 16, 22, 15, 18, 16, 6, 0, 0, 0, true, ATK_POINTBLANK}}, {2, ST_NONE, 0, {4, 4, 12, 9, 14, 16, 14, 8, 0, 12, 0, false, ATK_GUARDBASH}}},
        true,
        {{2, 28, 35, 7, 6, 70, 6, 1}, {5, 7, 42, 4, 4, 30, 5, 3}},
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
inline const Branch *weaponBranch(const WeaponDef *d, int16_t i) {
    return &d->branches[i];
}
inline const ShellDef *weaponShell(const WeaponDef *d, int16_t i) {
    return &d->shells[i];
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
    void (*onHit)(Game &, int dmg, int hx, int hy, int push, int effect);
    void (*onShove)(Game &, int dirX, int dirY, int amount, int freeze);
    void (*onStun)(Game &, int ticks);   // deflect / parry response
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
    int16_t vx, vy;   // 1/16 px per tick
    int16_t w, h;     // collision size (px)
    int16_t dmg;
    int16_t life;
    bool heavy;   // ball (render: big core) vs scatter pellet
};

// text == 0: spark / muzzle effect. text != 0: rising damage number.
struct Effect {
    int16_t x, y;
    int16_t t, life;
    bool crit;
    int16_t text;
};

struct Pole {
    Rect rect;          // hurt box: 20x36 at (140,40)
    int16_t hitFlash;   // 4 on hit, decays in updatePole()
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
    int16_t head;   // next write slot
    int16_t count;
};

// Player inherits the fp bodies so addMove/addVel/drainStam work directly on
// it (p.x/p.subX and p.stam/p.stamSub are the fp-owned fields).
struct Player : fp::FpBody, fp::FpStam {
    int16_t w, h;     // hurt box size (px)
    int16_t vx, vy;   // 1/16 px per tick (dodge/deflect/lunge/knockback)
    int16_t fx, fy;   // 1/16 unit facing vector
    int16_t hp, hpMax;
    int16_t stamMax;
    PState state;
    int16_t t;
    const Attack *atk;
    bool hitDone;
    int16_t chain, chainWin, aBuffer;
    Stance stance;
    int16_t stanceT, stanceAuto, whirlTick;
    int16_t throwCd, riposteT;
    int16_t bHeld;
    bool bReady, bLocked;
    int16_t iT;
    int8_t shell;   // 0 ball, 1 scatter
    int16_t reload;
    int16_t shells[2];

    void init(int8_t weapon);
};

// --------------------------------------- combat loader caches (ljj.2)
// Live RAM caches for src/core/combat.hpp. The cache types live here (not in
// combat.hpp) because Game stores them by value and combat.hpp includes
// game.hpp; keeping them next to Player/Monster/Target is the same layering.
// Sizes are the design contract (docs/creature-framework.md section 9):
// profile 22 B + attack 21 B + runtime 7 B = 50 B on AVR, no bulk table loads.
// Compact/packed by construction on AVR (byte fields, uint16 alignment 1).

struct CombatBox {
    int8_t ox, oy;
    uint8_t w, h;
};

// Part stage bits are 2 per part; up to 8 effective parts are tracked (docs
// section 4). The live part caches (pools + boxes) are sized by the data fact,
// so a build with no part pools pays one placeholder slot instead of eight.
constexpr uint8_t COMBAT_MAX_PARTS = 8;
constexpr uint8_t COMBAT_PART_SLOTS = combat::HAS_PARTS ? COMBAT_MAX_PARTS : 1;

// Full profile record mirror (16 fields, blob ABI order). Read whole at spawn
// and cached; the interpreter consumes the cache at decision time.
struct CombatProfile {
    uint8_t engageDist, keepDist, attackDist;
    uint8_t circleNum, circleDen, retreatNum, retreatDen;
    uint8_t staggerMax, staggerDecay, partCount;
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

// Combat runtime state: which creature was loaded, the creature's body part
// box (migration B: hurt/collide geometry read once at spawn), 2-bit part
// stages for up to 8 effective parts (saturating; parts beyond slot 7 are not
// stage-tracked), and the pattern step cursor (stepIdx + 256-tick countdown
// stepT).
//
// ljj.6 adds the live part caches for the first breakable-part creature
// (docs section 4): effective part ordinals run skeleton parts first, then the
// creature override list (bodyFirst/bodyCount and overFirst/overCount are the
// two global lists); partHp[k] is the remaining pool (0 = no pool) and
// partsHurt the facing-independent hurt envelope of all part rects (union over
// the 8 facings, anchor-relative to m.x/m.y), so the per-tick target sync is
// four stores and hit resolution/render rotate one box for the current facing.
// The arrays exist only when the shipped blob declares pools/stages
// (combat::HAS_PARTS); otherwise they fold to 1 slot.
struct CombatState {
    CombatProfile profile;      // 22 B AVR
    CombatAttackCache attack;   // 21 B AVR
    CombatBox body;             // 4 B AVR: skeleton body part box (spawn cache)
    uint8_t bodyFirst;          // hurtbox-list head (skeleton parts)
    uint8_t bodyCount;
    uint8_t overFirst;   // per-creature part override list head
    uint8_t overCount;
    uint8_t creature;                     // index into CREATURES
    uint16_t stages;                      // 2 bits x 8 parts, 0 = intact
    uint16_t partHp[COMBAT_PART_SLOTS];   // live part pools (0 = no pool)
    CombatBox partsHurt;                  // union of part rects over all facings
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
    int16_t w, h;   // hurt box size (px)
    int16_t hp, hpMax;
    MState state;
    int16_t t, cd;
    int16_t fx, fy;   // 1/16 unit facing vector
    // Attack identity + cache link (migration A): the global attack index the
    // combat loader cached in Game::combat.attack, COMBAT_NO_ATTACK when none.
    // Replaces the old MONSTER_ATTACKS pointer; render/debug read this plus the
    // cached window scalars, never a table pointer.
    uint8_t atkIdx;
    uint8_t winRemain;   // windows remaining after the cached one (multi-window)
    int16_t lvx, lvy;    // lunge velocity (1/16 px per tick)
    int16_t windupMax, hitFlash, stun, circleDir, spd;
};

struct Game {
    int16_t tick, freeze;
    int8_t weapon;
    int8_t over;          // Over: 0 none, 1 win, 2 lose
    int8_t mode;          // Mode: hunt or train (hrd)
    int8_t monsterKind;   // MonsterKind: chosen demo beast variant (6zb)
    int16_t camX, camY;   // camera top-left in world px (mock g.cam), updated in world.hpp
    bool prevA, prevB;
    Player player;
    Monster monster;
    Target target;
    int8_t lastShot;                  // 1 ball, 2 scatter; cleared by spawnShot (hrd)
    int16_t lastShotX, lastShotY;     // player centre at fire time
    int16_t lastShotFx, lastShotFy;   // facing at fire time
    int16_t projN;
    Projectile proj[MAX_PROJECTILES];
    int16_t fxN;
    Effect fx[MAX_EFFECTS];
    Pole pole;
    TrainStats train;
    CombatState combat;   // combat loader caches (ljj.2, 50 B AVR)
};

}   // namespace mh