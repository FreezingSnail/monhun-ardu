#pragma once
// Host unit tests for the demo playtest picker (bead monhun-ardu-1du),
// src/demo_menu.hpp: row nav wrap, weapon/beast cycle wrap, A/B edges once per
// press, the GO launch event, the held-button guard on re-entry, the hunt-end
// return edge, and the launch's beast home room + generated entrance spawn.
#include "test.hpp"
#include "../src/demo_menu.hpp"
#include "../src/core/zones.hpp"   // beastHomeRoom

using namespace mh;

namespace demotest {

const Input DI_IDLE = Input{0, 0, false, false};
const Input DI_UP = Input{0, -1, false, false};
const Input DI_DOWN = Input{0, 1, false, false};
const Input DI_A = Input{0, 0, true, false};
const Input DI_B = Input{0, 0, false, true};

// Move the cursor to the GO row from a fresh picker.
inline void demoToGo(DemoMenu &m) {
    demoStep(m, DI_DOWN);
    demoStep(m, DI_IDLE);
    demoStep(m, DI_DOWN);
    demoStep(m, DI_IDLE);
}

}   // namespace demotest

using namespace demotest;

void DemoSuite(TestRunner &runner) {
    TestSuite suite("Demo picker: cycles, launch, edges (src/demo_menu.hpp, 1du)");

    {
        Test t("demoWrap wraps both ways over the option count");
        t.assert(demoWrap(0, 1, DEMO_WEAPON_COUNT), 1, "0 + 1 -> 1");
        t.assert(demoWrap(2, 1, DEMO_WEAPON_COUNT), 0, "weapon last -> first");
        t.assert(demoWrap(0, -1, DEMO_WEAPON_COUNT), 2, "weapon first -> last");
        t.assert(demoWrap(3, 1, DEMO_BEAST_COUNT), 0, "beast last -> first");
        t.assert(demoWrap(0, -1, DEMO_BEAST_COUNT), 3, "beast first -> last");
        t.assert(demoWrap(0, 1, 0), 0, "empty count stays 0");
        suite.addTest(t);
    }

    {
        Test t("UP/DOWN moves the cursor one row per fresh direction (wrap)");
        DemoMenu m;
        demoInit(m);
        t.assert(m.row, DEMO_ROW_WEAPON, "boots on WEAPON");
        demoStep(m, DI_DOWN);
        t.assert(m.row, DEMO_ROW_BEAST, "down -> BEAST");
        demoStep(m, DI_IDLE);
        demoStep(m, DI_DOWN);
        t.assert(m.row, DEMO_ROW_GO, "down -> GO");
        demoStep(m, DI_IDLE);
        demoStep(m, DI_DOWN);
        t.assert(m.row, DEMO_ROW_WEAPON, "down from GO wraps to WEAPON");
        demoStep(m, DI_IDLE);
        demoStep(m, DI_UP);
        t.assert(m.row, DEMO_ROW_GO, "up from WEAPON wraps to GO");
        demoStep(m, DI_IDLE);
        demoStep(m, DI_UP);
        t.assert(m.row, DEMO_ROW_BEAST, "up -> BEAST");
        demoStep(m, DI_IDLE);
        demoStep(m, DI_UP);
        t.assert(m.row, DEMO_ROW_WEAPON, "up -> WEAPON");
        suite.addTest(t);
    }

    {
        Test t("A cycles the weapon row (wrap 3) and the beast row (wrap 4)");
        DemoMenu m;
        demoInit(m);
        t.assert(m.weapon, W_SWORD, "default weapon SWORD");
        demoStep(m, DI_A);
        t.assert(m.weapon, W_FLAIL, "SWORD -> FLAIL");
        demoStep(m, DI_IDLE);
        demoStep(m, DI_A);
        t.assert(m.weapon, W_GUN, "FLAIL -> GUN");
        demoStep(m, DI_IDLE);
        demoStep(m, DI_A);
        t.assert(m.weapon, W_SWORD, "GUN -> SWORD (wrap)");

        // Down to the beast row; the A cycles there now.
        demoStep(m, DI_IDLE);
        demoStep(m, DI_DOWN);
        t.assert(m.row, DEMO_ROW_BEAST, "on BEAST");
        demoStep(m, DI_IDLE);
        t.assert(m.beast, MON_LUNGE, "default beast LUNGE");
        const uint8_t beasts[4] = {static_cast<uint8_t>(MON_SWEEP), static_cast<uint8_t>(MON_HEAVY), static_cast<uint8_t>(MON_RAVAGER), static_cast<uint8_t>(MON_LUNGE)};
        for (uint8_t i = 0; i < 4; i++) {
            demoStep(m, DI_A);
            t.assert(m.beast, beasts[i], "beast cycle step");
            demoStep(m, DI_IDLE);
        }
        suite.addTest(t);
    }

    {
        Test t("A on GO returns DEMO_LAUNCH once per press");
        DemoMenu m;
        demoInit(m);
        demoToGo(m);
        t.assert(m.row, DEMO_ROW_GO, "on GO");
        t.assert(demoStep(m, DI_A), DEMO_LAUNCH, "A launches");
        t.assert(demoStep(m, DI_A), DEMO_NONE, "held A: no second launch");
        t.assert(demoStep(m, DI_IDLE), DEMO_NONE, "release: no event");
        t.assert(demoStep(m, DI_A), DEMO_LAUNCH, "fresh A launches again");
        suite.addTest(t);
    }

    {
        Test t("A is edge-once: a held press cannot re-cycle or re-launch");
        DemoMenu m;
        demoInit(m);
        demoStep(m, DI_A);
        t.assert(m.weapon, W_FLAIL, "first A cycles");
        t.assert(demoStep(m, DI_A), DEMO_NONE, "held A is silent");
        t.assert(m.weapon, W_FLAIL, "held A did not cycle again");
        demoStep(m, DI_IDLE);
        demoStep(m, DI_A);
        t.assert(m.weapon, W_GUN, "fresh A cycles again");
        suite.addTest(t);
    }

    {
        Test t("B is inert on the picker");
        DemoMenu m;
        demoInit(m);
        const uint8_t w0 = m.weapon;
        const uint8_t b0 = m.beast;
        t.assert(demoStep(m, DI_B), DEMO_NONE, "B no event");
        t.assert(m.row, DEMO_ROW_WEAPON, "cursor unmoved");
        t.assert(m.weapon, w0, "weapon unmoved");
        t.assert(m.beast, b0, "beast unmoved");
        suite.addTest(t);
    }

    {
        Test t("demoEnter keeps the picks and seeds the edges from held buttons");
        DemoMenu m;
        demoInit(m);
        demoStep(m, DI_A);    // weapon -> FLAIL
        demoEnter(m, DI_B);   // return to the picker with B held (camp hold-B)
        t.assert(m.weapon, W_FLAIL, "pick kept");
        t.assert(m.beast, MON_LUNGE, "beast pick kept");
        t.assert(m.row, DEMO_ROW_WEAPON, "cursor reset to WEAPON");
        t.assert(demoStep(m, DI_B), DEMO_NONE, "held B silent on entry");
        // A held on entry (hunt-end return) must not launch or cycle.
        DemoMenu m2;
        demoInit(m2);
        demoToGo(m2);
        demoStep(m2, DI_IDLE);
        demoEnter(m2, DI_A);
        t.assert(demoStep(m2, DI_A), DEMO_NONE, "held A silent on entry");
        t.assert(m2.weapon, W_SWORD, "held A did not cycle");
        t.assert(demoStep(m2, DI_IDLE), DEMO_NONE, "release");
        t.assert(demoStep(m2, DI_IDLE), DEMO_NONE, "idle");
        demoStep(m2, DI_DOWN);
        demoStep(m2, DI_IDLE);
        demoStep(m2, DI_DOWN);
        demoStep(m2, DI_IDLE);
        t.assert(demoStep(m2, DI_A), DEMO_LAUNCH, "fresh A on GO launches");
        suite.addTest(t);
    }

    {
        Test t("demoOverReturnStep: A rising edge only while over, once per press");
        bool prevA = false;
        t.assert(demoOverReturnStep(false, DI_A, prevA), false, "A edge pre-over: no return");
        t.assert(demoOverReturnStep(true, DI_A, prevA), false, "held A post-over: no edge");
        t.assert(demoOverReturnStep(true, DI_IDLE, prevA), false, "release: no return");
        t.assert(demoOverReturnStep(true, DI_A, prevA), true, "A edge post-over: return");
        for (uint8_t i = 0; i < 3; i++)
            t.assert(demoOverReturnStep(true, DI_A, prevA), false, "held A: no repeat");
        t.assert(demoOverReturnStep(true, DI_IDLE, prevA), false, "release again");
        t.assert(demoOverReturnStep(true, DI_A, prevA), true, "fresh A returns again");
        suite.addTest(t);
    }

    {
        Test t("demoHomeSpawn maps each home room to its generated entrance spawn");
        t.assert(demoHomeSpawn(zone::ROOM_AREA), zone::SPAWN_AREA_FROM_CAMP, "area entrance");
        t.assert(demoHomeSpawn(zone::ROOM_RIDGE), zone::SPAWN_RIDGE_FROM_AREA, "ridge entrance");
        t.assert(demoHomeSpawn(zone::ROOM_CAMP), zone::SPAWN_CAMP_ENTRY, "camp entry");
        t.assert(demoHomeSpawn(zone::ROOM_CAVERN), zone::SPAWN_CAVERN_FROM_AREA, "cavern from area");
        // The beast -> home mapping feeds it: lunge/sweep/ravager home at the
        // area, heavy at the ridge.
        t.assert(beastHomeRoom(MON_LUNGE), zone::ROOM_AREA, "lunge homes at the area");
        t.assert(beastHomeRoom(MON_HEAVY), zone::ROOM_RIDGE, "heavy homes at the ridge");
        t.assert(demoHomeSpawn(beastHomeRoom(MON_HEAVY)), zone::SPAWN_RIDGE_FROM_AREA, "heavy entrance spawn");
        suite.addTest(t);
    }

    {
        Test t("demoLaunch builds a fresh hunt in the beast's home at its entrance spawn");
        Game g;
        DemoMenu m;
        demoInit(m);
        m.weapon = static_cast<uint8_t>(W_GUN);
        m.beast = static_cast<uint8_t>(MON_HEAVY);
        demoLaunch(g, m);
        t.assert(g.weapon, W_GUN, "picked weapon");
        t.assert(g.monsterKind, MON_HEAVY, "picked beast");
        t.assert(g.mode, MODE_HUNT, "hunt mode");
        t.assert(g.roomId, zone::ROOM_RIDGE, "heavy homes at the ridge");
        t.assert(g.beastHere, 1, "the beast is present in its home");
        // The beast sits at the home room's monster spawn; the hunter lands on
        // the entrance spawn record, across the room.
        const ZoneSpawn beastSp = zoneSpawnRead(zone::SPAWN_RIDGE_START);
        t.assert(g.monster.x, static_cast<int16_t>(beastSp.x), "beast at the ridge monster spawn x");
        t.assert(g.monster.y, static_cast<int16_t>(beastSp.y), "beast at the ridge monster spawn y");
        const ZoneSpawn entry = zoneSpawnRead(zone::SPAWN_RIDGE_FROM_AREA);
        t.assert(g.player.x, static_cast<int16_t>(entry.x), "hunter at the ridge entrance spawn x");
        t.assert(g.player.y, static_cast<int16_t>(entry.y), "hunter at the ridge entrance spawn y");
        t.assert(g.dmgMul, UPGRADE_MUL_BASE, "identity damage multiplier");
        t.assert(g.spdMul, UPGRADE_MUL_BASE, "identity speed multiplier");
        t.assert(g.items[ITEM_HERB], 0, "empty inventory");
        t.assert(g.player.sheathed, 1, "demo hunts start with the weapon stowed");
        t.assert(g.player.sheatheLatch, 0, "no stale sheathe latch");
        suite.addTest(t);
    }

    {
        // nx9 (owner playtest): the GO A was read again on the first hunt tick
        // and drew the stowed weapon. The sketch order is demoLaunch then
        // primeHuntInput with the launching sample; the held A is swallowed and
        // the next deliberate press draws.
        Test t("GO launch + primeHuntInput: the held A does not draw the stowed weapon");
        Game g;
        DemoMenu m;
        demoInit(m);
        demoToGo(m);
        const Input A = Input{0, 0, true, false};
        t.assert(demoStep(m, A), DEMO_LAUNCH, "A on GO launches");
        demoLaunch(g, m);
        primeHuntInput(g, A);
        stepGame(g, A);
        t.assert(g.player.sheathed, 1, "weapon stays stowed through the transition");
        t.assert(g.player.state, PS_IDLE, "no draw windup from the launch press");
        stepGame(g, Input{0, 0, false, false});
        stepGame(g, A);
        t.assert(g.player.state, PS_DRAW, "the next deliberate A draws");
        suite.addTest(t);
    }

    {
        Test t("demoLaunch places each beast in its home room");
        const int8_t kinds[4] = {MON_LUNGE, MON_SWEEP, MON_HEAVY, MON_RAVAGER};
        const uint8_t homes[4] = {zone::ROOM_AREA, zone::ROOM_AREA, zone::ROOM_RIDGE, zone::ROOM_AREA};
        Game g;
        DemoMenu m;
        demoInit(m);
        for (uint8_t i = 0; i < 4; i++) {
            m.beast = static_cast<uint8_t>(kinds[i]);
            demoLaunch(g, m);
            t.assert(g.monsterKind, kinds[i], "launched beast kind");
            t.assert(g.roomId, homes[i], "beast lands in its home room");
        }
        suite.addTest(t);
    }

    {
        // Owner call (demo playtest, 512x112 area): the hunt must open with the
        // hunter at the home room's entrance spawn and the beast at its monster
        // spawn across the room -- present after a tick but off-screen, so the
        // hunter closes in. Replaces the old "on-screen at spawn" pin.
        Test t("demoLaunch: every beast present at its home spawn and off-screen from the entrance");
        const int8_t kinds[4] = {MON_LUNGE, MON_SWEEP, MON_HEAVY, MON_RAVAGER};
        const uint8_t homes[4] = {zone::ROOM_AREA, zone::ROOM_AREA, zone::ROOM_RIDGE, zone::ROOM_AREA};
        const uint8_t entrances[4] = {zone::SPAWN_AREA_FROM_CAMP, zone::SPAWN_AREA_FROM_CAMP, zone::SPAWN_RIDGE_FROM_AREA, zone::SPAWN_AREA_FROM_CAMP};
        const uint8_t beastSpawns[4] = {zone::SPAWN_AREA_START, zone::SPAWN_AREA_START, zone::SPAWN_RIDGE_START, zone::SPAWN_AREA_START};
        Game g;
        DemoMenu m;
        demoInit(m);
        const Input idle = Input{0, 0, false, false};
        for (uint8_t i = 0; i < 4; i++) {
            m.beast = static_cast<uint8_t>(kinds[i]);
            demoLaunch(g, m);
            stepGame(g, idle);   // refresh the presence cache like the demo loop
            t.assert(g.beastHere, 1, "beast present after a tick");
            t.assert(g.roomId, homes[i], "hunt opens in the beast's home room");
            const ZoneSpawn bSp = zoneSpawnRead(beastSpawns[i]);
            t.assert(g.monster.x, static_cast<int16_t>(bSp.x), "beast at its home monster spawn x");
            t.assert(g.monster.y, static_cast<int16_t>(bSp.y), "beast at its home monster spawn y");
            const ZoneSpawn eSp = zoneSpawnRead(entrances[i]);
            t.assert(g.player.x, static_cast<int16_t>(eSp.x), "hunter at the home entrance spawn x");
            t.assert(g.player.y, static_cast<int16_t>(eSp.y), "hunter at the home entrance spawn y");
            int16_t dx = static_cast<int16_t>(g.monster.x - g.player.x);
            if (dx < 0)
                dx = static_cast<int16_t>(-dx);
            t.assert(dx > SCREEN_W ? 1 : 0, 1, "hunter-beast distance exceeds the screen width");
        }
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
