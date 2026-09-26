#pragma once
// Host unit tests for the room runtime (bead monhun-ardu-fie.4):
// src/core/zones.hpp (room record readers + safe-room predicate) and the
// loadRoom / updateDoors / tryHeal / hold-B logic in src/core/world.hpp.
//
// The map data (data/map.json -> generated zone_data.hpp/zone_meta.hpp) is the
// source of truth; tests reference the symbolic zone:: ids, never literal
// record indices. docs/map-zones.md describes the blob ABI.
#include "test.hpp"
#include "../src/core/world.hpp"
#include "../src/generated/zone_meta.hpp"

using namespace mh;

namespace zonetest {

const Input Z_IDLE = Input{0, 0, false, false};
const Input Z_RIGHT = Input{1, 0, false, false};
const Input Z_B = Input{0, 0, false, true};

void zticks(Game &g, int n, const Input &in) {
    for (int i = 0; i < n; i++)
        stepGame(g, in);
}

// Park the beast far from the probe path so room tests stay deterministic
// (mirrors world_test's wparkBeast).
void zparkBeast(Game &g, int16_t x, int16_t y) {
    g.monster.state = MS_RECOVER;
    g.monster.t = 9999;
    g.monster.cd = 9999;
    g.monster.x = x;
    g.monster.y = y;
}

// One full-flow melee swing: tap A (sheathed draws + swings) through stepGame,
// then run the swing out to idle. Exercises the same Target::onHit path a real
// hit uses, not a direct handler call.
void zswing(Game &g) {
    stepGame(g, Input{0, 0, true, false});
    for (int i = 0; i < 80 && g.player.state != PS_IDLE; i++)
        zticks(g, 1, Z_IDLE);
}

// Player rect overlaps a world rect? (same body-rect rule the door/heal checks
// use, kept here so the test states the geometry explicitly).
bool zOverlap(int16_t px, int16_t py, int16_t pw, int16_t ph, int16_t x, int16_t y, int16_t w, int16_t h) {
    return px < x + w && px + pw > x && py < y + h && py + ph > y;
}

}   // namespace zonetest

using namespace zonetest;

void ZoneSuite(TestRunner &runner) {
    TestSuite suite("Room runtime: loadRoom / doors / spawns / clamps / heal (src/core/zones.hpp)");

    {
        Test t("room record load: extents, monster kind, spawn placement");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_AREA, zone::SPAWN_AREA_START);
        t.assert(g.roomId, zone::ROOM_AREA, "active room id");
        t.assert(g.roomW, 384, "area roomW");
        t.assert(g.roomH, 112, "area roomH");
        t.assert(g.roomMonsterKind, zone::MONSTER_LUNGE, "area has a beast");
        t.assert(g.player.x, 320, "area start spawn x");
        t.assert(g.player.y, 72, "area start spawn y");
        t.assert(g.roomDoorCount, 2, "area door count (camp + cavern)");
        t.assert(g.roomHealCount, 0, "area has no heal rect");

        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        t.assert(g.roomId, zone::ROOM_CAMP, "camp room id");
        t.assert(g.roomW, 128, "camp roomW");
        t.assert(g.roomH, 56, "camp roomH");
        t.assert(g.roomMonsterKind, zone::MONSTER_NONE, "camp is safe");
        t.assert(g.player.x, 20, "camp entry spawn x");
        t.assert(g.player.y, 44, "camp entry spawn y");
        t.assert(g.roomHealCount, 1, "camp heal rect count");
        t.assert(roomIsSafe(g), 1, "roomIsSafe camp");

        loadRoom(g, zone::ROOM_CAVERN, zone::SPAWN_CAVERN_FROM_AREA);
        t.assert(g.roomId, zone::ROOM_CAVERN, "cavern room id");
        t.assert(g.roomW, 256, "cavern roomW");
        t.assert(g.roomH, 112, "cavern roomH");
        t.assert(g.roomMonsterKind, zone::MONSTER_NONE, "cavern is safe (no beast)");
        t.assert(g.player.x, 116, "cavern from_area spawn x");
        t.assert(g.player.y, 88, "cavern from_area spawn y");
        t.assert(g.roomDoorCount, 1, "cavern door count");
        t.assert(g.roomPropCount, 6, "cavern gather node count");
        t.assert(roomIsSafe(g), 1, "roomIsSafe cavern");

        loadRoom(g, zone::ROOM_AREA, zone::SPAWN_AREA_START);
        t.assert(g.roomId, zone::ROOM_AREA, "area id after camp");
        t.assert(g.player.x, 320, "area start spawn x");
        t.assert(g.player.y, 72, "area start spawn y");
        suite.addTest(t);
    }

    {
        Test t("loadRoom clears transient state and resets the camera clamp");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        g.projN = 3;
        g.fxN = 2;
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        t.assert(g.projN, 0, "projectiles cleared");
        t.assert(g.fxN, 0, "effects cleared");
        // camp 128x56 is screen-sized: both camera maxima pin to 0.
        t.assert(g.camX, 0, "camp camera x pinned");
        t.assert(g.camY, 0, "camp camera y pinned");

        loadRoom(g, zone::ROOM_AREA, zone::SPAWN_AREA_START);
        // area 384x112: camMaxX 256, camMaxY 56. Spawn (320,72) -> tx 264 -> 256,
        // ty 52 -> 52.
        t.assert(g.camX, 256, "area camera x clamped to 384-128");
        t.assert(g.camY, 52, "area camera y follows the spawn");
        suite.addTest(t);
    }

    {
        Test t("door latch: spawn-in-door cannot ping-pong");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        t.assert(g.doorLatch, 1, "latch armed on load");
        // Walk into the camp door while latched: no transition.
        g.player.x = 120;
        g.player.y = 24;
        zticks(g, 1, Z_IDLE);
        t.assert(g.roomId, zone::ROOM_CAMP, "latched: still in camp");
        t.assert(g.doorLatch, 1, "still inside a door rect");
        // Leave every door rect: latch clears.
        g.player.x = 60;
        g.player.y = 44;
        zticks(g, 1, Z_IDLE);
        t.assert(g.doorLatch, 0, "latch cleared after leaving the rect");
        // Re-enter the door: transition to area at its from_camp spawn (8,80).
        g.player.x = 120;
        g.player.y = 24;
        zticks(g, 1, Z_IDLE);
        t.assert(g.roomId, zone::ROOM_AREA, "transitioned to area");
        t.assert(g.player.x, 8, "area from_camp spawn x");
        t.assert(g.player.y, 80, "area from_camp spawn y");
        t.assert(g.doorLatch, 1, "latch re-armed on arrival");
        suite.addTest(t);
    }

    {
        Test t("door round-trip area -> camp lands on the from_area spawn");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_AREA, zone::SPAWN_AREA_START);
        zparkBeast(g, 350, 90);   // keep the beast off the door probe
        // Clear the arrival latch, then enter the area door (0,72,8,24).
        g.player.x = 100;
        g.player.y = 80;
        zticks(g, 1, Z_IDLE);
        t.assert(g.doorLatch, 0, "area latch cleared");
        g.player.x = 0;
        g.player.y = 72;
        zticks(g, 1, Z_IDLE);
        t.assert(g.roomId, zone::ROOM_CAMP, "transitioned back to camp");
        t.assert(g.player.x, 104, "camp from_area spawn x");
        t.assert(g.player.y, 40, "camp from_area spawn y");
        suite.addTest(t);
    }

    {
        Test t("door round-trip area -> cavern lands on the from_area spawn");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_AREA, zone::SPAWN_AREA_START);
        zparkBeast(g, 350, 90);   // keep the beast off the door probe
        // Clear the arrival latch, then enter the north door (176,0,16,8).
        g.player.x = 100;
        g.player.y = 80;
        zticks(g, 1, Z_IDLE);
        t.assert(g.doorLatch, 0, "area latch cleared");
        g.player.x = 180;
        g.player.y = 0;
        zticks(g, 1, Z_IDLE);
        t.assert(g.roomId, zone::ROOM_CAVERN, "transitioned into the cavern");
        t.assert(g.player.x, 116, "cavern from_area spawn x");
        t.assert(g.player.y, 88, "cavern from_area spawn y");
        t.assert(roomIsSafe(g), 1, "cavern arrived safe");
        // Walk back out through the south mouth (112,104,16,8).
        g.player.x = 140;
        g.player.y = 60;
        zticks(g, 1, Z_IDLE);
        t.assert(g.doorLatch, 0, "cavern latch cleared");
        g.player.x = 112;
        g.player.y = 104;
        zticks(g, 1, Z_IDLE);
        t.assert(g.roomId, zone::ROOM_AREA, "returned to the area");
        t.assert(g.player.x, 184, "area from_cavern spawn x");
        t.assert(g.player.y, 16, "area from_cavern spawn y");
        suite.addTest(t);
    }

    {
        Test t("camp hold-B sheathed sets menuRequest, no room switch");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        g.menuRequest = false;
        // Hold B while sheathed for the full HOLD_TICKS window in the camp.
        g.player.sheathed = true;
        for (int i = 0; i < HOLD_TICKS + 1 && !g.menuRequest; i++)
            zticks(g, 1, Z_B);
        t.assert(g.menuRequest, 1, "menu request flagged");
        t.assert(g.roomId, zone::ROOM_CAMP, "core did not switch screens");
        suite.addTest(t);
    }

    {
        Test t("per-room player clamps at 128x56 and 384x112");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        g.player.x = 500;
        g.player.y = 500;
        zticks(g, 1, Z_IDLE);
        t.assert(g.player.x, 112, "camp player x clamp = 128-16");
        t.assert(g.player.y, 40, "camp player y clamp = 56-16");

        loadRoom(g, zone::ROOM_AREA, zone::SPAWN_AREA_START);
        zparkBeast(g, 20, 20);   // clear of the bottom-right clamp corner
        g.player.x = 500;
        g.player.y = 500;
        zticks(g, 1, Z_IDLE);
        t.assert(g.player.x, 368, "area player x clamp = 384-16");
        t.assert(g.player.y, 96, "area player y clamp = 112-16");
        suite.addTest(t);
    }

    {
        Test t("per-room monster clamps at 128x56 and 384x112");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        // Direct clamp probe (a safe room never runs the monster update).
        g.roomW = 128;
        g.roomH = 56;
        g.monster.x = 500;
        g.monster.y = 500;
        clampMonster(g);
        t.assert(g.monster.x, 128 - g.monster.w, "camp monster x clamp");
        t.assert(g.monster.y, 56 - g.monster.h, "camp monster y clamp");
        g.roomW = 384;
        g.roomH = 112;
        g.monster.x = 500;
        g.monster.y = 500;
        clampMonster(g);
        t.assert(g.monster.x, 384 - g.monster.w, "area monster x clamp");
        t.assert(g.monster.y, 112 - g.monster.h, "area monster y clamp");
        suite.addTest(t);
    }

    {
        Test t("monster persists across a door round-trip (hp/pos/FSM)");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_AREA, zone::SPAWN_AREA_START);
        g.monster.hp = 123;
        g.monster.x = 200;
        g.monster.y = 50;
        g.monster.state = MS_PURSUE;
        g.monster.stun = 5;
        const int8_t kind = g.monsterKind;

        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        t.assert(g.monster.hp, 123, "hp survives camp load");
        t.assert(g.monster.x, 200, "x survives camp load");
        t.assert(g.monster.y, 50, "y survives camp load");
        t.assert(g.monster.state, MS_PURSUE, "state survives camp load");
        t.assert(g.monster.stun, 5, "stun survives camp load");

        loadRoom(g, zone::ROOM_AREA, zone::SPAWN_AREA_FROM_CAMP);
        t.assert(g.monster.hp, 123, "hp survives area load");
        t.assert(g.monster.x, 200, "x survives area load");
        t.assert(g.monster.y, 50, "y survives area load");
        t.assert(g.monster.state, MS_PURSUE, "state survives area load");
        t.assert(g.monsterKind, kind, "monster kind unchanged");
        suite.addTest(t);
    }

    {
        Test t("safe room: no monster/target update, player still controlled");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        t.assert(roomIsSafe(g), 1, "camp is a safe room");
        t.assert(g.target.alive, 0, "no active target in a safe room");
        // Freeze the beast's record; a safe room must leave it untouched.
        g.monster.x = 10;
        g.monster.y = 10;
        g.monster.hp = 77;
        g.monster.state = MS_IDLE;
        g.monster.t = 9999;
        zticks(g, 120, Z_IDLE);
        t.assert(g.monster.x, 10, "safe room: monster x untouched");
        t.assert(g.monster.y, 10, "safe room: monster y untouched");
        t.assert(g.monster.hp, 77, "safe room: monster hp untouched");
        // Player controls stay live.
        const int16_t x0 = g.player.x;
        zticks(g, 5, Z_RIGHT);
        t.assertGreaterThan(g.player.x, x0, "player walks in a safe room");
        suite.addTest(t);
    }

    {
        Test t("heal requires sheathed + inside a heal rect");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        g.player.x = 44;   // inside the camp heal rect (40,8,32,24)
        g.player.y = 16;
        g.player.hp = 50;
        g.player.stam = 50;
        g.player.sheathed = false;
        g.player.sheatheLatch = false;
        zticks(g, 1, Z_B);
        t.assert(g.player.hp, 50, "unsheathed B does not heal");
        t.assert(g.player.stam, 50, "unsheathed B does not restore stamina");
        t.assert(g.fxN, 0, "unsheathed B spawns no heal spark");

        // Release B, then draw + press again while sheathed.
        zticks(g, 1, Z_IDLE);
        g.player.hp = 50;
        g.player.stam = 50;
        g.player.sheathed = true;
        g.player.sheatheLatch = false;
        zticks(g, 1, Z_B);
        t.assert(g.player.hp, 100, "sheathed heal restores hp");
        t.assert(g.player.stam, 100, "sheathed heal restores stamina");
        t.assertGreaterThan(g.fxN, 0, "sheathed heal spawns a spark");

        // Sheathed but outside the rect: no heal.
        zticks(g, 1, Z_IDLE);
        g.player.x = 100;
        g.player.y = 44;
        g.player.hp = 50;
        g.player.stam = 50;
        g.player.sheathed = true;
        g.player.sheatheLatch = false;
        zticks(g, 1, Z_B);
        t.assert(g.player.hp, 50, "sheathed B outside the rect does not heal");
        suite.addTest(t);
    }

    {
        Test t("hold-B sheathed in camp flags menuRequest (app layer routes)");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        g.player.sheathed = true;
        g.player.sheatheLatch = false;
        g.menuRequest = false;
        // Hold B exactly HOLD_TICKS ticks (the shared hold edge).
        for (int16_t i = 0; i < HOLD_TICKS; i++)
            stepGame(g, Z_B);
        t.assert(g.player.bHeld, HOLD_TICKS, "hold counter reached the edge");
        t.assert(g.menuRequest, 1, "hold-B sheathed in camp requests the menu");

        // Unsheathed hold in camp: no request.
        Game u;
        newGame(u, W_SWORD, MODE_HUNT);
        loadRoom(u, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        u.player.sheathed = false;
        u.menuRequest = false;
        for (int16_t i = 0; i < HOLD_TICKS; i++)
            stepGame(u, Z_B);
        t.assert(u.menuRequest, 0, "unsheathed hold does not request the menu");

        // Sheathed hold outside the camp: no request.
        Game a;
        newGame(a, W_SWORD, MODE_HUNT);
        loadRoom(a, zone::ROOM_AREA, zone::SPAWN_AREA_START);
        zparkBeast(a, 350, 90);
        a.player.sheathed = true;
        a.player.sheatheLatch = false;
        a.menuRequest = false;
        for (int16_t i = 0; i < HOLD_TICKS; i++)
            stepGame(a, Z_B);
        t.assert(a.menuRequest, 0, "sheathed hold outside camp does not request");
        suite.addTest(t);
    }

    {
        Test t("camp -> area re-arms the target callbacks and melee damage lands");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        t.assert(g.target.alive, 0, "camp clears the target");
        t.assert(g.target.onHit == nullptr, 1, "camp clears onHit");

        // Area load must re-wire the beast callbacks: a prior safe load nulled
        // them and the per-tick syncMonsterTarget only refreshes alive/rect.
        loadRoom(g, zone::ROOM_AREA, zone::SPAWN_AREA_FROM_CAMP);
        t.assert(g.target.alive, 1, "area re-arms alive (monster persists)");
        t.assert(g.target.onHit == monsterOnHit, 1, "area re-arms onHit");
        t.assert(g.target.onShove == monsterOnShove, 1, "area re-arms onShove");
        t.assert(g.target.onStun == monsterOnStun, 1, "area re-arms onStun");
        t.assertGreaterThan(g.target.rect.w, 0, "target rect w non-degenerate");
        t.assertGreaterThan(g.target.rect.h, 0, "target rect h non-degenerate");

        // Drive the normal melee path: park the beast in the draw-swing arc
        // (player at from_camp spawn 8,80 centre 16,88, facing E; the tighter
        // legs collider lands at y 89..97 so the band overlaps).
        zparkBeast(g, 24, 76);
        const int16_t hp0 = g.monster.hp;
        zswing(g);
        t.assertLessThan(g.monster.hp, hp0, "melee hit reduces beast hp after camp");

        // Camp re-entry clears; the next area load re-arms again.
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        t.assert(g.target.alive, 0, "camp re-entry clears the target");
        loadRoom(g, zone::ROOM_AREA, zone::SPAWN_AREA_FROM_CAMP);
        t.assert(g.target.alive, 1, "second area load re-arms alive");
        t.assert(g.target.onHit == monsterOnHit, 1, "second area load re-arms onHit");
        suite.addTest(t);
    }

    {
        Test t("gather props: reader surfaces item/yield, plain props read none");
        // Camp props: 0 = tent (plain), 1 = herb x1, 2 = herb x2, 3 = mushroom.
        const ZoneProp tent = zonePropRead(zone::PROP_CAMP_0);
        t.assert(tent.gatherItem, zone::GATHER_NONE, "camp tent has no gather item");
        t.assert(tent.gatherYield, 0, "camp tent gather yield 0");

        const ZoneProp campHerb0 = zonePropRead(zone::PROP_CAMP_1);
        t.assert(campHerb0.gatherItem, zone::GATHER_HERB, "camp herb 0 item");
        t.assert(campHerb0.gatherYield, 1, "camp herb 0 yield");

        const ZoneProp campHerb1 = zonePropRead(zone::PROP_CAMP_2);
        t.assert(campHerb1.gatherItem, zone::GATHER_HERB, "camp herb 1 item");
        t.assert(campHerb1.gatherYield, 2, "camp herb 1 yield");

        const ZoneProp campShroom = zonePropRead(zone::PROP_CAMP_3);
        t.assert(campShroom.gatherItem, zone::GATHER_BLUE_MUSHROOM, "camp mushroom item");
        t.assert(campShroom.gatherYield, 1, "camp mushroom yield");

        // Area props: herbs (0..2), mushrooms (3, 4), ore (5, 6), bug (7).
        const ZoneProp areaHerb0 = zonePropRead(zone::PROP_AREA_0);
        t.assert(areaHerb0.gatherItem, zone::GATHER_HERB, "area herb 0 item");
        t.assert(areaHerb0.gatherYield, 1, "area herb 0 yield");

        const ZoneProp areaHerb1 = zonePropRead(zone::PROP_AREA_1);
        t.assert(areaHerb1.gatherItem, zone::GATHER_HERB, "area herb 1 item");
        t.assert(areaHerb1.gatherYield, 2, "area herb 1 yield");

        const ZoneProp areaShroom = zonePropRead(zone::PROP_AREA_3);
        t.assert(areaShroom.gatherItem, zone::GATHER_BLUE_MUSHROOM, "area mushroom item");
        t.assert(areaShroom.gatherYield, 1, "area mushroom yield");

        const ZoneProp areaOre = zonePropRead(zone::PROP_AREA_5);
        t.assert(areaOre.gatherItem, zone::GATHER_ORE, "area ore item");
        t.assert(areaOre.gatherYield, 1, "area ore yield");

        const ZoneProp areaBug = zonePropRead(zone::PROP_AREA_7);
        t.assert(areaBug.gatherItem, zone::GATHER_BUG, "area bug item");
        t.assert(areaBug.gatherYield, 1, "area bug yield");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
