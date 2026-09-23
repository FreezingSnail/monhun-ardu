#pragma once
// Host unit tests for the generic list-screen logic (bead monhun-ardu-cgz):
// core/save.hpp (encode/decode/checksum/roundtrip/fallback/write-on-change) and
// screen_state.hpp (conditions, visibility, debounced nav, action switch), the
// pure screenReset() entry helper, plus the opening menu's A -> MENU_ACCEPT
// edge (app_state.hpp routes it; the hub loop sends it to the hub, monhun-
// ardu-dlp.3). The cart side
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
    r.unlock = 0;   // dlp.2: always unlocked unless a quest def supplies one
    r.recipe[0].item = 0;
    r.recipe[0].count = 0;
    r.recipe[1].item = 0;
    r.recipe[1].count = 0;
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

    // ------------------------------------------------- save v2 (prg.5 tail)
    {
        Test t("v2 record: equipment slots + inventory counts encode/decode at exact offsets");
        t.assert(SAVE_BYTES, static_cast<uint8_t>(14 + 3 + 1 + ITEM_COUNT + 1), "27 B for 8 items");
        t.assert(SAVE_EQUIP_OFF, 14, "equip starts after the v2 prefix");
        t.assert(SAVE_FLAGS_OFF, 17, "flags after the 3 equip slots");
        t.assert(SAVE_ITEMS_OFF, 18, "inventory after flags");
        t.assert(SAVE_CHECKSUM_OFF, static_cast<uint8_t>(18 + ITEM_COUNT), "checksum last");
        SaveBlock s;
        saveDefaults(s);
        s.equip[0] = 2;   // head
        s.equip[1] = 1;   // body
        s.equip[2] = 3;   // charm
        s.flags = SAVE_FLAG_SMITHY_SEEN;
        s.items[ITEM_HERB] = 7;
        s.items[ITEM_ORE] = 255;
        uint8_t bytes[SAVE_BYTES];
        saveEncode(s, bytes);
        t.assert(bytes[SAVE_EQUIP_OFF + 0], 2, "head slot byte");
        t.assert(bytes[SAVE_EQUIP_OFF + 1], 1, "body slot byte");
        t.assert(bytes[SAVE_EQUIP_OFF + 2], 3, "charm slot byte");
        t.assert(bytes[SAVE_FLAGS_OFF], SAVE_FLAG_SMITHY_SEEN, "flags byte");
        t.assert(bytes[SAVE_ITEMS_OFF + ITEM_HERB], 7, "herb count byte");
        t.assert(bytes[SAVE_ITEMS_OFF + ITEM_ORE], 255, "ore count byte");
        uint8_t sum = 0;
        for (uint8_t i = 0; i < SAVE_CHECKSUM_OFF; i++)
            sum = static_cast<uint8_t>(sum + bytes[i]);
        t.assert(bytes[SAVE_CHECKSUM_OFF], sum, "checksum covers the v2 tail");
        SaveBlock out;
        t.assert(saveDecode(bytes, out), true, "v3 decodes");
        t.assert(out.equip[0], 2, "head round-trip");
        t.assert(out.equip[2], 3, "charm round-trip");
        t.assert(out.flags, SAVE_FLAG_SMITHY_SEEN, "flags round-trip");
        t.assert(out.items[ITEM_HERB], 7, "herb round-trip");
        t.assert(out.items[ITEM_ORE], 255, "ore round-trip");
        suite.addTest(t);
    }

    {
        Test t("saveItemAdd/Consume saturate + ignore out-of-range; fold takes the larger");
        SaveBlock s;
        saveDefaults(s);
        saveItemAdd(s, ITEM_HERB, 2);
        saveItemAdd(s, ITEM_HERB, 3);
        t.assert(saveItemCount(s, ITEM_HERB), 5, "adds accumulate");
        saveItemAdd(s, ITEM_ORE, 250);
        saveItemAdd(s, ITEM_ORE, 100);
        t.assert(saveItemCount(s, ITEM_ORE), 255, "clamps at 255");
        saveItemAdd(s, 200, 5);
        t.assert(saveItemCount(s, 200), 0, "id past the table inert");
        t.assert(saveItemConsume(s, ITEM_HERB), 1, "consume ok");
        t.assert(saveItemCount(s, ITEM_HERB), 4, "consumed one");
        t.assert(saveItemConsume(s, ITEM_FANG), 0, "empty consume fails");
        t.assert(saveItemConsume(s, 200), 0, "out-of-range consume fails");

        // Hunt-end fold: gathered gains persist, a consumed herb does not erase stock.
        SaveBlock store;
        saveDefaults(store);
        store.items[ITEM_HERB] = 5;
        store.items[ITEM_SCALE] = 2;
        uint8_t live[ITEM_COUNT] = {0};
        live[ITEM_HERB] = 3;    // used herbs this hunt: lower than saved, kept
        live[ITEM_SCALE] = 4;   // carved scales: higher, folded in
        live[ITEM_ORE] = 6;     // gathered ore: new
        saveFoldItems(store, live);
        t.assert(store.items[ITEM_HERB], 5, "lower live count keeps the stock");
        t.assert(store.items[ITEM_SCALE], 4, "higher live count folds in");
        t.assert(store.items[ITEM_ORE], 6, "new gain folds in");
        suite.addTest(t);
    }

    {
        Test t("migration: a version-2 record keeps its prefix, v2 tail defaults, never crashes");
        // Build a valid v2 record (prg.5 tail is not present in the stream).
        SaveBlock v2;
        saveDefaults(v2);
        v2.zenny = 4321;
        v2.tier[1] = 2;
        saveQuestSet(v2, 5, 0);
        uint8_t bytes[SAVE_BYTES];
        saveEncode(v2, bytes);
        bytes[2] = SAVE_VERSION_V2;
        bytes[SAVE_CHECKSUM_OFF] = 0;
        for (uint8_t i = 0; i < SAVE_CHECKSUM_OFF; i++)
            bytes[SAVE_CHECKSUM_OFF] = static_cast<uint8_t>(bytes[SAVE_CHECKSUM_OFF] + bytes[i]);

        for (uint8_t i = 0; i < 64; i++)
            hostEeprom[i] = 0xEE;
        for (uint8_t i = 0; i < SAVE_BYTES; i++)
            hostEeprom[SAVE_EEPROM_ADDR + i] = bytes[i];
        SaveBlock out;
        t.assert(saveLoad(out, HOST_BACKEND), true, "older-version record migrates (fields loaded)");
        t.assert(out.zenny, 4321, "v2 zenny preserved");
        t.assert(out.tier[1], 2, "v2 tier preserved");
        t.assert(saveQuestGet(out, 5, 0), true, "v2 quest bit preserved");
        t.assert(out.items[ITEM_HERB], 0, "v2 inventory defaults empty");
        t.assert(out.equip[0], SAVE_EQUIP_NONE, "v2 equipment defaults none");
        suite.addTest(t);
    }

    {
        Test t("migration: a version-1 record preserves zenny/quests/tiers, no active quest");
        SaveBlock v1;
        saveDefaults(v1);
        v1.zenny = 999;
        v1.tier[0] = 1;
        saveQuestSet(v1, 2, 1);
        uint8_t bytes[SAVE_BYTES];
        saveEncode(v1, bytes);
        bytes[2] = SAVE_VERSION_V1;
        // v1 had no active-quest/progress bytes: zero them, then fix the checksum.
        bytes[SAVE_ACTIVE_OFF] = 0;
        bytes[SAVE_PROGRESS_OFF] = 0;
        bytes[SAVE_CHECKSUM_OFF] = 0;
        for (uint8_t i = 0; i < SAVE_CHECKSUM_OFF; i++)
            bytes[SAVE_CHECKSUM_OFF] = static_cast<uint8_t>(bytes[SAVE_CHECKSUM_OFF] + bytes[i]);

        for (uint8_t i = 0; i < 64; i++)
            hostEeprom[i] = 0xEE;
        for (uint8_t i = 0; i < SAVE_BYTES; i++)
            hostEeprom[SAVE_EEPROM_ADDR + i] = bytes[i];
        SaveBlock out;
        t.assert(saveLoad(out, HOST_BACKEND), true, "older-version record migrates (fields loaded)");
        t.assert(out.zenny, 999, "v1 zenny preserved");
        t.assert(out.tier[0], 1, "v1 tier preserved");
        t.assert(saveQuestGet(out, 2, 1), true, "v1 done bit preserved");
        t.assert(out.activeQuest, SAVE_QUEST_NONE, "v1 has no active quest");
        t.assert(out.progress, 0, "v1 progress default 0");
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
        Test t("COND_QUEST take row gated by the chain unlock; action re-checks (dlp.2)");
        SaveBlock s;
        saveDefaults(s);
        ScreenRow locked = row(0, screens::ACTION_TAKE_QUEST, screens::COND_QUEST, 2);
        locked.unlock = 2;   // the prior quest (index 1) must be done
        t.assert(screenCondOk(s, locked), false, "locked take row dead");
        t.assert(screenApplyAction(s, locked), false, "locked action rejected");
        t.assert(saveQuestGet(s, 2, 0), false, "quest not taken while locked");
        saveQuestSet(s, 1, 1);
        t.assert(screenCondOk(s, locked), true, "chain unlock makes row live");
        t.assert(screenApplyAction(s, locked), true, "unlocked take applies");
        t.assert(saveQuestGet(s, 2, 0), true, "taken after unlock");
        suite.addTest(t);
    }

    {
        Test t("turn-in row material reward comes from row.recipe[0] (dlp.2)");
        SaveBlock s;
        saveDefaults(s);
        ScreenRow turn = row(150, screens::ACTION_TURN_IN_QUEST, screens::COND_QUEST, 48);   // need 3, quest 0
        turn.recipe[0].item = static_cast<uint8_t>(ITEM_ORE + 1);
        turn.recipe[0].count = 2;
        questTake(s, 0);
        s.progress = 3;
        t.assert(screenCondOk(s, turn), true, "ready turn row live");
        t.assert(screenApplyAction(s, turn), true, "turn-in applies");
        t.assert(s.zenny, 150, "reward zenny paid");
        t.assert(s.items[ITEM_ORE], 2, "material reward granted");
        suite.addTest(t);
    }

    {
        Test t("menu A edge accepts once per press; B is not a menu action (qs.4)");
        MenuState m;
        t.assert(menuStep(m, ST_B), MENU_NONE, "B is silent in the menu");
        t.assert(menuStep(m, ST_IDLE), MENU_NONE, "release silent");
        t.assert(menuStep(m, ST_A), MENU_ACCEPT, "A edge -> accept (routes to hub)");
        t.assert(menuStep(m, ST_A), MENU_NONE, "held A silent");
        t.assert(menuStep(m, ST_IDLE), MENU_NONE, "release after A silent");
        t.assert(menuStep(m, ST_A), MENU_ACCEPT, "fresh A press fires again");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
