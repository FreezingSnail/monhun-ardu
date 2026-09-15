#pragma once
// Fixed-point math core, ported from mock/game.js (source of truth).
// All gameplay math is integer / fixed point so host tests and device
// share identical behaviour: positions are int pixels, sub-pixel
// remainders live in 1/16 units, velocities are 1/16 px per tick.
// No float, no <math.h>, no Arduino.h.

#include <stdint.h>

namespace fp {

const int16_t FP = 16; // 1 px = 16 fixed units

// 8-way unit vectors, 16 == full pixel
struct Dir8 { int16_t x, y; };

const Dir8 DIR8[8] = {
  {  16,   0 }, // E
  {  11,  11 }, // SE
  {   0,  16 }, // S
  { -11,  11 }, // SW
  { -16,   0 }, // W
  { -11, -11 }, // NW
  {   0, -16 }, // N
  {  11, -11 }, // NE
};

// truncating fixed divide, rounds toward zero (hardware friendly).
// C++ integer division already truncates toward zero; kept as a named
// function to mirror the prototype and forbid >> on negative values.
inline int16_t tdiv(int32_t a, int32_t b) {
  return static_cast<int16_t>(a / b);
}

// input (mx,my) is -1/0/1 per axis; returns DIR8 index or -1 when idle
inline int8_t dirIndexFromInput(int16_t mx, int16_t my) {
  if (mx > 0) return my < 0 ? 7 : my > 0 ? 1 : 0;
  if (mx < 0) return my < 0 ? 5 : my > 0 ? 3 : 4;
  if (my < 0) return 6;
  if (my > 0) return 2;
  return -1;
}

// map a movement delta to the nearest DIR8 index (for facing / knockback)
inline int8_t dirIndexFromDelta(int32_t dx, int32_t dy) {
  const int32_t adx = dx < 0 ? -dx : dx;
  const int32_t ady = dy < 0 ? -dy : dy;
  if (adx > ady * 2) return dx < 0 ? 4 : 0;
  if (ady > adx * 2) return dy < 0 ? 6 : 2;
  if (dx >= 0) return dy < 0 ? 7 : 1;
  return dy < 0 ? 5 : 3;
}

// integer sqrt (bit method)
// Seed is 1<<14 (a power of 4) so the >>2 chain stays on powers of 4.
// NOTE: mock/game.js seeds with 1<<15 (2^15, odd exponent); its >>2 chain
// hits 8 and 2, which are not powers of 4, so the mock returns wrong results
// for small inputs (isqrt(1)=0, isqrt(9)=4). Fixed here; the mock only used
// isqrt for mid-range distance thresholds where the error was latent.
inline int16_t isqrt(int32_t n) {
  int32_t r = 0;
  int32_t bit = 1 << 14;
  while (bit > n) bit >>= 2;
  while (bit) {
    if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
    else r >>= 1;
    bit >>= 2;
  }
  return static_cast<int16_t>(r);
}

// fixed velocity move (1/16 px per tick)
struct FpBody {
  int16_t x, y;
  int16_t subX, subY; // 1/16 px remainder
};

inline void addVel(FpBody& o, int16_t vx, int16_t vy) {
  o.subX += vx;
  o.subY += vy;
  o.x += tdiv(o.subX, FP);
  o.y += tdiv(o.subY, FP);
  o.subX %= FP;
  o.subY %= FP;
}

// accumulate fixed sub-pixel movement, keep x/y int pixels.
// signed % 16 (NOT & 15): mask bug caused up/left stutter in the prototype.
inline void addMove(FpBody& o, int16_t dx, int16_t dy, int16_t spd) {
  o.subX += tdiv(dx * spd, FP);
  o.subY += tdiv(dy * spd, FP);
  o.x += tdiv(o.subX, FP);
  o.y += tdiv(o.subY, FP);
  o.subX %= FP;
  o.subY %= FP;
}

// rotate a 1/16 unit vector by an integer cos/sin table (16 = 1.0).
// Matches prototype: arithmetic shift (floor) on the fixed result.
inline Dir8 rotFp(int16_t x, int16_t y, int16_t cosv, int16_t sinv) {
  Dir8 d;
  d.x = static_cast<int16_t>((x * cosv - y * sinv) >> 4);
  d.y = static_cast<int16_t>((x * sinv + y * cosv) >> 4);
  return d;
}

// stamina is int with a 1/16 accumulator; returns false when empty
struct FpStam {
  int16_t stam;
  int16_t stamSub;
};

inline bool drainStam(FpStam& p, int16_t amount) {
  p.stamSub += amount;
  while (p.stamSub >= FP) { p.stamSub -= FP; p.stam--; }
  return p.stam > 0;
}

} // namespace fp