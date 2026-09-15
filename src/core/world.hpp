#pragma once
// World geometry, camera follow + hunt/train mode toggles, ported from
// mock/game.js (source of truth): updateCamera, activeTarget, withWeapon,
// resetHunt. Tick entry is stepGame(): mock step() runs updateCamera() before
// updatePlayer(), so the camera reflects the player position from the previous
// tick (a deliberate 1-tick trail, not a bug — see output.md).
//
// The camera is int pixels only (no float). Clamp ranges are x 0..WORLD_W-W and
// y 0..WORLD_H-ARENA_H. No Arduino.h. Header-only.

#include <stdint.h>
#include "projectiles.hpp"

namespace mh {

// Screen geometry (mock W, H, HUD_H, ARENA_H): 128x64 with an 8 px HUD strip,
// leaving a 128x56 playfield that the camera scrolls through the world.
constexpr int16_t SCREEN_W  = 128;
constexpr int16_t SCREEN_H  = 64;
constexpr int16_t HUD_H     = 8;
constexpr int16_t ARENA_H   = SCREEN_H - HUD_H;      // 56
constexpr int16_t CAM_MAX_X = WORLD_W - SCREEN_W;    // 128
constexpr int16_t CAM_MAX_Y = WORLD_H - ARENA_H;     // 56

// Mock updateCamera(): centre the view on the player, clamped to the world.
// Every operand is an int (w/2, SCREEN_W/2 and ARENA_H/2 are exact), so this
// matches the mock's float expression exactly.
static void updateCamera(Game& g) {
  int16_t tx = static_cast<int16_t>(g.player.x + (g.player.w >> 1) - (SCREEN_W >> 1));
  int16_t ty = static_cast<int16_t>(g.player.y + (g.player.h >> 1) - (ARENA_H >> 1));
  if (tx < 0) tx = 0; else if (tx > CAM_MAX_X) tx = CAM_MAX_X;
  if (ty < 0) ty = 0; else if (ty > CAM_MAX_Y) ty = CAM_MAX_Y;
  g.camX = tx;
  g.camY = ty;
}

// Mock activeTarget(): the pole in train, the live beast in hunt, null once the
// beast is dead. The port keeps one Game::target (hurt rect + callbacks), so
// this points it at the right object and re-arms that mode's callbacks.
static void updateActiveTarget(Game& g) {
  if (g.mode == MODE_TRAIN) {
    armPoleTarget(g);
  } else {
    g.target.onHit = monsterOnHit;
    g.target.onShove = monsterOnShove;
    g.target.onStun = monsterOnStun;
    syncMonsterTarget(g); // alive=false -> reads as null (dead beast)
  }
}

// Read-only view of Mock activeTarget() for render / debug consumers.
static const Rect* activeTargetRect(const Game& g) {
  return g.target.alive ? &g.target.rect : nullptr;
}

// Mock newGame(weapon, mode): a fresh world in the requested area.
static void newGame(Game& g, int8_t weapon, int8_t mode) {
  initGame(g, weapon);
  g.camX = 0;
  g.camY = 0;
  initMonster(g);
  initWorld(g, mode);
  updateActiveTarget(g);
}

// Mock withWeapon(): swap the weapon but stay in the current area. Prototype
// bug fix: the mock's newGame defaults to hunt, so a naive swap dropped train.
static void withWeapon(Game& g, int8_t weapon) {
  newGame(g, weapon, g.mode);
}

// Mock resetHunt() / the R key: restart the current area with the current
// weapon, preserving both. Device bead: bind R to resetHunt(g) — it is NOT the
// same as newGame() (which would reset the area to hunt).
static void resetHunt(Game& g) {
  newGame(g, g.weapon, g.mode);
}

// One full tick in mock step() order: tick++, input edges, camera, then the
// over / freeze gates, then player/target/rounds. The gates are the mock's
// hitstop: a frozen tick still ages the clock, edges and effects-over branch,
// but skips all sim updates, so device play matches the prototype exactly.
static void stepGame(Game& g, const Input& inp) {
  g.tick++;
  bool aP, bP, bR;
  inputEdges(inp, g.prevA, g.prevB, aP, bP, bR); // edges run even while frozen
  updateCamera(g);
  if (g.over != OVER_NONE) {
    updateEffects(g); // mock: effects keep ticking after win/lose
    return;
  }
  if (g.freeze > 0) {
    g.freeze--;
    return; // mock: hitstop skips sim, not effects
  }
  stepWorldBody(g, inp, aP, bP, bR);
}

} // namespace mh
