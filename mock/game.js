'use strict';

/*
 * MonHun Arduboy mock — 128x64 four-shade block playtest.
 *
 * Goal: test control grammar and fight feel, not art.
 *   Arrows = 8-dir move.
 *   Z = A: attack. In stance = stance special.
 *   X = B: tap = defense, hold = stance. Release speed picks tap vs hold.
 *   1/2/3 = sword / flail / gunshield (restarts hunt). Q = swap shell.
 *   H = wireframe: red = hurt box, blue = hit box. T = slow motion, P = pause, R = reset.
 *   K = train area with pole dummy + damage numbers + DPS readout.
 *   [ / ] = cycle the sheathe-input prototype (default DOWN,DOWN then A+B).
 *   While stowed the hunter runs at SHEATHE_SPD and A draws into combo hit 1.
 *   , / . = cycle the combo-debounce profile (browser default HEAVY; HUD
 *   shows the active lock and countdown).
 *
 * Hardware note: all gameplay math is integer / fixed point, matching what
 * the Arduboy can do. Positions are int pixels, sub-pixel remainders live in
 * 1/16 units, velocities are 1/16 px per tick, facing is a 1/16 unit vector,
 * stamina is int with a 1/16 accumulator. No float in sim (render may use
 * precomputed tables / trig for looks only).
 *
 * Weapons:
 *   SWORD     fast taps, dodge (i-frames), parry stance + riposte special.
 *   FLAIL     slow momentum chain, deflect step, whirl stance + ball throw.
 *   GUNSHIELD slow walk, shove, guard stance + gun (ball / scatter).
 *
 * Monster: one medium blocky quadruped, lunge + sweep, windup tell,
 * recovery window. Head side of facing takes bonus damage.
 */

const W = 128;
const H = 64;
const HUD_H = 8;
const ARENA_H = H - HUD_H;
const WORLD_W = 256;
const WORLD_H = 112;
const TICK_MS = 1000 / 60;
const HOLD_TICKS = 11; // B held this long -> stance (~180ms)

/* ---------------------------------------------- sheathe input prototype
 * The stow combo is playtest-configurable: press [ / ] in the browser to
 * cycle variants and read the active one in the top-right HUD. Grammar
 * candidates (d-pad tap sequence + A/B):
 *   ab    A+B chord (3t grace). NOTE: collides with stance+A special (A while
 *         B held), so it stows before the stance special can fire.
 *   dab   tap Down, then A+B chord within SHEATHE_SEQ_WIN (the requested idea)
 *   ddab  double-tap Down, then A+B chord
 *   db    tap Down, then tap B
 *   dbh   hold Down + hold B to the stance threshold (replaces stance-south)
 *   dabhold  hold Down + A + B together for SHEATHE_HOLD_TICKS; the A press is
 *         deferred by CHORD_WIN so B can join (down+A still attacks otherwise)
 * A combo is only evaluated while the weapon is drawn; when stowed, A is the
 * draw attack and the combo does nothing.
 */
const SHEATHE_SEQ_WIN = 18; // d-pad tap -> chord window (~300ms)
const CHORD_WIN = 3;        // A/B chord grace (~50ms)
const SHEATHE_HOLD_TICKS = 8; // Down+A+B must be held this long (~130ms)
const SHEATHE_SPD = 24;     // 1/16 px per tick while stowed (1.5 px/t run)
const B_BUFFER = 36;        // B branch tap buffer: bridges recovery + debounce lock
const A_BUFFER = 10;        // attack input buffer, original (OFF profile)
const A_BUFFER_DEBOUNCE = 16; // wider buffer under debounce (covers the gap locks)
const CHARGE_MIN = 14;      // A held this long past the swing -> charge stance
const CHARGE_L2 = 20;       // extra charge ticks for level 2 (flash on the bar)
const SHEATHE_VARIANTS = ['ab', 'dab', 'ddab', 'db', 'dbh', 'dabhold'];
let sheatheVariant = 2;     // default ddab (double-tap down, then A+B)

function getSheatheVariant() {
  return SHEATHE_VARIANTS[sheatheVariant];
}

function setSheatheVariant(v) {
  if (typeof v === 'number' && isFinite(v)) {
    const n = SHEATHE_VARIANTS.length;
    sheatheVariant = ((v | 0) % n + n) % n;
  } else {
    const i = SHEATHE_VARIANTS.indexOf(v);
    if (i >= 0) sheatheVariant = i;
  }
  return SHEATHE_VARIANTS[sheatheVariant];
}

/* --------------------------------------------- combo debounce prototype
 * Recovery after a combo: a completed combo attack locks the next A for
 * `gap` ticks, the finisher (3rd hit) for `fin` ticks. The chain window only
 * opens when the lock ends, so one press made too early is dropped and mash
 * gets a forced beat. Press , / . to cycle profiles; HUD shows the lock.
 * Module default is OFF so the parity fixtures stay byte-identical until the
 * tuned profile is ported; boot() arms HEAVY for the browser playtest.
 */
const DEBOUNCE_PROFILES = [
  { name: 'OFF', gap: 0, fin: 0 },
  { name: 'LIGHT', gap: 3, fin: 12 },
  { name: 'MED', gap: 6, fin: 18 },
  { name: 'HEAVY', gap: 9, fin: 24 },
  { name: 'BRUTAL', gap: 14, fin: 36 },
];
let debounceProfile = 0;

function getDebounceProfile() {
  return DEBOUNCE_PROFILES[debounceProfile];
}

function setDebounceProfile(v) {
  if (typeof v === 'number' && isFinite(v)) {
    const n = DEBOUNCE_PROFILES.length;
    debounceProfile = ((v | 0) % n + n) % n;
  } else if (typeof v === 'string') {
    const i = DEBOUNCE_PROFILES.findIndex((p) => p.name === v.toUpperCase());
    if (i >= 0) debounceProfile = i;
  }
  return DEBOUNCE_PROFILES[debounceProfile];
}

// Attack input buffer: the original 10 ticks when OFF, wider under debounce so
// one loose press still survives the gap lock (BRUTAL gap is 14).
function aBufferTicks() {
  return getDebounceProfile().gap > 0 ? A_BUFFER_DEBOUNCE : A_BUFFER;
}

/* ------------------------------------------------- fixed point constants */

const FP = 16; // 1 px = 16 fixed units

// 8-way unit vectors, 16 == full pixel
const DIR8 = [
  { x: 16, y: 0 },    // E
  { x: 11, y: 11 },   // SE
  { x: 0, y: 16 },    // S
  { x: -11, y: 11 },  // SW
  { x: -16, y: 0 },   // W
  { x: -11, y: -11 }, // NW
  { x: 0, y: -16 },   // N
  { x: 11, y: -11 },  // NE
];

function dirIndexFromInput(mx, my) {
  if (mx > 0) return my < 0 ? 7 : my > 0 ? 1 : 0;
  if (mx < 0) return my < 0 ? 5 : my > 0 ? 3 : 4;
  if (my < 0) return 6;
  if (my > 0) return 2;
  return -1;
}

function dirIndexFromDelta(dx, dy) {
  const adx = Math.abs(dx);
  const ady = Math.abs(dy);
  if (adx > ady * 2) return dx < 0 ? 4 : 0;
  if (ady > adx * 2) return dy < 0 ? 6 : 2;
  if (dx >= 0) return dy < 0 ? 7 : 1;
  return dy < 0 ? 5 : 3;
}

// integer sqrt (bit method)
function isqrt(n) {
  let r = 0;
  let bit = 1 << 14; // seed must be an even power of 4, else small n break
  while (bit > n) bit >>= 2;
  while (bit) {
    if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
    else r >>= 1;
    bit >>= 2;
  }
  return r;
}

// fixed velocity move (1/16 px per tick)
function addVel(o, vx, vy) {
  o.subX += vx;
  o.subY += vy;
  o.x += tdiv(o.subX, 16);
  o.y += tdiv(o.subY, 16);
  o.subX %= 16;
  o.subY %= 16;
}

// truncating fixed multiply, rounds toward zero (hardware friendly)
function tdiv(a, b) {
  return ((a / b) | 0);
}

// accumulate fixed sub-pixel movement, keep x/y int pixels
function addMove(o, dx, dy, spd) {
  o.subX += tdiv(dx * spd, 16);
  o.subY += tdiv(dy * spd, 16);
  o.x += tdiv(o.subX, 16);
  o.y += tdiv(o.subY, 16);
  o.subX %= 16; // signed remainder: keeps up/left speed same as down/right
  o.subY %= 16;
}

// rotate a 1/16 unit vector by an integer cos/sin table (16 = 1.0)
function rotFp(x, y, cosv, sinv) {
  return { x: (x * cosv - y * sinv) >> 4, y: (x * sinv + y * cosv) >> 4 };
}

// Frame into the 8-frame rotating longtail spin sheet (bead monhun-ardu-nch.3),
// mirroring the C++ mh::spinSheetFrame (src/render_math.hpp): frame 0 is the
// east silhouette, each 45-deg clockwise step advances the frame, and the spin
// completes one revolution over the attack's active window. Integer only.
function spinSheetFrame(start8, tick, active) {
  if (!(tick > 0) || !(active > 0)) return start8 & 7;
  return (start8 + Math.floor((tick * 8) / active)) & 7;
}

// Body-sheet rotation frame for the heavy's locked tail_spin (beads nch.3/5),
// mirroring src/render.hpp drawMonster: MS_ATTACK spins the 8-frame sheet from
// the locked facing each active-window slice; MS_WINDUP holds the locked away
// frame (start8) so the beast looks away with its tail at the hunter for all 8
// directions. Returns -1 when the draw is not a heavy locked spin (no rotation).
function monsterSpinFrame(m) {
  if (!m || m.kind !== 'heavy' || !m.atk || m.atk.kind !== 'tailSpin') return -1;
  if (m.state === 'windup') return dirIndexFromDelta(m.face.x, m.face.y);
  if (m.state === 'attack') return spinSheetFrame(dirIndexFromDelta(m.face.x, m.face.y), m.t, m.atk.active);
  return -1;
}

// int stamina in 1/16 units; returns false when empty
function drainStam(p, amount) {
  p.stamSub += amount;
  while (p.stamSub >= 16) { p.stamSub -= 16; p.stam--; }
  return p.stam > 0;
}

const SHADES = ['#000000', '#4d4d4d', '#b3b3b3', '#ffffff'];

const WEAPON_DEFS = [
  {
    id: 'sword',
    name: 'SWORD',
    spd: 18, // 1/16 px per tick
    attacks: [
      { startup: 3, active: 5, recover: 8, dmg: 9, reach: 13, hw: 12, hh: 10, stam: 9 },
      { startup: 3, active: 5, recover: 8, dmg: 10, reach: 13, hw: 12, hh: 10, stam: 9 },
      { startup: 5, active: 6, recover: 14, dmg: 17, reach: 16, hw: 18, hh: 14, stam: 15 },
    ],
    special: { startup: 4, active: 6, recover: 16, dmg: 24, reach: 18, hw: 20, hh: 16, stam: 20 },
    // roll attack: A out of a dodge (tier-1 move-set expansion)
    roll: { id: 'rollslash', startup: 4, active: 5, recover: 10, dmg: 12, reach: 15, hw: 16, hh: 14, stam: 10 },
    // direction + A: forward thrust opener (combo hit 1 replacement)
    alt: { id: 'thrust', startup: 6, active: 4, recover: 12, dmg: 14, reach: 22, hw: 10, hh: 10, stam: 12, lunge: 20 },
    branches: [
      { stage: 1, atk: { id: 'stepslash', startup: 3, active: 5, recover: 12, dmg: 12, reach: 18, hw: 14, hh: 12, stam: 10, lunge: 42 } },
      { stage: 2, atk: { id: 'spincut', startup: 5, active: 7, recover: 15, dmg: 20, reach: 12, hw: 28, hh: 26, stam: 16 } },
      { stage: 3, atk: { id: 'helmsplit', startup: 8, active: 4, recover: 20, dmg: 26, reach: 16, hw: 20, hh: 22, stam: 18 } },
    ],
    canCancel: true,
  },
  {
    id: 'flail',
    name: 'FLAIL',
    spd: 15, // 1/16 px per tick
    attacks: [
      { startup: 8, active: 6, recover: 9, dmg: 14, reach: 19, hw: 20, hh: 16, stam: 13 },
      { startup: 6, active: 6, recover: 9, dmg: 17, reach: 21, hw: 22, hh: 16, stam: 12 },
      { startup: 5, active: 7, recover: 15, dmg: 25, reach: 24, hw: 24, hh: 20, stam: 17 },
    ],
    special: { startup: 4, active: 8, recover: 14, dmg: 27, reach: 32, hw: 14, hh: 18, stam: 22 },
    roll: { id: 'rollsweep', startup: 4, active: 6, recover: 13, dmg: 15, reach: 20, hw: 24, hh: 16, stam: 10 },
    alt: { id: 'widesweep', startup: 6, active: 6, recover: 14, dmg: 18, reach: 22, hw: 30, hh: 14, stam: 14 },
    // held A past the swing -> power swing; level 2 trips the beast
    charge: [
      { id: 'chargeslam1', startup: 4, active: 6, recover: 14, dmg: 24, reach: 26, hw: 28, hh: 18, stam: 14 },
      { id: 'chargeslam2', startup: 5, active: 8, recover: 20, dmg: 36, reach: 28, hw: 34, hh: 24, stam: 22, effect: 'trip' },
    ],
    branches: [
      { stage: 1, stance: 'whirl', auto: 50 },
      { stage: 2, atk: { id: 'trip', startup: 5, active: 6, recover: 16, dmg: 12, reach: 22, hw: 22, hh: 14, stam: 14, effect: 'trip' } },
      { stage: 3, atk: { id: 'earthslam', startup: 10, active: 6, recover: 24, dmg: 32, reach: 24, hw: 32, hh: 24, stam: 24, effect: 'trip', push: 12 } },
    ],
    canCancel: false,
  },
  {
    id: 'gunshield',
    name: 'GUN',
    spd: 9, // 1/16 px per tick
    attacks: [
      { startup: 5, active: 4, recover: 11, dmg: 6, reach: 11, hw: 14, hh: 12, stam: 8 },
      { startup: 5, active: 4, recover: 11, dmg: 7, reach: 11, hw: 14, hh: 12, stam: 8 },
      { startup: 7, active: 5, recover: 15, dmg: 11, reach: 13, hw: 16, hh: 14, stam: 13 },
    ],
    branches: [
      { stage: 1, atk: { id: 'pointblank', startup: 4, active: 5, recover: 16, dmg: 22, reach: 15, hw: 18, hh: 16, stam: 6, shell: true } },
      { stage: 2, atk: { id: 'guardbash', startup: 4, active: 4, recover: 12, dmg: 9, reach: 14, hw: 16, hh: 14, stam: 8, push: 12 } },
      { stage: 3, atk: { id: 'cannonblast', startup: 6, active: 3, recover: 20, dmg: 30, reach: 16, hw: 24, hh: 18, stam: 16, push: 16 } },
    ],
    canCancel: true,
    shells: {
      ball: { name: 'BALL', count: 2, dmg: 28, speedF: 35, w: 7, h: 6, reload: 70, stam: 6 },
      scatter: { name: 'SCAT', count: 5, dmg: 7, speedF: 42, w: 4, h: 4, reload: 30, pellets: 3, stam: 5 },
    },
    // shield bash out of a roll: carries the hunter forward (lunge 30)
    roll: { id: 'shieldbash', startup: 3, active: 4, recover: 12, dmg: 8, reach: 14, hw: 16, hh: 14, stam: 8, push: 10, lunge: 30 },
    alt: { id: 'shieldcharge', startup: 4, active: 5, recover: 14, dmg: 10, reach: 15, hw: 18, hh: 16, stam: 9, push: 14, lunge: 18 },
    // held A past the bash -> charged ball (bigger, faster; demo ammo unlimited)
    chargeShells: [
      { dmg: 34, speedF: 45, w: 7, h: 6, reload: 70, stam: 12, pellets: 1 },
      { dmg: 46, speedF: 55, w: 8, h: 8, reload: 70, stam: 18, pellets: 1 },
    ],
  },
];

const MONSTER_ATTACKS = {
  // Legacy kit (defs 0/1): single window via reach/hw/hh, byte-identical to the
  // published lunge/sweep so parity fixtures do not move.
  lunge: { kind: 'lunge', windup: 40, active: 10, recover: 55, speedF: 34, dmg: 12, reach: 12, hw: 24, hh: 22 },
  sweep: { kind: 'sweep', windup: 48, active: 12, recover: 60, dmg: 9, reach: 17, hw: 32, hh: 24 },
  // HEAVY kit (nch.1): bite lunges and tracks; tail_spin locks its facing at
  // windup and whips four contiguous windows (behind -> north -> front ->
  // south). `windows` entries are face-relative box centres (ox/oy, w/h),
  // matching the C++ CombatWindow decode. `lock-away` (nch.2) negates the
  // tracked vector once at windup entry so the tail -- window 0 behind the
  // turned-away back -- points at the hunter.
  bite: {
    kind: 'bite', windup: 30, active: 8, recover: 40, dmg: 10, speedF: 26,
    phys: 'BLUNT', facing: 'track',
    windows: [{ t0: 0, t1: 8, ox: 14, oy: 0, w: 18, h: 14, dmgMul: 100 }],
  },
  tailSpin: {
    kind: 'tailSpin', windup: 42, active: 20, recover: 55, dmg: 8,
    phys: 'BLUNT', facing: 'lock-away',
    windows: [
      { t0: 0, t1: 5, ox: -20, oy: 0, w: 24, h: 16, dmgMul: 100 },
      { t0: 6, t1: 10, ox: 0, oy: -22, w: 16, h: 24, dmgMul: 100 },
      { t0: 11, t1: 15, ox: 22, oy: 0, w: 24, h: 16, dmgMul: 100 },
      { t0: 16, t1: 20, ox: 0, oy: 22, w: 16, h: 24, dmgMul: 100 },
    ],
  },
};

// Active window for a windows-path attack at inclusive 1-based tick t, or null
// (legacy reach attacks have no windows array). Null also means "no hit": the
// C++ hit test only fires inside the cached window's [t0, t1].
function monsterActiveWindow(a, t) {
  if (!a || !a.windows) return null;
  for (const w of a.windows) if (t >= w.t0 && t <= w.t1) return w;
  return null;
}

// Telegraph window for a windows-path attack, mirroring the C++ RAM cache
// (attackLoad caches window 0 at windup; monsterWindowNext leaves the last
// window cached through the attack tail): active window if any, else the first
// during windup and the last after the windows end. Null for legacy attacks.
function monsterTellWindow(m, a) {
  if (!a || !a.windows) return null;
  if (m.state === 'windup') return a.windows[0];
  const win = monsterActiveWindow(a, m.t);
  return win || a.windows[a.windows.length - 1];
}

// Demo beast roster (bead monhun-ardu-6zb.1). Index 0 (lunge) is the legacy
// parity default: its def reproduces the values newGame() used to hardcode.
// atkDist is the lunge/sweep split distance; negative = never lunge (always
// sweep). `collide` is the body-collision box (epic monhun-ardu-nch): the
// chicken's legs only, so the hunter can overlap the raised body. Absent means
// the body box (ox/oy 0, w/h = def w/h), matching the C++ creature default.
// nch.4: HEAVY holds ground at keepDist 12 and spins inside spinDist 30; its
// faceHold commits the tracked facing for 10 ticks so the hunter can flank.
// The shipped kinds leave faceHold/keepDist/spinDist unset (0/24/24), which
// keeps every parity scene byte-identical.
const MONSTER_DEFS = [
  { kind: 'lunge', w: 32, h: 24, hp: 200, spd: 5, atkDist: 32, collide: { ox: 9, oy: 11, w: 12, h: 13 } },
  { kind: 'sweep', w: 28, h: 22, hp: 150, spd: 7, atkDist: -1 },
  { kind: 'heavy', w: 40, h: 28, hp: 320, spd: 3, atkDist: 24, keepDist: 12, spinDist: 30, faceHold: 10 },
];

// Fixed 3-hitzone model (build/zones-design.md), mirrored from
// data/creatures/lunge.json: body implicit (mul 100), optional head and
// appendage (legs) records with face-relative boxes, own pools and bodyShare.
// 4t4/nch.4: HEAVY mirrors its long-tail appendage record (heavy.json) so the
// from-behind tail hit is testable; the C++ resolver draws the same values from
// the combat blob. Boxes are face-relative origins (rotated through the facing
// frame); a drained pool + matching phys type flips the break bit.
const MONSTER_ZONES = {
  lunge: {
    // breakTypes is the C++ phys mask (PHYS_SLASH 0x01); both zones break on
    // slashing player hits, matching combat_data ZONES.
    head: { ox: 18, oy: 0, w: 11, h: 7, dmgMul: 130, hp: 40, bodyShare: 100, breakTypes: 1, staggerOnHit: 12 },
    appendage: { ox: 9, oy: 0, w: 9, h: 24, dmgMul: 150, hp: 60, bodyShare: 40, breakTypes: 1, staggerOnHit: 30 },
  },
  heavy: {
    // ox -24 sits the tail behind the body so a flanking hit lands here.
    appendage: { ox: -24, oy: 0, w: 24, h: 16, dmgMul: 150, hp: 60, bodyShare: 40, breakTypes: 1, staggerOnHit: 30 },
  },
};

// Player phys bit per weapon (W_SWORD, W_FLAIL, W_GUN), mirroring
// src/core/monster.hpp playerPhys().
const PHYS_BIT_BY_WEAPON = [1, 2, 4];

// Training-pole variants (bead monhun-ardu-6zb.5; part-locked zones 6zb.10),
// mirroring data/creatures/pole*.json: kind 0 PLAIN is the legacy pole (no
// zone, no break, keeps its head crit); 1 SEVER, 2 BREAK and 3 CRACK each
// carry ONE part-locked breakable zone (cap x-2..22, horn x+4..22, collar
// x-2..22 mid-height) so only a hit on the part drains, never a lower-post
// body hit. All three share the monster rule (any weapon drains; breakTypes is
// the full phys mask). `z` is the zone box relative to the pole rect (w 0 = no
// zone), pool the drain, breakTypes the required phys mask. Broken art is a
// stage, not a rect resize, so brokenW/brokenH stay the body size.
const POLE_DEFS = [
  { w: 20, h: 36, z: null, pool: 0, breakTypes: 0, brokenW: 20, brokenH: 36 },
  { w: 20, h: 36, z: { x: -2, y: 0, w: 24, h: 20 }, pool: 60, breakTypes: 7, brokenW: 20, brokenH: 36 },
  { w: 20, h: 36, z: { x: 4, y: 0, w: 18, h: 20 }, pool: 40, breakTypes: 7, brokenW: 20, brokenH: 36 },
  { w: 20, h: 36, z: { x: -2, y: 12, w: 24, h: 16 }, pool: 30, breakTypes: 7, brokenW: 20, brokenH: 36 },
];

const POLE_PLAIN = 0;
const POLE_SEVER = 1;
const POLE_BREAK = 2;
const POLE_CRACK = 3;

// Install a pole variant on the game's pole object. Kind 0 leaves the published
// 20x36 rect untouched (parity scenes keep setting pole.x/y directly).
function initPoleKind(g, kind) {
  let k = kind | 0;
  if (k < 0 || k >= POLE_DEFS.length) k = 0;
  const def = POLE_DEFS[k];
  g.pole.kind = k;
  g.pole.w = def.w;
  g.pole.h = def.h;
  g.pole.hp = def.pool;
  g.pole.broken = 0;
  return g;
}

function newGame(weaponIndex, mode, monsterIndex = 0) {
  const g = {
    tick: 0,
    freeze: 0,
    shake: 0,
    over: null,
    prevA: false,
    prevB: false,
    weapon: weaponIndex | 0,
    mode: mode === 'train' ? 'train' : 'hunt',
    monsterIndex: monsterIndex | 0,
    cam: { x: 0, y: 0 },
    train: { total: 0, last: 0, events: [] },
    pole: { x: 140, y: 40, w: 20, h: 36, hitFlash: 0, kind: 0, hp: 0, broken: 0 },
    player: {
      x: 96, y: 60, w: 16, h: 16,
      subX: 0, subY: 0, // 1/16 px remainder
      vx: 0, vy: 0,     // 1/16 px per tick
      fx: FP, fy: 0,    // 1/16 unit vector
      hp: 100, hpMax: 100, stam: 100, stamMax: 100, stamSub: 0,
      state: 'idle', t: 0, atk: null, hitDone: false,
      chain: 0, chainWin: 0, aBuffer: 0,
      // sheathe prototype: stowed flag, d-pad tap-sequence tracker + chord grace
      sheathed: false, sheatheLatch: false,
      seqDir: 0, seqT: 0, seq2: false, seqRel: false, chordT: 0, pMy: false,
      triWait: 0, triT: 0,   // 'dabhold': deferred A press + held 3-button session
      pA: false, aHold: 0, chargeT: 0, chargeArmed: false,   // charge attack
      finWin: false,   // combo finisher done: the next B is the stage-3 branch
      chainLock: 0,   // combo debounce prototype: ticks before A can chain again
      bBuffer: 0,     // B branch tap buffer (set in recovery / during the lock)
      stance: null, stanceT: 0, stanceAuto: 0, whirlTick: 0,
      throwCd: 0, riposteT: 0,
      bHeld: 0, bReady: false, bLocked: false,
      iT: 0,
      shell: 'ball', reload: 0,
      shells: {
        ball: WEAPON_DEFS[2].shells.ball.count,
        scatter: WEAPON_DEFS[2].shells.scatter.count,
      },
    },
    monster: {
      x: 200, y: 40, w: 32, h: 24,
      subX: 0, subY: 0,
      hp: 200, hpMax: 200,
      state: 'idle', t: 90, cd: 140,
      face: { x: -FP, y: 0 },
      atk: null, lvx: 0, lvy: 0, windupMax: 0,
      hitFlash: 0, stun: 0, circleDir: 1,
      spd: 5, // 1/16 px per tick
      faceT: 0, // nch.4 turn-commitment countdown
    },
    projectiles: [],
    effects: [],
  };
  initMonster(g, g.monsterIndex);
  return g;
}

// Spawn stats from MONSTER_DEFS[kind]; every other field keeps its published
// value (legacy kind 0 spawn, byte-for-byte).
function initMonster(g, kind = 0) {
  let k = kind | 0;
  if (k < 0 || k >= MONSTER_DEFS.length) k = 0;
  const def = MONSTER_DEFS[k];
  const m = g.monster;
  m.x = 200;
  m.y = 40;
  m.w = def.w;
  m.h = def.h;
  m.subX = 0;
  m.subY = 0;
  m.hp = def.hp;
  m.hpMax = def.hp;
  m.state = 'idle';
  m.t = 90;
  m.cd = 140;
  m.face = { x: -FP, y: 0 };
  m.atk = null;
  m.lvx = 0;
  m.lvy = 0;
  m.windupMax = 0;
  m.hitFlash = 0;
  m.stun = 0;
  m.circleDir = 1;
  m.spd = def.spd;
  m.faceT = 0; // nch.4: refresh facing on the first update tick
  m.collide = def.collide || { ox: 0, oy: 0, w: def.w, h: def.h };
  m.kind = def.kind;
  // Live zone pools (kind-keyed). Absent kind = no zones (body-only routing).
  const zones = MONSTER_ZONES[def.kind];
  m.zones = null;
  if (zones) {
    m.zones = {};
    for (const name of Object.keys(zones)) {
      m.zones[name] = { hp: zones[name].hp, broken: false };
    }
  }
  g.monsterIndex = k;
  return m;
}

/* ------------------------------------------------------------------ step */

function step(g, inp) {
  inp = inp || {};
  g.tick++;
  const aP = !!inp.a && !g.prevA;
  const bP = !!inp.b && !g.prevB;
  const bR = !inp.b && g.prevB;
  g.prevA = !!inp.a;
  g.prevB = !!inp.b;
  updateCamera(g);

  if (g.over) {
    if (g.shake > 0) g.shake = Math.max(0, g.shake - 0.4);
    updateEffects(g);
    return;
  }
  if (g.freeze > 0) {
    g.freeze--;
    if (g.shake > 0) g.shake = Math.max(0, g.shake - 0.25);
    return;
  }

  updatePlayer(g, inp, aP, bP, bR);
  if (g.mode === 'train') updatePole(g);
  else updateMonster(g);
  updateProjectiles(g);
  updateEffects(g);
  if (g.shake > 0) g.shake = Math.max(0, g.shake - 0.3);
}

/* ------------------------------------------------------------------ player */

function updatePlayer(g, inp, aP, bP, bR) {
  const p = g.player;
  const def = WEAPON_DEFS[g.weapon];

  if (p.iT > 0) p.iT--;
  if (p.throwCd > 0) p.throwCd--;
  if (p.reload > 0) p.reload--;
  if (p.riposteT > 0) p.riposteT--;
  if (getDebounceProfile().gap > 0) {
    // debounce on: recovery lock first, then the combo window. The window
    // only ticks in idle, so a chained attack cannot expire its own chain.
    if (p.chainLock > 0) {
      p.chainLock--;
      if (p.chainLock === 0) p.chainWin = 14;   // window opens after the recovery
    } else if (p.chainWin > 0 && p.state === 'idle') {
      p.chainWin--;
      if (p.chainWin === 0) { p.chain = 0; p.finWin = false; }
    }
  } else if (p.chainWin > 0) {
    p.chainWin--;
    if (p.chainWin === 0) { p.chain = 0; p.finWin = false; }
  }
  if (p.aBuffer > 0) p.aBuffer--;

  // A press/hold/release: charge attacks build after a swing while A is held
  // (CHARGE_MIN), then release fires level 1 (or level 2 at CHARGE_L2).
  const aR = !inp.a && p.pA;
  p.pA = !!inp.a;
  p.aHold = inp.a ? Math.min(255, p.aHold + 1) : 0;
  if (aR) p.chargeArmed = false;

  // sheathe prototype: d-pad tap sequence + chord grace, then the combo check
  // (consumes the press pair so it cannot also attack/dodge/stance).
  if (p.chordT > 0) p.chordT--;
  if (p.seqT > 0) {
    p.seqT--;
    if (p.seqT === 0) { p.seqDir = 0; p.seq2 = false; p.seqRel = false; }
  }
  const myNow = inp.my > 0;
  if (myNow && !p.pMy) {              // down press edge
    if (p.seqDir === 1 && p.seqT > 0) p.seq2 = true;
    p.seqDir = 1;
    p.seqT = SHEATHE_SEQ_WIN;
    p.seqRel = false;
  }
  if (!myNow && p.pMy && p.seqT > 0) p.seqRel = true;   // completed tap
  p.pMy = myNow;

  const sheatheConsumed = sheatheCombo(g, aP, bP, inp);
  if (!sheatheConsumed) {
    if (aP && !inp.b) p.chordT = CHORD_WIN;
    if (bP && !inp.a) p.chordT = CHORD_WIN;
  }

  const draining = p.stance === 'whirl' || p.stance === 'guard';
  if (!draining && p.stam < p.stamMax) {
    p.stamSub += 8; // 0.5 per tick
    if (p.stamSub >= 16) { p.stamSub -= 16; p.stam = Math.min(p.stamMax, p.stam + 1); }
  }

  // B: release speed picks tap defense vs hold stance. A tap inside attack
  // recovery or the debounce lock queues the A-B branch (B_BUFFER) so a loose
  // A A B still combos; it fires when the branch window opens.
  if (bP && !sheatheConsumed && getDebounceProfile().gap > 0) {
    const a = p.atk;
    const inRecovery = p.state === 'attack' && a && p.t >= a.startup + a.active;
    const inLock = p.state === 'idle' && (p.chain > 0 || p.finWin) && p.chainLock > 0;
    if (inRecovery || inLock) p.bBuffer = B_BUFFER;
  }
  if (bP) { p.bHeld = 0; p.bReady = true; }
  if (inp.b && p.bReady) {
    p.bHeld++;
    if (p.bHeld === HOLD_TICKS && !p.stance && !p.bLocked && !p.sheatheLatch) {
      if (getSheatheVariant() === 'dbh' && !p.sheathed && p.state === 'idle' && inp.my > 0) {
        trySheathe(p);   // hold Down + B = stow instead of the south stance
      } else {
        enterStance(g, def);
        p.bBuffer = 0;   // hold wins: drop any queued branch tap
      }
    }
  }
  if (bR) {
    if (!p.sheatheLatch) {
      if (p.bHeld < HOLD_TICKS) {
        if (!tryBranch(g, def, inp) && p.bBuffer === 0) tapDefense(g, def, inp);
      } else if (p.stance) {
        exitStance(p);
        p.bBuffer = 0;
      }
    }
    p.bReady = false;
    p.bHeld = 0;
    p.bLocked = false;
    p.sheatheLatch = false;
  }
  if (p.bBuffer > 0 && !inp.b && !p.sheathed) {
    if (tryBranch(g, def, inp)) {
      p.bBuffer = 0;   // queued branch fired as soon as the window allowed
    } else {
      p.bBuffer--;
    }
  }

  // A: attack / stance special (while stowed: draw into combo hit 1).
  // canAttackNow: the debounce profile attacks only from idle with no lock;
  // OFF keeps the original chainWin path byte-for-byte (parity fixtures).
  const canAttackNow =
    p.chainLock === 0 &&
    (getDebounceProfile().gap > 0 ? p.state === 'idle' : (p.state === 'idle' || p.chainWin > 0));
  if (aP && !sheatheConsumed) {
    if (p.sheathed) {
      if (p.state === 'idle') {
        p.sheathed = false;
        p.chain = 0;
        p.chainWin = 0;
        p.aBuffer = 0;
        startAttack(g, def);
      }
    } else if (p.state === 'dodge' || p.state === 'deflect' || p.state === 'shove') {
      startRollAttack(g, def);
    } else if (p.stance) {
      stanceSpecial(g, def);
    } else if (canAttackNow) {
      startAttack(g, def, !!(inp.mx || inp.my));
    } else {
      p.aBuffer = aBufferTicks();   // buffered: fires when the debounce lock expires
    }
  }
  if (!p.sheathed && p.aBuffer > 0 && canAttackNow) {
    p.aBuffer = 0;
    startAttack(g, def, !!(inp.mx || inp.my));
  }

  switch (p.state) {
    case 'idle': {
      let mx = inp.mx || 0;
      let my = inp.my || 0;
      if (p.stance === 'parry') { mx = 0; my = 0; }
      let sp = p.sheathed ? SHEATHE_SPD : def.spd;
      if (p.stance === 'whirl') sp = (sp * 6) / 10 | 0;
      if (p.stance === 'guard') sp = (sp * 4) / 10 | 0;
      // guard: strafe with the shield up -- move with the d-pad but keep the
      // current facing (turn by releasing B, moving, then re-guarding)
      movePlayer(p, mx, my, sp, p.stance === 'guard');
      applyDrift(p);
      break;
    }
    case 'attack':
    case 'special': {
      const a = p.atk;
      const total = a.startup + a.active + a.recover;
      p.t++;
      if (p.t >= a.startup && p.t < a.startup + a.active && !p.hitDone) {
        const hit = meleeHitbox(p, a);
        const tr = targetRect(g);
        if (tr && rectsOverlap(hit, tr)) {
          p.hitDone = true;
          const mult = (p.state === 'special' && p.riposteT > 0) ? 2 : 1;
          const hx = hit.x + hit.w / 2;
          const hy = hit.y + hit.h / 2;
          if (g.mode === 'train') {
            poleOnHit(g, a.dmg * mult, hx, hy);
          } else {
            monsterOnHit(g, a.dmg * mult, hx, hy);
            if (g.monster.state !== 'dead') {
              if (a.effect === 'trip') g.monster.stun = Math.max(g.monster.stun, 70);
              if (a.push) knockMonsterAway(g, g.monster, hx, hy, a.push);
            }
          }
        }
      }
      if (p.t >= total) {
        if (p.state === 'attack') {
          p.state = 'idle';
          const finisher = p.chain >= 2;
          p.chain = p.chain < 2 ? p.chain + 1 : 0;
          p.finWin = finisher;   // the next B in the window is the stage-3 branch
          const prof = getDebounceProfile();
          const lock = prof.gap > 0 ? (finisher ? prof.fin : prof.gap) : 0;
          p.chainLock = lock;
          p.chainWin = lock === 0 ? 14 : 0;   // window opens when the lock ends
        } else {
          p.state = 'idle';
          p.chain = 0;
          p.chainWin = 0;
          p.chainLock = 0;
          p.finWin = false;
        }
        p.t = 0;
        p.atk = null;
      }
      applyDrift(p);
      break;
    }
    case 'dodge': {
      p.t--;
      applyDrift(p, 14);
      if (p.t <= 0) p.state = 'idle';
      break;
    }
    case 'deflect': {
      p.t--;
      applyDrift(p, 14);
      if (p.t <= 0) p.state = 'idle';
      break;
    }
    case 'shove':
    case 'stun': {
      p.t--;
      if (p.t <= 0) p.state = 'idle';
      break;
    }
    case 'charge': {
      // rooted windup; release fires the level-1 or level-2 charge
      p.chargeT = Math.min(255, p.chargeT + 1);
      if (aR) {
        const fired = def.charge ? startChargeAttack(g, def)
          : def.chargeShells ? fireChargeShot(g, def) : false;
        if (!fired) p.state = 'idle';
        p.chargeArmed = false;
      }
      applyDrift(p);
      break;
    }
    default:
      p.state = 'idle';
  }

  // held A past the swing -> charge stance (weapons with charge data only)
  if (p.state === 'idle' && p.chargeArmed && inp.a && p.aHold >= CHARGE_MIN &&
      (def.charge || def.chargeShells)) {
    p.state = 'charge';
    p.chargeT = 0;
  }

  if (p.stance) updateStance(g, def);
  clampPlayer(p);
}

function movePlayer(p, mx, my, spd, lockFacing) {
  const i = dirIndexFromInput(mx, my);
  if (i < 0) return;
  const d = DIR8[i];
  if (!lockFacing) { p.fx = d.x; p.fy = d.y; }
  addMove(p, d.x, d.y, spd);
}

function applyDrift(p, mult) {
  const m = mult === undefined ? 13 : mult; // 13/16 per tick
  p.subX += p.vx;
  p.subY += p.vy;
  p.x += tdiv(p.subX, 16);
  p.y += tdiv(p.subY, 16);
  p.subX %= 16;
  p.subY %= 16;
  // decay toward zero; shift floors would leave -1 sticking forever
  const ax = (Math.abs(p.vx) * m) / 16 | 0;
  const ay = (Math.abs(p.vy) * m) / 16 | 0;
  p.vx = p.vx < 0 ? -ax : ax;
  p.vy = p.vy < 0 ? -ay : ay;
  if (p.vx > -1 && p.vx < 1) p.vx = 0;
  if (p.vy > -1 && p.vy < 1) p.vy = 0;
}

function startAttack(g, def, alt) {
  const p = g.player;
  if (p.sheathed) return;       // draw path clears the flag first
  if (p.chainLock > 0) return;  // debounce lock gates every attack entry
  // direction + A replaces combo hit 1 with the weapon's lunge opener
  const a = (alt && def.alt && p.chain === 0) ? def.alt : def.attacks[Math.min(p.chain, 2)];
  if (p.stam < 1) return;
  p.stam = Math.max(0, p.stam - a.stam);
  p.state = 'attack';
  p.atk = a;
  p.t = 0;
  p.hitDone = false;
  p.finWin = false;
  p.chargeArmed = true;   // hold A through this swing -> charge
  if (a.lunge) {
    p.vx = (p.fx * a.lunge) >> 4;
    p.vy = (p.fy * a.lunge) >> 4;
  }
}

/* --------------------------------------------------------- sheathe combo */

// Stow the weapon: only from idle (stance idle counts; the stance is dropped).
// Returns false when the state does not allow it, so the press pair falls
// through to its normal attack/dodge/stance meaning.
function trySheathe(p) {
  if (p.state !== 'idle') return false;
  if (p.stance) exitStance(p);
  p.sheathed = true;
  p.chain = 0;
  p.chainWin = 0;
  p.chainLock = 0;   // stowing drops the pending combo recovery
  p.aBuffer = 0;
  p.seqDir = 0; p.seqT = 0; p.seq2 = false; p.seqRel = false;
  p.triWait = 0; p.triT = 0;
  p.sheatheLatch = true;   // suppress roll/stance until B is released
  return true;
}

// Evaluate the active variant. A returns true only when the toggle actually
// fired; the caller then consumes the A/B press for this tick.
function sheatheCombo(g, aP, bP, inp) {
  const p = g.player;
  if (p.sheathed) return false;   // stowed: A draws, combos do nothing
  const v = getSheatheVariant();

  if (v === 'dabhold') {
    // Down + A + B held together. A is deferred CHORD_WIN ticks when down is
    // held so B can join; a held session stows after SHEATHE_HOLD_TICKS, and
    // releasing any button cancels it (a still-held A then swings).
    const all3 = inp.my > 0 && inp.a && inp.b;
    if (p.triT > 0) {
      if (!all3) {
        p.triT = 0;
        p.sheatheLatch = true;   // eat the stray release so it does not roll
        if (inp.a) p.aBuffer = aBufferTicks();   // cancelled: deferred A swings
        return false;
      }
      p.triT--;
      if (p.triT === 0) return trySheathe(p);
      return true;   // pending: consume A/B so no attack or stance fires
    }
    if (all3 && (aP || bP || p.triWait > 0)) {
      p.triT = SHEATHE_HOLD_TICKS;
      p.triWait = 0;
      p.aBuffer = 0;
      p.bBuffer = 0;
      return true;
    }
    if (aP && inp.my > 0 && !inp.b) {
      p.triWait = CHORD_WIN;   // defer the down+A attack while B can join
      return true;
    }
    if (p.triWait > 0) {
      p.triWait--;
      if (p.triWait === 0) p.aBuffer = aBufferTicks();   // B never joined: attack
      return true;
    }
  }

  const chordA = aP && (inp.b || p.chordT > 0);
  const seq = p.seqDir === 1 && p.seqT > 0;
  let hit = false;
  if (v === 'ab') hit = chordA;
  else if (v === 'dab') hit = chordA && seq && p.seqRel;
  else if (v === 'ddab') hit = chordA && p.seq2;
  else if (v === 'db') hit = bP && seq && p.seqRel;
  // 'dbh' resolves in the B-hold path (needs bHeld to reach HOLD_TICKS).
  return hit && trySheathe(p);
}

/* --------------------------------------------------------- roll attack */

// A out of a dodge (or the gun's evade-shove) cancels into the weapon's roll
// move; dodge i-frames keep ticking. The gun shield bash carries lunge 30 so
// it moves the hunter forward; sword/flail roll moves stop in place.
function startRollAttack(g, def) {
  const p = g.player;
  const a = def.roll;
  if (!a || p.stam < a.stam) return false;
  p.stam = Math.max(0, p.stam - a.stam);
  p.state = 'attack';
  p.atk = a;
  p.t = 0;
  p.hitDone = false;
  p.chain = 0;
  p.chainWin = 0;
  p.chainLock = 0;
  if (a.lunge) {
    p.vx = (p.fx * a.lunge) >> 4;
    p.vy = (p.fy * a.lunge) >> 4;
  }
  return true;
}

/* --------------------------------------------------------- charge attack */

// Flail-style charge release: level 1 below CHARGE_L2, level 2 at/above it.
function startChargeAttack(g, def) {
  const p = g.player;
  if (!def.charge) return false;
  const a = def.charge[p.chargeT >= CHARGE_L2 ? 1 : 0];
  if (!a || p.stam < a.stam) return false;
  p.stam = Math.max(0, p.stam - a.stam);
  p.state = 'attack';
  p.atk = a;
  p.t = 0;
  p.hitDone = false;
  p.chain = 0;
  p.chainWin = 0;
  p.chainLock = 0;
  p.finWin = false;
  if (a.lunge) {
    p.vx = (p.fx * a.lunge) >> 4;
    p.vy = (p.fy * a.lunge) >> 4;
  }
  return true;
}

// Gun charge release: fires the charged ball (level selects dmg/speed).
function fireChargeShot(g, def) {
  const p = g.player;
  if (!def.chargeShells) return false;
  const sh = def.chargeShells[p.chargeT >= CHARGE_L2 ? 1 : 0];
  if (!sh || p.stam < sh.stam) return false;
  p.stam -= sh.stam;
  p.reload = sh.reload;
  fireShell(g, p, sh);
  return true;
}

function tapDefense(g, def, inp) {
  const p = g.player;
  if (p.state === 'dodge' || p.state === 'deflect' || p.state === 'shove' ||
      p.state === 'stun' || p.state === 'special') return;
  if (p.state === 'attack' && !def.canCancel) return;

  // roll toward move input if any, else current facing
  let dx = p.fx;
  let dy = p.fy;
  if (inp && (inp.mx || inp.my)) {
    const d = DIR8[dirIndexFromInput(inp.mx, inp.my)];
    dx = d.x;
    dy = d.y;
    p.fx = dx;
    p.fy = dy;
  }

  if (p.sheathed) {
    // stowed: every weapon rolls with the sword dodge numbers (MH-style run +
    // evade while sheathed)
    if (p.stam < 14) return;
    p.stam -= 14;
    p.state = 'dodge';
    p.t = 16;
    p.iT = 14;
    p.vx = (dx * 54) >> 4;
    p.vy = (dy * 54) >> 4;
    exitStance(p);
  } else if (def.id === 'sword') {
    if (p.stam < 14) return;
    p.stam -= 14;
    p.state = 'dodge';
    p.t = 16;
    p.iT = 14;
    p.vx = (dx * 54) >> 4; // 3.4 px/t
    p.vy = (dy * 54) >> 4;
    exitStance(p);
  } else if (def.id === 'flail') {
    if (p.stam < 10) return;
    p.stam -= 10;
    p.state = 'deflect';
    p.t = 9;
    p.vx = -(dx * 30) >> 4; // 1.9 px/t back step
    p.vy = -(dy * 30) >> 4;
    exitStance(p);
  } else {
    if (p.stam < 10) return;
    p.stam -= 10;
    p.state = 'shove';
    p.t = 10;
    exitStance(p);
    const m = g.monster;
    const mdx = (m.x + (m.w >> 1)) - (p.x + (p.w >> 1));
    const mdy = (m.y + (m.h >> 1)) - (p.y + (p.h >> 1));
    const dist = isqrt(mdx * mdx + mdy * mdy);
    const dot = (mdx * p.fx + mdy * p.fy) >> 4; // px along facing
    if (dist > 0 && dist < 38 && dot * 5 > dist * 2 && m.state !== 'dead') {
      const d = DIR8[dirIndexFromDelta(mdx, mdy)];
      addMove(m, d.x, d.y, 10);
      g.freeze = Math.max(g.freeze, 2);
    }
  }
}

function tryBranch(g, def, inp) {
  const p = g.player;
  if (!def.branches) return false;

  let stage = 0;
  if (p.state === 'attack' && p.atk) {
    if (p.t < p.atk.startup + p.atk.active) return false; // only from recovery
    stage = p.chain < 2 ? p.chain + 1 : 3;
  } else if (p.state === 'idle' && p.chainWin > 0) {
    stage = p.finWin ? 3 : p.chain;   // finisher done: B is the stage-3 branch
  } else {
    return false;
  }

  let br = null;
  for (const b of def.branches) if (b.stage === stage) br = b;
  if (!br) return false;

  if (br.stance) {
    p.whirlTick = 0;
    p.stance = br.stance;
    p.stanceT = 0;
    p.stanceAuto = br.auto || 0;
    p.state = 'idle';
    p.atk = null;
    p.t = 0;
    p.chain = 0;
    p.chainWin = 0;
    p.finWin = false;
    return true;
  }

  const atk = br.atk;
  if (p.stam < atk.stam) return false;
  if (atk.shell) {
    if (p.shells.ball <= 0) return false;
    // Demo: ammo unlimited (no decrement); reload still arms.
    p.reload = 45;
  }
  p.stam -= atk.stam;
  p.state = 'attack';
  p.atk = atk;
  p.t = 0;
  p.hitDone = false;
  p.chain = 0;
  p.chainWin = 0;
  p.finWin = false;
  p.chargeArmed = false;   // branch attacks do not charge
  if (atk.lunge) {
    p.vx = (p.fx * atk.lunge) >> 4;
    p.vy = (p.fy * atk.lunge) >> 4;
  }
  return true;
}

function enterStance(g, def) {
  const p = g.player;
  if (p.stance) return;
  if (p.sheathed) { p.bLocked = true; return; }   // no stance while stowed
  if (p.stam < 10) { p.bLocked = true; return; }
  if (p.state !== 'idle' && !(p.state === 'attack' && def.canCancel)) { p.bLocked = true; return; }
  p.stance = def.id === 'sword' ? 'parry' : def.id === 'flail' ? 'whirl' : 'guard';
  p.stanceT = 0;
  p.state = 'idle';
  p.atk = null;
  p.t = 0;
}

function exitStance(p) {
  p.stance = null;
  p.stanceT = 0;
  p.stanceAuto = 0;
}

function updateStance(g, def) {
  const p = g.player;
  if (p.stanceAuto > 0) {
    p.stanceAuto--;
    if (p.stanceAuto === 0) { exitStance(p); return; }
  }
  p.stanceT++;

  if (p.stance === 'parry') {
    const ok = drainStam(p, 2); // ~0.12 per tick
    if (p.stanceT > 34 || !ok) { exitStance(p); p.bLocked = true; }
  } else if (p.stance === 'whirl') {
    const ok = drainStam(p, 8); // 0.5 per tick
    p.whirlTick++;
    if (!ok) { exitStance(p); p.bLocked = true; return; }
    if (p.whirlTick % 16 === 0) {
      const tr = targetRect(g);
      if (tr) {
        const cx = p.x + p.w / 2;
        const cy = p.y + p.h / 2;
        if (circleRectOverlap(cx, cy, 24, tr)) {
          if (g.mode === 'train') {
            poleOnHit(g, 8, cx, cy);
          } else {
            monsterOnHit(g, 8, cx, cy);
            knockMonsterAway(g, g.monster, cx, cy, 8);
          }
        }
      }
    }
  } else if (p.stance === 'guard') {
    const ok = drainStam(p, 1); // ~0.06 per tick
    if (!ok) { exitStance(p); p.bLocked = true; }
  }
}

function stanceSpecial(g, def) {
  const p = g.player;

  if (def.id === 'sword') {
    if (p.state !== 'idle') return;
    if (p.stam < def.special.stam) return;
    p.stam -= def.special.stam;
    p.state = 'special';
    p.atk = def.special;
    p.t = 0;
    p.hitDone = false;
    exitStance(p);
    p.bLocked = true;
  } else if (def.id === 'flail') {
    if (p.state !== 'idle' || p.throwCd > 0) return;
    if (p.stam < def.special.stam) return;
    p.stam -= def.special.stam;
    p.throwCd = 50;
    p.state = 'special';
    p.atk = def.special;
    p.t = 0;
    p.hitDone = false;
  } else {
    if (p.reload > 0) return;
    const sh = def.shells[p.shell];
    if (p.shells[p.shell] <= 0 || p.stam < sh.stam) return;
    p.stam -= sh.stam;
    // Demo: ammo unlimited (magazine stays at max); reload timer still paces.
    p.reload = sh.reload;
    fireShell(g, p, sh);
  }
}

function fireShell(g, p, sh) {
  const cx = p.x + (p.w >> 1);
  const cy = p.y + (p.h >> 1);
  const speedF = sh.speedF;
  const pellets = sh.pellets || 1;
  const dirs = [];
  if (pellets === 1) {
    dirs.push({ x: p.fx, y: p.fy });
  } else {
    dirs.push(rotFp(p.fx, p.fy, 15, 6));   // ~22 deg left
    dirs.push({ x: p.fx, y: p.fy });
    dirs.push(rotFp(p.fx, p.fy, 15, -6));  // ~22 deg right
  }
  g.effects.push({
    x: cx + ((p.fx * 10) >> 4),
    y: cy + ((p.fy * 10) >> 4),
    t: 0, life: 5, crit: true,
  });
  for (const d of dirs) {
    g.projectiles.push({
      x: (cx << 4) + ((d.x * 13) >> 4),
      y: (cy << 4) + ((d.y * 13) >> 4),
      subX: 0, subY: 0,
      vx: (d.x * speedF) >> 4,
      vy: (d.y * speedF) >> 4,
      w: sh.w, h: sh.h, dmg: sh.dmg, life: 90,
      heavy: pellets === 1,
    });
  }
}

function clampPlayer(p) {
  p.x = Math.max(0, Math.min(WORLD_W - p.w, p.x));
  p.y = Math.max(0, Math.min(WORLD_H - p.h, p.y));
}

function updateCamera(g) {
  const p = g.player;
  const tx = p.x + p.w / 2 - W / 2;
  const ty = p.y + p.h / 2 - ARENA_H / 2;
  g.cam.x = Math.max(0, Math.min(WORLD_W - W, tx));
  g.cam.y = Math.max(0, Math.min(WORLD_H - ARENA_H, ty));
}

/* ------------------------------------------------------------------ monster */

function updateMonster(g) {
  const m = g.monster;
  const p = g.player;
  const def = MONSTER_DEFS[g.monsterIndex];
  if (m.hitFlash > 0) m.hitFlash--;
  if (m.state === 'dead') return;

  const dx = (p.x + (p.w >> 1)) - (m.x + (m.w >> 1));
  const dy = (p.y + (p.h >> 1)) - (m.y + (m.h >> 1));
  const dist = isqrt(dx * dx + dy * dy);
  const di = dirIndexFromDelta(dx, dy);
  // Facing: lock attacks freeze the windup-start vector through windup + attack
  // (nch.1 tail_spin); every legacy lunge/sweep tracks. nch.4: a def's faceHold
  // commits the tracked vector for that many ticks (faceT counts down from
  // faceHold to 0, then the vector refreshes and re-arms); faceHold 0 (the
  // shipped kinds) recomputes every tick, keeping parity byte-identical.
  const lockFace = m.atk && (m.atk.facing === 'lock-at-windup' || m.atk.facing === 'lock-away') &&
                   (m.state === 'windup' || m.state === 'attack');
  const faceHold = def.faceHold || 0;
  if (!lockFace) {
    if (faceHold === 0) {
      m.face = { x: DIR8[di].x, y: DIR8[di].y };
    } else {
      if (m.faceT === 0) {
        m.face = { x: DIR8[di].x, y: DIR8[di].y };
        m.faceT = faceHold;
      }
      m.faceT--;
    }
  }

  if (m.stun > 0) {
    m.stun--;
    if (m.stun === 0) { m.state = 'recover'; m.t = 24; }
    return;
  }

  switch (m.state) {
    case 'idle':
      m.t--;
      if (m.t <= 0) m.state = 'pursue';
      break;
    case 'pursue':
      m.cd--;
      if (dist > 36) addMove(m, m.face.x, m.face.y, m.spd);
      else if (dist < (def.keepDist === undefined ? 24 : def.keepDist))
        addMove(m, -m.face.x, -m.face.y, (m.spd * 6) / 10 | 0);
      else {
        const s = DIR8[(di + 2) & 7];
        addMove(m, s.x * m.circleDir, s.y * m.circleDir, (m.spd * 8) / 10 | 0);
      }
      if (m.cd <= 0 && dist < 42) chooseAttack(g, dist);
      break;
    case 'windup':
      m.t--;
      if (m.t <= 0) startMonsterAttack(m);
      break;
    case 'attack': {
      const a = m.atk;
      m.t++;
      if (a.speedF && m.t <= a.active) addVel(m, m.lvx, m.lvy);
      if (m.t <= a.active && monsterHitsPlayer(g, a)) {
        // Lock-away tail hits push the hunter radially away from the beast; the
        // turned-away facing would pull them inward. Legacy/track keep the
        // facing-vector knockback.
        const knock = a.facing === 'lock-away' ? DIR8[di] : m.face;
        playerHit(g, a.dmg, m, knock);
      }
      if (m.t > a.active + a.recover) {
        m.state = 'pursue';
        m.cd = 55 + (g.tick % 40);
        m.circleDir = (g.tick % 2) ? 1 : -1;
      }
      break;
    }
    case 'recover':
      m.t--;
      if (m.t <= 0) { m.state = 'pursue'; m.cd = 55; }
      break;
    default:
      m.state = 'pursue';
  }

  clampMonster(g);
  pushApart(g);
}

// Lunge/sweep split comes from the roster def: kind 0 (atkDist 32) is the
// legacy "lunge beyond 32px" rule; negative atkDist (sweep) never lunges. HEAVY
// (nch.1) runs the new kit: tail_spin inside spinDist (30, nch.4) else bite.
function chooseAttack(g, dist) {
  const m = g.monster;
  const def = MONSTER_DEFS[g.monsterIndex];
  if (def.kind === 'heavy')
    m.atk = dist <= (def.spinDist === undefined ? 24 : def.spinDist) ? MONSTER_ATTACKS.tailSpin : MONSTER_ATTACKS.bite;
  else
    m.atk = def.atkDist >= 0 && dist > def.atkDist ? MONSTER_ATTACKS.lunge : MONSTER_ATTACKS.sweep;
  // nch.2: lock-away turns the back to the hunter once, reusing the tracked
  // vector updateMonster just computed this tick (tail_spin window 0 then points
  // back at the hunter).
  if (m.atk.facing === 'lock-away') m.face = { x: -m.face.x, y: -m.face.y };
  m.state = 'windup';
  m.t = m.atk.windup;
  m.windupMax = m.atk.windup;
}

function startMonsterAttack(m) {
  const a = m.atk;
  m.state = 'attack';
  m.t = 0;
  if (a.speedF) {
    m.lvx = (m.face.x * a.speedF) >> 4;
    m.lvy = (m.face.y * a.speedF) >> 4;
  } else {
    m.lvx = 0;
    m.lvy = 0;
  }
}

// Window-path hit test (bite / tail_spin): the active window's face-relative
// box centre + size. Legacy reach attacks keep the scalar projection so their
// behaviour is byte-identical.
function monsterHitsPlayer(g, a) {
  const m = g.monster;
  const win = monsterActiveWindow(a, m.t);
  if (a.windows && !win) return false;   // outside every window: no hit
  let cx, cy, hw, hh;
  if (win) {
    const d = facePoint(m.face.x, m.face.y, win.ox, win.oy);
    cx = m.x + (m.w >> 1) + d.x;
    cy = m.y + (m.h >> 1) + d.y;
    hw = win.w;
    hh = win.h;
  } else {
    cx = m.x + (m.w >> 1) + ((m.face.x * a.reach) >> 4);
    cy = m.y + (m.h >> 1) + ((m.face.y * a.reach) >> 4);
    hw = a.hw;
    hh = a.hh;
  }
  const r = { x: cx - (hw >> 1), y: cy - (hh >> 1), w: hw, h: hh };
  return rectsOverlap(r, g.player);
}

function playerHit(g, dmg, m, knock) {
  const p = g.player;
  if (p.iT > 0) return;
  // Knockback vector: the attack's facing by default; a lock-away tail hit
  // passes the radial beast->player direction instead.
  const k = knock || m.face;

  if (p.state === 'deflect' && p.t > 0) {
    m.stun = 28;
    g.freeze = Math.max(g.freeze, 5);
    g.shake = Math.max(g.shake, 2.6);
    g.effects.push({ x: p.x + 8, y: p.y + 8, t: 0, life: 6, crit: true });
    return;
  }
  if (p.stance === 'parry' && p.stanceT <= 20) {
    m.stun = 60;
    p.riposteT = 90;
    g.freeze = Math.max(g.freeze, 8);
    g.shake = Math.max(g.shake, 3);
    g.effects.push({ x: p.x + 8, y: p.y + 8, t: 0, life: 8, crit: true });
    return;
  }
  if (p.stance === 'guard') {
    p.stam -= 22;
    if (p.stam < 0) p.stam = 0;
    p.hp -= Math.max(1, (dmg * 25 / 100) | 0);
    p.vx = (k.x * 19) >> 4;
    p.vy = (k.y * 19) >> 4;
    g.freeze = Math.max(g.freeze, 3);
    g.shake = Math.max(g.shake, 2);
    if (p.stam <= 0) {
      exitStance(p);
      p.state = 'stun';
      p.t = 45;
      p.bLocked = true;
    }
    if (p.hp <= 0) lose(g);
    return;
  }

  p.hp -= dmg;
  p.iT = 34;
  p.vx = (k.x * 35) >> 4;
  p.vy = (k.y * 35) >> 4;
  p.state = 'idle';
  p.t = 0;
  p.atk = null;
  p.stance = null;
  p.stanceT = 0;
  g.freeze = Math.max(g.freeze, 6);
  g.shake = Math.max(g.shake, 3.6);
  g.effects.push({ x: p.x + 8, y: p.y + 8, t: 0, life: 8, crit: false });
  if (p.hp <= 0) lose(g);
}

function lose(g) {
  g.player.hp = 0;
  g.over = 'lose';
}

// Face-relative box offset, mirroring src/core/game.hpp combatFacePoint():
// the box centre sits at (ox, oy) in the facing frame, rotated into world
// space. With oy == 0 this is the legacy scalar reach projection.
function facePoint(fx, fy, ox, oy) {
  return {
    x: ((fx * ox) - (fy * oy)) >> 4,
    y: ((fy * ox) + (fx * oy)) >> 4,
  };
}

// Mirror combatZoneContains(): the zone world rect is the body anchor plus the
// DIR8-rotated box offset; containment is half-open.
function zoneContains(g, z, hx, hy) {
  const m = g.monster;
  const d = facePoint(m.face.x, m.face.y, z.ox, z.oy);
  const x = m.x + d.x;
  const y = m.y + d.y;
  return hx >= x && hx < x + z.w && hy >= y && hy < y + z.h;
}

// Mirror combatZoneHitResolve(): body implicit at mul 100; a present,
// unbroken zone replaces it only on a strictly higher multiplier (head first,
// then appendage, so the body wins ties). Drains the zone pool and flips the
// broken bit on a matching phys; routes base * dmgMul% * bodyShare% to HP.
function zoneHitResolve(g, base, physBit, hx, hy) {
  const m = g.monster;
  const defs = m.zones ? MONSTER_ZONES[m.kind] : null;
  if (base <= 0 || !defs) return { zone: null, mul: 100, dmg: Math.max(0, base) };
  let best = null;
  let bestMul = 100;
  for (const name of ['head', 'appendage']) {
    const z = defs[name];
    if (!z) continue;
    const pool = m.zones[name];
    if (pool.broken) continue;
    if (zoneContains(g, z, hx, hy) && z.dmgMul > bestMul) { best = name; bestMul = z.dmgMul; }
  }
  if (!best) return { zone: null, mul: 100, dmg: base };
  const out = (base * bestMul / 100) | 0;
  const pool = m.zones[best];
  const pct = Math.min(out, 255);
  pool.hp = pct < pool.hp ? pool.hp - pct : 0;
  if (pool.hp === 0 && (physBit & defs[best].breakTypes)) pool.broken = true;
  const body = (out * defs[best].bodyShare / 100) | 0;
  return { zone: best, mul: bestMul, dmg: body };
}

// Target::onHit (player hit landed): resolve the 3-hitzone model, then apply
// the crit/HP chain. The mock's single entry point for player damage.
function monsterOnHit(g, base, hx, hy) {
  const hit = zoneHitResolve(g, base, PHYS_BIT_BY_WEAPON[g.weapon], hx, hy);
  damageMonster(g, hit.dmg, hx, hy);
}

function damageMonster(g, dmg, hx, hy) {
  const m = g.monster;
  if (m.state === 'dead') return;
  const cx = m.x + (m.w >> 1);
  const cy = m.y + (m.h >> 1);
  const proj = ((hx - cx) * m.face.x + (hy - cy) * m.face.y) >> 4;
  const crit = proj > 3;
  const total = Math.max(1, (dmg * (crit ? 14 : 10)) / 10 | 0);
  m.hp -= total;
  m.hitFlash = 4;
  g.freeze = Math.max(g.freeze, crit ? 6 : 4);
  g.shake = Math.max(g.shake, crit ? 3.4 : 2.4);
  g.effects.push({ x: hx, y: hy, t: 0, life: 7, crit });
  if (m.hp <= 0) {
    m.hp = 0;
    m.state = 'dead';
    g.over = 'win';
    g.freeze = 12;
    g.shake = 5;
  }
}

function knockMonsterAway(g, m, cx, cy, amt) {
  const dx = (m.x + (m.w >> 1)) - cx;
  const dy = (m.y + (m.h >> 1)) - cy;
  const d = DIR8[dirIndexFromDelta(dx, dy)];
  addMove(m, d.x, d.y, amt);
  clampMonster(g);
}

function pushApart(g) {
  const p = g.player;
  const m = g.monster;
  if (m.state === 'dead') return;
  const tr = targetRect(g);
  if (!tr) return;
  if (!rectsOverlap(p, tr)) return;

  // pole never moves; lunging beast shoves you; otherwise the beast gives way
  const shovePlayer = g.mode === 'train' ||
    (g.mode === 'hunt' && (m.state === 'attack' || m.state === 'windup'));
  const a = shovePlayer ? p : tr;
  const b = shovePlayer ? tr : p;
  const ox = Math.min(a.x + a.w - b.x, b.x + b.w - a.x);
  const oy = Math.min(a.y + a.h - b.y, b.y + b.h - a.y);
  if (ox < oy) {
    const d = (a.x + (a.w >> 1)) < (b.x + (b.w >> 1)) ? -ox : ox;
    if (shovePlayer) p.x += d; else m.x += d;
  } else {
    const d = (a.y + (a.h >> 1)) < (b.y + (b.h >> 1)) ? -oy : oy;
    if (shovePlayer) p.y += d; else m.y += d;
  }
}

function activeTarget(g) {
  if (g.mode === 'train') return g.pole;
  return g.monster.state === 'dead' ? null : g.monster;
}

// Body-collision rect: the creature's collide box (legs-only for the chicken)
// anchored at m.x/m.y, or the body box when the def has none.
function targetRect(g) {
  if (g.mode === 'train') return g.pole;
  const m = g.monster;
  if (m.state === 'dead') return null;
  const c = m.collide || { ox: 0, oy: 0, w: m.w, h: m.h };
  return { x: m.x + c.ox, y: m.y + c.oy, w: c.w, h: c.h };
}

function updatePole(g) {
  if (g.pole.hitFlash > 0) g.pole.hitFlash--;
}

function damagePole(g, dmg, hx, hy) {
  const pole = g.pole;
  // PLAIN keeps its x1.4 head crit (its mul-140 head zone, unbreakable).
  // Variants carry one body-mul whole-pole zone instead, so no hit crits.
  const crit = pole.kind === 0 && hy < pole.y + 16;
  const total = Math.max(1, (dmg * (crit ? 14 : 10)) / 10 | 0);
  pole.hitFlash = 4;
  g.freeze = Math.max(g.freeze, crit ? 5 : 4);
  g.shake = Math.max(g.shake, crit ? 3 : 2.2);
  g.train.total += total;
  g.train.last = total;
  g.train.events.push({ tick: g.tick, dmg: total });
  g.effects.push({ x: hx, y: hy - 6, t: 0, life: 26, crit, text: String(total) });
  return total;
}

// Zone resolve for breakable poles: point-in-zone (half-open, facing-free) and
// the shared pool drain (any weapon drains). Pool 0 -> broken, the hurt rect
// refreshes (BREAK shrinks) and a small spark burst + freeze fires.
function poleOnHit(g, dmg, hx, hy) {
  const total = damagePole(g, dmg, hx, hy);
  const pole = g.pole;
  const def = POLE_DEFS[pole.kind] || POLE_DEFS[0];
  if (pole.kind === 0 || pole.broken || !def.z) return;
  const zx = pole.x + def.z.x;
  const zy = pole.y + def.z.y;
  if (hx < zx || hx >= zx + def.z.w || hy < zy || hy >= zy + def.z.h) return;
  if (total < pole.hp) { pole.hp -= total; return; }
  pole.hp = 0;
  pole.broken = 1;
  g.effects.push({ x: zx + (def.z.w >> 1), y: zy + (def.z.h >> 1), t: 0, life: 6, crit: false, text: '' });
  g.effects.push({ x: zx + (def.z.w >> 1) - 5, y: zy + (def.z.h >> 1) + 3, t: 0, life: 7, crit: false, text: '' });
  g.effects.push({ x: zx + (def.z.w >> 1) + 5, y: zy + (def.z.h >> 1) - 3, t: 0, life: 7, crit: false, text: '' });
  g.freeze = Math.max(g.freeze, 6);
  pole.w = def.brokenW;
  pole.h = def.brokenH;
}

function trainDps(g) {
  const cutoff = g.tick - 600;
  let sum = 0;
  for (const e of g.train.events) if (e.tick > cutoff) sum += e.dmg;
  return Math.round(sum / 10); // last 10 seconds
}

function clampMonster(g) {
  const m = g.monster;
  m.x = Math.max(0, Math.min(WORLD_W - m.w, m.x));
  m.y = Math.max(0, Math.min(WORLD_H - m.h, m.y));
}

/* ------------------------------------------------- projectiles / effects */

function updateProjectiles(g) {
  for (let i = g.projectiles.length - 1; i >= 0; i--) {
    const pr = g.projectiles[i];
    // projectile x/y are 1/16 px already and vx/vy too: add straight.
    // Do NOT route through the pixel-domain accumulator or shots crawl.
    pr.x += pr.vx;
    pr.y += pr.vy;
    pr.life--;
    const tr = targetRect(g);
    if (tr) {
      const r = { x: (pr.x >> 4) - (pr.w >> 1), y: (pr.y >> 4) - (pr.h >> 1), w: pr.w, h: pr.h };
      if (rectsOverlap(r, tr)) {
        if (g.mode === 'train') poleOnHit(g, pr.dmg, r.x + (r.w >> 1), r.y + (r.h >> 1));
        else monsterOnHit(g, pr.dmg, r.x + (r.w >> 1), r.y + (r.h >> 1));
        g.projectiles.splice(i, 1);
        continue;
      }
    }
    if (pr.life <= 0 ||
        pr.x < -(8 << 4) || pr.x > (WORLD_W + 8) << 4 ||
        pr.y < -(8 << 4) || pr.y > (WORLD_H + 8) << 4) {
      g.projectiles.splice(i, 1);
    }
  }
}

function updateEffects(g) {
  for (let i = g.effects.length - 1; i >= 0; i--) {
    const e = g.effects[i];
    e.t++;
    if (e.t >= e.life) g.effects.splice(i, 1);
  }
}

/* ------------------------------------------------------------------ helpers */

function rectsOverlap(a, b) {
  return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

function circleRectOverlap(cx, cy, r, rect) {
  const nx = Math.max(rect.x, Math.min(cx, rect.x + rect.w));
  const ny = Math.max(rect.y, Math.min(cy, rect.y + rect.h));
  const dx = cx - nx;
  const dy = cy - ny;
  return dx * dx + dy * dy <= r * r;
}

function meleeHitbox(p, a) {
  const cx = p.x + (p.w >> 1) + ((p.fx * a.reach) >> 4);
  const cy = p.y + (p.h >> 1) + ((p.fy * a.reach) >> 4);
  return { x: cx - (a.hw >> 1), y: cy - (a.hh >> 1), w: a.hw, h: a.hh };
}

/* ------------------------------------------------------------------ font */

const FONT = {
  A: [0b010, 0b101, 0b111, 0b101, 0b101],
  B: [0b110, 0b101, 0b110, 0b101, 0b110],
  C: [0b011, 0b100, 0b100, 0b100, 0b011],
  D: [0b110, 0b101, 0b101, 0b101, 0b110],
  E: [0b111, 0b100, 0b110, 0b100, 0b111],
  F: [0b111, 0b100, 0b110, 0b100, 0b100],
  G: [0b011, 0b100, 0b101, 0b101, 0b011],
  H: [0b101, 0b101, 0b111, 0b101, 0b101],
  I: [0b111, 0b010, 0b010, 0b010, 0b111],
  J: [0b001, 0b001, 0b001, 0b101, 0b010],
  K: [0b101, 0b101, 0b110, 0b101, 0b101],
  L: [0b100, 0b100, 0b100, 0b100, 0b111],
  M: [0b101, 0b111, 0b111, 0b101, 0b101],
  N: [0b101, 0b111, 0b101, 0b101, 0b101],
  O: [0b010, 0b101, 0b101, 0b101, 0b010],
  P: [0b110, 0b101, 0b110, 0b100, 0b100],
  Q: [0b010, 0b101, 0b101, 0b110, 0b011],
  R: [0b110, 0b101, 0b110, 0b101, 0b101],
  S: [0b011, 0b100, 0b010, 0b001, 0b110],
  T: [0b111, 0b010, 0b010, 0b010, 0b010],
  U: [0b101, 0b101, 0b101, 0b101, 0b011],
  V: [0b101, 0b101, 0b101, 0b101, 0b010],
  W: [0b101, 0b101, 0b111, 0b111, 0b101],
  X: [0b101, 0b101, 0b010, 0b101, 0b101],
  Y: [0b101, 0b101, 0b010, 0b010, 0b010],
  Z: [0b111, 0b001, 0b010, 0b100, 0b111],
  0: [0b111, 0b101, 0b101, 0b101, 0b111],
  1: [0b010, 0b110, 0b010, 0b010, 0b111],
  2: [0b111, 0b001, 0b111, 0b100, 0b111],
  3: [0b111, 0b001, 0b011, 0b001, 0b111],
  4: [0b101, 0b101, 0b111, 0b001, 0b001],
  5: [0b111, 0b100, 0b111, 0b001, 0b111],
  6: [0b111, 0b100, 0b111, 0b101, 0b111],
  7: [0b111, 0b001, 0b001, 0b001, 0b001],
  8: [0b111, 0b101, 0b111, 0b101, 0b111],
  9: [0b111, 0b101, 0b111, 0b001, 0b111],
  ' ': [0, 0, 0, 0, 0],
  ':': [0, 0b010, 0, 0b010, 0],
  '-': [0, 0, 0b111, 0, 0],
  '.': [0, 0, 0, 0, 0b010],
  '/': [0b001, 0b001, 0b010, 0b100, 0b100],
};

function textWidth(str, scale) {
  scale = scale || 1;
  return str.length * 4 * scale - scale;
}

function drawText(ctx, str, x, y, shade, scale) {
  scale = scale || 1;
  ctx.fillStyle = SHADES[shade];
  let cx = x;
  for (const ch of String(str).toUpperCase()) {
    const glyph = FONT[ch] || FONT[' '];
    for (let row = 0; row < 5; row++) {
      const bits = glyph[row];
      for (let col = 0; col < 3; col++) {
        if (bits & (4 >> col)) ctx.fillRect(cx + col * scale, y + row * scale, scale, scale);
      }
    }
    cx += 4 * scale;
  }
}

function centerX(str, scale) {
  return Math.max(0, Math.round((W - textWidth(str, scale)) / 2));
}

/* ------------------------------------------------------------------ render */

function render(ctx, g, opts) {
  opts = opts || {};
  const def = WEAPON_DEFS[g.weapon];

  let sx = 0;
  let sy = 0;
  if (g.shake > 0.2) {
    sx = Math.round(Math.sin(g.tick * 1.7) * g.shake);
    sy = Math.round(Math.cos(g.tick * 2.3) * g.shake * 0.7);
  }

  const camX = Math.round(g.cam.x);
  const camY = Math.round(g.cam.y);
  ctx.save();
  ctx.translate(sx - camX, sy - camY);
  ctx.fillStyle = SHADES[0];
  ctx.fillRect(camX - 8, camY - 8, W + 16, H + 16);

  drawArena(ctx);
  if (g.mode === 'train') drawPole(ctx, g);
  else drawMonster(ctx, g);
  drawPlayer(ctx, g, def);
  drawProjectiles(ctx, g);
  drawEffects(ctx, g);
  if (opts.debug) drawDebug(ctx, g);
  ctx.restore();

  drawHud(ctx, g, def);
  drawOverlays(ctx, g, opts);
}

function drawArena(ctx) {
  ctx.fillStyle = SHADES[1];
  for (let i = 0; i < 260; i++) {
    if ((i * 7) % 3 === 0) continue;
    ctx.fillRect((i * 53) % WORLD_W, (i * 29) % WORLD_H, 1, 1);
  }
  ctx.fillStyle = SHADES[2];
  strokeRect(ctx, 0, 0, WORLD_W, WORLD_H);
}

function drawPlayer(ctx, g, def) {
  const p = g.player;
  const x = Math.round(p.x);
  const y = Math.round(p.y);
  const cx = x + 8;
  const cy = y + 8;
  const bodyShade = p.state === 'dodge' ? 2 : 3;

  ctx.fillStyle = SHADES[1];
  ctx.fillRect(x + 2, y + p.h - 1, p.w - 4, 1);

  // body
  ctx.fillStyle = SHADES[bodyShade];
  ctx.fillRect(x + 5, y + 1, 6, 6);
  ctx.fillRect(x + 4, y + 7, 8, 6);
  ctx.fillRect(x + 5, y + 13, 2, 2);
  ctx.fillRect(x + 9, y + 13, 2, 2);

  if (p.state === 'charge') {
    // charge meter above the hunter: fills to CHARGE_L2, flashes white at max
    const frac = Math.min(1, p.chargeT / CHARGE_L2);
    ctx.fillStyle = SHADES[p.chargeT >= CHARGE_L2 ? 3 : 2];
    ctx.fillRect(cx - 8, y - 4, Math.max(1, Math.round(16 * frac)), 2);
  }

  if (p.sheathed) {
    // stowed: weapon overlay hidden (body + head only)
  } else if (def.id === 'sword') {
    if (p.state === 'attack' || p.state === 'special') {
      const a = p.atk;
      const phase = p.t < a.startup ? 0 : p.t < a.startup + a.active ? 1 : 2;
      const reach = phase === 1 ? a.reach : a.reach * 0.6;
      const hx = cx + ((p.fx * reach) >> 4);
      const hy = cy + ((p.fy * reach) >> 4);
      ctx.fillStyle = SHADES[phase === 1 ? 2 : 1];
      ctx.fillRect(hx - Math.round(a.hw / 2), hy - Math.round(a.hh / 2), a.hw, a.hh);
      ctx.fillStyle = SHADES[3];
      ctx.fillRect(hx - 2, hy - 2, 4, 4);
      if (p.state === 'special' && p.riposteT > 0) {
        ctx.fillStyle = SHADES[2];
        ctx.fillRect(hx - Math.round(a.hw / 2) - 2, hy - Math.round(a.hh / 2) - 2, a.hw + 4, a.hh + 4);
      }
    } else if (p.stance === 'parry') {
      ctx.fillStyle = SHADES[3];
      ctx.fillRect(cx - 1, cy - 12, 2, 14);
      ctx.fillStyle = SHADES[2];
      ctx.fillRect(cx - 3, cy - 14, 6, 2);
    } else {
      ctx.fillStyle = SHADES[3];
      ctx.fillRect(cx + ((p.fx * 7) >> 4) - 1, cy + ((p.fy * 7) >> 4) - 1, 3, 3);
    }
  } else if (def.id === 'flail') {
    if (p.stance === 'whirl') {
      const ang = p.whirlTick * 0.35;
      for (let i = 0; i < 6; i++) {
        const a2 = ang + i * (Math.PI / 3);
        ctx.fillStyle = SHADES[2];
        ctx.fillRect(Math.round(cx + Math.cos(a2) * 20), Math.round(cy + Math.sin(a2) * 14), 2, 2);
      }
      ctx.fillStyle = SHADES[3];
      ctx.fillRect(Math.round(cx + Math.cos(p.whirlTick * 0.55) * 20) - 2,
                   Math.round(cy + Math.sin(p.whirlTick * 0.55) * 14) - 2, 4, 4);
    } else if (p.state === 'attack' || p.state === 'special') {
      const a = p.atk;
      const phase = p.t < a.startup ? 0 : p.t < a.startup + a.active ? 1 : 2;
      const reach = phase === 1 ? a.reach : a.reach * 0.5;
      ctx.fillStyle = SHADES[2];
      for (let i = 1; i <= 3; i++) {
        const rr = (reach * i) >> 2;
        ctx.fillRect(cx + ((p.fx * rr) >> 4), cy + ((p.fy * rr) >> 4), 1, 1);
      }
      ctx.fillStyle = SHADES[3];
      ctx.fillRect(cx + ((p.fx * reach) >> 4) - 2, cy + ((p.fy * reach) >> 4) - 2, 4, 4);
    } else {
      ctx.fillStyle = SHADES[2];
      ctx.fillRect(cx + ((p.fx * 4) >> 4), cy + ((p.fy * 4) >> 4), 1, 1);
      ctx.fillStyle = SHADES[3];
      ctx.fillRect(cx + ((p.fx * 9) >> 4) - 1, cy + ((p.fy * 9) >> 4) - 1, 3, 3);
    }
    if (p.state === 'deflect') {
      ctx.fillStyle = SHADES[2];
      ctx.fillRect(x - 2, y + 2, 1, 12);
      ctx.fillRect(x + p.w + 1, y + 2, 1, 12);
    }
  } else {
    const guard = p.stance === 'guard';
    const shx = cx + ((p.fx * 5) >> 4);
    const shy = cy + ((p.fy * 5) >> 4);
    ctx.fillStyle = SHADES[guard ? 3 : 2];
    ctx.fillRect(shx - 5, shy - 7, 10, 14);
    ctx.fillStyle = SHADES[0];
    ctx.fillRect(shx - 1, shy - 7, 2, 14);
    if (p.state === 'shove') {
      ctx.fillStyle = SHADES[3];
      ctx.fillRect(shx + ((p.fx * 4) >> 4) - 5, shy + ((p.fy * 4) >> 4) - 7, 10, 14);
    }
    if (p.reload > 0) {
      ctx.fillStyle = SHADES[2];
      ctx.fillRect(x + 3, y - 3, 10, 2);
    }
  }

  if (p.iT > 0 && (g.tick % 4) < 2) {
    ctx.fillStyle = SHADES[0];
    ctx.fillRect(x + 6, y + 3, 4, 1);
  }
  if (p.state === 'stun') {
    ctx.fillStyle = SHADES[3];
    const a2 = g.tick * 0.3;
    ctx.fillRect(Math.round(cx + Math.cos(a2) * 7), y - 2 + Math.round(Math.sin(a2) * 2), 2, 2);
  }
}

function drawMonster(ctx, g) {
  const m = g.monster;
  const x = Math.round(m.x);
  const y = Math.round(m.y);

  if (m.state === 'dead') {
    ctx.fillStyle = SHADES[1];
    ctx.fillRect(x, y + 16, m.w, 8);
    ctx.fillStyle = SHADES[2];
    ctx.fillRect(x + m.w / 2 - 4, y + 14, 8, 4);
    return;
  }

  const flashing = m.state === 'windup' && (Math.floor((m.windupMax - m.t) / 4) % 2 === 0);
  let body = 1;
  if (m.state === 'recover') body = 2;
  if (m.hitFlash > 0) body = 3;
  if (flashing) body = 3;

  // nch.3/nch.5: heavy's locked tail_spin rotates the whole creature about its
  // centre in 45-deg steps from the locked facing (nch.3), and holds the locked
  // away frame through the windup (nch.5) so the beast looks away with the tail
  // at the hunter. Only the body rotates; the stun sparkle and the telegraph
  // core stay world-aligned (drawn after restore).
  const spinFrame = monsterSpinFrame(m);
  const spinDraw = spinFrame >= 0;
  if (spinDraw) {
    ctx.save();
    ctx.translate(x + m.w / 2, y + m.h / 2);
    ctx.rotate(spinFrame * Math.PI / 4);
    ctx.translate(-(x + m.w / 2), -(y + m.h / 2));
  }

  ctx.fillStyle = SHADES[0];
  ctx.fillRect(x + 2, y + m.h - 1, m.w - 4, 1);
  for (let i = 0; i < 4; i++) ctx.fillRect(x + 3 + i * 8, y + m.h - 3, 3, 3);

  ctx.fillStyle = SHADES[body];
  ctx.fillRect(x + 2, y + 4, m.w - 4, 14);
  ctx.fillRect(x + 6, y + 1, m.w - 12, 6);

  const headX = m.face.x >= 0 ? x + m.w - 10 : x;
  ctx.fillStyle = SHADES[flashing ? 3 : (m.state === 'recover' ? 2 : 3)];
  ctx.fillRect(headX, y + 6, 10, 12);
  ctx.fillStyle = SHADES[0];
  ctx.fillRect(m.face.x >= 0 ? headX + 7 : headX + 1, y + 13, 2, 2);
  ctx.fillRect(headX + 4, y + 9, 2, 2);

  if (spinDraw) ctx.restore();

  if (m.stun > 0) {
    ctx.fillStyle = SHADES[2];
    const a = g.tick * 0.35;
    ctx.fillRect(Math.round(x + m.w / 2 + Math.cos(a) * 9), y - 3 + Math.round(Math.sin(a) * 2), 2, 2);
  }

  if (m.state === 'windup' || m.state === 'attack') {
    const a = m.atk;
    // Core marker at the window/hit centre. Window-path attacks use the real
    // face-relative box centre; legacy reach attacks keep the published scalar
    // projection. The full-window box fill is gone (nch.2: it read as a debug
    // hurt zone) -- only the core tell remains.
    let ax, ay;
    const win = monsterTellWindow(m, a);
    if (win) {
      const d = facePoint(m.face.x, m.face.y, win.ox, win.oy);
      ax = x + m.w / 2 + d.x;
      ay = y + m.h / 2 + d.y;
    } else {
      ax = x + m.w / 2 + m.face.x * a.reach;
      ay = y + m.h / 2 + m.face.y * a.reach;
    }
    if (m.state === 'windup') {
      ctx.fillStyle = SHADES[2];
      ctx.fillRect(Math.round(ax) - 1, Math.round(ay) - 1, 2, 2);
    } else {
      ctx.fillStyle = SHADES[3];
      ctx.fillRect(Math.round(ax) - 2, Math.round(ay) - 2, 4, 4);
    }
  }
}

// Damage-stage selection for the breakable pole art (bead monhun-ardu-6zb.7),
// mirroring src/core/projectiles.hpp poleDamageStage(): 0 intact, 1 damaged
// (pool at or below half), 2 broken. Kind 0 (PLAIN) has no zone and is always
// stage 0 (it draws its own legacy pole).
function poleStage(pole, def) {
  if (pole.broken) return 2;
  if (pole.kind !== 0 && pole.hp * 2 <= def.pool) return 1;
  return 0;
}

// Neutral fracture marker (tools/gen-art.py _fracture mirror): a 7-px jagged
// crack burst in `shade`; chipped drops the two far-end pixels (5 px) for the
// damaged stage. All three 6zb.10 parts are LIGHT, so the marker is BLACK (0)
// on every part face.
const POLE_FRACTURE = [[2, 0], [1, 1], [1, 2], [2, 2], [2, 3], [3, 3], [3, 4]];
// BREAK's curved horn (tools/gen-art.py _BREAK_HORN mirror): [x, y, w] LIGHT
// runs rising out of the upper post to the right, in the 24 px variant frame.
const POLE_HORN = [
  [18, 1, 4], [18, 2, 5], [17, 3, 5], [17, 4, 6], [16, 5, 6], [16, 6, 6],
  [15, 7, 6], [15, 8, 6], [14, 9, 6], [13, 10, 6], [12, 11, 6], [11, 12, 6],
  [10, 13, 6],
];

function poleFracture(ctx, x, y, chipped, shade) {
  ctx.fillStyle = SHADES[shade];
  for (let i = 0; i < POLE_FRACTURE.length; i++) {
    if (chipped && (i === 0 || i === 6)) continue;
    ctx.fillRect(x + POLE_FRACTURE[i][0], y + POLE_FRACTURE[i][1], 1, 1);
  }
}

// 16-px DARK post + ring bands, centered in the 24 px variant frame.
function polePost(ctx, x, y) {
  ctx.fillStyle = SHADES[1];
  ctx.fillRect(x + 4, y + 12, 16, 24);
  ctx.fillStyle = SHADES[0];
  for (let i = 0; i < 3; i++) ctx.fillRect(x + 4, y + 20 + i * 7, 16, 1);
}

function poleGround(ctx, x, y) {
  ctx.fillStyle = SHADES[0];
  ctx.fillRect(x, y + 34, 24, 2);
}

// A detached part lying on the ground: LIGHT body + BLACK cut edge + fracture.
function polePiece(ctx, x, y, px, py, w, h, head) {
  ctx.fillStyle = SHADES[head];
  ctx.fillRect(x + px, y + py, w, h);
  ctx.fillStyle = SHADES[0];
  ctx.fillRect(x + px, y + py, w, 1);
  ctx.fillRect(x + px + 4, y + py + h - 1, 2, 1);
  ctx.fillRect(x + px + w - 2, y + py + 1, 1, 1);
}

function drawPole(ctx, g) {
  const pole = g.pole;
  const kind = pole.kind | 0;
  const def = POLE_DEFS[kind] || POLE_DEFS[0];
  const head = pole.hitFlash > 0 ? 3 : 2;

  // PLAIN keeps the legacy 20x40 draw exactly (no zone, no stage).
  if (kind === 0) {
    const x = Math.round(pole.x);
    const y = Math.round(pole.y);
    ctx.fillStyle = SHADES[1];
    ctx.fillRect(x + 2, y + 12, 16, 24);
    ctx.fillStyle = SHADES[0];
    for (let i = 0; i < 3; i++) ctx.fillRect(x + 2, y + 20 + i * 7, 16, 1);
    ctx.fillStyle = SHADES[head];
    ctx.fillRect(x, y, 20, 16);
    ctx.fillStyle = SHADES[0];
    ctx.fillRect(x + 8, y + 5, 4, 4);
    ctx.fillRect(x - 2, y + 34, 20 + 4, 2);
    return;
  }

  // Variant sheets are 24 px wide with a baked 2 px margin so the 24 px part
  // stays centered on the 20 px rect: draw the sheet 2 px left of the anchor.
  const x = Math.round(pole.x) - 2;
  const y = Math.round(pole.y);
  const stage = poleStage(pole, def);

  if (kind === 1) {
    // SEVER: 24 px cap on the post; broken = jagged stump + cap on the ground.
    if (stage === 2) {
      for (let k = 0; k < 4; k++) {
        const sx = 4 + k * 4, top = 13 + k * 2;
        ctx.fillStyle = SHADES[1];
        ctx.fillRect(x + sx, y + top, 4, 36 - top);
        ctx.fillStyle = SHADES[0];
        ctx.fillRect(x + sx, y + top, 4, 1);
      }
      polePiece(ctx, x, y, 3, 36, 14, 4, head);
    } else {
      polePost(ctx, x, y);
      ctx.fillStyle = SHADES[head];
      ctx.fillRect(x, y, 24, 14);
      ctx.fillStyle = SHADES[3];
      ctx.fillRect(x + 2, y + 11, 20, 2);
      ctx.fillStyle = SHADES[0];
      ctx.fillRect(x, y + 14, 24, 1);      // seam where the cap meets the post
      poleFracture(ctx, x + 9, y + 3, stage === 1, 0);
      if (stage === 1) {
        ctx.fillStyle = SHADES[0];
        ctx.fillRect(x + 16, y + 2, 1, 4);
        ctx.fillRect(x + 17, y + 3, 1, 2);
        ctx.fillRect(x + 5, y + 8, 1, 3);
      }
    }
  } else if (kind === 2) {
    // BREAK: thick curved horn out of the upper post; broken = base stub + horn
    // on the ground.
    polePost(ctx, x, y);
    if (stage === 2) {
      ctx.fillStyle = SHADES[head];
      ctx.fillRect(x + 10, y + 11, 6, 3);
      ctx.fillRect(x + 12, y + 14, 2, 1);
      ctx.fillStyle = SHADES[0];
      ctx.fillRect(x + 10, y + 11, 6, 1);
      polePiece(ctx, x, y, 2, 36, 13, 4, head);
      ctx.fillStyle = SHADES[head];
      ctx.fillRect(x + 15, y + 37, 6, 2);
      ctx.fillStyle = SHADES[0];
      ctx.fillRect(x + 15, y + 37, 6, 1);
    } else {
      ctx.fillStyle = SHADES[head];
      for (const [hx0, hy0, hw] of POLE_HORN) ctx.fillRect(x + hx0, y + hy0, hw, 1);
      ctx.fillStyle = SHADES[3];
      for (const [hx0, hy0] of POLE_HORN) ctx.fillRect(x + hx0, y + hy0, 1, 1);
      ctx.fillStyle = SHADES[0];
      ctx.fillRect(x + 10, y + 13, 8, 1);  // seam where the horn meets the post
      poleFracture(ctx, x + 16, y + 5, stage === 1, 0);
      if (stage === 1) {
        ctx.fillStyle = SHADES[0];
        ctx.fillRect(x + 20, y + 3, 1, 3);
        ctx.fillRect(x + 13, y + 10, 1, 3);
      }
    }
  } else if (kind === 3) {
    // CRACK: 24 px collar ringing the post; broken = split collar with a
    // displaced chunk on the post + one on the ground.
    polePost(ctx, x, y);
    if (stage === 2) {
      ctx.fillStyle = SHADES[head];
      ctx.fillRect(x, y + 16, 24, 4);
      ctx.fillRect(x + 3, y + 26, 21, 4);
      ctx.fillStyle = SHADES[0];
      ctx.fillRect(x, y + 16, 24, 1);
      ctx.fillRect(x + 3, y + 26, 21, 1);
      const teeth = [[2, 21], [6, 22], [10, 23], [14, 22], [18, 21], [21, 23]];
      for (const [tx, ty] of teeth) ctx.fillRect(x + tx, y + ty, 1, 1);
      polePiece(ctx, x, y, 3, 36, 14, 4, head);
    } else {
      ctx.fillStyle = SHADES[head];
      ctx.fillRect(x, y + 18, 24, 12);
      ctx.fillStyle = SHADES[3];
      ctx.fillRect(x + 1, y + 19, 22, 2);
      ctx.fillStyle = SHADES[0];
      ctx.fillRect(x, y + 18, 24, 1);
      ctx.fillRect(x, y + 29, 24, 1);
      poleFracture(ctx, x + 10, y + 21, stage === 1, 0);
      if (stage === 1) {
        ctx.fillStyle = SHADES[0];
        ctx.fillRect(x + 3, y + 21, 1, 4);
        ctx.fillRect(x + 19, y + 23, 1, 3);
      }
    }
  }
  poleGround(ctx, x, y);
}

function drawProjectiles(ctx, g) {
  for (const pr of g.projectiles) {
    const x = pr.x >> 4;
    const y = pr.y >> 4;
    const bx = (pr.vx * 2) >> 4;
    const by = (pr.vy * 2) >> 4;

    // smoke trail behind the shot
    ctx.fillStyle = SHADES[1];
    ctx.fillRect(x - bx * 2 - 1, y - by * 2 - 1, 2, 2);
    ctx.fillRect(x - bx * 3 - 1, y - by * 3 - 1, 2, 2);
    ctx.fillStyle = SHADES[2];
    ctx.fillRect(x - bx - 1, y - by - 1, 2, 2);

    const hw = pr.w >> 1;
    const hh = pr.h >> 1;
    if (pr.heavy) {
      // cannonball: gray rim, white core, dark base
      ctx.fillStyle = SHADES[2];
      ctx.fillRect(x - hw, y - hh, pr.w, pr.h);
      ctx.fillStyle = SHADES[3];
      ctx.fillRect(x - hw + 1, y - hh + 1, pr.w - 2, pr.h - 2);
      ctx.fillStyle = SHADES[0];
      ctx.fillRect(x - hw, y + hh - 2, pr.w, 1);
    } else {
      // scatter pellet: bright nose, gray tail
      ctx.fillStyle = SHADES[2];
      ctx.fillRect(x - hw, y - hh, pr.w, pr.h);
      ctx.fillStyle = SHADES[3];
      ctx.fillRect(x - hw + 1, y - hh + 1, 2, 2);
    }
  }
}

function drawEffects(ctx, g) {
  for (const e of g.effects) {
    const r = e.life - e.t;
    if (e.text) {
      drawText(ctx, e.text, Math.round(e.x) - 2, Math.round(e.y - (e.life - e.t) / 3), e.crit ? 3 : 2, 1);
      continue;
    }
    ctx.fillStyle = SHADES[e.crit ? 3 : 2];
    ctx.fillRect(Math.round(e.x - r), Math.round(e.y), 1, 1);
    ctx.fillRect(Math.round(e.x + r), Math.round(e.y - 1), 1, 1);
    ctx.fillRect(Math.round(e.x), Math.round(e.y - r), 1, 1);
    ctx.fillRect(Math.round(e.x), Math.round(e.y + r), 1, 1);
  }
}

function drawHud(ctx, g, def) {
  const p = g.player;
  const m = g.monster;

  ctx.fillStyle = SHADES[0];
  ctx.fillRect(0, ARENA_H, W, HUD_H);
  ctx.fillStyle = SHADES[1];
  ctx.fillRect(0, ARENA_H, W, 1);

  bar(ctx, 2, ARENA_H + 2, 40, 4, p.hp / p.hpMax, 3);
  bar(ctx, 46, ARENA_H + 2, 30, 4, p.stam / p.stamMax, 2);
  drawText(ctx, def.name, 80, ARENA_H + 3, 3, 1);

  if (def.id === 'gunshield') {
    const sh = def.shells[p.shell];
    const label = p.reload > 0 ? 'RLD' : sh.name[0] + p.shells[p.shell];
    drawText(ctx, label, 112, ARENA_H + 3, p.reload > 0 ? 1 : 3, 1);
    if (p.reload > 0) {
      const w2 = Math.round(12 * (1 - p.reload / sh.reload));
      ctx.fillStyle = SHADES[2];
      ctx.fillRect(112, ARENA_H + 6, Math.max(1, w2), 1);
    }
  }

  if (g.mode === 'train') {
    const last = 'LAST ' + g.train.last;
    const dps = 'DPS ' + trainDps(g);
    drawText(ctx, last, W - 2 - textWidth(last, 1), 2, 3, 1);
    drawText(ctx, dps, W - 2 - textWidth(dps, 1), 10, 2, 1);
  } else {
    bar(ctx, W - 52, 2, 48, 3, m.hp / m.hpMax, 3);
  }
}

function bar(ctx, x, y, w, h, ratio, shade) {
  ctx.fillStyle = SHADES[1];
  ctx.fillRect(x, y, w, h);
  const fw = Math.max(0, Math.round((w - 2) * Math.max(0, Math.min(1, ratio))));
  ctx.fillStyle = SHADES[shade];
  ctx.fillRect(x + 1, y + 1, fw, h - 2);
}

function drawOverlays(ctx, g, opts) {
  if (g.over) {
    dimScreen(ctx);
    const msg = g.over === 'win' ? 'CARVED' : 'HUNT FAIL';
    const sub = 'PRESS R RESET';
    drawText(ctx, msg, centerX(msg, 2), 18, 3, 2);
    drawText(ctx, sub, centerX(sub, 1), 40, 2, 1);
  }
  if (opts.paused) {
    dimScreen(ctx);
    const s = 'PAUSED';
    drawText(ctx, s, centerX(s, 2), 24, 3, 2);
  }
  if (opts.slow) drawText(ctx, 'SLOW', 2, 2, 2, 1);
  if (opts.debug) drawText(ctx, 'DBG', 2, 10, 2, 1);
  // sheathe + debounce prototype readout (cycle with [ / ] and , / .)
  const sv = 'SHEATH ' + getSheatheVariant().toUpperCase() + (g.player.sheathed ? ' ON' : ' OFF');
  drawText(ctx, sv, W - 2 - textWidth(sv, 1), 10, g.player.sheathed ? 3 : 2, 1);
  const dbg = 'LOCK ' + getDebounceProfile().name
    + (g.player.chainLock > 0 ? ' ' + g.player.chainLock : '')
    + (g.player.bBuffer > 0 ? ' B' + g.player.bBuffer : '');
  drawText(ctx, dbg, W - 2 - textWidth(dbg, 1), 18, (g.player.chainLock > 0 || g.player.bBuffer > 0) ? 3 : 2, 1);
}

function dimScreen(ctx) {
  ctx.fillStyle = SHADES[0];
  for (let y = 0; y < ARENA_H; y += 2) {
    for (let x = (y % 4) ? 1 : 0; x < W; x += 2) ctx.fillRect(x, y, 1, 1);
  }
}

function strokeRect(ctx, x, y, w, h) {
  x = Math.round(x);
  y = Math.round(y);
  w = Math.round(w);
  h = Math.round(h);
  ctx.fillRect(x, y, w, 1);
  ctx.fillRect(x, y + h - 1, w, 1);
  ctx.fillRect(x, y, 1, h);
  ctx.fillRect(x + w - 1, y, 1, h);
}

function drawDebug(ctx, g) {
  const p = g.player;
  const tgt = activeTarget(g);

  // hurt boxes = red
  wire(ctx, p.x, p.y, p.w, p.h, '#ff4444');
  if (tgt) wire(ctx, tgt.x, tgt.y, tgt.w, tgt.h, '#ff4444');

  // hit boxes = blue
  if ((p.state === 'attack' || p.state === 'special') && p.atk) {
    const a = p.atk;
    const hx = p.x + (p.w >> 1) + ((p.fx * a.reach) >> 4);
    const hy = p.y + (p.h >> 1) + ((p.fy * a.reach) >> 4);
    wire(ctx, hx - (a.hw >> 1), hy - (a.hh >> 1), a.hw, a.hh, '#4488ff');
  }
  if (g.mode === 'hunt' && (g.monster.state === 'windup' || g.monster.state === 'attack') && g.monster.atk) {
    const a = g.monster.atk;
    const m = g.monster;
    const hx = m.x + (m.w >> 1) + ((m.face.x * a.reach) >> 4);
    const hy = m.y + (m.h >> 1) + ((m.face.y * a.reach) >> 4);
    wire(ctx, hx - (a.hw >> 1), hy - (a.hh >> 1), a.hw, a.hh, '#4488ff');
  }
  for (const pr of g.projectiles) {
    wire(ctx, (pr.x >> 4) - (pr.w >> 1), (pr.y >> 4) - (pr.h >> 1), pr.w, pr.h, '#4488ff');
  }
  if (p.stance === 'whirl') {
    const cx = p.x + (p.w >> 1);
    const cy = p.y + (p.h >> 1);
    wire(ctx, cx - 24, cy - 24, 48, 48, '#4488ff');
  }

  drawText(ctx, p.state.toUpperCase().slice(0, 7), 2, ARENA_H - 7, 2, 1);
  if (g.mode === 'hunt') {
    const ms = g.monster.state.toUpperCase().slice(0, 7);
    drawText(ctx, ms, W - 2 - textWidth(ms, 1), ARENA_H - 7, 2, 1);
  }
}

function wire(ctx, x, y, w, h, color) {
  x = Math.round(x);
  y = Math.round(y);
  w = Math.round(w);
  h = Math.round(h);
  ctx.save();
  ctx.strokeStyle = color;
  ctx.lineWidth = 1;
  ctx.strokeRect(x + 0.5, y + 0.5, Math.max(1, w - 1), Math.max(1, h - 1));
  ctx.restore();
}

/* ------------------------------------------------------------------ boot */

// weapon swap and reset keep the current area, the chosen beast and the pole
// variant
function withWeapon(game, weaponIndex) {
  const kind = game.pole.kind | 0;
  const g = newGame(weaponIndex, game.mode, game.monsterIndex);
  if (g.mode === 'train') initPoleKind(g, kind);
  return g;
}

function resetHunt(game) {
  const kind = game.pole.kind | 0;
  const g = newGame(game.weapon, game.mode, game.monsterIndex);
  if (g.mode === 'train') initPoleKind(g, kind);
  return g;
}

function boot() {
  const canvas = document.getElementById('screen');
  const ctx = canvas.getContext('2d');
  ctx.imageSmoothingEnabled = false;

  const keys = Object.create(null);
  let weaponIndex = 0;
  let game = newGame(weaponIndex);
  setDebounceProfile('HEAVY');   // browser playtest default (module default stays OFF)
  let paused = false;
  let slow = false;
  let debug = false;

  const prevent = ['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Space'];

  window.addEventListener('keydown', (e) => {
    if (prevent.indexOf(e.code) !== -1) e.preventDefault();
    if (e.repeat) return;
    keys[e.code] = true;

    if (e.code === 'Digit1' || e.code === 'Digit2' || e.code === 'Digit3') {
      weaponIndex = Number(e.code.slice(-1)) - 1;
      game = withWeapon(game, weaponIndex);
    } else if (e.code === 'KeyR') {
      game = resetHunt(game);
    } else if (e.code === 'KeyK') {
      game = newGame(weaponIndex, game.mode === 'hunt' ? 'train' : 'hunt', game.monsterIndex);
    } else if (e.code === 'KeyQ') {
      const p = game.player;
      p.shell = p.shell === 'ball' ? 'scatter' : 'ball';
    } else if (e.code === 'KeyP') {
      paused = !paused;
    } else if (e.code === 'KeyT') {
      slow = !slow;
    } else if (e.code === 'KeyH') {
      debug = !debug;
    } else if (e.code === 'BracketLeft' || e.code === 'BracketRight') {
      const dir = e.code === 'BracketRight' ? 1 : -1;
      setSheatheVariant(SHEATHE_VARIANTS.indexOf(getSheatheVariant()) + dir);
      console.log('sheathe variant -> ' + getSheatheVariant());
    } else if (e.code === 'Comma' || e.code === 'Period') {
      const dir = e.code === 'Period' ? 1 : -1;
      const i = DEBOUNCE_PROFILES.findIndex((p) => p.name === getDebounceProfile().name);
      setDebounceProfile(i + dir);
      console.log('debounce profile -> ' + getDebounceProfile().name);
    }
  });
  window.addEventListener('keyup', (e) => { keys[e.code] = false; });
  // focus loss can eat keyup; drop all held keys so hunter never slides on its own
  window.addEventListener('blur', () => {
    for (const k in keys) keys[k] = false;
  });

  let last = performance.now();
  let acc = 0;

  function frame(now) {
    requestAnimationFrame(frame);
    let dt = now - last;
    last = now;
    if (dt > 200) dt = 200;
    acc += dt * (slow ? 0.25 : 1);

    while (acc >= TICK_MS) {
      acc -= TICK_MS;
      if (paused) { acc = 0; break; }
      step(game, {
        mx: (keys['ArrowRight'] ? 1 : 0) - (keys['ArrowLeft'] ? 1 : 0),
        my: (keys['ArrowDown'] ? 1 : 0) - (keys['ArrowUp'] ? 1 : 0),
        a: !!keys['KeyZ'],
        b: !!keys['KeyX'],
      });
    }
    render(ctx, game, { debug, paused, slow });
  }

  requestAnimationFrame(frame);
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    newGame, step, render, withWeapon, resetHunt, isqrt, initPoleKind,
    damagePole, poleOnHit, poleStage,
    monsterActiveWindow, monsterTellWindow,
    spinSheetFrame, dirIndexFromDelta, monsterSpinFrame,
    zoneHitResolve, zoneContains,
    WEAPON_DEFS, MONSTER_ATTACKS, MONSTER_DEFS, POLE_DEFS,
    POLE_PLAIN, POLE_SEVER, POLE_BREAK, POLE_CRACK,
    getSheatheVariant, setSheatheVariant, SHEATHE_VARIANTS, SHEATHE_SPD,
    getDebounceProfile, setDebounceProfile, DEBOUNCE_PROFILES,
    CHARGE_MIN, CHARGE_L2,
    W, H, ARENA_H, HOLD_TICKS, SHADES, WORLD_W, WORLD_H, FP,
  };
}
if (typeof window !== 'undefined' && typeof document !== 'undefined') boot();
