#pragma once
// World geometry, camera follow + hunt/train mode toggles, ported from
// mock/game.js (source of truth): updateCamera, activeTarget, withWeapon,
// resetHunt. Tick entry is stepGame(): mock step() runs updateCamera() before
// updatePlayer(), so the camera reflects the player position from the previous
// tick (a deliberate 1-tick trail, not a bug — see output.md).
//
// The camera is int pixels only (no float). Clamp ranges are x 0..roomW-W and
// y 0..roomH-ARENA_H for the active room (roomW/roomH default to WORLD_W/H).
// No Arduino.h. Header-only.

#include <stdint.h>
#include "projectiles.hpp"
#include "zones.hpp"   // room record readers + roomIsSafe

namespace mh {

// Screen geometry (mock W, H, HUD_H, ARENA_H): 128x64 with an 8 px HUD strip,
// leaving a 128x56 playfield that the camera scrolls through the world.
constexpr int16_t SCREEN_W = 128;
constexpr int16_t SCREEN_H = 64;
constexpr int16_t HUD_H = 8;
constexpr int16_t ARENA_H = SCREEN_H - HUD_H;       // 56
constexpr int16_t CAM_MAX_X = WORLD_W - SCREEN_W;   // 128 (legacy/default room)
constexpr int16_t CAM_MAX_Y = WORLD_H - ARENA_H;    // 56  (legacy/default room)

// Active-room camera maximums. A room narrower/shorter than the screen pins the
// camera at 0 so the follow never goes negative.
MH_NOINLINE static int16_t camMaxX(const Game &g) {
    const int16_t m = static_cast<int16_t>(roomBoundW(g) - SCREEN_W);
    return m < 0 ? 0 : m;
}
MH_NOINLINE static int16_t camMaxY(const Game &g) {
    const int16_t m = static_cast<int16_t>(roomBoundH(g) - ARENA_H);
    return m < 0 ? 0 : m;
}

// Mock updateCamera(): centre the view on the player, clamped to the room.
// Every operand is an int (w/2, SCREEN_W/2 and ARENA_H/2 are exact), so this
// matches the mock's float expression exactly.
static void updateCamera(Game &g) {
    int16_t tx = static_cast<int16_t>(g.player.x + (g.player.w >> 1) - (SCREEN_W >> 1));
    int16_t ty = static_cast<int16_t>(g.player.y + (g.player.h >> 1) - (ARENA_H >> 1));
    const int16_t mx = camMaxX(g);
    const int16_t my = camMaxY(g);
    if (tx < 0)
        tx = 0;
    else if (tx > mx)
        tx = mx;
    if (ty < 0)
        ty = 0;
    else if (ty > my)
        ty = my;
    g.camX = tx;
    g.camY = ty;
}

// Mock activeTarget(): the pole in train, the live beast in hunt, null once the
// beast is dead. The port keeps one Game::target (hurt rect + callbacks), so
// this points it at the right object and re-arms that mode's callbacks.
static void updateActiveTarget(Game &g) {
    if (g.mode == MODE_TRAIN) {
        armPoleTarget(g);
    } else if (roomIsSafe(g)) {
        g.target = Target{};   // no beast in a safe room: reads as null
    } else {
        g.target.onHit = monsterOnHit;
        g.target.onShove = monsterOnShove;
        g.target.onStun = monsterOnStun;
        syncMonsterTarget(g);   // alive=false -> reads as null (dead beast)
    }
}

// Read-only view of Mock activeTarget() for render / debug consumers.
static const Rect *activeTargetRect(const Game &g) {
    return g.target.alive ? &g.target.rect : nullptr;
}

// ------------------------------------------------------------- room runtime
// Heal spark effect lifetime (ticks); same spark family the hit paths spawn.
constexpr uint8_t HEAL_SPARK_LIFE = 8;
// Door-cross transition wipe length (ticks): loadRoom arms Game::fade and
// stepGame decays it; the render black-wipes the arena for these ticks.
constexpr uint8_t FADE_TICKS = 4;

static inline Rect bodyRect(const Player &p) {
    Rect r;
    r.x = p.x;
    r.y = p.y;
    r.w = p.w;
    r.h = p.h;
    return r;
}

// loadRoom: install a room record, place the hunter at a global spawn, clear
// transient world state and reset the camera clamp to the new extents. The
// monster is deliberately NOT reset: hp/zones/position/FSM persist across
// transitions (only newGame initializes it), so a hunt round-trip preserves the
// beast. `spawn` is a global spawn index (zone::SPAWN_*); invalid ids fall back
// to room 0 / spawn 0. Carved out of the parity image (no room scenes there).
static void loadRoom(Game &g, uint8_t roomId, uint8_t spawn) {
    if (!ROOM_BOUNDS_ENABLED)
        return;
    if (roomId >= zone::ROOMS_COUNT)
        roomId = 0;
    if (spawn >= zone::SPAWNS_COUNT)
        spawn = 0;
    const ZoneRoom room = zoneRoomRead(roomId);
    g.roomId = roomId;
    g.roomW = static_cast<int16_t>(room.w);
    g.roomH = static_cast<int16_t>(room.h);
    g.roomFirstDoor = room.firstDoor;
    g.roomDoorCount = room.doorCount;
    g.roomFirstHeal = room.firstHeal;
    g.roomHealCount = room.healCount;
    g.roomFirstProp = room.firstProp;
    g.roomPropCount = room.propCount;
    g.roomMonsterKind = room.monsterKind;

    const ZoneSpawn sp = zoneSpawnRead(spawn);
    g.player.x = static_cast<int16_t>(sp.x);
    g.player.y = static_cast<int16_t>(sp.y);
    g.player.subX = 0;
    g.player.subY = 0;
    g.player.vx = 0;
    g.player.vy = 0;

    g.projN = 0;   // clear projectiles / effects (mock newGame's transient state)
    g.fxN = 0;
    g.lastShot = 0;

    updateCamera(g);       // clamp the follow to the new room's extents
    g.doorLatch = true;    // suppress doors until the spawn rect is left
    g.fade = FADE_TICKS;   // render black-wipe on arrival (no cart traffic)
    // Re-arm the target for the arrival room: a safe room clears it, a beast
    // room re-wires the monster callbacks (a prior safe load nulled them, and
    // the per-tick syncMonsterTarget only refreshes alive/rect, so without this
    // hits landed on a null callback after camp -> area). Train re-arms the
    // pole. Monster state is untouched: hp/pos/FSM persist by design.
    updateActiveTarget(g);
}

// Door check (stepGame): a player-rect overlap with any door rect transitions
// to door.to at door.toSpawn. The post-spawn latch suppresses doors until the
// hunter has left every door rect, so a spawn inside a door cannot ping-pong.
// A door to the reserved "menu" target only sets Game::menuRequest: the core
// never switches to the menu itself (the app layer routes it, fie.6).
static void updateDoors(Game &g) {
    if (!ROOM_BOUNDS_ENABLED)
        return;
    if (g.roomDoorCount == 0) {
        g.doorLatch = false;
        return;
    }
    const Rect pr = bodyRect(g.player);
    bool inside = false;
    for (uint8_t i = 0; i < g.roomDoorCount; i++) {
        const ZoneDoor d = zoneDoorRead(static_cast<uint8_t>(g.roomFirstDoor + i));
        Rect dr;
        dr.x = static_cast<int16_t>(d.x);
        dr.y = static_cast<int16_t>(d.y);
        dr.w = static_cast<int16_t>(d.w);
        dr.h = static_cast<int16_t>(d.h);
        if (!pr.overlaps(dr))
            continue;
        inside = true;
        if (g.doorLatch)
            continue;   // still standing in the spawn door: no transition
        if (d.toRoom == zone::DOOR_MENU) {
            g.menuRequest = true;   // reserved target: app layer opens the menu
            return;
        }
        loadRoom(g, d.toRoom, d.toSpawn);
        return;
    }
    if (!inside)
        g.doorLatch = false;
}

// Heal (stepGame): a sheathed B press inside a heal rect restores hp/stamina to
// max and spawns a spark through the shared effect path. No heal while
// unsheathed (the press keeps its normal sheathed-roll meaning).
static void tryHeal(Game &g, bool bP) {
    if (!ROOM_BOUNDS_ENABLED || !bP || !g.player.sheathed || g.roomHealCount == 0)
        return;
    const Rect pr = bodyRect(g.player);
    for (uint8_t i = 0; i < g.roomHealCount; i++) {
        const ZoneHeal h = zoneHealRead(static_cast<uint8_t>(g.roomFirstHeal + i));
        Rect hr;
        hr.x = static_cast<int16_t>(h.x);
        hr.y = static_cast<int16_t>(h.y);
        hr.w = h.w;
        hr.h = h.h;
        if (!pr.overlaps(hr))
            continue;
        g.player.hp = g.player.hpMax;
        g.player.stam = g.player.stamMax;
        g.player.stamSub = 0;
        addEffect(g, static_cast<int16_t>(g.player.x + (g.player.w >> 1)), static_cast<int16_t>(g.player.y + (g.player.h >> 1)), HEAL_SPARK_LIFE, false, 0);
        return;
    }
}

// Mock newGame(weapon, mode, monsterIndex): a fresh world in the requested
// area with the chosen beast variant (0 = legacy LUNGE, the parity default).
MH_NOINLINE static void newGame(Game &g, int8_t weapon, int8_t mode, int8_t monsterKind = 0) {
    initGame(g, weapon);
    g.camX = 0;
    g.camY = 0;
    initMonster(g, monsterKind);
    initWorld(g, mode);
    updateActiveTarget(g);
}

// Mock withWeapon(): swap the weapon but stay in the current area and keep the
// chosen beast. Prototype bug fix: the mock's newGame defaults to hunt, so a
// naive swap dropped train. A train swap also keeps the picked pole variant.
static void withWeapon(Game &g, int8_t weapon) {
    const int8_t poleKind = g.pole.kind;
    newGame(g, weapon, g.mode, g.monsterKind);
    if (g.mode == MODE_TRAIN)
        initPoleKind(g, poleKind);
}

// Mock resetHunt() / the R key: restart the current area with the current
// weapon, preserving weapon, area, beast and pole variant. Device bead: bind R
// to resetHunt(g) — it is NOT the same as newGame() (which would reset the
// area to hunt).
static void resetHunt(Game &g) {
    const int8_t poleKind = g.pole.kind;
    newGame(g, g.weapon, g.mode, g.monsterKind);
    if (g.mode == MODE_TRAIN)
        initPoleKind(g, poleKind);
}

// One full tick in mock step() order: tick++, input edges, camera, then the
// over / freeze gates, then player/target/rounds. The gates are the mock's
// hitstop: a frozen tick still ages the clock, edges and effects-over branch,
// but skips all sim updates, so device play matches the prototype exactly.
static void stepGame(Game &g, const Input &inp) {
    g.tick++;
    if (ROOM_BOUNDS_ENABLED && g.fade)
        g.fade--;   // door-cross wipe decays one tick per logic tick
    bool aP, bP, bR;
    inputEdges(inp, g.prevA, g.prevB, aP, bP, bR);   // edges run even while frozen
    updateCamera(g);
    if (g.over != OVER_NONE) {
        updateEffects(g);   // mock: effects keep ticking after win/lose
        return;
    }
    if (g.freeze > 0) {
        g.freeze--;
        return;   // mock: hitstop skips sim, not effects
    }
    stepWorldBody(g, inp, aP, bP, bR);
    tryHeal(g, bP);
    // Hold-B while sheathed in the camp requests the menu (the app layer routes
    // it, fie.6). Exact HOLD_TICKS edge: one request per press. Uses the shared
    // hold constant, no new magic number.
    if (ROOM_BOUNDS_ENABLED && g.player.sheathed && g.player.bHeld == HOLD_TICKS && g.roomId == zone::ROOM_CAMP)
        g.menuRequest = true;
    updateDoors(g);
}

}   // namespace mh
