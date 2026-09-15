#pragma once
// Monster FSM + hit resolution, ported from mock/game.js (source of truth):
// updateMonster / chooseAttack / startMonsterAttack / monsterHitsPlayer /
// playerHit / damageMonster / knockMonsterAway / pushApart.
//
// The beast plugs into Game::target so the player FSM resolves melee against it
// (and the training-pole bead, hrd, plugs in the same way). Tick order mirrors
// the mock: player first, then monster, then the push-apart correction.
// No float, no Arduino.h. Header-only.

#include <stdint.h>
#include "player.hpp"

namespace mh {

// Keep Game::target (the live hurt box + callbacks) in step with the beast.
static void syncMonsterTarget(Game& g) {
  Monster& m = g.monster;
  g.target.alive = (m.state != MS_DEAD);
  g.target.rect.x = m.x;
  g.target.rect.y = m.y;
  g.target.rect.w = m.w;
  g.target.rect.h = m.h;
}

static void clampMonster(Game& g) {
  Monster& m = g.monster;
  if (m.x < 0) m.x = 0;
  if (m.x > WORLD_W - m.w) m.x = WORLD_W - m.w;
  if (m.y < 0) m.y = 0;
  if (m.y > WORLD_H - m.h) m.y = WORLD_H - m.h;
}

static void knockMonsterAway(Game& g, Monster& m, int32_t cx, int32_t cy, int16_t amt) {
  const int32_t dx = (m.x + (m.w >> 1)) - cx;
  const int32_t dy = (m.y + (m.h >> 1)) - cy;
  const fp::Dir8& d = fp::DIR8[fp::dirIndexFromDelta(dx, dy)];
  fp::addMove(m, d.x, d.y, amt);
  clampMonster(g);
}

static void damageMonster(Game& g, int16_t dmg, int16_t hx, int16_t hy) {
  Monster& m = g.monster;
  if (m.state == MS_DEAD) return;
  const int32_t cx = m.x + (m.w >> 1);
  const int32_t cy = m.y + (m.h >> 1);
  // projection of the hit point onto the facing axis; >3 px on the head side
  // is a crit (x1.4, integer 14/10).
  const int32_t proj = ((hx - cx) * m.fx + (hy - cy) * m.fy) >> 4;
  const bool crit = proj > 3;
  int32_t total = (dmg * (crit ? 14 : 10)) / 10;
  if (total < 1) total = 1;
  m.hp -= static_cast<int16_t>(total);
  m.hitFlash = 4;
  const int16_t fr = crit ? 6 : 4;
  if (g.freeze < fr) g.freeze = fr;
  if (m.hp <= 0) {
    m.hp = 0;
    m.state = MS_DEAD;
    g.over = OVER_WIN;
    g.freeze = 12;
  }
}

// Target::onHit — player melee landed: damage, trip stun, knockback.
static void monsterOnHit(Game& g, int dmg, int hx, int hy, int push, int effect) {
  Monster& m = g.monster;
  if (m.state == MS_DEAD) return;
  damageMonster(g, static_cast<int16_t>(dmg), static_cast<int16_t>(hx), static_cast<int16_t>(hy));
  if (m.state == MS_DEAD) return;
  if (effect == 1 && m.stun < 70) m.stun = 70; // trip
  if (push) knockMonsterAway(g, m, hx, hy, static_cast<int16_t>(push));
}

// Target::onShove — gunshield shove: beast always gives way.
static void monsterOnShove(Game& g, int dirX, int dirY, int amount, int freeze) {
  fp::addMove(g.monster, static_cast<int16_t>(dirX), static_cast<int16_t>(dirY),
              static_cast<int16_t>(amount));
  if (g.freeze < freeze) g.freeze = static_cast<int16_t>(freeze);
}

// Target::onStun — deflect (28) / parry (60) freeze the beast.
static void monsterOnStun(Game& g, int ticks) {
  g.monster.stun = static_cast<int16_t>(ticks);
}

// Spawn the hunt beast and wire it into Game::target. Call after initGame().
static void initMonster(Game& g) {
  Monster& m = g.monster;
  m.x = 200; m.y = 40; m.w = 32; m.h = 24;
  m.subX = 0; m.subY = 0;
  m.hp = 200; m.hpMax = 200;
  m.state = MS_IDLE; m.t = 90; m.cd = 140;
  m.fx = -fp::FP; m.fy = 0; // face W
  m.atk = nullptr; m.lvx = 0; m.lvy = 0; m.windupMax = 0;
  m.hitFlash = 0; m.stun = 0; m.circleDir = 1;
  m.spd = 5;
  g.over = OVER_NONE;
  g.target.onHit = monsterOnHit;
  g.target.onShove = monsterOnShove;
  g.target.onStun = monsterOnStun;
  syncMonsterTarget(g);
}

static void chooseAttack(Monster& m, int32_t dist) {
  m.atk = dist > 32 ? &MONSTER_ATTACKS[0] : &MONSTER_ATTACKS[1]; // lunge / sweep
  m.state = MS_WINDUP;
  m.t = m.atk->windup;
  m.windupMax = m.atk->windup;
}

static void startMonsterAttack(Monster& m) {
  const MonsterAttack& a = *m.atk;
  m.state = MS_ATTACK;
  m.t = 0;
  if (a.kind == MK_LUNGE) {
    m.lvx = (m.fx * a.speedF) >> 4;
    m.lvy = (m.fy * a.speedF) >> 4;
  } else {
    m.lvx = 0;
    m.lvy = 0;
  }
}

static bool monsterHitsPlayer(const Game& g, const MonsterAttack& a) {
  const Monster& m = g.monster;
  const int32_t cx = m.x + (m.w >> 1) + ((m.fx * a.reach) >> 4);
  const int32_t cy = m.y + (m.h >> 1) + ((m.fy * a.reach) >> 4);
  Rect r;
  r.x = static_cast<int16_t>(cx - (a.hw >> 1));
  r.y = static_cast<int16_t>(cy - (a.hh >> 1));
  r.w = a.hw; r.h = a.hh;
  const Player& p = g.player;
  Rect pr;
  pr.x = p.x; pr.y = p.y; pr.w = p.w; pr.h = p.h;
  return r.overlaps(pr);
}

static void pushApart(Game& g) {
  Player& p = g.player;
  Monster& m = g.monster;
  if (m.state == MS_DEAD) return;

  Rect pr; pr.x = p.x; pr.y = p.y; pr.w = p.w; pr.h = p.h;
  Rect mr; mr.x = m.x; mr.y = m.y; mr.w = m.w; mr.h = m.h;
  if (!pr.overlaps(mr)) return;

  // Pole never moves; an attacking/windup beast shoves the player; otherwise the
  // beast gives way, so idle players are never shoved (mock bug fix).
  const bool shovePlayer = (m.state == MS_ATTACK || m.state == MS_WINDUP);
  const int16_t ax = shovePlayer ? p.x : m.x;
  const int16_t ay = shovePlayer ? p.y : m.y;
  const int16_t aw = shovePlayer ? p.w : m.w;
  const int16_t ah = shovePlayer ? p.h : m.h;
  const int16_t bx = shovePlayer ? m.x : p.x;
  const int16_t by = shovePlayer ? m.y : p.y;
  const int16_t bw = shovePlayer ? m.w : p.w;
  const int16_t bh = shovePlayer ? m.h : p.h;

  const int32_t ox = (ax + aw - bx) < (bx + bw - ax) ? (ax + aw - bx) : (bx + bw - ax);
  const int32_t oy = (ay + ah - by) < (by + bh - ay) ? (ay + ah - by) : (by + bh - ay);
  if (ox < oy) {
    const int16_t d = static_cast<int16_t>((ax + (aw >> 1)) < (bx + (bw >> 1)) ? -ox : ox);
    if (shovePlayer) p.x += d; else m.x += d;
  } else {
    const int16_t d = static_cast<int16_t>((ay + (ah >> 1)) < (by + (bh >> 1)) ? -oy : oy);
    if (shovePlayer) p.y += d; else m.y += d;
  }
}

static void updateMonster(Game& g) {
  Monster& m = g.monster;
  Player& p = g.player;

  if (m.hitFlash > 0) m.hitFlash--;
  syncMonsterTarget(g);
  if (m.state == MS_DEAD) return;

  const int32_t dx = (p.x + (p.w >> 1)) - (m.x + (m.w >> 1));
  const int32_t dy = (p.y + (p.h >> 1)) - (m.y + (m.h >> 1));
  const int32_t dist = fp::isqrt(dx * dx + dy * dy);
  const int8_t di = fp::dirIndexFromDelta(dx, dy);
  m.fx = fp::DIR8[di].x;
  m.fy = fp::DIR8[di].y;

  if (m.stun > 0) {
    m.stun--;
    if (m.stun == 0) { m.state = MS_RECOVER; m.t = 24; }
    return;
  }

  switch (m.state) {
    case MS_IDLE:
      m.t--;
      if (m.t <= 0) m.state = MS_PURSUE;
      break;
    case MS_PURSUE:
      m.cd--;
      if (dist > 36) {
        fp::addMove(m, m.fx, m.fy, m.spd);
      } else if (dist < 24) {
        fp::addMove(m, -m.fx, -m.fy, (m.spd * 6) / 10);
      } else {
        const fp::Dir8& s = fp::DIR8[(di + 2) & 7]; // perpendicular circle
        fp::addMove(m, static_cast<int16_t>(s.x * m.circleDir),
                       static_cast<int16_t>(s.y * m.circleDir), (m.spd * 8) / 10);
      }
      if (m.cd <= 0 && dist < 42) chooseAttack(m, dist);
      break;
    case MS_WINDUP:
      m.t--;
      if (m.t <= 0) startMonsterAttack(m);
      break;
    case MS_ATTACK: {
      const MonsterAttack& a = *m.atk;
      m.t++;
      if (a.kind == MK_LUNGE && m.t <= a.active) fp::addVel(m, m.lvx, m.lvy);
      if (m.t <= a.active && monsterHitsPlayer(g, a)) {
        playerHurt(g, a.dmg, m.fx, m.fy);
        if (p.hp <= 0) {
          p.hp = 0;
          if (g.over == OVER_NONE) g.over = OVER_LOSE;
        }
      }
      if (m.t > a.active + a.recover) {
        m.state = MS_PURSUE;
        m.cd = static_cast<int16_t>(55 + (g.tick % 40));
        m.circleDir = (g.tick % 2) ? 1 : -1;
      }
      break;
    }
    case MS_RECOVER:
      m.t--;
      if (m.t <= 0) { m.state = MS_PURSUE; m.cd = 55; }
      break;
    default:
      m.state = MS_PURSUE;
  }

  clampMonster(g);
  pushApart(g);
}

// Raw beast update (mock updateMonster).
static void stepMonster(Game& g) { updateMonster(g); }

// One full hunt tick in mock step() order: player, then monster. The target
// rect is synced first so melee sees the beast's current position.
static void stepHunt(Game& g, const Input& inp) {
  syncMonsterTarget(g);
  stepPlayer(g, inp);
  updateMonster(g);
}

} // namespace mh
