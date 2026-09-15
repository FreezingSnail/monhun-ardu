#pragma once
// Host unit tests for src/core/monster.hpp — permanent, co-located with repo
// tests. Mirrors mock/game.test.js monster-half expectations (mock is the
// source of truth for numbers): tell timing, recovery windows, push rule,
// crit zone, stun->recover, knockback ints, and the player-hit routing.
#include "test.hpp"
#include "../src/core/monster.hpp"

using namespace mh;

namespace {

// initGame + spawn the beast, wired into Game::target.
void newHunt(Game& g, int8_t weapon = W_SWORD) {
  initGame(g, weapon);
  initMonster(g);
}

// Full hunt ticks (player then monster).
void hunt(Game& g, int n, Input in = Input{ 0, 0, false, false }) {
  for (int i = 0; i < n; i++) stepHunt(g, in);
}

// Raw beast ticks (no player update, no tick advance).
void beast(Game& g, int n) {
  for (int i = 0; i < n; i++) updateMonster(g);
}

} // namespace

void MonsterSuite(TestRunner& runner) {
  TestSuite suite("Monster FSM + hit resolution (src/core/monster.hpp)");

  {
    Test t("MONSTER_ATTACKS + init fields match prototype");
    t.assert(MONSTER_ATTACKS[0].kind, MK_LUNGE, "lunge kind");
    t.assert(MONSTER_ATTACKS[0].windup, 40, "lunge windup");
    t.assert(MONSTER_ATTACKS[0].active, 10, "lunge active");
    t.assert(MONSTER_ATTACKS[0].recover, 55, "lunge recover");
    t.assert(MONSTER_ATTACKS[0].speedF, 34, "lunge speedF");
    t.assert(MONSTER_ATTACKS[0].dmg, 12, "lunge dmg");
    t.assert(MONSTER_ATTACKS[0].reach, 12, "lunge reach");
    t.assert(MONSTER_ATTACKS[0].hw, 24, "lunge hw");
    t.assert(MONSTER_ATTACKS[0].hh, 22, "lunge hh");
    t.assert(MONSTER_ATTACKS[1].kind, MK_SWEEP, "sweep kind");
    t.assert(MONSTER_ATTACKS[1].windup, 48, "sweep windup");
    t.assert(MONSTER_ATTACKS[1].active, 12, "sweep active");
    t.assert(MONSTER_ATTACKS[1].recover, 60, "sweep recover");
    t.assert(MONSTER_ATTACKS[1].dmg, 9, "sweep dmg");
    t.assert(MONSTER_ATTACKS[1].reach, 17, "sweep reach");
    t.assert(MONSTER_ATTACKS[1].hw, 32, "sweep hw");
    t.assert(MONSTER_ATTACKS[1].hh, 24, "sweep hh");

    Game g;
    newHunt(g);
    t.assert(g.monster.w, 32, "monster width");
    t.assert(g.monster.h, 24, "monster height");
    t.assert(g.monster.hp, 200, "monster hp");
    t.assert(g.monster.hpMax, 200, "monster hpMax");
    t.assert(g.monster.state, MS_IDLE, "starts idle");
    t.assert(g.monster.t, 90, "idle timer 90");
    t.assert(g.monster.cd, 140, "cooldown 140");
    t.assert(g.monster.fx, -fp::FP, "starts facing W");
    t.assert(g.monster.fy, 0, "starts facing flat");
    t.assert(g.monster.circleDir, 1, "circle dir right");
    t.assert(g.monster.spd, 5, "pursue speed 5/16");
    suite.addTest(t);
  }

  {
    Test t("chooseAttack: lunge beyond 32px, sweep inside");
    Game g;
    newHunt(g);
    chooseAttack(g.monster, 33);
    t.assert(g.monster.atk->kind, MK_LUNGE, "dist 33 -> lunge");
    t.assert(g.monster.state, MS_WINDUP, "windup state");
    t.assert(g.monster.t, 40, "lunge tell 40");
    t.assert(g.monster.windupMax, 40, "windupMax recorded");
    chooseAttack(g.monster, 32);
    t.assert(g.monster.atk->kind, MK_SWEEP, "dist 32 -> sweep");
    t.assert(g.monster.t, 48, "sweep tell 48");
    suite.addTest(t);
  }

  {
    Test t("windup tell counts down 40 ticks, then attack starts");
    Game g;
    newHunt(g);
    Monster& m = g.monster;
    m.atk = &MONSTER_ATTACKS[0];
    m.state = MS_WINDUP;
    m.t = 40;
    m.windupMax = 40;
    beast(g, 39);
    t.assert(m.state, MS_WINDUP, "still winding up at 39");
    t.assert(m.t, 1, "one tell tick left");
    beast(g, 1);
    t.assert(m.state, MS_ATTACK, "tell released into attack");
    t.assert(m.t, 0, "attack starts at t 0");
    t.assert(m.fx, -fp::FP, "faced west at release");
    t.assert(m.lvx, -34, "lunge velocity int (-16*34)>>4");
    t.assert(m.lvy, 0, "flat lunge");
    suite.addTest(t);
  }

  {
    Test t("sweep recovery window closes, cd = 55 + tick%40, circle flips");
    Game g;
    newHunt(g);
    Monster& m = g.monster;
    m.atk = &MONSTER_ATTACKS[1];
    m.state = MS_ATTACK;
    m.t = 0;
    m.x = 10; m.y = 10; // far from player: no contact
    hunt(g, 72);
    t.assert(m.state, MS_ATTACK, "still active+recovering at 72");
    hunt(g, 1);
    t.assert(m.state, MS_PURSUE, "attack releases to pursue");
    t.assert(m.cd, 55 + (g.tick % 40), "post-attack cooldown");
    t.assert(m.circleDir, (g.tick % 2) ? 1 : -1, "circle direction flips");
    suite.addTest(t);
  }

  {
    Test t("recovery state releases to pursue with cd 55");
    Game g;
    newHunt(g);
    g.monster.state = MS_RECOVER;
    g.monster.t = 3;
    beast(g, 3);
    t.assert(g.monster.state, MS_PURSUE, "recover -> pursue");
    t.assert(g.monster.cd, 55, "short cooldown after recover");
    suite.addTest(t);
  }

  {
    Test t("stun ends into recover 24, then pursue cd 55");
    Game g;
    newHunt(g);
    g.monster.stun = 5;
    beast(g, 5);
    t.assert(g.monster.stun, 0, "stun drained");
    t.assert(g.monster.state, MS_RECOVER, "stun -> recover");
    t.assert(g.monster.t, 24, "recover window 24");
    beast(g, 24);
    t.assert(g.monster.state, MS_PURSUE, "recover -> pursue after 24");
    t.assert(g.monster.cd, 55, "recover cooldown 55");
    suite.addTest(t);
  }

  {
    Test t("crit zone: facing-side hit is x1.4 (integer 14/10)");
    Game g;
    newHunt(g);
    Monster& m = g.monster;
    m.x = 200; m.y = 40; m.fx = -16; m.fy = 0; // center (216,52), facing W
    m.hp = 200; g.freeze = 0;
    damageMonster(g, 10, 210, 52); // west of center: proj +6
    t.assert(m.hp, 186, "crit 10 -> 14");
    t.assert(m.hitFlash, 4, "hit flash 4");
    t.assert(g.freeze, 6, "crit freeze 6");

    m.hp = 200; g.freeze = 0;
    damageMonster(g, 10, 220, 52); // east of center: proj -4
    t.assert(m.hp, 190, "no crit 10 -> 10");
    t.assert(g.freeze, 4, "normal freeze 4");

    m.fx = 16; m.hp = 200; // facing E, same side flips
    damageMonster(g, 10, 222, 52);
    t.assert(m.hp, 186, "head side of new facing crits");

    m.fx = -16; m.hp = 200;
    damageMonster(g, 0, 220, 52); // floor at 1 even for 0 dmg
    t.assert(m.hp, 199, "damage floors at 1");
    suite.addTest(t);
  }

  {
    Test t("hp <= 0 -> dead + over win + long freeze");
    Game g;
    newHunt(g);
    g.monster.hp = 5;
    damageMonster(g, 10, 220, 52);
    t.assert(g.monster.hp, 0, "hp clamped to 0");
    t.assert(g.monster.state, MS_DEAD, "dead state");
    t.assert(g.over, OVER_WIN, "hunt won");
    t.assert(g.freeze, 12, "death freeze 12");
    suite.addTest(t);
  }

  {
    Test t("lunge velocities are integer from 8-dir facing");
    Game g;
    newHunt(g);
    Monster& m = g.monster;
    m.atk = &MONSTER_ATTACKS[0];
    m.fx = 16; m.fy = 0;
    startMonsterAttack(m);
    t.assert(m.state, MS_ATTACK, "attack entered");
    t.assert(m.lvx, 34, "E lunge 34");
    t.assert(m.lvy, 0, "E lunge flat");
    m.fx = 11; m.fy = 11;
    startMonsterAttack(m);
    t.assert(m.lvx, 23, "SE lunge integer trunc (11*34)>>4");
    t.assert(m.lvy, 23, "SE lunge y");
    m.atk = &MONSTER_ATTACKS[1];
    m.fx = 16; m.fy = 0;
    startMonsterAttack(m);
    t.assert(m.lvx, 0, "sweep stationary");
    t.assert(m.lvy, 0, "sweep stationary y");
    suite.addTest(t);
  }

  {
    Test t("lunge moves via addVel with sub-pixel carry");
    Game g;
    newHunt(g);
    Monster& m = g.monster;
    m.atk = &MONSTER_ATTACKS[0];
    m.state = MS_ATTACK;
    m.t = 0;
    m.lvx = 34; m.lvy = 0;
    m.x = 10; m.y = 10; m.subX = 0; m.subY = 0;
    updateMonster(g);
    t.assert(m.x, 12, "34/16 px moved");
    t.assert(m.subX, 2, "sub-pixel remainder carried");
    suite.addTest(t);
  }

  {
    Test t("push rule: beast gives way, idle player never shoved");
    Game g;
    newHunt(g);
    Monster& m = g.monster;
    m.state = MS_PURSUE;
    m.cd = 30000;
    m.x = g.player.x + 8;
    m.y = g.player.y + 4;
    const int px = g.player.x;
    const int py = g.player.y;
    const int mx0 = m.x;
    beast(g, 40);
    t.assert(g.player.x, px, "player x untouched");
    t.assert(g.player.y, py, "player y untouched");
    t.assert(g.player.hp, 100, "player unhurt");
    t.assert(m.x != mx0 || m.y != g.player.y + 4, true, "beast moved aside");
    suite.addTest(t);
  }

  {
    Test t("push rule: attacking/windup beast shoves the player");
    Game g;
    newHunt(g);
    Monster& m = g.monster;
    m.state = MS_WINDUP;
    m.atk = &MONSTER_ATTACKS[0];
    m.t = 30000;
    m.x = g.player.x + 8;
    m.y = g.player.y + 4;
    const int px = g.player.x;
    const int py = g.player.y;
    const int mx0 = m.x;
    updateMonster(g);
    t.assert(g.player.x, px - 8, "player shoved west by overlap");
    t.assert(g.player.y, py, "player y unchanged");
    t.assert(m.x, mx0, "beast holds ground while attacking");
    suite.addTest(t);
  }

  {
    Test t("monster sweep attack damages player (mock parity)");
    Game g;
    newHunt(g);
    Monster& m = g.monster;
    m.x = g.player.x + 20;
    m.y = g.player.y;
    m.state = MS_ATTACK;
    m.atk = &MONSTER_ATTACKS[1];
    m.t = 0;
    hunt(g, 10);
    t.assert(g.player.hp, 91, "sweep 9 dmg lands once");
    t.assertGreaterThan(g.player.iT, 0, "i-frames started");
    suite.addTest(t);
  }

  {
    Test t("player attack damages monster through Game::target");
    Game g;
    newHunt(g);
    Monster& m = g.monster;
    m.state = MS_RECOVER; // parked, no movement
    m.t = 30000;
    m.x = g.player.x + 20;
    m.y = g.player.y;
    hunt(g, 1, Input{ 0, 0, true, false });
    hunt(g, 1, Input{ 0, 0, false, false });
    hunt(g, 18);
    t.assert(m.hp, 188, "sword hit crit for 12");
    suite.addTest(t);
  }

  {
    Test t("playerHit routing: parry/deflect stun, guard chip, iT gate");
    // parry: stun 60 + riposte 90
    Game g;
    newHunt(g, W_SWORD);
    g.monster.state = MS_RECOVER;
    g.monster.t = 30000;
    hunt(g, 13, Input{ 0, 0, false, true });
    t.assert(g.player.stance, ST_PARRY, "parry up");
    g.monster.stun = 0;
    playerHurt(g, 10, -16, 0);
    t.assert(g.monster.stun, 60, "parry stuns monster 60");
    t.assert(g.player.riposteT, 90, "parry riposte timer");

    // deflect: stun 28
    Game g2;
    newHunt(g2, W_FLAIL);
    g2.monster.state = MS_RECOVER;
    g2.monster.t = 30000;
    hunt(g2, 1, Input{ 0, 0, false, true });
    hunt(g2, 1, Input{ 0, 0, false, false });
    t.assert(g2.player.state, PS_DEFLECT, "deflect up");
    g2.monster.stun = 0;
    playerHurt(g2, 10, -16, 0);
    t.assert(g2.monster.stun, 28, "deflect stuns monster 28");
    t.assert(g2.player.state, PS_DEFLECT, "deflect absorbs");

    // guard: chip 25%, no monster stun
    Game g3;
    newHunt(g3, W_GUN);
    g3.monster.state = MS_RECOVER;
    g3.monster.t = 30000;
    hunt(g3, 13, Input{ 0, 0, false, true });
    t.assert(g3.player.stance, ST_GUARD, "guard up");
    g3.monster.stun = 0;
    playerHurt(g3, 10, -16, 0);
    t.assert(g3.player.hp, 98, "guard chip 2");
    t.assert(g3.player.stam, 78, "guard cost 22");
    t.assert(g3.monster.stun, 0, "guard does not stun monster");

    // normal hit: 35 knockback int + 34 i-frames gate repeats
    Game g4;
    newHunt(g4);
    g4.monster.state = MS_RECOVER;
    g4.monster.t = 30000;
    playerHurt(g4, 10, -16, 0);
    t.assert(g4.player.hp, 90, "normal hit 10");
    t.assert(g4.player.iT, 34, "i-frames 34");
    t.assert(g4.player.vx, -35, "knockback -35 fixed");
    playerHurt(g4, 10, -16, 0);
    t.assert(g4.player.hp, 90, "i-frames block repeat");
    suite.addTest(t);
  }

  {
    Test t("idle hunt runs: beast engages, hunter survives");
    Game g;
    newHunt(g);
    hunt(g, 240);
    t.assert(g.over, OVER_NONE, "no game over");
    t.assert(g.monster.state != MS_IDLE, true, "beast engaged");
    t.assertGreaterThan(g.player.hp, 0, "hunter survived 240 idle ticks");
    suite.addTest(t);
  }

  runner.addTestSuite(suite);
}
