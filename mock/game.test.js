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

// Place the beast at a given center-to-center distance from the player, primed
// to pick an attack on the next tick (pursue state, cooldown expired).
function placeAtDistance(g, d) {
  const p = g.player;
  const m = g.monster;
  m.state = 'pursue';
  m.t = 0;
  m.cd = 0;
  m.x = p.x + (p.w >> 1) + d - (m.w >> 1);
  m.y = p.y + (p.h >> 1) - (m.h >> 1);
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

test('isqrt exact for small and large values', () => {
  const cases = [[0, 0], [1, 1], [2, 1], [9, 3], [10, 3], [15, 3], [16, 4], [255, 15], [256, 16], [1000, 31], [65535, 255]];
  for (const [n, want] of cases) assert.equal(G.isqrt(n), want, 'isqrt(' + n + ')');
});

test('ball shell flies at ~2.2 px per tick', () => {
  const g = G.newGame(2, 'train');
  g.pole.x = 5000;
  park(g);
  ticks(g, G.HOLD_TICKS + 2, { b: true });
  G.step(g, inp({ b: true, a: true }));
  const pr = g.projectiles[0];
  const x0 = pr.x;
  ticks(g, 10, { b: true });
  const pxPerTick = (pr.x - x0) / 16 / 10;
  assert.ok(pxPerTick > 2.0 && pxPerTick < 2.4, 'ball speed ' + pxPerTick);
});

test('all three weapon starts are valid', () => {
  for (let i = 0; i < 3; i++) {
    const g = G.newGame(i);
    ticks(g, 60);
    assert.equal(g.over, null);
  }
});

test('monster variants: roster + spawn stats per def, default is legacy LUNGE', () => {
  assert.equal(G.MONSTER_DEFS.length, 3);
  assert.deepEqual(G.MONSTER_DEFS.map(d => d.atkDist), [32, -1, 24]);
  const want = [
    { w: 32, h: 24, hp: 200, spd: 5 },
    { w: 28, h: 22, hp: 150, spd: 7 },
    { w: 40, h: 28, hp: 320, spd: 3 },
  ];
  for (let i = 0; i < want.length; i++) {
    const g = G.newGame(0, 'hunt', i);
    const m = g.monster;
    assert.equal(g.monsterIndex, i, 'kind index recorded');
    assert.equal(m.w, want[i].w, 'w ' + i);
    assert.equal(m.h, want[i].h, 'h ' + i);
    assert.equal(m.hp, want[i].hp, 'hp ' + i);
    assert.equal(m.hpMax, want[i].hp, 'hpMax ' + i);
    assert.equal(m.spd, want[i].spd, 'spd ' + i);
    assert.equal(m.x, 200);
    assert.equal(m.y, 40);
    assert.equal(m.t, 90);
    assert.equal(m.cd, 140);
    assert.equal(m.state, 'idle');
  }
  const d = G.newGame(0);
  assert.equal(d.monsterIndex, 0, 'newGame defaults to LUNGE');
  assert.equal(d.monster.w, 32);
  assert.equal(d.monster.hp, 200);
});

test('monster variants: SWEEP never lunges, HEAVY lunges past 24', () => {
  const sweep = G.newGame(0, 'hunt', 1);
  for (const d of [10, 33, 41]) {
    const m = placeAtDistance(sweep, d);
    G.step(sweep, inp({}));
    assert.equal(m.atk.kind, 'sweep', 'sweep variant at dist ' + d);
  }

  // HEAVY atkDist 24 with the pursue engage gate at dist < 42: lunge band
  // 25..41, sweep at 24 and below. A lower atkDist widens the lunge band.
  for (let d = 25; d <= 41; d++) {
    const g = G.newGame(0, 'hunt', 2);
    const m = placeAtDistance(g, d);
    G.step(g, inp({}));
    assert.equal(m.atk.kind, 'lunge', 'heavy lunges at dist ' + d);
  }
  for (const d of [0, 10, 24]) {
    const g = G.newGame(0, 'hunt', 2);
    const m = placeAtDistance(g, d);
    G.step(g, inp({}));
    assert.equal(m.atk.kind, 'sweep', 'heavy sweeps at dist ' + d);
  }

  const lunge = G.newGame(0);
  placeAtDistance(lunge, 33);
  G.step(lunge, inp({}));
  assert.equal(lunge.monster.atk.kind, 'lunge', 'legacy lunge at 33');
  const inside = G.newGame(0);
  placeAtDistance(inside, 32);
  G.step(inside, inp({}));
  assert.equal(inside.monster.atk.kind, 'sweep', 'legacy sweep at 32');
});

test('monster variants: weapon swap and reset keep the chosen beast', () => {
  let g = G.newGame(0, 'hunt', 2);
  g = G.withWeapon(g, 1);
  assert.equal(g.monsterIndex, 2, 'swap keeps kind');
  assert.equal(g.monster.w, 40);
  assert.equal(g.monster.hp, 320);
  g = G.resetHunt(g);
  assert.equal(g.monsterIndex, 2, 'reset keeps kind');
  assert.equal(g.monster.hp, 320);
});

test('monster variants: train mode with HEAVY keeps the pole path intact', () => {
  const g = G.newGame(0, 'train', 2);
  assert.equal(g.monsterIndex, 2);
  const mx = g.monster.x;
  const my = g.monster.y;
  g.pole.x = g.player.x + 20;
  g.pole.y = g.player.y;
  G.step(g, inp({ a: true }));
  ticks(g, 20);
  assert.ok(g.train.total > 0, 'pole should take damage');
  ticks(g, 200);
  assert.equal(g.monster.x, mx, 'beast must not move in train mode');
  assert.equal(g.monster.y, my);
  assert.equal(g.player.hp, 100, 'nothing can hurt player in train mode');
});
