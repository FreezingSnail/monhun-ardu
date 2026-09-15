'use strict';

const test = require('node:test');
const assert = require('node:assert');
const G = require('./game.js');

function inp(o) {
  return Object.assign({ mx: 0, my: 0, a: false, b: false }, o);
}

function ticks(g, n, o) {
  for (let i = 0; i < n; i++) G.step(g, inp(o || {}));
}

function park(g) {
  const m = g.monster;
  m.state = 'recover';
  m.t = 999999;
  m.cd = 999999;
  return m;
}

test('idle hunt runs without crashing, monster engages, player survives', () => {
  const g = G.newGame(0);
  ticks(g, 240);
  assert.equal(g.tick, 240);
  assert.equal(g.over, null);
  assert.ok(g.player.hp > 0, 'player should survive 240 idle ticks');
  assert.ok(g.monster.state !== 'idle', 'monster should have engaged');
});

test('sword tap attack damages monster', () => {
  const g = G.newGame(0);
  const m = park(g);
  m.x = g.player.x + 20;
  m.y = g.player.y;
  const hp0 = m.hp;
  G.step(g, inp({ a: true }));
  ticks(g, 20);
  assert.ok(m.hp < hp0, 'monster hp should drop after sword attack');
});

test('gunshield hold-B enters guard, stance+A fires a shell', () => {
  const g = G.newGame(2);
  park(g);
  const shells0 = g.player.shells.ball;
  ticks(g, G.HOLD_TICKS + 2, { b: true });
  assert.equal(g.player.stance, 'guard');
  G.step(g, inp({ b: true, a: true }));
  assert.equal(g.player.shells.ball, shells0 - 1);
  assert.ok(g.projectiles.length >= 1, 'a projectile should exist');
  assert.ok(g.player.reload > 0, 'reload should be running');
});

test('flail whirl stance enters and ball throw damages monster', () => {
  const g = G.newGame(1);
  const m = park(g);
  m.x = g.player.x + 40;
  m.y = g.player.y;
  const hp0 = m.hp;
  ticks(g, G.HOLD_TICKS + 2, { b: true });
  assert.equal(g.player.stance, 'whirl');
  G.step(g, inp({ b: true, a: true }));
  ticks(g, 14, { b: true });
  assert.ok(m.hp < hp0, 'throw should connect');
});

test('sword tap-B dodges with i-frames, hold-B enters parry', () => {
  const g = G.newGame(0);
  park(g);
  G.step(g, inp({ b: true }));
  G.step(g, inp({ b: false }));
  assert.equal(g.player.state, 'dodge');
  assert.ok(g.player.iT > 0);

  ticks(g, 20);
  ticks(g, G.HOLD_TICKS + 2, { b: true });
  assert.equal(g.player.stance, 'parry');
});

test('monster sweep attack damages player', () => {
  const g = G.newGame(0);
  const m = park(g);
  m.x = g.player.x + 20;
  m.y = g.player.y;
  m.state = 'attack';
  m.atk = G.MONSTER_ATTACKS.sweep;
  m.t = 0;
  m.face = { x: -16, y: 0 }; // fixed point 16 == 1.0
  const hp0 = g.player.hp;
  ticks(g, 10);
  assert.ok(g.player.hp < hp0, 'player should take sweep damage');
});

function stubCtx() {
  const noop = () => {};
  return new Proxy({}, { get: () => noop, set: () => true });
}

test('render runs for every weapon, live and over states', () => {
  for (let i = 0; i < 3; i++) {
    const g = G.newGame(i);
    ticks(g, 30);
    assert.doesNotThrow(() => G.render(stubCtx(), g, { debug: true, slow: true }));
    g.over = 'win';
    g.monster.state = 'dead';
    assert.doesNotThrow(() => G.render(stubCtx(), g, { paused: true }));
  }
});

test('sword A then B = stepslash branch', () => {
  const g = G.newGame(0);
  const m = park(g);
  m.x = g.player.x + 200;
  G.step(g, inp({ a: true }));
  ticks(g, 20);
  assert.ok(g.player.chainWin > 0, 'chain window should be open');
  G.step(g, inp({ b: true }));
  G.step(g, inp({ b: false }));
  assert.equal(g.player.state, 'attack');
  assert.ok(g.player.atk && g.player.atk.id === 'stepslash', 'branch move should run');
});

test('flail A then B = release into whirl stance', () => {
  const g = G.newGame(1);
  const m = park(g);
  m.x = g.player.x + 200;
  G.step(g, inp({ a: true }));
  ticks(g, 30);
  G.step(g, inp({ b: true }));
  G.step(g, inp({ b: false }));
  assert.equal(g.player.stance, 'whirl');
  assert.ok(g.player.stanceAuto > 0, 'released whirl should run on its own');
});

test('sword dodge roll moves the player', () => {
  const g = G.newGame(0);
  park(g);
  ticks(g, 5, { mx: 1 });
  const x0 = g.player.x;
  G.step(g, inp({ b: true, mx: 1 }));
  G.step(g, inp({ b: false, mx: 1 }));
  ticks(g, 10);
  assert.ok(g.player.x > x0 + 10, 'roll should cover real distance');
});

test('camera scrolls with player and clamps at world edge', () => {
  const g = G.newGame(0);
  const m = park(g);
  m.x = 20;
  m.y = 0; // park monster off the player's path
  ticks(g, 60, { mx: 1 });
  assert.ok(g.cam.x > 0, 'camera should follow');
  ticks(g, 400, { mx: 1 });
  assert.equal(g.cam.x, G.WORLD_W - G.W);
  assert.equal(g.player.x, G.WORLD_W - g.player.w);
});

test('gunshield ball shell is a visible block', () => {
  const g = G.newGame(2);
  park(g);
  ticks(g, G.HOLD_TICKS + 2, { b: true });
  G.step(g, inp({ b: true, a: true }));
  assert.equal(g.projectiles.length, 1);
  assert.ok(g.projectiles[0].w >= 6, 'ball projectile should be large enough to see');
});

test('train mode: pole takes damage, monster stays frozen', () => {
  const g = G.newGame(0, 'train');
  assert.equal(g.mode, 'train');
  const mx = g.monster.x;
  const my = g.monster.y;
  g.pole.x = g.player.x + 20;
  g.pole.y = g.player.y;
  G.step(g, inp({ a: true }));
  ticks(g, 20);
  assert.ok(g.train.total > 0, 'pole should take damage');
  assert.ok(g.train.last > 0);
  assert.ok(g.effects.some(e => e.text), 'damage number should spawn');
  ticks(g, 200);
  assert.equal(g.monster.x, mx, 'monster must not move in train mode');
  assert.equal(g.monster.y, my);
  assert.equal(g.player.hp, 100, 'nothing can hurt player in train mode');
});

test('player does not slide after knockback settles', () => {
  const g = G.newGame(0);
  const m = park(g);
  m.x = 20;
  m.y = 0;
  g.player.vx = -5;
  g.player.vy = -3;
  ticks(g, 60);
  const x = g.player.x;
  const y = g.player.y;
  ticks(g, 60);
  assert.equal(g.player.x, x, 'x must not drift');
  assert.equal(g.player.y, y, 'y must not drift');
  assert.equal(g.player.vx, 0);
  assert.equal(g.player.vy, 0);
});

test('beast body never shoves an idle player', () => {
  const g = G.newGame(0);
  const m = park(g);
  m.state = 'pursue';
  m.cd = 999999;
  m.x = g.player.x + 8; // overlapping the hunter
  m.y = g.player.y + 4;
  const x = g.player.x;
  const y = g.player.y;
  ticks(g, 40);
  assert.equal(g.player.x, x, 'player must not slide while beast overlaps');
  assert.equal(g.player.y, y);
});

test('up/left speed matches down/right (no stutter drift)', () => {
  function run(dir, n) {
    const g = G.newGame(0, 'train');
    const m = g.monster;
    m.state = 'recover';
    m.t = 999999;
    m.x = 20;
    m.y = 40;
    ticks(g, n, dir);
    return g.player;
  }
  const right = run({ mx: 1 }, 28);
  const left = run({ mx: -1 }, 28);
  const down = run({ my: 1 }, 28);
  const up = run({ my: -1 }, 28);
  assert.ok(Math.abs((right.x - 96) - (96 - left.x)) <= 2, 'left must match right');
  assert.ok(Math.abs((down.y - 60) - (60 - up.y)) <= 2, 'up must match down');
  assert.ok(left.x < 96 && up.y < 60, 'up/left must actually move');
});

test('weapon swap and reset keep train mode', () => {
  let g = G.newGame(0, 'train');
  g = G.withWeapon(g, 2);
  assert.equal(g.mode, 'train', 'swap must stay in train');
  assert.equal(g.weapon, 2);
  g = G.resetHunt(g);
  assert.equal(g.mode, 'train', 'reset must stay in train');
  assert.equal(g.weapon, 2);

  g = G.newGame(0, 'hunt');
  g = G.withWeapon(g, 1);
  assert.equal(g.mode, 'hunt');
});

test('all three weapon starts are valid', () => {
  for (let i = 0; i < 3; i++) {
    const g = G.newGame(i);
    ticks(g, 60);
    assert.equal(g.over, null);
  }
});
