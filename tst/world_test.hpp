#pragma once
// Host unit tests for src/core/world.hpp — world bounds / room clamp /
// reset semantics. The projectile cull suite was retired with the hitscan
// rework (no in-flight shots exist).
// margin, camera follow + clamp (both axes, mock order), hunt/train mode
// toggles and the active-target indirection. mock/game.js is the source of
// truth (mock/game.test.js covers the same feel via the browser harness).
#include "test.hpp"
#include "../src/core/world.hpp"

using namespace mh;

// Named namespace (not anonymous): several *_test.hpp headers share one TU, so
// anonymous-namespace helpers would collide across suites.
namespace worldtest {

const Input WTEST_IDLE = Input{0, 0, false, false};
const Input WTEST_RIGHT = Input{1, 0, false, false};
const Input WTEST_UP = Input{0, -1, false, false};
const Input WTEST_DOWN = Input{0, 1, false, false};

// Inert beast so camera/world tests are deterministic (mock game.test.js
// park()): it never pursues, attacks, or shoves.
void wparkBeast(Game &g, int16_t x, int16_t y) {
    g.monster.state = MS_RECOVER;
    g.monster.t = 9999;
    g.monster.cd = 9999;
    g.monster.x = x;
    g.monster.y = y;
}

void wticks(Game &g, int n, const Input &in) {
    for (int i = 0; i < n; i++)
        stepGame(g, in);
}

}   // namespace worldtest

using namespace worldtest;

void WorldSuite(TestRunner &runner) {
    TestSuite suite("World, camera + hunt/train mode toggles (src/core/world.hpp)");

    {
        Test t("world + camera constants match the mock");
        t.assert(WORLD_W, 256, "WORLD_W");
        t.assert(WORLD_H, 112, "WORLD_H");
        t.assert(SCREEN_W, 128, "screen w");
        t.assert(SCREEN_H, 64, "screen h");
        t.assert(HUD_H, 8, "HUD strip");
        t.assert(ARENA_H, 56, "arena height = 64-8");
        t.assert(CAM_MAX_X, 128, "camera x max = WORLD_W-W");
        t.assert(CAM_MAX_Y, 56, "camera y max = WORLD_H-ARENA_H");
        suite.addTest(t);
    }

    {
        Test t("camera order: updateCamera runs before logic (1-tick trail, mock)");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        wparkBeast(g, 20, 0);
        // start player x 96 -> centre 104, tx = 104-64 = 40 (pre-move position)
        stepGame(g, WTEST_RIGHT);
        t.assert(g.player.x, 96, "player sub-pixel step only (sword 14/16)");
        t.assert(g.player.subX, 14, "sub-pixel accumulator carried");
        t.assert(g.camX, 40, "camera saw the pre-move centre (mock order)");
        t.assert(g.camY, 40, "camera y from the start row");
        suite.addTest(t);
    }

    {
        Test t("camera follows the player and clamps at the world x edge");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        wparkBeast(g, 20, 0);   // parked off the hunter's path
        wticks(g, 60, WTEST_RIGHT);
        t.assertGreaterThan(g.camX, 0, "camera follows right");
        wticks(g, 400, WTEST_RIGHT);
        t.assert(g.camX, CAM_MAX_X, "camera clamps at WORLD_W-W");
        t.assert(g.player.x, WORLD_W - g.player.w, "player clamped at world edge");
        suite.addTest(t);
    }

    {
        Test t("camera clamps on the y axis at both ends");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        wparkBeast(g, 20, 0);
        g.player.x = 128;   // centre screen horizontally: camera x stays put
        wticks(g, 400, WTEST_UP);
        t.assert(g.player.y, 0, "player clamped at top");
        t.assert(g.camY, 0, "camera clamps to 0 at top");
        wticks(g, 400, WTEST_DOWN);
        t.assert(g.player.y, WORLD_H - g.player.h, "player clamped at bottom");
        t.assert(g.camY, CAM_MAX_Y, "camera clamps to WORLD_H-ARENA_H");
        suite.addTest(t);
    }

    {
        Test t("default room extents are the legacy world; camera uses the active room");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        t.assert(g.roomW, WORLD_W, "default roomW = WORLD_W");
        t.assert(g.roomH, WORLD_H, "default roomH = WORLD_H");
        t.assert(camMaxX(g), CAM_MAX_X, "default camMaxX");
        t.assert(camMaxY(g), CAM_MAX_Y, "default camMaxY");
        // A 128x56 room pins both camera axes at 0 (screen-sized room).
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        t.assert(camMaxX(g), 0, "camp camMaxX pinned to 0");
        t.assert(camMaxY(g), 0, "camp camMaxY pinned to 0");
        t.assert(g.camX, 0, "camp camera x pinned");
        t.assert(g.camY, 0, "camp camera y pinned");
        // A 384x112 room allows horizontal scroll past the legacy CAM_MAX_X.
        loadRoom(g, zone::ROOM_AREA, zone::SPAWN_AREA_START);
        t.assert(camMaxX(g), 256, "area camMaxX = 384-128");
        t.assert(camMaxY(g), 56, "area camMaxY = 112-56");
        suite.addTest(t);
    }

    {
        Test t("idle player does not slide over 120 ticks of zero input");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        wparkBeast(g, 20, 0);
        const int16_t x0 = g.player.x;
        const int16_t y0 = g.player.y;
        wticks(g, 120, WTEST_IDLE);
        t.assert(g.player.x, x0, "x unchanged");
        t.assert(g.player.y, y0, "y unchanged");
        t.assert(g.player.vx, 0, "vx stays zero");
        t.assert(g.player.vy, 0, "vy stays zero");
        suite.addTest(t);
    }

    {
        Test t("activeTarget indirection: hunt beast, then null once dead");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        updateActiveTarget(g);
        t.assert(g.target.alive, 1, "live beast is targetable");
        // q0o: the lunge target rect is the body hurt rect (32x24) -- the
        // authored 9x9 legs collide box is collision-only. Pinning the body
        // keeps the drawn bird hittable.
        t.assert(g.target.rect.w, g.monster.w, "beast body hurt box w");
        t.assert(g.target.rect.h, g.monster.h, "beast body hurt box h");
        g.monster.state = MS_DEAD;
        updateActiveTarget(g);
        t.assert(g.target.alive, 0, "dead beast reads as null");
        t.assert(activeTargetRect(g) == nullptr ? 1 : 0, 1, "activeTargetRect null");
        suite.addTest(t);
    }

    {
        Test t("withWeapon + resetHunt keep the current area (mock bug fix)");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        withWeapon(g, W_GUN);
        t.assert(g.mode, MODE_HUNT, "swap keeps hunt");
        t.assert(g.weapon, W_GUN, "weapon swapped");
        resetHunt(g);
        t.assert(g.mode, MODE_HUNT, "reset keeps hunt");
        t.assert(g.weapon, W_GUN, "reset keeps weapon");
        t.assert(g.tick, 0, "reset zeroes the clock");
        t.assert(g.player.hp, 100, "reset restores the hunter");

        newGame(g, W_SWORD, MODE_HUNT);
        withWeapon(g, W_FLAIL);
        t.assert(g.mode, MODE_HUNT, "hunt stays hunt on swap");
        resetHunt(g);
        t.assert(g.mode, MODE_HUNT, "hunt stays hunt on reset");
        t.assert(g.weapon, W_FLAIL, "reset keeps the swapped weapon");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
