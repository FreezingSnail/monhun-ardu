#pragma once
// Host unit tests for the generic list-screen logic (bead monhun-ardu-cgz):
// core/save.hpp (encode/decode/checksum/roundtrip/fallback/write-on-change) and
// screen_state.hpp (conditions, visibility, debounced nav, action switch), the
// pure screenReset() entry helper, plus the opening menu's A -> MENU_ACCEPT
// edge (qs.4 removed the old B -> MENU_SCREEN stub). The cart side
// (src/screens.hpp) is device-only and is pinned by tst/fxdatatest/screens_test.hpp.
#include "test.hpp"
#include "../src/screen_state.hpp"
#include "../src/menu_state.hpp"

using namespace mh;

namespace screenstest {

// Host EEPROM model: the save record lives at SAVE_EEPROM_ADDR in a small array.
uint8_t hostEeprom[64];
uint16_t hostWrites = 0;

uint8_t hostRead(uint16_t addr) {
    return hostEeprom[addr];
}
void hostWrite(uint16_t addr, uint8_t value) {
    hostEeprom[addr] = value;
}
void hostCountedWrite(uint16_t addr, uint8_t value) {
    hostEeprom[addr] = value;
    hostWrites++;
}
const SaveBackend HOST_BACKEND = {hostRead, hostWrite};
const SaveBackend COUNTED_BACKEND = {hostRead, hostCountedWrite};

const Input ST_IDLE = Input{0, 0, false, false};
const Input ST_UP = Input{0, -1, false, false};
const Input ST_DOWN = Input{0, 1, false, false};
const Input ST_A = Input{0, 0, true, false};
const Input ST_B = Input{0, 0, false, true};

inline ScreenRow row(uint16_t cost, uint8_t action, uint8_t cond, uint8_t param, uint8_t flags = 0) {
    ScreenRow r;
    r.cost = cost;
    r.action = action;
    r.cond = cond;
    r.param = param;
    r.flags = flags;
    return r;
}

// Enter a synthetic screen state for nav tests.
inline ScreenState navState(uint8_t rowCount) {
    ScreenState s;
    s.screen = 0;
    s.rowCount = rowCount;
    s.active = true;
    return s;
}

}   // namespace screenstest

using namespace screenstest;

void ScreenSuite(TestRunner &runner) {
    TestSuite suite("Screens: save block, row conditions, nav, actions (qs.1)");

    // ---------------------------------------------------------------- save
    {
        Test t("save record encodes magic/version/fields/checksum little-endian");
        SaveBlock s;
        saveDefaults(s);
        s.zenny = 12345;
        s.tier[1] = 2;
        saveQuestSet(s, 3, 0);
        uint8_t bytes[SAVE_BYTES];
        saveEncode(s, bytes);
        t.assert(bytes[0], 0x4D, "magic low byte 'M'");
        t.assert(bytes[1], 0x48, "magic high byte 'H'");
        t.assert(bytes[2], SAVE_VERSION, "version");
        t.assert(bytes[3], 0x39, "zenny low");
        t.assert(bytes[4], 0x30, "zenny high");
        t.assert(bytes[5 + 0], 0x40, "quest3 taken bit");
        t.assert(bytes[SAVE_TIER_OFF + 1], 2, "tier[1]");
        uint8_t sum = 0;
        for (uint8_t i = 0; i < SAVE_CHECKSUM_OFF; i++)
            sum = static_cast<uint8_t>(sum + bytes[i]);
        t.assert(bytes[SAVE_CHECKSUM_OFF], sum, "checksum is the byte sum");
        t.assert(sizeof(SaveBlock) >= SAVE_BYTES - 3, true, "policy: struct is fields only");
        suite.addTest(t);
    }

    {
        Test t("save decode round-trips every field");
        SaveBlock s;
        saveDefaults(s);
        s.zenny = 4321;
        s.tier[0] = 1;
        s.tier[2] = 3;
        saveQuestSet(s, 7, 1);
        uint8_t bytes[SAVE_BYTES];
        saveEncode(s, bytes);
        SaveBlock out;
        t.assert(saveDecode(bytes, out), true, "decode succeeds");
        t.assert(out.zenny, 4321, "zenny round-trip");
        t.assert(out.tier[0], 1, "tier0 round-trip");
        t.assert(out.tier[2], 3, "tier2 round-trip");
        t.assert(saveQuestGet(out, 7, 1), true, "quest done bit round-trip");
        t.assert(saveQuestGet(out, 7, 0), false, "quest taken bit stays clear");
        suite.addTest(t);
    }

    {
        Test t("bad magic / version / checksum all fall back to defaults");
        SaveBlock s;
        saveDefaults(s);
        s.zenny = 999;
        uint8_t bytes[SAVE_BYTES];
        saveEncode(s, bytes);

        bytes[0] ^= 0xFF;   // corrupt magic
        SaveBlock out;
        t.assert(saveDecode(bytes, out), false, "bad magic rejected");
        saveEncode(s, bytes);
        bytes[2] = 99;   // corrupt version
        t.assert(saveDecode(bytes, out), false, "bad version rejected");
        saveEncode(s, bytes);
        bytes[5] ^= 0x01;   // corrupt payload without fixing checksum
        t.assert(saveDecode(bytes, out), false, "bad checksum rejected");

        for (uint8_t i = 0; i < 64; i++)
            hostEeprom[i] = 0xEE;
        t.assert(saveLoad(out, HOST_BACKEND), false, "junk load returns false");
        t.assert(out.zenny, 0, "junk load yields default zenny");
        t.assert(saveQuestGet(out, 0, 0), false, "junk load yields default quests");
        t.assert(out.tier[0], 0, "junk load yields default tiers");
        suite.addTest(t);
    }

    {
        Test t("saveStore round-trips through the backend and writes only changes");
        SaveBlock s;
        saveDefaults(s);
        s.zenny = 777;
        s.tier[2] = 1;
        t.assert(saveStore(s, HOST_BACKEND), true, "store verifies");
        SaveBlock out;
        t.assert(saveLoad(out, HOST_BACKEND), true, "load after store");
        t.assert(out.zenny, 777, "stored zenny");
        t.assert(out.tier[2], 1, "stored tier");
        hostWrites = 0;
        t.assert(saveStore(s, COUNTED_BACKEND), true, "second store verifies");
        t.assert(hostWrites, 0, "identical block writes nothing");
        s.zenny = 778;
        t.assert(saveStore(s, COUNTED_BACKEND), true, "changed store verifies");
        t.assert(hostWrites, 2, "zenny low + checksum bytes rewritten");
        suite.addTest(t);
    }

    // ---------------------------------------------------------- conditions
    {
        Test t("conditions: zenny >= cost, flag set, tier < max, always");
        SaveBlock s;
        saveDefaults(s);
        s.zenny = 50;
        t.assert(screenCondOk(s, row(100, 0, screens::COND_ZENNY, 0)), false, "zenny short");
        t.assert(screenCondOk(s, row(50, 0, screens::COND_ZENNY, 0)), true, "zenny exact");
        t.assert(screenCondOk(s, row(0, 0, screens::COND_ZENNY, 0)), true, "zenny free");
        saveQuestSet(s, 5, 0);
        t.assert(screenCondOk(s, row(0, 0, screens::COND_FLAG, 5)), true, "flag 5 set");
        t.assert(screenCondOk(s, row(0, 0, screens::COND_FLAG, 4)), false, "flag 4 clear");
        s.tier[1] = SCREEN_MAX_TIER - 1;
        t.assert(screenCondOk(s, row(0, 0, screens::COND_TIER, 1)), true, "tier below max");
        s.tier[1] = SCREEN_MAX_TIER;
        t.assert(screenCondOk(s, row(0, 0, screens::COND_TIER, 1)), false, "tier at max");
        t.assert(screenCondOk(s, row(9, 0, screens::COND_ALWAYS, 0)), true, "always");
        suite.addTest(t);
    }

    // ---------------------------------------------------------------- nav
    {
        Test t("nav taps step and wrap, page scroll follows the cursor");
        ScreenState s = navState(7);
        t.assert(screenStep(s, ST_IDLE), SCREEN_NONE, "idle silent");
        t.assert(s.cursor, 0, "starts at row 0");
        screenStep(s, ST_DOWN);
        screenStep(s, ST_IDLE);
        t.assert(s.cursor, 1, "down tap -> 1");
        screenStep(s, ST_DOWN);
        screenStep(s, ST_IDLE);
        t.assert(s.cursor, 2, "down tap -> 2");
        screenStep(s, ST_UP);
        screenStep(s, ST_IDLE);
        t.assert(s.cursor, 1, "up tap -> 1");
        s.cursor = 0;
        screenStep(s, ST_UP);
        screenStep(s, ST_IDLE);
        t.assert(s.cursor, 6, "up from 0 wraps to last");
        for (uint8_t i = 0; i < 7; i++) {
            screenStep(s, ST_DOWN);
            screenStep(s, ST_IDLE);
        }
        t.assert(s.cursor, 6, "wrap forward lands on last");
        t.assert(s.scroll, 6, "last page scrolls to 6");
        suite.addTest(t);
    }

    {
        Test t("page scroll is a multiple of 6 and advances only on page change");
        ScreenState s = navState(13);
        screenStep(s, ST_DOWN);   // row 1
        screenStep(s, ST_IDLE);
        t.assert(s.cursor, 1, "row 1");
        t.assert(s.scroll, 0, "still page 0");
        for (uint8_t i = 0; i < 5; i++) {
            screenStep(s, ST_DOWN);
            screenStep(s, ST_IDLE);
        }
        t.assert(s.cursor, 6, "row 6");
        t.assert(s.scroll, 6, "page 1 starts at 6");
        suite.addTest(t);
    }

    {
        Test t("hold waits SCREEN_NAV_DELAY then repeats every SCREEN_NAV_REPEAT");
        ScreenState s = navState(13);
        screenStep(s, ST_DOWN);
        t.assert(s.cursor, 1, "press steps immediately");
        for (uint8_t i = 0; i < SCREEN_NAV_DELAY - 1; i++)
            screenStep(s, ST_DOWN);
        t.assert(s.cursor, 1, "held before delay");
        screenStep(s, ST_DOWN);
        t.assert(s.cursor, 2, "delay tick steps");
        for (uint8_t i = 0; i < SCREEN_NAV_REPEAT - 1; i++)
            screenStep(s, ST_DOWN);
        t.assert(s.cursor, 2, "between repeats");
        screenStep(s, ST_DOWN);
        t.assert(s.cursor, 3, "repeat steps");
        screenStep(s, ST_IDLE);
        t.assert(s.cursor, 3, "release keeps the pick");
        suite.addTest(t);
    }

    {
        Test t("A and B edges fire once per press, nav still applies");
        ScreenState s = navState(3);
        t.assert(screenStep(s, ST_A), SCREEN_ACCEPT, "A edge accepts");
        t.assert(screenStep(s, ST_A), SCREEN_NONE, "held A silent");
        t.assert(screenStep(s, ST_IDLE), SCREEN_NONE, "release silent");
        t.assert(screenStep(s, ST_B), SCREEN_BACK, "B edge backs out");
        t.assert(screenStep(s, ST_B), SCREEN_NONE, "held B silent");
        const Input downA = Input{0, 1, true, false};
        s = navState(3);
        t.assert(screenStep(s, downA), SCREEN_ACCEPT, "diagonal A accepts");
        t.assert(s.cursor, 1, "diagonal nav applied");
        suite.addTest(t);
    }

    // ------------------------------------------------------------- actions
    {
        Test t("buy upgrade spends zenny, bumps tier, stops at the cap");
        SaveBlock s;
        saveDefaults(s);
        s.zenny = 250;
        ScreenRow r = row(100, screens::ACTION_BUY_UPGRADE, screens::COND_ZENNY, 0);
        t.assert(screenApplyAction(s, r), true, "first buy changes save");
        t.assert(s.zenny, 150, "zenny debited");
        t.assert(s.tier[0], 1, "tier bumped");
        t.assert(screenApplyAction(s, r), true, "second buy changes save");
        t.assert(s.zenny, 50, "zenny debited again");
        t.assert(s.tier[0], 2, "tier 2");
        t.assert(screenApplyAction(s, r), false, "unaffordable buy rejected");
        t.assert(s.tier[0], 2, "tier unchanged when broke");
        s.zenny = 1000;
        s.tier[1] = SCREEN_MAX_TIER;
        t.assert(screenApplyAction(s, row(100, screens::ACTION_BUY_UPGRADE, 0, 1)), false, "cap reached");
        t.assert(s.tier[1], SCREEN_MAX_TIER, "tier pinned at cap");
        suite.addTest(t);
    }

    {
        Test t("COND_UPGRADE smith rows: next-tier gate, lock, bought, funds");
        SaveBlock s;
        saveDefaults(s);
        s.zenny = 500;
        // param = (unlock << 4) | (weapon << 2) | tier
        const ScreenRow t1 = row(100, screens::ACTION_BUY_UPGRADE, screens::COND_UPGRADE, static_cast<uint8_t>((0 << 4) | (0 << 2) | 1));
        const ScreenRow t2 = row(250, screens::ACTION_BUY_UPGRADE, screens::COND_UPGRADE, static_cast<uint8_t>((0 << 4) | (0 << 2) | 2));
        t.assert(screenCondOk(s, t1), true, "tier 1 available");
        t.assert(screenCondOk(s, t2), false, "tier 2 not the next tier");
        t.assert(screenApplyAction(s, t1), true, "tier 1 bought");
        t.assert(s.tier[0], 1, "tier 1 stored");
        t.assert(s.zenny, 400, "zenny debited");
        t.assert(screenCondOk(s, t1), false, "bought row dead");
        t.assert(screenCondOk(s, t2), true, "tier 2 now available");
        t.assert(screenApplyAction(s, t2), true, "tier 2 bought");
        t.assert(s.tier[0], 2, "tier 2 stored");
        t.assert(screenCondOk(s, t2), false, "max row dead");

        SaveBlock locked;
        saveDefaults(locked);
        locked.zenny = 500;
        const ScreenRow gate = row(100, screens::ACTION_BUY_UPGRADE, screens::COND_UPGRADE, static_cast<uint8_t>((1 << 4) | (0 << 2) | 1));
        t.assert(screenCondOk(locked, gate), false, "locked until flag");
        saveQuestSet(locked, 0, 1);
        t.assert(screenCondOk(locked, gate), true, "quest done unlocks tier");

        SaveBlock poor;
        saveDefaults(poor);
        poor.zenny = 99;
        t.assert(screenCondOk(poor, t1), false, "one short of cost");
        t.assert(screenApplyAction(poor, t1), false, "purchase rejected");
        t.assert(poor.tier[0], 0, "tier unchanged");
        suite.addTest(t);
    }

    {
        Test t("take/turn-in quest set the taken then done bits");
        SaveBlock s;
        saveDefaults(s);
        ScreenRow take = row(0, screens::ACTION_TAKE_QUEST, 0, 2);
        t.assert(screenApplyAction(s, take), true, "take changes save");
        t.assert(saveQuestGet(s, 2, 0), true, "quest taken");
        t.assert(screenApplyAction(s, take), false, "double take rejected");
        ScreenRow turn = row(0, screens::ACTION_TURN_IN_QUEST, 0, 2);
        t.assert(screenApplyAction(s, turn), true, "turn-in changes save");
        t.assert(saveQuestGet(s, 2, 0), false, "taken cleared");
        t.assert(saveQuestGet(s, 2, 1), true, "done set");
        t.assert(screenApplyAction(s, turn), false, "double turn-in rejected");
        t.assert(screenApplyAction(s, row(0, screens::ACTION_TURN_IN_QUEST, 0, 3)), false, "untaken turn-in rejected");
        t.assert(screenApplyAction(s, row(0, screens::ACTION_LEAVE, 0, 0)), false, "leave changes nothing");
        suite.addTest(t);
    }

    {
        Test t("menu A edge opens the hub once per press; B is not a menu action (qs.4)");
        MenuState m;
        t.assert(menuStep(m, ST_B), MENU_NONE, "B is silent in the menu");
        t.assert(menuStep(m, ST_IDLE), MENU_NONE, "release silent");
        t.assert(menuStep(m, ST_A), MENU_ACCEPT, "A edge -> accept/hub");
        t.assert(menuStep(m, ST_A), MENU_NONE, "held A silent");
        t.assert(menuStep(m, ST_IDLE), MENU_NONE, "release after A silent");
        t.assert(menuStep(m, ST_A), MENU_ACCEPT, "fresh A press fires again");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
