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
#include "../generated/items_meta.hpp"    // item ids/kinds + inventory cap (prg.2)
#include "../armor_state.hpp"             // ArmorAgg: equipped-piece stat cache (arm.2)

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

constexpr int16_t HOLD_TICKS = 11;   // B held this long -> stance (~180ms)
// S2 stow: A held this long on an armed press puts the weapon away (sword/gun
// after the swing; flail at CHARGE_MIN + this in PS_CHARGE). The d-pad stays
// free for rolls in every direction, stance included.
constexpr uint8_t STOW_HOLD_TICKS = 24;
// Items + gathering (bead monhun-ardu-feel.22; item table prg.2): sheathed A
// inside a gather node runs PS_GATHER, sheathed B-hold runs PS_ITEM (herb use).
// The inventory is a per-hunt u8 array indexed by the generated item ids
// (items_meta.hpp, data/items.json); node depletion is a Game bitmask reset in
// newGame, never by loadRoom (a node stays picked for the hunt). The mh
// aliases below keep the core/test surface stable; the canonical constants
// live in namespace item.
constexpr uint8_t ITEM_COUNT = item::ITEM_COUNT;   // generated inventory cap (<= ITEM_MAX)
constexpr uint8_t ITEM_HERB = item::ITEM_HERB;
constexpr uint8_t ITEM_BLUE_MUSHROOM = item::ITEM_BLUE_MUSHROOM;
constexpr uint8_t ITEM_ORE = item::ITEM_ORE;
constexpr uint8_t ITEM_BUG = item::ITEM_BUG;
constexpr uint8_t ITEM_SCALE = item::ITEM_SCALE;
constexpr uint8_t ITEM_SHELL = item::ITEM_SHELL;
constexpr uint8_t ITEM_FANG = item::ITEM_FANG;
constexpr uint8_t ITEM_TAIL = item::ITEM_TAIL;
static_assert(ITEM_COUNT <= item::ITEM_MAX, "Game::items[] cap is item::ITEM_MAX (one u8 per id)");
constexpr uint8_t ITEM_NODE_NONE = 0xFF;   // Player::itemNode: no node bound
constexpr uint8_t GATHER_TICKS = 40;       // rooted gather window (ticks)
constexpr uint8_t ITEM_USE_TICKS = 40;     // rooted herb-use window (ticks)
// Carve (bead monhun-ardu-prg.3): after a win the carcass is a sheathed-style
// interact. CARVE_TICKS is the rooted window; CARVE_MAX carves per hunt; the
// packed drop table has combat::CARVE_SLOTS fixed slots.
constexpr uint8_t CARVE_TICKS = 40;
constexpr uint8_t CARVE_MAX = 3;
// Hitscan gun (gun rework): ticks to nock the next arrowshot. Player::reload
// doubles as the nock timer (0 = ready); the HUD lane at x=67 shows the state.
constexpr uint8_t ARROW_NOCK_TICKS = 24;
// Fixed packed drop-table slot count. Kept literal so the toolchain bootstrap
// (gen.sh compiles fxdump, which includes this header, before gen-combat.py
// rewrites the generated headers) never depends on the fresh symbol;
// combat.hpp static_asserts it against the generated combat::CARVE_SLOTS.
constexpr uint8_t CARVE_SLOTS = 4;
constexpr int16_t CHAIN_WIN = 14;         // chain follow-up window after a combo hit
constexpr uint8_t CHAIN_GAP = 9;          // HEAVY debounce: lock after a non-finisher hit
constexpr uint8_t COMBO_LOCK = 24;        // HEAVY debounce: lock after the finisher (chain >= 2)
constexpr uint8_t A_BUFFER = 16;          // attack input buffer in ticks (covers the gap lock)
constexpr uint8_t B_BRANCH_BUFFER = 36;   // B branch tap buffer: bridges recovery + lock
// Sheathe (feel.17): hold B and double-tap Down. The stow rides the feel.16
// double-tap detector instead of the old A+B chord, since B is the stance
// modifier for every weapon.
// Double-tap d-pad dodge (feel.16): two same-direction press edges inside this
// window fire the weapon tap-defense toward the tapped direction.
constexpr uint8_t DTAP_WIN = 10;
constexpr uint8_t SHEATHE_SPD = 24;   // 1/16 px per tick while stowed (1.5 px/t run)
// Parity carve facts (same pattern as MH_COMBAT_PARTS): the test_parity scenes
// never sheathe and never queue a B branch through recovery/lock, so those input
// paths fold out of that image; the host suite keeps covering them.
// B-branch buffer carve (prg.11, same pattern as MH_STAGE3/MH_ROLL_ALT): prg.11
// turned the shipping default off (the A-A-B queue through recovery/lock is
// trimmed for the progression-wave budget; a loose A A B no longer combos
// through the gap). The carve stays: -DMH_B_BRANCH_BUFFER=1 re-enables the
// buffer, and the host suite (Makefile TEST_FLAGS) covers the path forced on.
#ifndef MH_SHEATHE
#define MH_SHEATHE 1
#endif
#ifndef MH_B_BRANCH_BUFFER
#define MH_B_BRANCH_BUFFER 0
#endif
constexpr bool SHEATHE_ENABLED = MH_SHEATHE;
constexpr bool B_BRANCH_BUFFER_ENABLED = MH_B_BRANCH_BUFFER;
// Carve carve (prg.3, same pattern as MH_SHEATHE): the carcass interact is the
// stowed A verb, so a build that folds out the sheathe path (the at-budget
// parity image, MH_SHEATHE 0) also folds carve out. Shipping/host keep it.
#ifndef MH_CARVE
#define MH_CARVE 1
#endif
constexpr bool CARVE_ENABLED = MH_CARVE && SHEATHE_ENABLED;
// Roll-attack + direction+A opener carve (same pattern as MH_SHEATHE): prg.8
// turned the shipping default off (the direction+A opener and the A-out-of-
// evade roll attack are trimmed for the progression-wave budget). The carve
// stays: a build that passes -DMH_ROLL_ALT=1 re-enables both, and the host
// suite (tst/player_test.hpp) covers them with the flag forced on.
#ifndef MH_ROLL_ALT
#define MH_ROLL_ALT 0
#endif
constexpr bool ROLL_ALT_ENABLED = MH_ROLL_ALT;
// Stage-3 finisher carve (same pattern as MH_SHEATHE): prg.8 turned the
// shipping default off (the B-after-finisher stage-3 branch is trimmed for the
// progression-wave budget). The carve stays: -DMH_STAGE3=1 re-enables the
// finWin writes / idle stage-3 mapping / inLock extension, and the host suite
// covers the finisher path with the flag forced on.
#ifndef MH_STAGE3
#define MH_STAGE3 0
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
// Player-move push rule (bug fix 2026-09-19; re-enabled shipping 2026-09-24,
// monhun-ardu-ryh.1): the per-tick player-move flag makes pushApart resolve a
// body overlap on the mover's side, so a hunter pressing into a body (walk,
// dodge roll, shove) is pushed back and can never shove the beast. prg.11 had
// carved it out of the shipping build for the progression-wave budget, which
// silently restored the pre-fix give-way rule (owner bug: "roll pushes
// monster"). The default is the fix now (1); test_parity (MH_PUSH_MOVE 0) never
// walked the hunter into the beast either way.
#ifndef MH_PUSH_MOVE
#define MH_PUSH_MOVE 1
#endif
constexpr bool PUSH_MOVE_ENABLED = MH_PUSH_MOVE;
// Active-room bounds carve (bead monhun-ardu-fie.4, same pattern as MH_SHEATHE):
// the parity image is at the board flash limit, so its clamp/camera/cull
// expressions — and the whole room runtime — fold back to the legacy WORLD_W/H
// constants. Every parity fixture room extent equals WORLD_W/H and no scene
// calls loadRoom, so behavior stays byte-identical; shipping/perf keep the
// runtime active-room bounds.
#ifndef MH_ROOM_BOUNDS
#define MH_ROOM_BOUNDS 1
#endif
constexpr bool ROOM_BOUNDS_ENABLED = MH_ROOM_BOUNDS;
constexpr int16_t CHARGE_MIN = 14;   // A held this long past the swing -> charge stance
constexpr int16_t WORLD_W = 256;
constexpr int16_t WORLD_H = 112;

// hrd: projectiles / effects / training pole. Caps are device-sized ring
// buffers: the mock uses unbounded JS arrays, the device overrides the oldest
// entry when full (documented in src/core/projectiles.hpp).
constexpr int16_t MAX_PROJECTILES = 12;
constexpr int16_t MAX_EFFECTS = 12;
constexpr int16_t PROJ_LIFE = 90;   // ticks, mock fireShell()

// Hunt is the only shipped mode (prg.8 removed the training pole/mode). The
// enum + Game::mode stay so the menu/sim routing and the mode marker art index
// keep their shape; every value is MODE_HUNT.
enum Mode : int8_t {
    MODE_HUNT = 0
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
    PS_CHARGE,   // appended (ynb): existing 0..6 values must not move
    PS_GATHER,   // appended (feel.22): sheathed node gather, stationary
    PS_ITEM,     // appended (feel.22): sheathed herb use, stationary
    PS_CARVE,    // appended (prg.3): carcass carve, stationary on the over screen
    PS_DRAW      // appended (feel.24): rooted weapon draw windup (per-weapon)
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
        // 16-bit math on purpose: every Rect is screen/world pixel space
        // (|x|,|y| <= WORLD_W/H + a camera margin, w/h <= 128), so the sums stay
        // far inside int16 and AVR keeps them in register pairs. Promoting to
        // int32 here cost ~4x the code for the same answer.
        return x < static_cast<int16_t>(o.x + o.w) && static_cast<int16_t>(x + w) > o.x && y < static_cast<int16_t>(o.y + o.h) && static_cast<int16_t>(y + h) > o.y;
    }
};

// prototype circleRectOverlap: clamp circle center into rect, compare radius²
// 16-bit: the |d| > r early-out bounds both deltas by r (<= 24 at the only call
// site), so d*d and r*r stay tiny and no 32-bit multiply is emitted.
inline bool circleRectOverlap(int16_t cx, int16_t cy, int16_t r, const Rect &rect) {
    const int16_t nx = cx < rect.x ? rect.x : (cx > static_cast<int16_t>(rect.x + rect.w) ? static_cast<int16_t>(rect.x + rect.w) : cx);
    const int16_t ny = cy < rect.y ? rect.y : (cy > static_cast<int16_t>(rect.y + rect.h) ? static_cast<int16_t>(rect.y + rect.h) : cy);
    int16_t dx = static_cast<int16_t>(cx - nx);
    int16_t dy = static_cast<int16_t>(cy - ny);
    if (dx < 0)
        dx = static_cast<int16_t>(-dx);
    if (dy < 0)
        dy = static_cast<int16_t>(-dy);
    if (dx > r || dy > r)
        return false;
    return static_cast<int16_t>(dx * dx + dy * dy) <= static_cast<int16_t>(r * r);
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
    Attack charge[2];           // held-A release melee (ynb); charge-lite reads slot 0 only
    ShellDef chargeShells[2];   // held-A release shells (ynb); dead after the prg.11 carve
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
        14,
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
        12,
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
        7,
        {{5, 4, 11, 6, 11, 14, 12, 8, 0, 0, 0, false, ATK_NONE}, {5, 4, 11, 7, 11, 14, 12, 8, 0, 0, 0, false, ATK_NONE}, {7, 5, 15, 11, 13, 16, 14, 13, 0, 0, 0, false, ATK_NONE}},
        {6, 4, 16, 12, 44, 8, 6, 14, 0, 0, 0, false, ATK_NONE},   // arrowshot: hitscan special (reach 44)
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
// Held-A release data (ynb). Charge-lite (prg.11): only slot 0 is ever read —
// a single-level melee charge. A weapon "has charge data" when its slot-0 entry
// is non-zero; the level-1 dmg is the single read that distinguishes the shipped
// sets (flail charge dmg 24, all others 0). The gun's charge shells are dead
// data after the carve (the charged ball was removed), so no shell accessor.
inline const Attack *weaponCharge(const WeaponDef *d, int16_t i) {
    return &d->charge[i];
}
inline bool weaponHasCharge(const WeaponDef *d) {
    return mhFxReadI16(&d->charge[0].dmg) != 0;
}

// Draw windup per weapon (feel.24). Unsheathing is a rooted commitment that
// scales with the weapon's heft, so the weight is felt on the first press:
// sword 6, flail 10 (between), gun 16 (slowest). Indexed by WeaponId, so the
// ordering is W_SWORD < W_FLAIL < W_GUN.
constexpr uint8_t DRAW_TICKS[3] = {6, 10, 16};
inline uint8_t weaponDrawTicks(int8_t id) {
    return DRAW_TICKS[id];
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
MH_NOINLINE inline int16_t attackStam(const Attack *a) {
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
MH_NOINLINE inline int8_t shellPellets(const ShellDef *s) {
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

// One transient spark/muzzle effect (prg.8 removed the rising damage-number
// text variant; every effect is a spark, `crit` selects the bright plane).
struct Effect {
    int16_t x, y;
    uint8_t t, life;   // life <= 26, t ages to life
    bool crit;
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
    // Sheathe (feel.17): stowed flag + latch (suppresses roll/stance until B
    // release); combo debounce lock + B branch tap buffer.
    bool sheathed, sheatheLatch;
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
    // Double-tap d-pad dodge (feel.16). dTapDir = last press-edge dir8 (0xFF when
    // none), dTapT = DTAP_WIN countdown, pDir = dir8 sampled last tick (0xFF
    // idle). Appended last so existing fields/sizes do not move.
    int8_t dTapDir;
    uint8_t dTapT;
    int8_t pDir;
    // Gather bind (feel.22): global ZoneProp index of the node the running
    // PS_GATHER will deplete, ITEM_NODE_NONE when no gather is bound. Appended
    // last so existing fields/sizes do not move.
    uint8_t itemNode;
    // Move remainder (gun rework): 1/256 px fraction of dx*spd that movePlayer
    // carries across ticks, so a diagonal keeps the authored 11/16 instead of
    // truncating per tick (worst in guard strafe). Appended last.
    int8_t remX, remY;
    // S2 stow latch: true when the current A hold began on an armed press (not
    // the stowed draw), so draw-and-keep-holding never re-stows. Appended last.
    bool aStowOk;

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

// Full profile record mirror (18 fields, blob ABI order). Read whole at spawn
// and cached; the interpreter consumes the cache at decision time. zoneFlags
// carries the per-creature zone presence bits (build/zones-design.md). faceHold
// (nch.4) is the turn-commitment cadence: 0 recomputes facing every tick, >0
// refreshes it only every faceHold ticks. turnRate (feel.14) bounds how many
// DIR8 steps the refreshed facing may rotate toward the player (0 = legacy
// snap).
struct CombatProfile {
    uint8_t engageDist, keepDist, attackDist;
    uint8_t circleNum, circleDen, retreatNum, retreatDen;
    uint8_t staggerMax, staggerDecay, zoneFlags;
    uint8_t faceHold;
    uint8_t turnRate;
    uint16_t cdBase, cdJitter, spawnT, spawnCd, stunRecoverT, staggerRecoverT;
};

// Creature enrage phase (feel.6): one-shot escalation cached at spawn, applied
// when the creature's HP percent first crosses hpPct. hpPct 0 (the shipped
// default) keeps the runtime branch inert. fired is the one-shot latch; cue is
// stored for a future enrage audio path (no audio consumer in this bead).
struct CombatEnrage {
    uint8_t hpPct;
    uint8_t spdMul;
    uint8_t faceHold;
    uint8_t cue;
    uint8_t fired;   // one-shot latch
};

// Art descriptor (epic monhun-ardu-bih): the per-creature ART record cached at
// spawn. sheet is a 1-based index into the generated art_sheets.hpp table
// (0 = none -> the legacy per-kind draw path). anchorY offsets the art from the
// body-box top; stride is the west frame offset (0 = no mirror); the remaining
// slots are frame indices into the sheet. Field order is the packed ART ABI.
struct CombatArt {
    uint8_t sheet;
    int8_t anchorY;
    uint8_t stride, idle0, idleCount, windup, attack, recover, flash, dead;
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
// int16 throughout: |ox|,|oy| <= 128 (the pole head zone datum) and |fx|,|fy|
// <= 16, so each product is <= 2048 and the signed sum <= 4096 -- a 32-bit
// rotation only bought __mulhisi3 calls on every hit test and telegraph draw.
inline void combatFacePoint(int16_t fx, int16_t fy, int16_t ox, int16_t oy, int16_t &dx, int16_t &dy) {
    dx = static_cast<int16_t>(((fx * ox) - (fy * oy)) >> 4);
    dy = static_cast<int16_t>(((fy * ox) + (fx * oy)) >> 4);
}

inline void combatFaceOffset(int16_t fx, int16_t fy, const CombatBox &b, int16_t &dx, int16_t &dy) {
    combatFacePoint(fx, fy, b.ox, b.oy, dx, dy);
}

// Attack scalar cache + the currently loaded window. Read once at attack start
// (~16 FX reads), refreshed only when the interpreter switches windows;
// per-tick code consumes this cache and performs no cart reads.
struct CombatAttackCache {
    uint16_t windup, active, recover, dmg;
    uint8_t moveType, moveSpeedF;
    int8_t moveDx, moveDy;   // hop: face-relative velocity (1/16 px/tick, feel.7)
    uint8_t facing;
    uint8_t wallStun;   // ticks self-stunned on a room-bound clamp (feel.4)
    uint8_t tell;       // windup telegraph shape (feel.5)
    uint8_t artSheet;   // whole-body attack art sheet index (bih.4; 0 = generic body)
    uint8_t artFrame;   // 2-facing attack pose frame base (bih.4)
    uint8_t artMode;    // 0 normal 2-facing sheet, 1 locked-spin whole-body (bih.4)
    uint8_t winIdx;     // index of the cached window in the WINDOWS section
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
    uint16_t unlockMask;   // attacks disabled while this zone is broken (bit per global idx)
    uint8_t partSheet;     // 1-based art sheet index for the part overlay (bih.5; 0 = none)
};

// Combat runtime state: which creature was loaded, the implicit body box (the
// creature w/h at (0,0)), the body-collision box (authored `collide` box, or
// the body box when absent), the two optional zone slots (slot 0 head, slot 1
// appendage; headZone/appendZone hold the global ZONES index or COMBAT_NO_ZONE)
// and the single broken bit per zone. The pattern step cursor
// (stepIdx + 256-tick countdown stepT) and stagger meter are unchanged.
struct CombatState {
    CombatProfile profile;                     // 24 B AVR
    CombatAttackCache attack;                  // 21 B AVR
    CombatBox body;                            // 4 B AVR
    CombatBox collide;                         // 4 B AVR: body-collision rect
    CombatZoneCache zone[COMBAT_ZONE_SLOTS];   // 26 B AVR
    uint8_t headZone;                          // global ZONE index or COMBAT_NO_ZONE
    uint8_t appendZone;
    uint8_t creature;     // index into CREATURES
    uint8_t zoneBroken;   // bit0 head, bit1 appendage
    uint8_t isStatic;     // creature record flags bit0: static prop (pole)
    uint8_t patternIdx;
    uint8_t stepIdx;
    uint8_t stepT;         // 8-bit countdown: step `after`/WAIT ticks cap at 255
    uint8_t stagger;       // stagger meter accumulator (profile.staggerMax = 0 -> unused)
    CombatEnrage enrage;   // 5 B AVR: one-shot HP-threshold escalation cache
    CombatArt art;         // 10 B AVR: art descriptor read once at spawn (bih)
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

// Monster roster (bead monhun-ardu-6zb.1): the demo beast variants plus the
// static training pole (bih.2). Index 0 (LUNGE) is the legacy parity default:
// its def reproduces the values initMonster() used to hardcode. atkDist is the
// lunge/sweep split distance; a negative value means "never lunge" (SWEEP always
// sweeps).
enum MonsterKind : int8_t {
    MON_LUNGE = 0,
    MON_SWEEP = 1,
    MON_HEAVY = 2,
    MON_RAVAGER = 3,   // ljj.6: first breakable-part creature (data/creatures/ravager.json)
    MON_POLE = 4       // bih.2: the static training post (data/creatures/pole.json), spd 0 / no attacks
};
struct MonsterDef {
    int8_t kind;
    int16_t w, h, hp, spd, atkDist;
};

// On AVR the roster lives on the FX cart as one packed 55 B blob (bead
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
MH_PROGMEM const MonsterDef MONSTER_DEFS[5] = {
    {MON_LUNGE, 32, 24, 200, 5, 32},   {MON_SWEEP, 28, 22, 150, 7, -1}, {MON_HEAVY, 40, 28, 320, 3, 24},
    {MON_RAVAGER, 32, 24, 260, 6, 24}, {MON_POLE, 20, 36, 300, 0, -1},   // bih.2: still, no attacks (atkDist -1 = never lunge)
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
    // Camera top-left in world px. int16 (not uint8) so a room wider than
    // 256 px can scroll: CAM_MAX_X for a 384 px room is 256, which overflows a
    // byte. roomW/roomH are the active-room extents that bound the camera,
    // clamps and projectile cull; they default to the legacy WORLD_W/H so every
    // existing (non-room) scene is byte-identical.
    int16_t camX, camY;
    int16_t roomW, roomH;
    // Room runtime (bead monhun-ardu-fie.4). Cached from the active room record
    // at loadRoom: per-tick door/heal checks read only these scalars (plus the
    // door/heal rects off the blob on demand), never the room record again.
    // roomMonsterKind == zone::MONSTER_NONE marks a safe room (no monster/target
    // updates); default 0 keeps every pre-room scene a live hunt.
    uint8_t roomId;
    uint8_t roomMonsterKind;
    uint16_t roomFirstDoor;
    uint8_t roomDoorCount;
    uint16_t roomFirstHeal;
    uint8_t roomHealCount;
    // Prop range (bead monhun-ardu-fie.5): the render draws the active room's
    // prop records over the room-image blit, so the range is cached like the
    // door/heal ranges (the 9 B records are read off the blob on demand).
    uint16_t roomFirstProp;
    uint8_t roomPropCount;
    // (The prg.7 camp-smithy range cache is gone: the SMITH screen that consumed
    // it was removed by ui.3.1, and ui.4 put FORGE on the hub. monhun-ardu-5co.8.)
    bool doorLatch;     // suppress doors until the player leaves every door rect
    bool menuRequest;   // menu door / hold-B sheathed in camp: app layer consumes
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
    CombatState combat;   // combat loader caches (ljj.2, 50 B AVR)
    // Quest kill accounting (bead monhun-ardu-me6): the sketch sets questTarget
    // from the active quest's def at hunt start (-1 = none), restores progress
    // from the save, and every monster death of that kind bumps questProgress.
    // The hunt-end commit writes questProgress back to the save. Not part of the
    // deterministic sim hash: a default Game (questTarget -1) never counts.
    // Quest goal accounting (bead monhun-ardu-dlp.1, record v2): goalKind is
    // quests::GOAL_KILL / GOAL_GATHER for the active quest (-1 = none). The
    // sketch arms it from the active QuestDef at hunt start; no accounting hook
    // reads it yet (dlp.2 wires the gather/kill split). Like questTarget it is
    // not part of the deterministic sim hash: a default Game (-1) never counts.
    int8_t questGoalKind;
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
    // Room-transition wipe (bead monhun-ardu-fie.5): loadRoom arms it and
    // stepGame decays it, so the render can black-wipe the arena for ~4 ticks
    // after a door cross without any new cart traffic. 0 = no transition.
    uint8_t fade;
    // Items + gather nodes (beads monhun-ardu-feel.22 + prg.2). items[] is the
    // per-hunt inventory indexed by the generated item ids (item::ITEM_*), reset
    // in newGame. gatherMask is one bit per global prop record: set once that
    // gather node has been picked this hunt (reset in newGame only -- loadRoom
    // never touches it, so a node stays depleted across a room round-trip).
    // Appended last so a default Game keeps every existing field offset.
    uint8_t items[ITEM_COUNT];
    uint16_t gatherMask;
    // Carve (bead monhun-ardu-prg.3): carvesDone is the successful-carve count
    // this hunt (cap CARVE_MAX; newGame resets it). carveHold mirrors a live
    // PS_CARVE so the over-screen app layer knows the A press belongs to the
    // carcass, not the return-to-menu edge. Appended last so every existing
    // field offset holds.
    uint8_t carvesDone;
    bool carveHold;
    // Armor engine cache (bead monhun-ardu-arm.2): resolved from the save's
    // equipped pieces at hunt start / equip change (src/armor.hpp). armorHead is
    // the equipped head piece + 1 for the render slot loop (0 = base head), and
    // armorFx is the magnitude cache the combat path reads (arm.3). Appended
    // last so every existing field offset holds.
    ArmorAgg armor;
    uint8_t armorHead;
    ArmorEffects armorFx;
};

// Active-room extents for the bound expressions. With ROOM_BOUNDS_ENABLED
// carved out (parity image) these fold to the legacy WORLD_W/H constants, so
// the clamps/camera/cull drop back to the exact pre-room code and flash.
static inline int16_t roomBoundW(const Game &g) {
    return ROOM_BOUNDS_ENABLED ? g.roomW : WORLD_W;
}
static inline int16_t roomBoundH(const Game &g) {
    return ROOM_BOUNDS_ENABLED ? g.roomH : WORLD_H;
}

}   // namespace mh