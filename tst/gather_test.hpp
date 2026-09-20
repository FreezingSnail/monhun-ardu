#pragma once
// Host unit tests for items + gathering (bead monhun-ardu-feel.22):
// src/core/items.hpp (node query / depletion / inventory) and the PS_GATHER /
// PS_ITEM states in src/core/player.hpp, plus the world.hpp heal-press priority.
//
// The map data (data/map.json -> generated zone_data.hpp/zone_meta.hpp) is the
// source of truth; tests reference the symbolic zone:: ids, never literal
// record indices. docs/map-zones.md describes the blob ABI, docs/feel-design.md
// the input verbs.
#include "test.hpp"
#include "../src/core/world.hpp"
#include "../src/generated/zone_meta.hpp"

using namespace mh;

namespace gathertest {

const Input GT_IDLE = Input{0, 0, false, false};
const Input GT_A = Input{0, 0, true, false};
const Input GT_B = Input{0, 0, false, true};
const Input GT_RIGHT = Input{1, 0, false, false};

void gticks(Game &g, int n, const Input &in) {
    for (int i = 0; i < n; i++)
        stepGame(g, in);
}

// Park the beast far from the probe path so room tests stay deterministic
// (mirrors world_test's wparkBeast).
void gparkBeast(Game &g, int16_t x, int16_t y) {
    g.monster.state = MS_RECOVER;
    g.monster.t = 9999;
    g.monster.cd = 9999;
    g.monster.x = x;
    g.monster.y = y;
}

// Stand the hunter inside a node and start a sheathed gather; returns false
// when the state did not enter PS_GATHER.
bool beginGather(Game &g, int16_t x, int16_t y) {
    g.player.x = x;
    g.player.y = y;
    g.player.sheathed = true;
    g.player.sheatheLatch = false;
    stepGame(g, GT_A);
    return g.player.state == PS_GATHER;
}

// Run a rooted action out to idle (or bail when it left the state early).
void runToIdle(Game &g, uint8_t window) {
    for (int i = 0; i < window + 4 && g.player.state != PS_IDLE; i++)
        gticks(g, 1, GT_IDLE);
}

// Hold B through the item-use window; returns the state seen at the hold edge.
int8_t holdItem(Game &g) {
    for (int16_t i = 0; i < HOLD_TICKS; i++)
        gticks(g, 1, GT_B);
    const int8_t edge = g.player.state;
    for (int i = 0; i < ITEM_USE_TICKS + 4 && g.player.state == PS_ITEM; i++)
        gticks(g, 1, GT_B);
    return edge;
}

}   // namespace gathertest

using namespace gathertest;

void GatherSuite(TestRunner &runner) {
    TestSuite suite("Items + gathering: nodes, inventory, PS_GATHER/PS_ITEM (monhun-ardu-feel.22)");

    {
        Test t("sheathed A in a node gathers: completes, depletes, adds the yield");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        // PROP_CAMP_1 is a herb node (8,8,8,8) yield 1.
        t.assert(beginGather(g, 8, 8), 1, "enters PS_GATHER");
        t.assert(g.player.t, 1, "gather timer armed (one tick elapsed)");
        t.assert(g.player.itemNode, zone::PROP_CAMP_1, "bound the camp herb node");
        t.assert(g.items[ITEM_HERB], 0, "inventory empty before completion");
        runToIdle(g, GATHER_TICKS);
        t.assert(g.player.state, PS_IDLE, "back to idle after gather");
        t.assert(g.items[ITEM_HERB], 1, "yield 1 added on completion");
        t.assert(gatherNodeDepleted(g, zone::PROP_CAMP_1), 1, "node depleted");
        t.assertGreaterThan(g.fxN, 0, "gather completion spawned a spark");
        suite.addTest(t);
    }

    {
        Test t("gather yield 2 adds both herbs");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        // PROP_CAMP_2 is a herb node (72,40,8,8) yield 2.
        t.assert(beginGather(g, 72, 40), 1, "enters PS_GATHER at the yield-2 node");
        runToIdle(g, GATHER_TICKS);
        t.assert(g.items[ITEM_HERB], 2, "yield 2 added");
        suite.addTest(t);
    }

    {
        Test t("movement input cancels a gather before it applies");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        t.assert(beginGather(g, 8, 8), 1, "gather running");
        gticks(g, 1, GT_RIGHT);
        t.assert(g.player.state, PS_IDLE, "movement cancels gather");
        t.assert(g.items[ITEM_HERB], 0, "no herb on cancel");
        t.assert(gatherNodeDepleted(g, zone::PROP_CAMP_1), 0, "node not depleted on cancel");
        suite.addTest(t);
    }

    {
        Test t("damage cancels a gather before it applies");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        t.assert(beginGather(g, 8, 8), 1, "gather running");
        playerHurt(g, 5, 1, 0);
        t.assert(g.player.state, PS_IDLE, "damage cancels gather");
        t.assert(g.items[ITEM_HERB], 0, "no herb on damage cancel");
        t.assert(gatherNodeDepleted(g, zone::PROP_CAMP_1), 0, "node not depleted on damage");
        suite.addTest(t);
    }

    {
        Test t("a depleted node is inert: A draws instead of gathering");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        t.assert(beginGather(g, 8, 8), 1, "first gather");
        runToIdle(g, GATHER_TICKS);
        t.assert(g.items[ITEM_HERB], 1, "first gather added a herb");
        // Press A again in the same (now picked) node: no gather, the weapon draws.
        g.player.x = 8;
        g.player.y = 8;
        g.player.sheathed = true;
        g.player.sheatheLatch = false;
        stepGame(g, GT_A);
        t.assert(g.player.state != PS_GATHER ? 1 : 0, 1, "no second gather");
        t.assert(g.player.sheathed, 0, "A drew the weapon instead");
        t.assert(g.items[ITEM_HERB], 1, "inventory unchanged");
        suite.addTest(t);
    }

    {
        Test t("newGame resets nodes + inventory; loadRoom does not reset nodes");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        t.assert(beginGather(g, 8, 8), 1, "gather running");
        runToIdle(g, GATHER_TICKS);
        t.assert(gatherNodeDepleted(g, zone::PROP_CAMP_1), 1, "node picked");
        // A room round-trip keeps the picked node (rooms do not reset nodes).
        loadRoom(g, zone::ROOM_AREA, zone::SPAWN_AREA_START);
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        t.assert(gatherNodeDepleted(g, zone::PROP_CAMP_1), 1, "node stays picked across rooms");
        t.assert(g.items[ITEM_HERB], 1, "inventory survives a room load");
        // newGame clears both.
        newGame(g, W_SWORD, MODE_HUNT);
        t.assert(g.gatherMask, 0, "newGame resets the node mask");
        t.assert(g.items[ITEM_HERB], 0, "newGame resets the inventory");
        t.assert(g.player.sheathed, 0, "newGame clears the stow flag");
        suite.addTest(t);
    }

    {
        Test t("sheathed B hold uses a herb: heals exactly 20, decrements");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        g.player.hp = 50;
        g.items[ITEM_HERB] = 2;
        g.player.sheathed = true;
        g.player.sheatheLatch = false;
        t.assert(holdItem(g), PS_ITEM, "hold edge enters PS_ITEM");
        t.assert(g.player.hp, 70, "heals exactly 20");
        t.assert(g.items[ITEM_HERB], 1, "one herb consumed");
        suite.addTest(t);
    }

    {
        Test t("herb heal clamps at hpMax");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        g.player.hp = 95;
        g.items[ITEM_HERB] = 1;
        g.player.sheathed = true;
        g.player.sheatheLatch = false;
        holdItem(g);
        t.assert(g.player.hp, 100, "heal clamped at hpMax");
        t.assert(g.items[ITEM_HERB], 0, "herb consumed");
        suite.addTest(t);
    }

    {
        Test t("zero herbs: sheathed B hold is a no-op (no PS_ITEM, no heal)");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        g.player.hp = 50;
        g.items[ITEM_HERB] = 0;
        g.player.sheathed = true;
        g.player.sheatheLatch = false;
        for (int16_t i = 0; i < HOLD_TICKS + 6; i++)
            gticks(g, 1, GT_B);
        t.assert(g.player.state, PS_IDLE, "no item use without a herb");
        t.assert(g.player.hp, 50, "hp unchanged");
        t.assert(g.items[ITEM_HERB], 0, "inventory unchanged");
        suite.addTest(t);
    }

    {
        Test t("item use cancels on movement: no heal, no decrement");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        g.player.hp = 50;
        g.items[ITEM_HERB] = 1;
        g.player.sheathed = true;
        g.player.sheatheLatch = false;
        for (int16_t i = 0; i < HOLD_TICKS; i++)
            gticks(g, 1, GT_B);
        t.assert(g.player.state, PS_ITEM, "item use running");
        gticks(g, 1, Input{1, 0, true, true});   // move while B still held
        t.assert(g.player.state, PS_IDLE, "movement cancels item use");
        t.assert(g.player.hp, 50, "no heal on cancel");
        t.assert(g.items[ITEM_HERB], 1, "no herb consumed on cancel");
        suite.addTest(t);
    }

    {
        Test t("unsheathed B hold is still stance, not a herb use");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        gparkBeast(g, 20, 0);
        g.player.hp = 50;
        g.items[ITEM_HERB] = 2;
        g.player.sheathed = false;
        for (int16_t i = 0; i < HOLD_TICKS; i++)
            gticks(g, 1, GT_B);
        t.assert(g.player.stance, ST_PARRY, "sword hold -> parry stance");
        t.assert(g.player.state, PS_IDLE, "no item use while armed");
        t.assert(g.player.hp, 50, "hp unchanged");
        t.assert(g.items[ITEM_HERB], 2, "inventory unchanged");
        suite.addTest(t);
    }

    {
        Test t("heal-rect B press wins over item use (no double action)");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
        g.player.x = 44;   // inside the camp heal rect (40,8,32,24)
        g.player.y = 16;
        g.player.hp = 50;
        g.player.stam = 40;
        g.items[ITEM_HERB] = 2;
        g.player.sheathed = true;
        g.player.sheatheLatch = false;
        for (int16_t i = 0; i < HOLD_TICKS + 6; i++)
            gticks(g, 1, GT_B);
        t.assert(g.player.hp, 100, "tent healed to max");
        t.assert(g.player.stam, 100, "tent restored stamina");
        t.assert(g.items[ITEM_HERB], 2, "tent press did not also eat");
        t.assert(g.player.state != PS_ITEM ? 1 : 0, 1, "no item use on a heal press");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
