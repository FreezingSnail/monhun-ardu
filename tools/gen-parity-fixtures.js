'use strict';

/*
 * Parity fixture generator (bead monhun-ardu-p82).
 *
 * Runs the source-of-truth mock (mock/game.js) over fixed, deterministic input
 * scripts and writes tst/fxdatatest/parity_fixtures.hpp. The device parity test
 * (tst/fxdatatest/parity_test.hpp) replays the exact same input scripts through
 * the C++ core (mh::stepGame) and compares:
 *   - a 16-bit FNV-style hash of the full sim state, tick by tick;
 *   - a packed field snapshot every CP_STRIDE ticks and at the final tick
 *     (x, y, hp, stam, state, stance, chain, ammo, reload, projN, train stats,
 *      monster pos/state/hp/stun, camera).
 *
 * JS is used only to produce the fixtures; the test itself is C++/Ardens.
 * No float enters the fixture: every value is an integer field (mock cam is
 * already an int-valued clamp result).
 *
 * Usage: node tools/gen-parity-fixtures.js
 */

const fs = require('node:fs');
const path = require('node:path');
const G = require('../mock/game.js');

const CP_STRIDE = 64;

/* ------------------------------------------------------------ enum mapping */

const PSTATE = { idle: 0, attack: 1, special: 2, dodge: 3, deflect: 4, shove: 5, stun: 6 };
const STANCE = { parry: 1, whirl: 2, guard: 3 };
const MSTATE = { idle: 0, pursue: 1, windup: 2, attack: 3, recover: 4, dead: 5 };
const MODO = { lunge: 0, sweep: 1 };
const SHELL = { ball: 0, scatter: 1 };
const OVER = { win: 1, lose: 2 };
const ATKID = { stepslash: 1, spincut: 2, trip: 3, pointblank: 4, guardbash: 5 };

function pstate(p) { return p.state ? (PSTATE[p.state] || 0) : 0; }
function stance(p) { return p.stance ? (STANCE[p.stance] || 0) : 0; }
function mstate(m) { return m.state ? (MSTATE[m.state] || 0) : 0; }
function shellEnum(p) { return p.shell === 'scatter' ? 1 : 0; }
function overEnum(g) { return g.over ? (OVER[g.over] || 0) : 0; }

/* ------------------------------------------------------------ state hash */

function mix(h, v) {
  h = (h ^ (v | 0)) >>> 0;
  h = Math.imul(h, 16777619) >>> 0;
  return h;
}

function atkField(a, name) { return a && a[name] !== undefined ? a[name] : 0; }

// Mirrors mock/game.js trainDps() (not exported by the mock module).
function trainDps(g) {
  const cutoff = g.tick - 600;
  let sum = 0;
  for (const e of g.train.events) if (e.tick > cutoff) sum += e.dmg;
  return Math.round(sum / 10);
}

function hashState(g) {
  const p = g.player;
  const m = g.monster;
  let h = 2166136261 >>> 0;

  // game scalars
  h = mix(h, g.tick); h = mix(h, g.freeze);
  h = mix(h, overEnum(g));
  h = mix(h, g.mode === 'train' ? 1 : 0);
  h = mix(h, g.prevA ? 1 : 0); h = mix(h, g.prevB ? 1 : 0);
  h = mix(h, Math.round(g.cam.x)); h = mix(h, Math.round(g.cam.y));

  // player
  h = mix(h, p.x); h = mix(h, p.y); h = mix(h, p.subX); h = mix(h, p.subY);
  h = mix(h, p.vx); h = mix(h, p.vy); h = mix(h, p.fx); h = mix(h, p.fy);
  h = mix(h, p.hp); h = mix(h, p.stam); h = mix(h, p.stamSub);
  h = mix(h, pstate(p)); h = mix(h, p.t); h = mix(h, p.hitDone ? 1 : 0);
  h = mix(h, p.chain); h = mix(h, p.chainWin); h = mix(h, p.aBuffer);
  h = mix(h, stance(p)); h = mix(h, p.stanceT); h = mix(h, p.stanceAuto);
  h = mix(h, p.whirlTick); h = mix(h, p.throwCd); h = mix(h, p.riposteT);
  h = mix(h, p.bHeld); h = mix(h, p.bReady ? 1 : 0); h = mix(h, p.bLocked ? 1 : 0);
  h = mix(h, p.iT); h = mix(h, shellEnum(p)); h = mix(h, p.reload);
  h = mix(h, p.shells.ball); h = mix(h, p.shells.scatter);
  h = mix(h, p.atk ? (ATKID[p.atk.id] || 0) : 0);
  h = mix(h, atkField(p.atk, 'startup')); h = mix(h, atkField(p.atk, 'active'));
  h = mix(h, atkField(p.atk, 'recover')); h = mix(h, atkField(p.atk, 'dmg'));
  h = mix(h, atkField(p.atk, 'reach')); h = mix(h, atkField(p.atk, 'stam'));

  // monster
  h = mix(h, m.x); h = mix(h, m.y); h = mix(h, m.subX); h = mix(h, m.subY);
  h = mix(h, m.hp); h = mix(h, mstate(m)); h = mix(h, m.t); h = mix(h, m.cd);
  h = mix(h, m.face.x); h = mix(h, m.face.y); h = mix(h, m.lvx); h = mix(h, m.lvy);
  h = mix(h, m.windupMax); h = mix(h, m.hitFlash); h = mix(h, m.stun);
  h = mix(h, m.circleDir); h = mix(h, m.spd);
  h = mix(h, m.atk ? (MODO[m.atk.kind] || 0) : 0);
  h = mix(h, atkField(m.atk, 'windup')); h = mix(h, atkField(m.atk, 'active'));
  h = mix(h, atkField(m.atk, 'recover')); h = mix(h, atkField(m.atk, 'dmg'));
  h = mix(h, atkField(m.atk, 'reach')); h = mix(h, atkField(m.atk, 'hw'));
  h = mix(h, atkField(m.atk, 'hh'));

  // pole / train
  h = mix(h, g.pole.hitFlash); h = mix(h, g.train.total); h = mix(h, g.train.last);
  h = mix(h, trainDps(g));

  // projectiles (fp x/y capture sub-pixel exactly)
  h = mix(h, g.projectiles.length);
  for (const pr of g.projectiles) {
    h = mix(h, pr.x); h = mix(h, pr.y); h = mix(h, pr.vx); h = mix(h, pr.vy);
    h = mix(h, pr.w); h = mix(h, pr.h); h = mix(h, pr.dmg); h = mix(h, pr.life);
    h = mix(h, pr.heavy ? 1 : 0);
  }

  // effects
  h = mix(h, g.effects.length);
  for (const e of g.effects) {
    h = mix(h, e.x); h = mix(h, e.y); h = mix(h, e.t); h = mix(h, e.life);
    h = mix(h, e.crit ? 1 : 0); h = mix(h, e.text ? Number(e.text) : 0);
  }

  // sheathe + combo debounce/buffer state (udb; appended after effects, mirrored
  // by tst/fxdatatest/parity_test.hpp hashState in the same order)
  h = mix(h, p.sheathed ? 1 : 0);
  h = mix(h, p.chainLock);
  h = mix(h, p.bBuffer);

  return (h ^ (h >>> 16)) & 0xffff;
}

/* ------------------------------------------------------------ snapshots */

function snapshot(g) {
  const p = g.player;
  const m = g.monster;
  return [
    p.x, p.y, p.hp, p.stam, pstate(p), stance(p), p.chain,
    p.shells.ball, p.shells.scatter, p.reload, g.projectiles.length,
    g.train.total, g.train.last, m.x, m.y, mstate(m), m.hp, m.stun,
    Math.round(g.cam.x), Math.round(g.cam.y),
  ];
}

/* ------------------------------------------------------------ input scripts */

function inp(mx, my, a, b) { return { mx: mx | 0, my: my | 0, a: !!a, b: !!b }; }
function rep(n, o) { const out = []; for (let i = 0; i < n; i++) out.push(inp(o.mx, o.my, o.a, o.b)); return out; }

// Parked beast: never pursues, attacks or shoves. t/cd stay well short of the
// int16 ceiling so the C++ port stores the same value the mock does.
function park(g) {
  g.monster.state = 'recover';
  g.monster.t = 30000;
  g.monster.cd = 30000;
  return g.monster;
}

// Note: mirrored by tst/fxdatatest/parity_test.hpp; keep names/order in sync.
const SCENARIOS = [
  { name: 'sword_move_right_16', weapon: 0, mode: 'hunt',
    setup: g => { park(g); }, inputs: () => rep(16, { mx: 1 }) },
  { name: 'sword_move_left_16', weapon: 0, mode: 'hunt',
    setup: g => { park(g); }, inputs: () => rep(16, { mx: -1 }) },
  { name: 'sword_move_down_up', weapon: 0, mode: 'hunt',
    setup: g => { park(g); }, inputs: () => rep(16, { my: 1 }).concat(rep(16, { my: -1 })) },
  { name: 'sword_chain_taps', weapon: 0, mode: 'hunt',
    setup: g => { const m = park(g); m.x = g.player.x + 200; m.y = g.player.y; },
    inputs: () => [inp(0,0,true,false)]
      .concat(rep(20, {}), [inp(0,0,true,false)], rep(20, {}), [inp(0,0,true,false)], rep(20, {})) },
  { name: 'sword_stepsplash_branch', weapon: 0, mode: 'hunt',
    setup: g => { const m = park(g); m.x = g.player.x + 200; m.y = g.player.y; },
    inputs: () => [inp(0,0,true,false)].concat(rep(20, {}), [inp(0,0,false,true), inp(0,0,false,false)]) },
  { name: 'sword_dodge_roll', weapon: 0, mode: 'hunt',
    setup: g => { park(g); },
    inputs: () => rep(5, { mx: 1 }).concat([inp(1,0,false,true), inp(1,0,false,false)], rep(10, { mx: 1 })) },
  { name: 'sword_parry_hold', weapon: 0, mode: 'hunt',
    setup: g => { park(g); }, inputs: () => rep(13, { b: true }) },
  { name: 'flail_release_whirl', weapon: 1, mode: 'hunt',
    setup: g => { const m = park(g); m.x = g.player.x + 200; m.y = g.player.y; },
    inputs: () => [inp(0,0,true,false)].concat(rep(30, {}), [inp(0,0,false,true), inp(0,0,false,false)]) },
  { name: 'flail_whirl_throw_hit', weapon: 1, mode: 'hunt',
    setup: g => { const m = park(g); m.x = g.player.x + 40; m.y = g.player.y; },
    inputs: () => rep(13, { b: true }).concat([inp(0,0,true,true)], rep(14, { b: true })) },
  { name: 'gun_guard_fire_ball', weapon: 2, mode: 'hunt',
    setup: g => { const m = park(g); m.x = g.player.x + 200; m.y = g.player.y; },
    inputs: () => rep(13, { b: true }).concat([inp(0,0,true,true)], rep(5, { b: true })) },
  { name: 'gun_reload_cycle', weapon: 2, mode: 'train',
    setup: g => { park(g); g.pole.x = 5000; },
    inputs: () => rep(13, { b: true }).concat([inp(0,0,true,true)], rep(90, {})) },
  { name: 'gun_pointblank_branch', weapon: 2, mode: 'hunt',
    setup: g => { const m = park(g); m.x = g.player.x + 200; m.y = g.player.y; },
    inputs: () => [inp(0,0,true,false)].concat(rep(20, {}), [inp(0,0,false,true), inp(0,0,false,false)]) },
  { name: 'monster_sweep_hit', weapon: 0, mode: 'hunt',
    setup: g => {
      const m = park(g);
      m.x = g.player.x + 20; m.y = g.player.y;
      m.state = 'attack'; m.atk = G.MONSTER_ATTACKS.sweep; m.t = 0;
      m.face = { x: -16, y: 0 };
    },
    inputs: () => rep(10, {}) },
  { name: 'beast_no_shove_idle', weapon: 0, mode: 'hunt',
    setup: g => {
      const m = park(g);
      m.state = 'pursue'; m.cd = 30000;
      m.x = g.player.x + 8; m.y = g.player.y + 4;
    },
    inputs: () => rep(40, {}) },
  { name: 'train_pole_head', weapon: 0, mode: 'train',
    setup: g => { park(g); g.pole.x = g.player.x + 20; g.pole.y = g.player.y; },
    inputs: () => [inp(0,0,true,false)].concat(rep(80, {})) },
  { name: 'train_pole_body', weapon: 0, mode: 'train',
    setup: g => { park(g); g.pole.x = g.player.x + 20; g.pole.y = g.player.y + 20; },
    inputs: () => [inp(0,0,true,false)].concat(rep(80, {})) },
  { name: 'train_monster_frozen', weapon: 0, mode: 'train',
    setup: g => { park(g); }, inputs: () => rep(120, {}) },
  { name: 'stamina_guard_drain', weapon: 2, mode: 'train',
    setup: g => { park(g); g.pole.x = 5000; }, inputs: () => rep(70, { b: true }) },
  { name: 'camera_world_clamp', weapon: 0, mode: 'hunt',
    setup: g => { const m = park(g); m.x = 20; m.y = 0; }, inputs: () => rep(220, { mx: 1 }) },
  { name: 'monster_windup_cycle', weapon: 0, mode: 'hunt',
    setup: g => { park(g); }, inputs: () => rep(260, {}) },
];

/* ------------------------------------------------------------ fixture emit */

function hex(v, width) {
  return '0x' + (v >>> 0).toString(16).padStart(width, '0');
}

function bytes(arr) {
  const rows = [];
  for (let i = 0; i < arr.length; i += 16) {
    rows.push('    ' + arr.slice(i, i + 16).map(v => hex(v, 2)).join(', ') + ',');
  }
  return rows.join('\n');
}

function words(arr) {
  const rows = [];
  for (let i = 0; i < arr.length; i += 12) {
    rows.push('    ' + arr.slice(i, i + 12).map(v => hex(v & 0xffff, 4)).join(', ') + ',');
  }
  return rows.join('\n');
}

function run() {
  const inputs = [];
  const hashes = [];
  const snaps = [];
  const overrides = [];
  const meta = [];

  const cpFields = snapshot(G.newGame(0)).length;

  for (let s = 0; s < SCENARIOS.length; s++) {
    const sc = SCENARIOS[s];
    // Device ships the HEAVY debounce profile; arm it per scene. The mock module
    // default stays OFF so mock/game.test.js keeps the OFF baseline.
    G.setDebounceProfile('HEAVY');
    const g = G.newGame(sc.weapon, sc.mode);
    if (sc.setup) sc.setup(g);
    const script = sc.inputs(g);

    // Concrete setup overrides so the C++ test can reproduce the start state.
    const m = g.monster;
    const overOff = overrides.length;
    overrides.push(
      m.x, m.y, MSTATE[m.state] || 0, m.t, m.cd, m.face.x, m.face.y,
      m.atk ? (MODO[m.atk.kind] || 0) : -1, g.pole.x, g.pole.y
    );

    const inOff = inputs.length;
    const hashOff = hashes.length;
    const snapOff = snaps.length;
    const n = script.length;

    for (let t = 0; t < n; t++) {
      const raw = script[t];
      G.step(g, { mx: raw.mx, my: raw.my, a: raw.a, b: raw.b });
      inputs.push((raw.a ? 1 : 0) | (raw.b ? 2 : 0) |
                  ((raw.mx + 1) << 2) | ((raw.my + 1) << 4));
      hashes.push(hashState(g));
      const tick = t + 1;
      if (tick % CP_STRIDE === 0 || tick === n) {
        snaps.push(...snapshot(g));
      }
    }

    meta.push({ inOff, hashOff, snapOff, overOff, n,
                ncp: (snaps.length - snapOff) / cpFields,
                weapon: sc.weapon, mode: sc.mode === 'train' ? 1 : 0 });
  }

  const out = [];
  out.push('// Generated by tools/gen-parity-fixtures.js — DO NOT EDIT.');
  out.push('// Source of truth: mock/game.js. Regenerate: node tools/gen-parity-fixtures.js');
  out.push('#pragma once');
  out.push('#include <stdint.h>');
  out.push('#include "src/core/progmem.hpp"');
  out.push('');
  out.push('namespace parity_fx {');
  out.push('');
  out.push('const uint16_t CP_STRIDE    = ' + CP_STRIDE + ';');
  out.push('const uint16_t CP_FIELDS    = ' + cpFields + ';');
  out.push('const uint16_t OVERRIDE_FIELDS = 10;');
  out.push('const uint16_t META_FIELDS  = 8;');
  out.push('const uint16_t SCENE_COUNT  = ' + SCENARIOS.length + ';');
  out.push('');
  out.push('// Per-tick input byte: bit0 A, bit1 B, bits2-3 mx+1, bits4-5 my+1.');
  out.push('MH_PROGMEM const uint8_t inputs[] = {');
  out.push(bytes(inputs));
  out.push('};');
  out.push('');
  out.push('// Per-tick 16-bit full-state hash (all ticks, all scenes, in order).');
  out.push('MH_PROGMEM const uint16_t hashes[] = {');
  out.push(words(hashes));
  out.push('};');
  out.push('');
  out.push('// Field snapshots: CP_FIELDS int16 per checkpoint, stride CP_STRIDE + final.');
  out.push('MH_PROGMEM const int16_t snaps[] = {');
  out.push(words(snaps));
  out.push('};');
  out.push('');
  out.push('// Start-state overrides, OVERRIDE_FIELDS int16 per scene:');
  out.push('// { monsterX, monsterY, monsterState, monsterT, monsterCd, faceX,');
  out.push('//   faceY, monsterAtkKind (-1 none), poleX, poleY }.');
  out.push('MH_PROGMEM const int16_t overrides[] = {');
  out.push(words(overrides));
  out.push('};');
  out.push('');
  out.push('// Per scene: { inputOffset, hashOffset, snapOffset, overrideOffset,');
  out.push('//              ticks, checkpoints, weapon, mode }.');
  out.push('MH_PROGMEM const uint16_t meta[] = {');
  out.push('    ' + meta.map(m =>
    [m.inOff, m.hashOff, m.snapOff, m.overOff, m.n, m.ncp, m.weapon, m.mode].map(v => String(v)).join(', ')
  ).join(',\n    ') + ',');
  out.push('};');
  out.push('');
  out.push('} // namespace parity_fx');
  out.push('');

  const dest = path.join(__dirname, '..', 'tst', 'fxdatatest', 'parity_fixtures.hpp');
  fs.writeFileSync(dest, out.join('\n'));

  const totalTicks = hashes.length;
  console.log('wrote ' + path.relative(process.cwd(), dest));
  console.log('scenes=' + SCENARIOS.length + ' ticks=' + totalTicks +
              ' snapshots=' + (snaps.length / cpFields) + ' cpFields=' + cpFields);
}

run();
