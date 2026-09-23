#pragma once
// Host unit tests for the generic list-screen logic (bead monhun-ardu-cgz):
// core/save.hpp (encode/decode/checksum/roundtrip/fallback/write-on-change,
// v4 weapon + v3 migration by monhun-ardu-isp.1) and screen_state.hpp
// (conditions, visibility, debounced nav, action switch) plus the pure
// screenReset() entry helper. The cart side (src/screens.hpp) is device-only
// and is pinned by tst/fxdatatest/screens_test.hpp.
#include "test.hpp"
#include "../src/screen_state.hpp"

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
        s.weapon = W_FLAIL;
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
        t.assert(bytes[SAVE_WEAPON_OFF], W_FLAIL, "weapon byte");
        uint8_t sum = 0;
        for (uint8_t i = 0; i < SAVE_CHECKSUM_OFF; i++)
            sum = static_cast<uint8_t>(sum + bytes[i]);
        t.assert(bytes[SAVE_CHECKSUM_OFF], sum, "checksum is the byte sum");
        t.assert(sizeof(SaveBlock) >= SAVE_CHECKSUM_OFF - 3, true, "policy: struct is fields only");
        suite.addTest(t);
    }

    {
        Test t("save decode round-trips every field");
        SaveBlock s;
        saveDefaults(s);
        s.zenny = 4321;
        s.tier[0] = 1;
        s.tier[2] = 3;
        s.weapon = W_GUN;
        saveQuestSet(s, 7, 1);
        uint8_t bytes[SAVE_BYTES];
        saveEncode(s, bytes);
        SaveBlock out;
        t.assert(saveDecode(bytes, out), true, "decode succeeds");
        t.assert(out.zenny, 4321, "zenny round-trip");
        t.assert(out.tier[0], 1, "tier0 round-trip");
        t.assert(out.tier[2], 3, "tier2 round-trip");
        t.assert(out.weapon, W_GUN, "weapon round-trip");
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

    // --------------------------------------- save v4 tail (prg.5 + isp.1)
    {
        Test t("v4 record: equipment/inventory tail + weapon byte at exact offsets");
        t.assert(SAVE_BYTES, static_cast<uint8_t>(14 + 3 + 1 + ITEM_COUNT + 1 + 1), "28 B for 8 items + weapon");
        t.assert(SAVE_EQUIP_OFF, 14, "equip starts after the v2 prefix");
        t.assert(SAVE_FLAGS_OFF, 17, "flags after the 3 equip slots");
        t.assert(SAVE_ITEMS_OFF, 18, "inventory after flags");
        t.assert(SAVE_WEAPON_OFF, static_cast<uint8_t>(18 + ITEM_COUNT), "weapon after the inventory");
        t.assert(SAVE_CHECKSUM_OFF, static_cast<uint8_t>(SAVE_WEAPON_OFF + 1), "checksum last");
        SaveBlock s;
        saveDefaults(s);
        s.equip[0] = 2;   // head
        s.equip[1] = 1;   // body
        s.equip[2] = 3;   // charm
        s.flags = SAVE_FLAG_SMITHY_SEEN;
        s.items[ITEM_HERB] = 7;
        s.items[ITEM_ORE] = 255;
        s.weapon = W_FLAIL;
        uint8_t bytes[SAVE_BYTES];
        saveEncode(s, bytes);
        t.assert(bytes[SAVE_EQUIP_OFF + 0], 2, "head slot byte");
        t.assert(bytes[SAVE_EQUIP_OFF + 1], 1, "body slot byte");
        t.assert(bytes[SAVE_EQUIP_OFF + 2], 3, "charm slot byte");
        t.assert(bytes[SAVE_FLAGS_OFF], SAVE_FLAG_SMITHY_SEEN, "flags byte");
        t.assert(bytes[SAVE_ITEMS_OFF + ITEM_HERB], 7, "herb count byte");
        t.assert(bytes[SAVE_ITEMS_OFF + ITEM_ORE], 255, "ore count byte");
        t.assert(bytes[SAVE_WEAPON_OFF], W_FLAIL, "weapon byte");
        uint8_t sum = 0;
        for (uint8_t i = 0; i < SAVE_CHECKSUM_OFF; i++)
            sum = static_cast<uint8_t>(sum + bytes[i]);
        t.assert(bytes[SAVE_CHECKSUM_OFF], sum, "checksum covers the v4 tail");
        SaveBlock out;
        t.assert(saveDecode(bytes, out), true, "v4 decodes");
        t.assert(out.equip[0], 2, "head round-trip");
        t.assert(out.equip[2], 3, "charm round-trip");
        t.assert(out.flags, SAVE_FLAG_SMITHY_SEEN, "flags round-trip");
        t.assert(out.items[ITEM_HERB], 7, "herb round-trip");
        t.assert(out.items[ITEM_ORE], 255, "ore round-trip");
        t.assert(out.weapon, W_FLAIL, "weapon round-trip");
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
        bytes[SAVE_V3_CHECKSUM_OFF] = 0;
        for (uint8_t i = 0; i < SAVE_V3_CHECKSUM_OFF; i++)
            bytes[SAVE_V3_CHECKSUM_OFF] = static_cast<uint8_t>(bytes[SAVE_V3_CHECKSUM_OFF] + bytes[i]);

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
        t.assert(out.weapon, W_SWORD, "v2 weapon defaults sword");
        suite.addTest(t);
    }

    {
        Test t("migration: a version-3 record keeps its tail, weapon defaults to sword");
        // Build a valid v4 record then re-label it v3: the tail bytes are
        // identical (v4 appended the weapon byte after the v3 tail), but the v3
        // checksum lives at byte 26.
        SaveBlock v3;
        saveDefaults(v3);
        v3.zenny = 4321;
        v3.tier[1] = 2;
        v3.equip[0] = 3;
        v3.flags = SAVE_FLAG_SMITHY_SEEN;
        v3.items[ITEM_HERB] = 6;
        v3.weapon = W_GUN;   // must NOT survive: a v3 record has no weapon byte
        saveQuestSet(v3, 5, 0);
        uint8_t bytes[SAVE_BYTES];
        saveEncode(v3, bytes);
        bytes[2] = SAVE_VERSION_V3;
        bytes[SAVE_V3_CHECKSUM_OFF] = 0;
        for (uint8_t i = 0; i < SAVE_V3_CHECKSUM_OFF; i++)
            bytes[SAVE_V3_CHECKSUM_OFF] = static_cast<uint8_t>(bytes[SAVE_V3_CHECKSUM_OFF] + bytes[i]);

        for (uint8_t i = 0; i < 64; i++)
            hostEeprom[i] = 0xEE;
        for (uint8_t i = 0; i < SAVE_BYTES; i++)
            hostEeprom[SAVE_EEPROM_ADDR + i] = bytes[i];
        SaveBlock out;
        t.assert(saveLoad(out, HOST_BACKEND), true, "v3 record migrates");
        t.assert(out.zenny, 4321, "v3 zenny preserved");
        t.assert(out.equip[0], 3, "v3 equip preserved");
        t.assert(out.flags, SAVE_FLAG_SMITHY_SEEN, "v3 flags preserved");
        t.assert(out.items[ITEM_HERB], 6, "v3 inventory preserved");
        t.assert(out.weapon, W_SWORD, "v3 weapon defaults sword");
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
        // v1 had no active-quest/progress bytes: zero them, then fix the checksum
        // at the legacy offset (byte 26).
        bytes[SAVE_ACTIVE_OFF] = 0;
        bytes[SAVE_PROGRESS_OFF] = 0;
        bytes[SAVE_V3_CHECKSUM_OFF] = 0;
        for (uint8_t i = 0; i < SAVE_V3_CHECKSUM_OFF; i++)
            bytes[SAVE_V3_CHECKSUM_OFF] = static_cast<uint8_t>(bytes[SAVE_V3_CHECKSUM_OFF] + bytes[i]);

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
        Test t("conditions: always is live; the quest/crafted gates have their own suites");
        SaveBlock s;
        saveDefaults(s);
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
        Test t("equip weapon writes the v4 byte; out-of-range + same weapon are no-ops");
        SaveBlock s;
        saveDefaults(s);
        t.assert(s.weapon, W_SWORD, "default sword");
        t.assert(screenApplyAction(s, row(0, screens::ACTION_EQUIP_WEAPON, screens::COND_ALWAYS, W_FLAIL)), true, "equip flail changes save");
        t.assert(s.weapon, W_FLAIL, "flail equipped");
        t.assert(screenApplyAction(s, row(0, screens::ACTION_EQUIP_WEAPON, screens::COND_ALWAYS, W_FLAIL)), false, "same weapon is a no-op");
        t.assert(s.weapon, W_FLAIL, "still flail");
        t.assert(screenApplyAction(s, row(0, screens::ACTION_EQUIP_WEAPON, screens::COND_ALWAYS, SAVE_TIER_COUNT)), false, "param at the weapon count rejected");
        t.assert(screenApplyAction(s, row(0, screens::ACTION_EQUIP_WEAPON, screens::COND_ALWAYS, 255)), false, "param 255 rejected");
        t.assert(s.weapon, W_FLAIL, "weapon unchanged after rejects");
        t.assert(screenApplyAction(s, row(0, screens::ACTION_EQUIP_WEAPON, screens::COND_ALWAYS, W_GUN)), true, "equip gun changes save");
        t.assert(s.weapon, W_GUN, "gun equipped");
        suite.addTest(t);
    }

    // Armor craft/equip moved onto the detail card (ui.3.1, 5co.6): the
    // screenApplyAction switch no longer carries an armor case. The card path is
    // pinned by tst/card_state_test.hpp; armorEquipToggle itself by
    // tst/armor_engine_test.hpp.

    // -------------------------------------- gear skill readout cache (gs.2)
    {
        Test t("screenReset zeroes the gear skill cache; ROW_F_SKILL decodes");
        t.assert(screens::ROW_F_HIDE_LOCKED, 0x01, "hide_locked flag stable");
        t.assert(screens::ROW_F_ZENNY, 0x02, "zenny flag stable");
        t.assert(screens::ROW_F_SKILL, 0x04, "skill flag value");
        ScreenState s;
        for (uint8_t i = 0; i < armor::SKILL_COUNT; i++) {
            s.skillPoints[i] = static_cast<uint8_t>(i + 1);
            s.skillTier[i] = 2;
        }
        screenReset(s, screens::SCREEN_GEAR, screens::SCREEN_GEAR_ROWS);
        for (uint8_t i = 0; i < armor::SKILL_COUNT; i++) {
            t.assert(s.skillPoints[i], 0, "reset zeroes points");
            t.assert(s.skillTier[i], 0, "reset zeroes tier");
        }
        // A skill row flags ROW_F_SKILL and decodes its skill index from param.
        const ScreenRow r = row(0, screens::ACTION_NONE, screens::COND_ALWAYS, armor::SKILL_ATTACK_UP, screens::ROW_F_SKILL);
        t.assert((r.flags & screens::ROW_F_SKILL) != 0, true, "ROW_F_SKILL decodes");
        t.assert(r.param, armor::SKILL_ATTACK_UP, "param is the skill index");
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

    runner.addTestSuite(suite);
}
