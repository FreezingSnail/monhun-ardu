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
  // 76y: the chicken's target rect is its legs (9,11,12,13), so sword reach
  // must meet the legs; the melee centre still lands on the body.
  m.x = g.player.x + 14;
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
  // Demo ammo is unlimited (bead monhun-ardu-zza.0): reload still paces shots
  // but the magazine is never decremented.
  assert.equal(g.player.shells.ball, shells0);
  assert.ok(g.projectiles.length >= 1, 'a projectile should exist');
  assert.ok(g.player.reload > 0, 'reload should be running');
});

test('flail whirl stance enters and ball throw damages monster', () => {
  const g = G.newGame(1);
  const m = park(g);
  // 76y: the thrown ball must meet the legs-only target rect, so the beast
  // starts inside throw range (the old +40 assumed the full body box).
  m.x = g.player.x + 28;
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

test('monster variants: SWEEP never lunges, HEAVY spins inside 24 else bites', () => {
  const sweep = G.newGame(0, 'hunt', 1);
  for (const d of [10, 33, 41]) {
    const m = placeAtDistance(sweep, d);
    G.step(sweep, inp({}));
    assert.equal(m.atk.kind, 'sweep', 'sweep variant at dist ' + d);
  }

  // HEAVY (nch.4) kit: tail_spin at dist <= 30, bite from 31 out to the
  // pursue engage gate at 42.
  for (const d of [31, 41]) {
    const g = G.newGame(0, 'hunt', 2);
    const m = placeAtDistance(g, d);
    G.step(g, inp({}));
    assert.equal(m.atk.kind, 'bite', 'heavy bites at dist ' + d);
  }
  for (const d of [0, 10, 24, 30]) {
    const g = G.newGame(0, 'hunt', 2);
    const m = placeAtDistance(g, d);
    G.step(g, inp({}));
    assert.equal(m.atk.kind, 'tailSpin', 'heavy spins at dist ' + d);
  }
  // The spin is a four-window, contiguous, lock-away attack.
  const spin = G.MONSTER_ATTACKS.tailSpin;
  assert.equal(spin.facing, 'lock-away');
  assert.equal(spin.windows.length, 4);
  assert.deepEqual(spin.windows.map(w => [w.t0, w.t1]), [[0, 5], [6, 10], [11, 15], [16, 20]]);
  assert.ok(spin.windows[0].ox < 0 && spin.windows[2].ox > 0, 'whips back then front');

  // CHICKEN (nch.7): peck inside 28 (source-order p_peck), leap 29..41.
  const leapFar = G.newGame(0);
  placeAtDistance(leapFar, 33);
  G.step(leapFar, inp({}));
  assert.equal(leapFar.monster.atk.kind, 'leap', 'chicken leaps at 33');
  const peckClose = G.newGame(0);
  placeAtDistance(peckClose, 28);
  G.step(peckClose, inp({}));
  assert.equal(peckClose.monster.atk.kind, 'peck', 'chicken pecks at 28');
});

test('monster variants: CHICKEN pecks <= 28 and leaps 29..41', () => {
  for (const d of [0, 20, 27, 28]) {
    const g = G.newGame(0, 'hunt', 0);
    const m = placeAtDistance(g, d);
    G.step(g, inp({}));
    assert.equal(m.atk.kind, 'peck', 'peck at dist ' + d);
  }
  for (const d of [29, 35, 41]) {
    const g = G.newGame(0, 'hunt', 0);
    const m = placeAtDistance(g, d);
    G.step(g, inp({}));
    assert.equal(m.atk.kind, 'leap', 'leap at dist ' + d);
  }
});

test('monster variants: CHICKEN leap locks facing at windup; hunter flanks behind', () => {
  const g = G.newGame(0, 'hunt', 0);
  const m = g.monster;
  // Hunter due east at leap range: facing refreshes east, then p_leap commits it.
  m.x = 80;
  m.y = 40;
  g.player.x = m.x + (m.w >> 1) + 32 - (g.player.w >> 1);
  g.player.y = m.y + (m.h >> 1) - (g.player.h >> 1);
  m.state = 'pursue';
  m.t = 0;
  m.cd = 0;
  G.step(g, inp({}));
  assert.equal(m.atk.kind, 'leap', 'leap chosen at range');
  assert.equal(m.state, 'windup');
  assert.equal(m.face.x, 16, 'leap commits the east facing at windup');
  assert.equal(m.face.y, 0, 'level east');
  // Cross behind during windup: lock-at-windup freezes the vector.
  g.player.x = 20;
  for (let i = 0; i < G.MONSTER_ATTACKS.leap.windup; i++) G.step(g, inp({}));
  assert.equal(m.state, 'attack', 'windup completed into the leap');
  assert.equal(m.face.x, 16, 'facing stays committed through the leap');
  assert.equal(m.face.y, 0, 'facing stays level');
});

test('monster variants: CHICKEN broken legs force peck at leap range', () => {
  const g = G.newGame(0, 'hunt', 0);
  const m = park(g);
  m.zones.appendage.broken = true;   // disableAttacks: ['leap']
  placeAtDistance(g, 33);
  G.step(g, inp({}));
  assert.equal(m.atk.kind, 'peck', 'broken legs fall back to the close peck');
  assert.notEqual(m.atk.kind, 'leap', 'the leap is disabled while legs are broken');
});

test('monster variants: heavy tail_spin turns away at windup, frozen through attack', () => {
  const g = G.newGame(0, 'hunt', 2);
  const m = park(g);
  // Hunter east of the beast -> tracked vector +x; lock-away negates it once on
  // windup entry so the beast faces west, tail toward the hunter.
  m.x = 80;
  m.y = 40;
  g.player.x = 110;
  g.player.y = m.y + (m.h >> 1) - (g.player.h >> 1);
  m.state = 'pursue';
  m.t = 0;
  m.cd = 0;
  G.step(g, inp({}));
  assert.equal(m.atk.kind, 'tailSpin', 'spin selected inside 24');
  assert.equal(m.state, 'windup', 'windup entered');
  assert.ok(m.face.x < 0, 'beast turned its back (faces west)');
  assert.equal(m.face.y, 0, 'level turn-away');

  // Facing stays frozen through the attack even with the hunter behind.
  m.t = 1;
  G.step(g, inp({}));
  assert.equal(m.state, 'attack');
  g.player.x = 0;
  g.player.y = 0;
  G.step(g, inp({}));
  assert.ok(m.face.x < 0, 'facing frozen through the attack');
});

test('monster variants: lock-away tail hit knocks the hunter away from the beast', () => {
  const g = G.newGame(0, 'hunt', 2);
  const m = park(g);
  m.x = 100;
  m.y = 40;    // centre (120,54)
  m.atk = G.MONSTER_ATTACKS.tailSpin;
  m.state = 'attack';
  m.t = 0;
  m.face = { x: -16, y: 0 };  // turned away; window 0 rotates onto the hunter side
  g.player.x = 130;
  g.player.y = 46;
  g.player.iT = 0;
  const hp0 = g.player.hp;
  G.step(g, inp({}));
  assert.ok(g.player.hp < hp0, 'tail window hit lands');
  assert.ok(g.player.vx > 0, 'radial knock pushes east, away from the beast');
});

test('monster variants: HEAVY faceHold commits facing; flank hit lands the tail', () => {
  const g = G.newGame(0, 'hunt', 2);
  const m = park(g);
  const def = G.MONSTER_DEFS[2];
  assert.equal(def.faceHold, 10, 'heavy faceHold 10');
  assert.equal(def.keepDist, 12, 'heavy holds ground at 12');
  assert.equal(def.spinDist, 30, 'heavy spins inside 30');
  m.x = 80;
  m.y = 40;
  g.player.x = 120;
  g.player.y = m.y + (m.h >> 1) - (g.player.h >> 1); // hunter due east
  m.state = 'pursue';
  m.cd = 999999; // never choose an attack; cadence only
  G.step(g, inp({}));
  assert.equal(m.face.x, 16, 'facing east on first refresh');
  assert.equal(m.face.y, 0, 'level east');
  assert.equal(m.faceT, 9, 'faceHold countdown re-armed (10 set, decremented)');
  // Hunter crosses behind (west); facing stays committed east for the hold.
  g.player.x = 30;
  for (let i = 0; i < 9; i++) G.step(g, inp({}));
  assert.equal(m.face.x, 16, 'facing stale through the full hold');
  assert.equal(m.faceT, 0, 'countdown reached zero');
  // Hit from behind under the stale east facing: the tail box (ox -24) rotates
  // to the west side of the body and wins the higher multiplier.
  const tail = G.zoneHitResolve(g, 10, 1, m.x - 12, m.y + (m.h >> 1));
  assert.equal(tail.zone, 'appendage', 'from-behind hit lands the tail zone');
  // The next tick refreshes facing west, rotating the tail back in front.
  G.step(g, inp({}));
  assert.equal(m.face.x, -16, 'facing refreshed west after faceHold ticks');
  const body = G.zoneHitResolve(g, 10, 1, m.x - 12, m.y + (m.h >> 1));
  assert.equal(body.zone, null, 'same world point no longer in the tail');
});

test('monster variants: HEAVY hunter pressing in sees repeated tail_spin', () => {
  const g = G.newGame(0, 'hunt', 2);
  g.monster.hp = 100000; // survive the whole probe
  let spins = 0;
  let prev = g.monster.state;
  for (let i = 0; i < 2000; i++) {
    g.over = null;        // the probe measures attack selection, not the hunt
    g.player.hp = 100;    // top the hunter up so the sim keeps ticking
    G.step(g, inp({ mx: 1 })); // hunter pressing in toward the beast
    const st = g.monster.state;
    if (st === 'windup' && prev !== 'windup' && g.monster.atk && g.monster.atk.kind === 'tailSpin')
      spins++;
    prev = st;
  }
  // keepDist 12 holds the spin band instead of retreating to 24 and biting; the
  // pre-nch.4 mock saw 0 spins / 12 bites in this free-chase probe.
  assert.ok(spins >= 4, 'tail_spin repeats while the hunter presses in: ' + spins);
});

test('monster variants: HEAVY tail_spin holds when the hunter is in the 12..30 band', () => {
  const g = G.newGame(0, 'hunt', 2);
  g.monster.hp = 100000;
  let spins = 0;
  let bites = 0;
  let prev = g.monster.state;
  for (let i = 0; i < 2000; i++) {
    g.over = null;
    g.player.hp = 100;
    // Hold the hunter 18px due east and keep pressing in: the pair stays in the
    // 12..30 spin band, so attack selection is measured there (no retreat).
    const m = g.monster;
    const p = g.player;
    p.x = m.x + (m.w >> 1) - (p.w >> 1) + 18;
    p.y = m.y + (m.h >> 1) - (p.h >> 1);
    G.step(g, inp({ mx: 1 }));
    if (g.monster.state === 'windup' && prev !== 'windup' && g.monster.atk) {
      if (g.monster.atk.kind === 'tailSpin') spins++;
      else if (g.monster.atk.kind === 'bite') bites++;
    }
    prev = g.monster.state;
  }
  // The old keepDist 24 backed out of the band; keepDist 12 spins every cycle.
  assert.ok(spins >= 8, 'tail_spin repeats in the band: ' + spins);
  assert.equal(bites, 0, 'no bite while the hunter holds the 12..30 band');
});

test('monster variants: window telegraph mirrors the C++ window cache', () => {
  const bite = G.MONSTER_ATTACKS.bite;
  const spin = G.MONSTER_ATTACKS.tailSpin;
  // Windup: the tell is always window 0 (attackLoad caches it; no refresh runs
  // until MS_ATTACK), even when the countdown t overlaps window ticks.
  assert.equal(G.monsterTellWindow({ state: 'windup', t: 30 }, bite), bite.windows[0]);
  assert.equal(G.monsterTellWindow({ state: 'windup', t: 7 }, spin), spin.windows[0]);
  // Active: the window covering t.
  assert.equal(G.monsterTellWindow({ state: 'attack', t: 7 }, spin), spin.windows[1]);
  assert.equal(G.monsterTellWindow({ state: 'attack', t: 16 }, spin), spin.windows[3]);
  // Attack tail past the last window: the cache holds the last window.
  assert.equal(G.monsterTellWindow({ state: 'attack', t: 30 }, spin), spin.windows[3]);
  // Legacy reach attacks keep the scalar-path tell (no windows array).
  assert.equal(G.monsterTellWindow({ state: 'attack', t: 3 }, G.MONSTER_ATTACKS.sweep), null);
  // Hit test only fires inside a window (outside returns null, never NaN).
  assert.equal(G.monsterActiveWindow(spin, 25), null);
  assert.equal(G.monsterActiveWindow(spin, 16), spin.windows[3]);
  assert.equal(G.monsterActiveWindow(G.MONSTER_ATTACKS.sweep, 3), null);
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

test('pole variants: defs, initPoleKind and plain parity', () => {
  assert.equal(G.POLE_DEFS.length, 4);
  assert.deepEqual(G.POLE_DEFS[1].z, { x: -2, y: 0, w: 24, h: 20 }, 'sever cap box');
  assert.deepEqual(G.POLE_DEFS[2].z, { x: 4, y: 0, w: 18, h: 20 }, 'break horn box');
  assert.deepEqual(G.POLE_DEFS[3].z, { x: -2, y: 12, w: 24, h: 16 }, 'crack collar box');
  const g = G.newGame(0, 'train');
  assert.equal(g.pole.kind, 0, 'newGame defaults to PLAIN');
  assert.equal(g.pole.hp, 0, 'plain pool 0');
  assert.equal(g.pole.w, 20);
  G.initPoleKind(g, G.POLE_SEVER);
  assert.equal(g.pole.hp, 60, 'sever pool 60');
  G.initPoleKind(g, G.POLE_BREAK);
  assert.equal(g.pole.w, 20, 'break rect 20 wide (horn is a stage, not a resize)');
  assert.equal(g.pole.hp, 40, 'break pool 40');
  G.initPoleKind(g, G.POLE_CRACK);
  assert.equal(g.pole.hp, 30, 'crack pool 30');
  G.initPoleKind(g, 99);
  assert.equal(g.pole.kind, 0, 'out-of-range clamps to plain');
});

test('pole variants: damage stage from hp/hpMax and broken state', () => {
  const g = G.newGame(1, 'train');   // BREAK (all weapons break it)
  G.initPoleKind(g, G.POLE_BREAK);
  const pole = g.pole;
  const def = G.POLE_DEFS[G.POLE_BREAK];
  assert.equal(G.poleStage(pole, def), 0, 'full pool intact');
  pole.hp = 21;
  assert.equal(G.poleStage(pole, def), 0, 'above half stays intact');
  pole.hp = 20;
  assert.equal(G.poleStage(pole, def), 1, 'half pool damaged');
  pole.hp = 1;
  assert.equal(G.poleStage(pole, def), 1, 'low pool damaged');
  // Drain through the real part-centre hit path; broken wins regardless of pool.
  pole.hp = 40;
  pole.broken = 0;
  g.pole.x = 140;
  g.pole.y = 40;
  G.poleOnHit(g, 20, 153, 50);   // horn box x 144..162, y 40..60
  assert.equal(pole.hp, 20, 'blunt drains to half');
  assert.equal(G.poleStage(pole, def), 1, 'damaged after one break hit');
  G.poleOnHit(g, 20, 153, 50);
  assert.equal(pole.broken, 1, 'horn snapped off');
  assert.equal(pole.hp, 0);
  assert.equal(G.poleStage(pole, def), 2, 'broken stage after break');
  const p = G.newGame(0, 'train');
  assert.equal(G.poleStage(p.pole, G.POLE_DEFS[0]), 0, 'plain never leaves stage 0');
});

test('pole variants: sword drains the cap only, no head crit, lower post safe', () => {
  const g = G.newGame(0, 'train');
  G.initPoleKind(g, G.POLE_SEVER);
  const pole = g.pole;
  pole.x = 140;
  pole.y = 40;
  // Cap-centre hit: mul-101 zone, body-mul, no x1.4 crit.
  assert.equal(G.damagePole(g, 10, 150, 50), 10, 'cap hit is body-mul');
  g.train.total = 0;
  g.train.last = 0;
  G.poleOnHit(g, 10, 150, 72);   // lower post: body, no pool
  assert.equal(pole.hp, 60, 'lower-post hit does not drain');
  assert.equal(pole.broken, 0, 'lower-post hit does not break');
  for (let i = 0; i < 6; i++) G.poleOnHit(g, 10, 150, 50);
  assert.equal(pole.broken, 1, 'sever breaks');
  assert.equal(pole.w, 20, 'sever rect unchanged');
  assert.equal(pole.hp, 0, 'pool drained');
  G.poleOnHit(g, 10, 150, 50);
  assert.equal(g.train.last, 10, 'body hit after break');
});

test('pole variants: flail breaks the horn; rect stays 20', () => {
  const g = G.newGame(1, 'train');
  G.initPoleKind(g, G.POLE_BREAK);
  const pole = g.pole;
  pole.x = 140;
  pole.y = 40;
  assert.equal(pole.w, 20, 'rect starts 20');
  G.poleOnHit(g, 20, 153, 50);   // horn-centre play point
  assert.equal(pole.hp, 20, 'blunt drains the pool');
  assert.equal(pole.broken, 0);
  G.poleOnHit(g, 20, 153, 72);   // lower post: no drain
  assert.equal(pole.hp, 20, 'lower-post hit does not drain');
  G.poleOnHit(g, 20, 153, 50);
  assert.equal(pole.broken, 1, 'horn snaps off');
  assert.equal(pole.hp, 0);
  assert.equal(pole.w, 20, 'rect stays 20');
  assert.equal(pole.h, 36);
  assert.ok(g.freeze >= 6, 'break freeze');
});

test('pole variants: every weapon drains and breaks each variant on its part', () => {
  const variants = [G.POLE_SEVER, G.POLE_BREAK, G.POLE_CRACK];
  const cx = [150, 153, 150];
  const cy = [50, 50, 60];
  for (let vi = 0; vi < variants.length; vi++) {
    assert.equal(G.POLE_DEFS[variants[vi]].breakTypes, 7, 'variant accepts all phys');
    for (let weapon = 0; weapon < 3; weapon++) {
      const g = G.newGame(weapon, 'train');
      G.initPoleKind(g, variants[vi]);
      g.pole.x = 140;
      g.pole.y = 40;
      G.poleOnHit(g, 100, cx[vi], cy[vi]);
      assert.equal(g.pole.hp, 0, `variant ${vi} weapon ${weapon} drains`);
      assert.equal(g.pole.broken, 1, `variant ${vi} weapon ${weapon} breaks`);
    }
  }
});

test('pole variants: play path drains + breaks after part-centre hits, lower post safe', () => {
  const variants = [G.POLE_SEVER, G.POLE_BREAK, G.POLE_CRACK];
  const pools = [60, 40, 30];
  const cx = [150, 153, 150];
  const cy = [50, 50, 60];
  for (let vi = 0; vi < variants.length; vi++) {
    const hits = Math.ceil(pools[vi] / 10);
    for (let weapon = 0; weapon < 3; weapon++) {
      const g = G.newGame(weapon, 'train');
      G.initPoleKind(g, variants[vi]);
      g.pole.x = 140;
      g.pole.y = 40;
      G.poleOnHit(g, 10, 150, 72);   // lower post first: body only
      assert.equal(g.pole.hp, pools[vi], `variant ${vi} weapon ${weapon} lower post safe`);
      let n = 0;
      while (n < hits && !g.pole.broken) {
        G.poleOnHit(g, 10, cx[vi], cy[vi]);
        n++;
      }
      assert.equal(g.pole.hp, 0, `variant ${vi} weapon ${weapon} part drains`);
      assert.equal(g.pole.broken, 1, `variant ${vi} weapon ${weapon} breaks`);
      assert.equal(g.train.total, hits * 10 + 10, `variant ${vi} weapon ${weapon} every hit counted`);
    }
  }
});

test('pole variants: gun drains the part-locked collar and breaks it', () => {
  const g = G.newGame(2, 'train');
  G.initPoleKind(g, G.POLE_CRACK);
  g.pole.x = 140;
  g.pole.y = 40;
  assert.equal(g.pole.hp, 30);
  const sparks = () => g.effects.filter(e => !e.text).length;
  G.poleOnHit(g, 10, 150, 60);   // collar centre (box x -2..22, y 12..28)
  G.poleOnHit(g, 10, 150, 60);
  assert.equal(g.pole.broken, 0);
  const before = sparks();
  G.poleOnHit(g, 10, 150, 60);
  assert.equal(g.pole.broken, 1, 'pole cracked');
  assert.equal(g.pole.hp, 0);
  assert.ok(sparks() - before >= 3, 'break burst');
});

test('pole variants: weapon swap and reset keep the kind', () => {
  let g = G.newGame(0, 'train');
  G.initPoleKind(g, G.POLE_CRACK);
  g = G.withWeapon(g, 1);
  assert.equal(g.pole.kind, 3, 'swap keeps the pole variant');
  g = G.resetHunt(g);
  assert.equal(g.pole.kind, 3, 'reset keeps the pole variant');
});

test('pole variants: plain pole stays byte-identical (no zone, no break)', () => {
  const g = G.newGame(0, 'train');
  g.pole.x = 140;
  g.pole.y = 40;
  G.poleOnHit(g, 10, 150, 48);
  assert.equal(g.train.last, 14, 'plain head x1.4');
  assert.equal(g.pole.broken, 0, 'plain never breaks');
  assert.equal(g.pole.hp, 0, 'plain pool 0');
  assert.equal(g.pole.w, 20, 'plain rect unchanged');
});

test('spin sheet frame math matches the device selector (nch.3)', () => {
  // frame 0 is east; progress8 = (tick * 8) / active, truncated, wrapped mod 8.
  assert.equal(G.spinSheetFrame(0, 0, 20), 0, 'frame 0 at the first tick');
  assert.equal(G.spinSheetFrame(0, 2, 20), 0, 'still frame 0');
  assert.equal(G.spinSheetFrame(0, 3, 20), 1, 'second slice');
  assert.equal(G.spinSheetFrame(0, 5, 20), 2, 'third slice');
  assert.equal(G.spinSheetFrame(0, 20, 20), 0, 'full revolution wraps at the last tick');
  assert.equal(G.spinSheetFrame(4, 0, 20), 4, 'starts at the locked facing');
  assert.equal(G.spinSheetFrame(4, 20, 20), 4, 'and wraps back to it');
  assert.equal(G.spinSheetFrame(0, 3, 8), 3, 'active 8 advances per tick');
  assert.equal(G.spinSheetFrame(0, 1, 0), 0, 'stale active collapses to start');
});

test('DIR8 index from the locked facing vector (nch.3)', () => {
  assert.equal(G.dirIndexFromDelta(16, 0), 0, 'east');
  assert.equal(G.dirIndexFromDelta(11, 11), 1, 'southeast');
  assert.equal(G.dirIndexFromDelta(0, 16), 2, 'south');
  assert.equal(G.dirIndexFromDelta(-11, 11), 3, 'southwest');
  assert.equal(G.dirIndexFromDelta(-16, 0), 4, 'west');
  assert.equal(G.dirIndexFromDelta(-11, -11), 5, 'northwest');
  assert.equal(G.dirIndexFromDelta(0, -16), 6, 'north');
  assert.equal(G.dirIndexFromDelta(11, -11), 7, 'northeast');
});

test('heavy tail_spin windup holds the locked away body frame (nch.5)', () => {
  // Away facing south (start8 2): the windup body frame is 2, so the beast's
  // head points away and the tail at the hunter. The old E/W beast sheet
  // ignored fy and would have drawn the east side profile (head right) here.
  const m = { kind: 'heavy', state: 'windup', t: 10, atk: G.MONSTER_ATTACKS.tailSpin, face: { x: 0, y: 16 } };
  assert.equal(G.monsterSpinFrame(m), 2, 'windup away south -> frame 2');
  m.face = { x: -16, y: 0 };
  assert.equal(G.monsterSpinFrame(m), 4, 'windup away west -> frame 4');
  m.face = { x: 0, y: -16 };
  assert.equal(G.monsterSpinFrame(m), 6, 'windup away north -> frame 6');
  // The attack still spins from the locked facing over the active window.
  m.state = 'attack';
  m.t = 5;
  m.face = { x: 16, y: 0 };
  assert.equal(G.monsterSpinFrame(m), 2, 'attack spins from start8 (east t5)');
  // Not a heavy locked spin -> no rotation.
  m.atk = G.MONSTER_ATTACKS.bite;
  assert.equal(G.monsterSpinFrame(m), -1, 'bite does not rotate the body');
});

/* ------------------------------------------------- sheathe input prototype
 * The stow combo is playtest-configurable (SHEATHE_VARIANTS, default 'dab').
 * Each test pins its variant explicitly so the suite never depends on the
 * browser-cycle default.
 */

test('sheathe prototype: tap down then A+B stows, A draws (dab)', () => {
  G.setSheatheVariant('dab');
  const g = G.newGame(0);
  G.step(g, inp({ my: 1 }));
  G.step(g, inp({}));                    // tap complete
  G.step(g, inp({ a: true, b: true }));  // chord inside the window
  assert.equal(g.player.sheathed, true, 'down tap + chord stows');
  assert.equal(g.player.state, 'idle', 'chord consumed: no attack');
  assert.equal(g.player.atk, null, 'no swing');
  G.step(g, inp({}));                    // release B
  G.step(g, inp({ a: true }));           // A = draw into combo hit 1
  assert.equal(g.player.sheathed, false, 'A draws the weapon');
  assert.equal(g.player.state, 'attack', 'draw swings immediately');
  assert.ok(g.player.atk, 'draw attack runs');
});

test('sheathe prototype: stale down tap does not stow (dab)', () => {
  G.setSheatheVariant('dab');
  const g = G.newGame(0);
  G.step(g, inp({ my: 1 }));
  G.step(g, inp({}));
  ticks(g, 25);                          // window (18t) long gone
  G.step(g, inp({ a: true, b: true }));
  assert.equal(g.player.sheathed, false, 'expired sequence does not stow');
});

test('sheathe prototype: double-tap down then A+B stows (ddab)', () => {
  G.setSheatheVariant('ddab');
  const g = G.newGame(0);
  G.step(g, inp({ my: 1 }));
  G.step(g, inp({}));
  G.step(g, inp({ my: 1 }));             // second tap inside the window
  G.step(g, inp({}));
  G.step(g, inp({ a: true, b: true }));
  assert.equal(g.player.sheathed, true, 'double tap + chord stows');
});

test('sheathe prototype: tap down then B stows instead of rolling (db)', () => {
  G.setSheatheVariant('db');
  const g = G.newGame(0);
  G.step(g, inp({ my: 1 }));
  G.step(g, inp({}));
  G.step(g, inp({ b: true }));           // B press fires the combo
  G.step(g, inp({}));                    // release: no roll
  assert.equal(g.player.sheathed, true, 'down tap + B stows');
  assert.equal(g.player.state, 'idle', 'release did not roll');
});

test('sheathe prototype: hold down+B stows instead of the stance (dbh)', () => {
  G.setSheatheVariant('dbh');
  const g = G.newGame(1);
  ticks(g, 13, { my: 1, b: true });      // hold past HOLD_TICKS
  assert.equal(g.player.sheathed, true, 'hold down+B stows');
  assert.equal(g.player.stance, null, 'no whirl stance south');
});

test('sheathe prototype: raw A+B chord stows; stowed B taps roll (ab)', () => {
  G.setSheatheVariant('ab');
  const g = G.newGame(0);
  G.step(g, inp({ a: true, b: true }));
  assert.equal(g.player.sheathed, true, 'raw chord stows');
  G.step(g, inp({}));
  ticks(g, 13, { b: true });             // hold B while stowed
  assert.equal(g.player.stance, null, 'no stance while stowed');
  G.step(g, inp({}));                    // release
  G.step(g, inp({ b: true }));           // fresh B tap rolls
  G.step(g, inp({}));
  assert.equal(g.player.state, 'dodge', 'stowed B tap rolls');
});

test('sheathe prototype: stowed run outruns the sword walk (ab)', () => {
  G.setSheatheVariant('ab');
  const walk = G.newGame(0);
  ticks(walk, 10, { mx: 1 });
  const run = G.newGame(0);
  G.step(run, inp({ a: true, b: true }));
  G.step(run, inp({}));
  ticks(run, 10, { mx: 1 });
  assert.equal(walk.player.x - 96, 11, 'sword walk 18/16 px per tick');
  assert.equal(run.player.x - 96, 15, 'stowed run 24/16 px per tick');
});

test('sheathe prototype: stowed player renders without the weapon overlay', () => {
  G.setSheatheVariant('ab');
  const g = G.newGame(2);
  G.step(g, inp({ a: true, b: true }));
  assert.equal(g.player.sheathed, true, 'stowed for the draw pass');
  assert.doesNotThrow(() => G.render(stubCtx(), g, { debug: true, slow: true }));
});

/* --------------------------------------------------- combo debounce prototype
 * Module default is OFF (parity fixtures), browser boot arms MED. Each test
 * pins its profile explicitly.
 */

function waitIdle(g) {
  for (let i = 0; i < 80 && g.player.state !== 'idle'; i++) G.step(g, inp({}));
}

// Wait for idle, tap A, run until an attack starts (buffering through the
// debounce lock when needed). Returns true when an attack started.
function comboHit(g) {
  waitIdle(g);
  G.step(g, inp({ a: true }));
  for (let i = 0; i < 40 && g.player.state !== 'attack'; i++) G.step(g, inp({}));
  return g.player.state === 'attack';
}

test('debounce: OFF keeps the zero-gap buffered chain (fixture default)', () => {
  G.setDebounceProfile('OFF');
  const g = G.newGame(0);
  assert.ok(comboHit(g), 'hit 1 starts');
  waitIdle(g);
  assert.equal(g.player.chain, 1, 'chain advanced');
  assert.equal(g.player.chainLock, 0, 'no lock when off');
  assert.equal(g.player.chainWin, 14, 'window armed immediately');
  assert.ok(comboHit(g), 'hit 2 starts from the open window');
});

test('debounce: gap locks each non-finisher hit, window opens after (med)', () => {
  G.setDebounceProfile('MED');
  const g = G.newGame(0);
  assert.ok(comboHit(g), 'hit 1 starts');
  waitIdle(g);
  assert.equal(g.player.chain, 1, 'chain advanced');
  assert.equal(g.player.chainLock, 6, 'gap lock after hit 1');
  assert.equal(g.player.chainWin, 0, 'window closed during the lock');
  G.step(g, inp({ a: true }));
  assert.equal(g.player.state, 'idle', 'press during the lock is buffered');
  for (let i = 0; i < 40 && g.player.state !== 'attack'; i++) G.step(g, inp({}));
  assert.equal(g.player.state, 'attack', 'buffered press fires after the lock');
});

test('debounce: finisher lock is longer and one early press expires (med)', () => {
  G.setDebounceProfile('MED');
  const g = G.newGame(0);
  assert.ok(comboHit(g), 'hit 1');
  waitIdle(g);
  assert.ok(comboHit(g), 'hit 2');
  waitIdle(g);
  assert.ok(comboHit(g), 'hit 3');
  waitIdle(g);
  assert.equal(g.player.chain, 0, 'finisher resets the chain');
  assert.equal(g.player.chainLock, 18, 'finisher lock 18');
  assert.equal(g.player.chainWin, 0, 'window closed');
  G.step(g, inp({ a: true }));            // aBuffer 16 < lock 18: expires
  for (let i = 0; i < 30; i++) G.step(g, inp({}));
  assert.equal(g.player.state, 'idle', 'early single press was dropped');
  assert.ok(comboHit(g), 'fresh press after the lock restarts the chain');
});

test('debounce: one loose A press survives the longest gap lock (brutal)', () => {
  G.setDebounceProfile('BRUTAL');
  const g = G.newGame(0);
  assert.ok(comboHit(g), 'hit 1');
  waitIdle(g);
  assert.equal(g.player.chainLock, 14, 'brutal gap lock');
  G.step(g, inp({ a: true }));            // single press at lock start
  for (let i = 0; i < 40 && g.player.state !== 'attack'; i++) G.step(g, inp({}));
  assert.equal(g.player.state, 'attack', 'buffered press fires after the long lock');
});

/* -------------------------------------------------- B branch input buffer */

test('branch buffer: loose A A B still combos through the debounce lock (med)', () => {
  G.setDebounceProfile('MED');
  G.setSheatheVariant('ddab');
  const g = G.newGame(0);
  assert.ok(comboHit(g), 'hit 1');
  waitIdle(g);
  assert.ok(comboHit(g), 'hit 2 running');
  // run A2 into recovery (startup 3 + active 5 = 8)
  for (let i = 0; i < 9 && g.player.state === 'attack'; i++) G.step(g, inp({}));
  G.step(g, inp({ b: true }));                             // tap B in recovery
  for (let i = 0; i < 8; i++) G.step(g, inp({ b: true })); // hold through completion
  G.step(g, inp({}));                                      // release during the lock
  assert.equal(g.player.state, 'idle', 'no roll while the branch is queued');
  assert.ok(g.player.bBuffer > 0, 'B buffer still pending');
  for (let i = 0; i < 30 && g.player.state !== 'attack'; i++) G.step(g, inp({}));
  assert.equal(g.player.state, 'attack', 'queued branch fires when the window opens');
  assert.equal(g.player.atk.id, 'spincut', 'stage-2 branch runs');
});

test('branch buffer: early B in startup still dodge-cancels (med)', () => {
  G.setDebounceProfile('MED');
  const g = G.newGame(0);
  assert.ok(comboHit(g), 'hit 1 running');
  G.step(g, inp({ b: true }));   // t ~2: startup, no branch yet and no buffer
  G.step(g, inp({}));
  assert.equal(g.player.state, 'dodge', 'sword dodge-cancel preserved');
  assert.equal(g.player.bBuffer, 0, 'nothing queued');
});

test('branch buffer: holding B through recovery still enters the stance (med)', () => {
  G.setDebounceProfile('MED');
  const g = G.newGame(0);
  assert.ok(comboHit(g), 'hit 1 running');
  for (let i = 0; i < 9 && g.player.state === 'attack'; i++) G.step(g, inp({}));
  for (let i = 0; i < 13; i++) G.step(g, inp({ b: true }));   // past HOLD_TICKS
  assert.equal(g.player.stance, 'parry', 'hold still enters parry');
  assert.equal(g.player.bBuffer, 0, 'release cleared the queue');
  G.step(g, inp({}));
  assert.equal(g.player.stance, null, 'release exits the stance');
});

/* --------------------------------------- sheathe variant: hold Down+A+B */

test('sheathe dabhold: holding Down+A+B stows after the hold window', () => {
  G.setSheatheVariant('dabhold');
  const g = G.newGame(0);
  ticks(g, 1, { my: 1 });
  G.step(g, inp({ my: 1, a: true }));                 // A deferred, waiting for B
  G.step(g, inp({ my: 1, a: true, b: true }));        // B joins: session starts
  assert.equal(g.player.sheathed, false, 'not stowed yet');
  assert.equal(g.player.state, 'idle', 'A deferred, no attack');
  ticks(g, 8, { my: 1, a: true, b: true });
  assert.equal(g.player.sheathed, true, 'held 3-button stows');
});

test('sheathe dabhold: Down+A without B still swings (deferred attack)', () => {
  G.setSheatheVariant('dabhold');
  const g = G.newGame(0);
  ticks(g, 1, { my: 1 });
  G.step(g, inp({ my: 1, a: true }));
  for (let i = 0; i < 10 && g.player.state !== 'attack'; i++) G.step(g, inp({ my: 1, a: true }));
  assert.equal(g.player.state, 'attack', 'deferred A attacks');
  assert.equal(g.player.sheathed, false, 'not stowed');
});

test('sheathe dabhold: releasing B early cancels the stow, A swings', () => {
  G.setSheatheVariant('dabhold');
  const g = G.newGame(0);
  ticks(g, 1, { my: 1 });
  G.step(g, inp({ my: 1, a: true }));
  G.step(g, inp({ my: 1, a: true, b: true }));
  G.step(g, inp({ my: 1, a: true }));                 // B released: session cancels
  for (let i = 0; i < 10 && g.player.state !== 'attack'; i++) G.step(g, inp({ my: 1, a: true }));
  assert.equal(g.player.sheathed, false, 'cancelled');
  assert.equal(g.player.state, 'attack', 'deferred A still swings');
});

/* --------------------------------------- move-set expansion: roll attacks */

test('roll attack: A out of the evade runs the weapon roll move', () => {
  for (const [w, id] of [[0, 'rollslash'], [1, 'rollsweep'], [2, 'shieldbash']]) {
    const g = G.newGame(w);
    G.step(g, inp({ b: true }));   // tap B: sword dodge / flail deflect / gun shove
    G.step(g, inp({}));
    assert.ok(g.player.state === 'dodge' || g.player.state === 'deflect' || g.player.state === 'shove', 'evade state w' + w);
    G.step(g, inp({ a: true }));
    assert.equal(g.player.state, 'attack', 'roll attack started w' + w);
    assert.equal(g.player.atk.id, id, 'roll move id w' + w);
  }
});

test('shield bash lunges the hunter forward (lunge 30)', () => {
  const g = G.newGame(2);
  G.step(g, inp({ b: true }));   // shove
  G.step(g, inp({}));
  const x0 = g.player.x;
  G.step(g, inp({ a: true }));   // bash, facing east by default
  assert.equal(g.player.atk.id, 'shieldbash');
  assert.ok(g.player.vx > 0, 'forward velocity applied');
  for (let i = 0; i < 8; i++) G.step(g, inp({}));
  assert.ok(g.player.x > x0, 'bash carried the hunter forward');
});

test('guard strafe: d-pad moves with locked facing (gun)', () => {
  const g = G.newGame(2);
  ticks(g, 4, { mx: 1 });                 // face east
  assert.equal(g.player.fx, 16, 'facing east');
  ticks(g, 13, { b: true });              // hold B -> guard
  assert.equal(g.player.stance, 'guard', 'guard up');
  const x0 = g.player.x;
  ticks(g, 16, { mx: -1, b: true });      // strafe west, shield still east
  assert.equal(g.player.fx, 16, 'facing locked while strafing');
  assert.ok(g.player.x < x0, 'strafed west');
});

/* ----------------------------------- move-set expansion: stage-3 finisher */

test('stage-3 finisher: A A A then B runs the finisher branch (all weapons)', () => {
  G.setDebounceProfile('MED');   // debounce profiles keep the chain through swings
  for (const [w, id] of [[0, 'helmsplit'], [1, 'earthslam'], [2, 'cannonblast']]) {
    const g = G.newGame(w);
    assert.ok(comboHit(g), 'hit 1 w' + w);
    waitIdle(g);
    assert.ok(comboHit(g), 'hit 2 w' + w);
    waitIdle(g);
    assert.ok(comboHit(g), 'hit 3 w' + w);
    waitIdle(g);
    assert.equal(g.player.finWin, true, 'finisher window armed w' + w);
    assert.equal(g.player.chain, 0, 'chain reset for the finisher w' + w);
    G.step(g, inp({ b: true }));   // tap B: buffered through the finisher lock
    G.step(g, inp({}));
    assert.equal(g.player.bBuffer > 0, true, 'finisher branch queued w' + w);
    for (let i = 0; i < 60 && g.player.state !== 'attack'; i++) G.step(g, inp({}));
    assert.equal(g.player.state, 'attack', 'finisher branch runs w' + w);
    assert.equal(g.player.atk.id, id, 'finisher id w' + w);
  }
});

/* ----------------------------------------- move-set expansion: direction + A */

test('direction + A: thrust opener replaces combo hit 1', () => {
  G.setDebounceProfile('OFF');
  const g = G.newGame(0);
  ticks(g, 4, { mx: 1 });                 // face east, moving
  G.step(g, inp({ mx: 1, a: true }));
  assert.equal(g.player.state, 'attack', 'thrust runs');
  assert.equal(g.player.atk.id, 'thrust', 'alt move id');
  assert.ok(g.player.vx > 0, 'thrust lunges forward');
});

test('direction + A: mid-combo hits keep the normal combo data', () => {
  G.setDebounceProfile('OFF');
  const g = G.newGame(0);
  ticks(g, 4, { mx: 1 });
  G.step(g, inp({ mx: 1, a: true }));     // alt hit 1
  waitIdle(g);
  G.step(g, inp({ mx: 1, a: true }));     // chain 1 -> normal combo hit 2
  assert.equal(g.player.state, 'attack', 'hit 2 started');
  assert.equal(g.player.atk.reach, 13, 'normal combo hit 2 data');
});

/* -------------------------------------------- move-set expansion: charge */

test('charge: flail hold A past the swing -> level 1, longer hold -> level 2', () => {
  G.setDebounceProfile('OFF');
  const g = G.newGame(1);
  let guard = 0;
  while (guard++ < 80 && g.player.state !== 'charge') G.step(g, inp({ a: true }));
  assert.equal(g.player.state, 'charge', 'charge entered after the swing');
  assert.equal(g.player.chargeT, 0, 'meter starts at 0');
  G.step(g, inp({}));                     // release right away: level 1
  assert.equal(g.player.state, 'attack', 'charge swing runs');
  assert.equal(g.player.atk.id, 'chargeslam1', 'level 1 id');

  const g2 = G.newGame(1);
  guard = 0;
  while (guard++ < 80 && g2.player.state !== 'charge') G.step(g2, inp({ a: true }));
  ticks(g2, G.CHARGE_L2, { a: true });    // hold to max
  G.step(g2, inp({}));
  assert.equal(g2.player.atk.id, 'chargeslam2', 'level 2 id');
});

test('charge: gun release fires the charged ball', () => {
  G.setDebounceProfile('OFF');
  const g = G.newGame(2);
  let guard = 0;
  while (guard++ < 80 && g.player.state !== 'charge') G.step(g, inp({ a: true }));
  assert.equal(g.player.state, 'charge', 'gun charge entered');
  G.step(g, inp({}));
  assert.ok(g.projectiles.length > 0, 'projectile spawned');
  assert.equal(g.projectiles[0].dmg, 34, 'level-1 charge ball dmg');
});

test('charge: weapons without charge data never enter the stance (sword)', () => {
  G.setDebounceProfile('OFF');
  const g = G.newGame(0);
  for (let i = 0; i < 40; i++) G.step(g, inp({ a: true }));
  assert.notEqual(g.player.state, 'charge', 'sword has no charge');
});
