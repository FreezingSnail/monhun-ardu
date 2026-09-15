#pragma once
// Shared game structs + weapon tables, ported from mock/game.js (source of truth).
// No float, no Arduino.h — ints and fp.hpp only. Header-only; host tests and
// device share the exact same tables and constants.
//
// WEAPON_DEFS is a byte-for-byte port of mock/game.js WEAPON_DEFS. Do not
// retune numbers here.

#include <stdint.h>
#include "fp.hpp"

namespace mh {

constexpr int16_t HOLD_TICKS = 11; // B held this long -> stance (~180ms)
constexpr int16_t CHAIN_WIN  = 14; // chain follow-up window after a combo hit
constexpr int16_t A_BUFFER   = 10; // attack input buffer in ticks
constexpr int16_t WORLD_W    = 256;
constexpr int16_t WORLD_H    = 112;

enum WeaponId : int8_t { W_SWORD = 0, W_FLAIL = 1, W_GUN = 2 };
enum PState : int8_t {
  PS_IDLE = 0, PS_ATTACK, PS_SPECIAL, PS_DODGE, PS_DEFLECT, PS_SHOVE, PS_STUN
};
enum Stance : int8_t { ST_NONE = 0, ST_PARRY, ST_WHIRL, ST_GUARD };
enum AtkId : int8_t {
  ATK_NONE = 0, ATK_STEPSLASH, ATK_SPINCUT, ATK_TRIP, ATK_POINTBLANK, ATK_GUARDBASH
};

struct Rect {
  int16_t x, y, w, h;
  bool overlaps(const Rect& o) const {
    const int32_t ax = x, aw = w, ox = o.x, ow = o.w;
    const int32_t ay = y, ah = h, oy = o.y, oh = o.h;
    return ax < ox + ow && ax + aw > ox && ay < oy + oh && ay + ah > oy;
  }
};

// prototype circleRectOverlap: clamp circle center into rect, compare radius²
inline bool circleRectOverlap(int16_t cx, int16_t cy, int16_t r, const Rect& rect) {
  const int32_t nx = cx < rect.x ? rect.x : (cx > rect.x + rect.w ? rect.x + rect.w : cx);
  const int32_t ny = cy < rect.y ? rect.y : (cy > rect.y + rect.h ? rect.y + rect.h : cy);
  const int32_t dx = cx - nx;
  const int32_t dy = cy - ny;
  return dx * dx + dy * dy <= static_cast<int32_t>(r) * r;
}

struct Attack {
  int16_t startup, active, recover, dmg, reach, hw, hh, stam;
  int16_t lunge;  // branch lunge velocity, 0 = none
  int16_t push;   // branch knockback push distance, 0 = none
  int8_t  effect; // 0 none, 1 trip
  bool    shell;  // consumes a ball shell
  int8_t  id;     // AtkId (ATK_NONE for plain combo attacks)
};

struct Branch {
  int8_t  stage;
  int8_t  stance; // ST_WHIRL for stance branches, ST_NONE for attack branches
  int16_t autoT;  // stanceAuto ticks when entering a stance branch
  Attack  atk;
};

struct ShellDef {
  int16_t count, dmg, speedF, w, h, reload, stam;
  int8_t  pellets; // 1 = ball (heavy), 3 = scatter
};

struct WeaponDef {
  int8_t  id;
  int16_t spd; // 1/16 px per tick
  Attack  attacks[3];
  Attack  special;
  Branch  branches[2];
  bool    canCancel; // may tap-B out of an attack into dodge
  ShellDef shells[2];
};

constexpr WeaponDef WEAPON_DEFS[3] = {
  // sword: fast taps, dodge (i-frames), parry stance + riposte special
  { W_SWORD, 18,
    { { 3, 5, 8, 9, 13, 12, 10, 9, 0, 0, 0, false, ATK_NONE },
      { 3, 5, 8, 10, 13, 12, 10, 9, 0, 0, 0, false, ATK_NONE },
      { 5, 6, 14, 17, 16, 18, 14, 15, 0, 0, 0, false, ATK_NONE } },
    { 4, 6, 16, 24, 18, 20, 16, 20, 0, 0, 0, false, ATK_NONE },
    { { 1, ST_NONE, 0, { 3, 5, 12, 12, 18, 14, 12, 10, 42, 0, 0, false, ATK_STEPSLASH } },
      { 2, ST_NONE, 0, { 5, 7, 15, 20, 12, 28, 26, 16, 0, 0, 0, false, ATK_SPINCUT } } },
    true,
    { { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0 } },
  },
  // flail: slow momentum chain, deflect step, whirl stance + ball throw
  { W_FLAIL, 15,
    { { 8, 6, 9, 14, 19, 20, 16, 13, 0, 0, 0, false, ATK_NONE },
      { 6, 6, 9, 17, 21, 22, 16, 12, 0, 0, 0, false, ATK_NONE },
      { 5, 7, 15, 25, 24, 24, 20, 17, 0, 0, 0, false, ATK_NONE } },
    { 4, 8, 14, 27, 32, 14, 18, 22, 0, 0, 0, false, ATK_NONE },
    { { 1, ST_WHIRL, 50, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, ATK_NONE } },
      { 2, ST_NONE, 0, { 5, 6, 16, 12, 22, 22, 14, 14, 0, 0, 1, false, ATK_TRIP } } },
    false,
    { { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0 } },
  },
  // gunshield: slow walk, shove, guard stance + gun (ball / scatter)
  { W_GUN, 9,
    { { 5, 4, 11, 6, 11, 14, 12, 8, 0, 0, 0, false, ATK_NONE },
      { 5, 4, 11, 7, 11, 14, 12, 8, 0, 0, 0, false, ATK_NONE },
      { 7, 5, 15, 11, 13, 16, 14, 13, 0, 0, 0, false, ATK_NONE } },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, ATK_NONE }, // gunshield fires shells, no melee special
    { { 1, ST_NONE, 0, { 4, 5, 16, 22, 15, 18, 16, 6, 0, 0, 0, true, ATK_POINTBLANK } },
      { 2, ST_NONE, 0, { 4, 4, 12, 9, 14, 16, 14, 8, 0, 12, 0, false, ATK_GUARDBASH } } },
    true,
    { { 2, 28, 35, 7, 6, 70, 6, 1 },
      { 5, 7, 42, 4, 4, 30, 5, 3 } },
  },
};

struct Input {
  int16_t mx, my; // -1/0/1 per axis
  bool a, b;
};

struct Game;

// Hurt-box target + hit-resolution indirection. zq5 (monster) and hrd (pole)
// plug their damage/knockback/trip handlers in here; this bead only resolves
// the overlap and forwards the exact mock numbers.
struct Target {
  Rect rect; // hurt box (monster body or training pole)
  bool alive;
  void (*onHit)(Game&, int dmg, int hx, int hy, int push, int effect);
  void (*onShove)(Game&, int dirX, int dirY, int amount, int freeze);
  void (*onStun)(Game&, int ticks); // deflect / parry response
};

// Player inherits the fp bodies so addMove/addVel/drainStam work directly on
// it (p.x/p.subX and p.stam/p.stamSub are the fp-owned fields).
struct Player : fp::FpBody, fp::FpStam {
  int16_t w, h;        // hurt box size (px)
  int16_t vx, vy;      // 1/16 px per tick (dodge/deflect/lunge/knockback)
  int16_t fx, fy;      // 1/16 unit facing vector
  int16_t hp, hpMax;
  int16_t stamMax;
  PState  state;
  int16_t t;
  const Attack* atk;
  bool    hitDone;
  int16_t chain, chainWin, aBuffer;
  Stance  stance;
  int16_t stanceT, stanceAuto, whirlTick;
  int16_t throwCd, riposteT;
  int16_t bHeld;
  bool    bReady, bLocked;
  int16_t iT;
  int8_t  shell; // 0 ball, 1 scatter
  int16_t reload;
  int16_t shells[2];

  void init(int8_t weapon);
};

// Monster attack table — byte-for-byte port of mock/game.js MONSTER_ATTACKS.
// speedF only applies to the lunge; sweep is stationary.
enum MKind : int8_t { MK_LUNGE = 0, MK_SWEEP = 1 };
struct MonsterAttack {
  int8_t  kind;
  int16_t windup, active, recover, speedF, dmg, reach, hw, hh;
};

constexpr MonsterAttack MONSTER_ATTACKS[2] = {
  { MK_LUNGE, 40, 10, 55, 34, 12, 12, 24, 22 },
  { MK_SWEEP, 48, 12, 60,  0,  9, 17, 32, 24 },
};

enum MState : int8_t { MS_IDLE = 0, MS_PURSUE, MS_WINDUP, MS_ATTACK, MS_RECOVER, MS_DEAD };
enum Over : int8_t { OVER_NONE = 0, OVER_WIN, OVER_LOSE };

// FSM fields mirror the mock monster object; face is a fixed 1/16 unit vector
// (never a normalized float).
struct Monster : fp::FpBody {
  int16_t w, h;      // hurt box size (px)
  int16_t hp, hpMax;
  MState  state;
  int16_t t, cd;
  int16_t fx, fy;    // 1/16 unit facing vector
  const MonsterAttack* atk;
  int16_t lvx, lvy;  // lunge velocity (1/16 px per tick)
  int16_t windupMax, hitFlash, stun, circleDir, spd;
};

struct Game {
  int16_t tick, freeze;
  int8_t  weapon;
  int8_t  over;       // Over: 0 none, 1 win, 2 lose
  bool    prevA, prevB;
  Player  player;
  Monster monster;
  Target  target;
  int8_t  lastShot;   // gunshield: 0 none, 1 ball, 2 scatter (hrd consumes)
  int16_t lastShotX, lastShotY;
};

} // namespace mh