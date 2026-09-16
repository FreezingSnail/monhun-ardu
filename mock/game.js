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
    branches: [
      { stage: 1, atk: { id: 'stepslash', startup: 3, active: 5, recover: 12, dmg: 12, reach: 18, hw: 14, hh: 12, stam: 10, lunge: 42 } },
      { stage: 2, atk: { id: 'spincut', startup: 5, active: 7, recover: 15, dmg: 20, reach: 12, hw: 28, hh: 26, stam: 16 } },
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
    branches: [
      { stage: 1, stance: 'whirl', auto: 50 },
      { stage: 2, atk: { id: 'trip', startup: 5, active: 6, recover: 16, dmg: 12, reach: 22, hw: 22, hh: 14, stam: 14, effect: 'trip' } },
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
    ],
    canCancel: true,
    shells: {
      ball: { name: 'BALL', count: 2, dmg: 28, speedF: 35, w: 7, h: 6, reload: 70, stam: 6 },
      scatter: { name: 'SCAT', count: 5, dmg: 7, speedF: 42, w: 4, h: 4, reload: 30, pellets: 3, stam: 5 },
    },
  },
];

const MONSTER_ATTACKS = {
  lunge: { kind: 'lunge', windup: 40, active: 10, recover: 55, speedF: 34, dmg: 12, reach: 12, hw: 24, hh: 22 },
  sweep: { kind: 'sweep', windup: 48, active: 12, recover: 60, dmg: 9, reach: 17, hw: 32, hh: 24 },
};

// Demo beast roster (bead monhun-ardu-6zb.1). Index 0 (lunge) is the legacy
// parity default: its def reproduces the values newGame() used to hardcode.
// atkDist is the lunge/sweep split distance; negative = never lunge (always
// sweep).
const MONSTER_DEFS = [
  { kind: 'lunge', w: 32, h: 24, hp: 200, spd: 5, atkDist: 32 },
  { kind: 'sweep', w: 28, h: 22, hp: 150, spd: 7, atkDist: -1 },
  { kind: 'heavy', w: 40, h: 28, hp: 320, spd: 3, atkDist: 24 },
];

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
    pole: { x: 140, y: 40, w: 20, h: 36, hitFlash: 0 },
    player: {
      x: 96, y: 60, w: 16, h: 16,
      subX: 0, subY: 0, // 1/16 px remainder
      vx: 0, vy: 0,     // 1/16 px per tick
      fx: FP, fy: 0,    // 1/16 unit vector
      hp: 100, hpMax: 100, stam: 100, stamMax: 100, stamSub: 0,
      state: 'idle', t: 0, atk: null, hitDone: false,
      chain: 0, chainWin: 0, aBuffer: 0,
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
  if (p.chainWin > 0) {
    p.chainWin--;
    if (p.chainWin === 0) p.chain = 0;
  }
  if (p.aBuffer > 0) p.aBuffer--;

  const draining = p.stance === 'whirl' || p.stance === 'guard';
  if (!draining && p.stam < p.stamMax) {
    p.stamSub += 8; // 0.5 per tick
    if (p.stamSub >= 16) { p.stamSub -= 16; p.stam = Math.min(p.stamMax, p.stam + 1); }
  }

  // B: release speed picks tap defense vs hold stance
  if (bP) { p.bHeld = 0; p.bReady = true; }
  if (inp.b && p.bReady) {
    p.bHeld++;
    if (p.bHeld === HOLD_TICKS && !p.stance && !p.bLocked) enterStance(g, def);
  }
  if (bR) {
    if (p.bHeld < HOLD_TICKS) {
      if (!tryBranch(g, def, inp)) tapDefense(g, def, inp);
    } else if (p.stance) {
      exitStance(p);
    }
    p.bReady = false;
    p.bHeld = 0;
    p.bLocked = false;
  }

  // A: attack / stance special
  if (aP) {
    if (p.stance) {
      stanceSpecial(g, def);
    } else if (p.state === 'idle' || p.chainWin > 0) {
      startAttack(g, def);
    } else {
      p.aBuffer = 10;
    }
  }
  if (p.aBuffer > 0 && (p.state === 'idle' || p.chainWin > 0)) {
    p.aBuffer = 0;
    startAttack(g, def);
  }

  switch (p.state) {
    case 'idle': {
      let mx = inp.mx || 0;
      let my = inp.my || 0;
      if (p.stance === 'parry') { mx = 0; my = 0; }
      let sp = def.spd;
      if (p.stance === 'whirl') sp = (sp * 6) / 10 | 0;
      if (p.stance === 'guard') sp = (sp * 4) / 10 | 0;
      movePlayer(p, mx, my, sp);
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
        const tgt = activeTarget(g);
        if (tgt && rectsOverlap(hit, tgt)) {
          p.hitDone = true;
          const mult = (p.state === 'special' && p.riposteT > 0) ? 2 : 1;
          const hx = hit.x + hit.w / 2;
          const hy = hit.y + hit.h / 2;
          if (g.mode === 'train') {
            damagePole(g, a.dmg * mult, hx, hy);
          } else {
            damageMonster(g, a.dmg * mult, hx, hy);
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
          p.chain = p.chain < 2 ? p.chain + 1 : 0;
          p.chainWin = 14;
        } else {
          p.state = 'idle';
          p.chain = 0;
          p.chainWin = 0;
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
    default:
      p.state = 'idle';
  }

  if (p.stance) updateStance(g, def);
  clampPlayer(p);
}

function movePlayer(p, mx, my, spd) {
  const i = dirIndexFromInput(mx, my);
  if (i < 0) return;
  const d = DIR8[i];
  p.fx = d.x;
  p.fy = d.y;
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

function startAttack(g, def) {
  const p = g.player;
  const a = def.attacks[Math.min(p.chain, 2)];
  if (p.stam < 1) return;
  p.stam = Math.max(0, p.stam - a.stam);
  p.state = 'attack';
  p.atk = a;
  p.t = 0;
  p.hitDone = false;
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

  if (def.id === 'sword') {
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
    stage = Math.min(p.chain + 1, 2);
  } else if (p.state === 'idle' && p.chainWin > 0) {
    stage = p.chain;
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
    return true;
  }

  const atk = br.atk;
  if (p.stam < atk.stam) return false;
  if (atk.shell) {
    if (p.shells.ball <= 0) return false;
    p.shells.ball--;
    p.reload = 45;
  }
  p.stam -= atk.stam;
  p.state = 'attack';
  p.atk = atk;
  p.t = 0;
  p.hitDone = false;
  p.chain = 0;
  p.chainWin = 0;
  if (atk.lunge) {
    p.vx = (p.fx * atk.lunge) >> 4;
    p.vy = (p.fy * atk.lunge) >> 4;
  }
  return true;
}

function enterStance(g, def) {
  const p = g.player;
  if (p.stance) return;
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
      const tgt = activeTarget(g);
      if (tgt) {
        const cx = p.x + p.w / 2;
        const cy = p.y + p.h / 2;
        if (circleRectOverlap(cx, cy, 24, tgt)) {
          if (g.mode === 'train') {
            damagePole(g, 8, cx, cy);
          } else {
            damageMonster(g, 8, cx, cy);
            knockMonsterAway(g, tgt, cx, cy, 8);
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
    p.shells[p.shell]--;
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
  if (m.hitFlash > 0) m.hitFlash--;
  if (m.state === 'dead') return;

  const dx = (p.x + (p.w >> 1)) - (m.x + (m.w >> 1));
  const dy = (p.y + (p.h >> 1)) - (m.y + (m.h >> 1));
  const dist = isqrt(dx * dx + dy * dy);
  const di = dirIndexFromDelta(dx, dy);
  m.face = { x: DIR8[di].x, y: DIR8[di].y };

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
      else if (dist < 24) addMove(m, -m.face.x, -m.face.y, (m.spd * 6) / 10 | 0);
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
      if (a.kind === 'lunge' && m.t <= a.active) addVel(m, m.lvx, m.lvy);
      if (m.t <= a.active && monsterHitsPlayer(g, a)) playerHit(g, a.dmg, m);
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
// legacy "lunge beyond 32px" rule; negative atkDist (sweep) never lunges.
function chooseAttack(g, dist) {
  const m = g.monster;
  const def = MONSTER_DEFS[g.monsterIndex];
  m.atk = def.atkDist >= 0 && dist > def.atkDist ? MONSTER_ATTACKS.lunge : MONSTER_ATTACKS.sweep;
  m.state = 'windup';
  m.t = m.atk.windup;
  m.windupMax = m.atk.windup;
}

function startMonsterAttack(m) {
  const a = m.atk;
  m.state = 'attack';
  m.t = 0;
  if (a.kind === 'lunge') {
    m.lvx = (m.face.x * a.speedF) >> 4;
    m.lvy = (m.face.y * a.speedF) >> 4;
  } else {
    m.lvx = 0;
    m.lvy = 0;
  }
}

function monsterHitsPlayer(g, a) {
  const m = g.monster;
  const cx = m.x + (m.w >> 1) + ((m.face.x * a.reach) >> 4);
  const cy = m.y + (m.h >> 1) + ((m.face.y * a.reach) >> 4);
  const r = { x: cx - (a.hw >> 1), y: cy - (a.hh >> 1), w: a.hw, h: a.hh };
  return rectsOverlap(r, g.player);
}

function playerHit(g, dmg, m) {
  const p = g.player;
  if (p.iT > 0) return;

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
    p.vx = (m.face.x * 19) >> 4;
    p.vy = (m.face.y * 19) >> 4;
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
  p.vx = (m.face.x * 35) >> 4;
  p.vy = (m.face.y * 35) >> 4;
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
  const tgt = activeTarget(g);
  if (!tgt) return;
  if (!rectsOverlap(p, tgt)) return;

  // pole never moves; lunging beast shoves you; otherwise the beast gives way
  const shovePlayer = g.mode === 'train' ||
    (g.mode === 'hunt' && (g.monster.state === 'attack' || g.monster.state === 'windup'));
  const a = shovePlayer ? p : tgt;
  const b = shovePlayer ? tgt : p;
  const ox = Math.min(a.x + a.w - b.x, b.x + b.w - a.x);
  const oy = Math.min(a.y + a.h - b.y, b.y + b.h - a.y);
  if (ox < oy) {
    if (a.x + (a.w >> 1) < b.x + (b.w >> 1)) a.x -= ox; else a.x += ox;
  } else {
    if (a.y + (a.h >> 1) < b.y + (b.h >> 1)) a.y -= oy; else a.y += oy;
  }
}

function activeTarget(g) {
  if (g.mode === 'train') return g.pole;
  return g.monster.state === 'dead' ? null : g.monster;
}

function updatePole(g) {
  if (g.pole.hitFlash > 0) g.pole.hitFlash--;
}

function damagePole(g, dmg, hx, hy) {
  const pole = g.pole;
  const crit = hy < pole.y + 16;
  const total = Math.max(1, (dmg * (crit ? 14 : 10)) / 10 | 0);
  pole.hitFlash = 4;
  g.freeze = Math.max(g.freeze, crit ? 5 : 4);
  g.shake = Math.max(g.shake, crit ? 3 : 2.2);
  g.train.total += total;
  g.train.last = total;
  g.train.events.push({ tick: g.tick, dmg: total });
  g.effects.push({ x: hx, y: hy - 6, t: 0, life: 26, crit, text: String(total) });
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
    const tgt = activeTarget(g);
    if (tgt) {
      const r = { x: (pr.x >> 4) - (pr.w >> 1), y: (pr.y >> 4) - (pr.h >> 1), w: pr.w, h: pr.h };
      if (rectsOverlap(r, tgt)) {
        if (g.mode === 'train') damagePole(g, pr.dmg, r.x + (r.w >> 1), r.y + (r.h >> 1));
        else damageMonster(g, pr.dmg, r.x + (r.w >> 1), r.y + (r.h >> 1));
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

  if (def.id === 'sword') {
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

  if (m.stun > 0) {
    ctx.fillStyle = SHADES[2];
    const a = g.tick * 0.35;
    ctx.fillRect(Math.round(x + m.w / 2 + Math.cos(a) * 9), y - 3 + Math.round(Math.sin(a) * 2), 2, 2);
  }

  if (m.state === 'windup' || m.state === 'attack') {
    const a = m.atk;
    const ax = x + m.w / 2 + m.face.x * a.reach;
    const ay = y + m.h / 2 + m.face.y * a.reach;
    if (m.state === 'windup') {
      ctx.fillStyle = SHADES[1];
      ctx.fillRect(Math.round(ax - a.hw / 2), Math.round(ay - a.hh / 2), a.hw, a.hh);
      ctx.fillStyle = SHADES[2];
      ctx.fillRect(Math.round(ax) - 1, Math.round(ay) - 1, 2, 2);
    } else {
      ctx.fillStyle = SHADES[2];
      ctx.fillRect(Math.round(ax - a.hw / 2), Math.round(ay - a.hh / 2), a.hw, a.hh);
      ctx.fillStyle = SHADES[3];
      ctx.fillRect(Math.round(ax) - 2, Math.round(ay) - 2, 4, 4);
    }
  }
}

function drawPole(ctx, g) {
  const pole = g.pole;
  const x = Math.round(pole.x);
  const y = Math.round(pole.y);
  ctx.fillStyle = SHADES[1];
  ctx.fillRect(x + 2, y + 12, pole.w - 4, pole.h - 12);
  ctx.fillStyle = SHADES[0];
  for (let i = 0; i < 3; i++) ctx.fillRect(x + 2, y + 20 + i * 7, pole.w - 4, 1);
  ctx.fillStyle = SHADES[pole.hitFlash > 0 ? 3 : 2];
  ctx.fillRect(x, y, pole.w, 16);
  ctx.fillStyle = SHADES[0];
  ctx.fillRect(x + 8, y + 5, 4, 4);
  ctx.fillStyle = SHADES[0];
  ctx.fillRect(x - 2, y + pole.h - 2, pole.w + 4, 2);
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

// weapon swap and reset keep the current area and the chosen beast
function withWeapon(game, weaponIndex) {
  return newGame(weaponIndex, game.mode, game.monsterIndex);
}

function resetHunt(game) {
  return newGame(game.weapon, game.mode, game.monsterIndex);
}

function boot() {
  const canvas = document.getElementById('screen');
  const ctx = canvas.getContext('2d');
  ctx.imageSmoothingEnabled = false;

  const keys = Object.create(null);
  let weaponIndex = 0;
  let game = newGame(weaponIndex);
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
    newGame, step, render, withWeapon, resetHunt, isqrt,
    WEAPON_DEFS, MONSTER_ATTACKS, MONSTER_DEFS,
    W, H, ARENA_H, HOLD_TICKS, SHADES, WORLD_W, WORLD_H, FP,
  };
}
if (typeof window !== 'undefined' && typeof document !== 'undefined') boot();
