#pragma once
// On-device end-to-end suite for the app flow (bead monhun-ardu-mgn qs.4; hub as
// the root screen monhun-ardu-isp.1): boot -> hub -> HUNT -> fight -> win/death
// -> A -> hub -> QUESTS board, plus the hub -> quests take -> gear -> hub
// detour. Drives the same src/app_state.hpp routing and src/app_setup.hpp hunt
// start (huntStart + quest/tier/items/armor arming) as the shipping sketch.
// ui.3.1 (5co.6): the SMITH screen is gone; the armor card craft/equip E2E is
// pinned by cards_test.

#include "harness/fxtest.hpp"
#include "src/screens.hpp"
#include "src/forge.hpp"   // forgeReadNode/forgeNodeEquipToggle + forge::NODE_* (ui.4)
#include "src/cards.hpp"   // cardLoad/cardHint/cardApply + the quest card (087)
#include "src/app_state.hpp"
#include "src/app_setup.hpp"
#include "src/core/world.hpp"
#include "src/core/monster.hpp"

#include <stdint.h>

namespace hubfx {

using namespace mh;

static const SaveBackend REAL_BACKEND = {saveEepromRead, saveEepromWrite};

// Card state for the quest-card launch (monhun-ardu-087): file scope so the
// suite's stack frame stays small (the device RAM guard is tight).
static DetailState dState;
static CardItem dCard;
static ScreenRow dRow;

static const Input H_IDLE = {0, 0, false, false};
static const Input H_DOWN = {0, 1, false, false};
static const Input H_A = {0, 0, true, false};
static const Input H_B = {0, 0, false, true};

// ---- framebuffer helpers (screens_test.hpp style) --------------------------
static void clearFb() {
    uint8_t *b = arduboy.getBuffer();
    for (uint16_t i = 0; i < 1024; i++)
        b[i] = 0;
}

static uint8_t bitAt(uint8_t x, uint8_t y) {
    return static_cast<uint8_t>((arduboy.getBuffer()[static_cast<uint16_t>(y >> 3) * 128 + x] >> (y & 7)) & 1);
}

static uint16_t countBits(uint8_t xa, uint8_t xb, uint8_t ya, uint8_t yb) {
    uint16_t n = 0;
    for (uint8_t y = ya; y <= yb; y++)
        for (uint8_t x = xa; x <= xb; x++)
            n += bitAt(x, y);
    return n;
}

// One d-pad tap: press then release, so the next direction is a fresh press.
static void tap(ScreenState &s, const Input &dir) {
    screenStep(s, dir);
    screenStep(s, H_IDLE);
}

// Mirror of the sketch's screen branch: nav on A/B, and on a non-routing A
// resolve + apply the cursor row's save action (committing it). Returns the
// routing destination for this tick.
static AppNav screenTick(ScreenState &s, SaveBlock &save, const Input &in) {
    const ScreenEvent ev = screenStep(s, in);
    if (ev == SCREEN_BACK)
        return appScreenBack(s.screen);
    if (ev != SCREEN_ACCEPT)
        return APP_NAV_NONE;
    ScreenRow row;
    if (!screenCursorRow(s, row) || !screenCondOk(save, row))
        return APP_NAV_NONE;
    const AppNav nav = appScreenAccept(s.screen, row);
    if (nav == APP_NAV_NONE && screenApplyAction(save, row))
        saveStore(save, REAL_BACKEND);
    return nav;
}

// A/B press+release, releasing first so a held button from the previous
// transition cannot swallow the edge (the appNavApply guard).
static AppNav pressA(ScreenState &s, SaveBlock &save) {
    screenTick(s, save, H_IDLE);
    const AppNav nav = screenTick(s, save, H_A);
    screenTick(s, save, H_IDLE);
    return nav;
}

static AppNav pressB(ScreenState &s, SaveBlock &save) {
    screenTick(s, save, H_IDLE);
    const AppNav nav = screenTick(s, save, H_B);
    screenTick(s, save, H_IDLE);
    return nav;
}

// Mirror the sketch's quest-card launch (monhun-ardu-087): the card A already
// applied the row action (a fresh take wrote the save); appQuestCardLaunch then
// closes the card + board via APP_NAV_HUNT and the caller arms the hunt.
static bool cardLaunch(ScreenState &s, SaveBlock &save, Game &g, const Input &in) {
    if (!appQuestCardLaunch(save, dRow))
        return false;
    cardClose(dState);
    appNavApply(APP_NAV_HUNT, s, save, g, in);
    huntStart(g, save);
    questApplyToGame(g, save);
    upgradeApplyToGame(g, save);
    itemsApplyToGame(g, save);
    return true;
}

// Open the quest card for `row` off the cart (the sketch's card-open branch).
// The carved test_hub image cannot afford cardHint/cardApply (the whole armor +
// forge card machinery); the quest take runs through screenApplyAction, which is
// exactly what cardApply's quest-row default case does. cardLoad/cardApply/card
// hints are pinned by test_cards and the host card suite.
static void cardOpenRow(const ScreenRow &row, const SaveBlock &save) {
    cardLoad(dState, dCard, cardRowIndex(row), save, false);
    dRow = row;
}

inline void test_hub(FxTest &test) {
    // Known save: 500 zenny, a 4-herb pantry, no quest, no tier.
    SaveBlock save;
    saveDefaults(save);
    save.zenny = 500;
    save.items[ITEM_HERB] = 4;
    save.items[ITEM_ORE] = 3;
    save.items[ITEM_SCALE] = 2;
    saveStore(save, REAL_BACKEND);

    ScreenState screen;
    Game g;

    // --------------------------------------- boot: the hub is the root (isp.1)
    screenEnter(screen, screens::SCREEN_HUB, save);
    test.expectEq(static_cast<uint32_t>(screen.active), 1, F("boot hub active"));
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("boot hub screen"));
    test.expectEq(static_cast<uint32_t>(screen.rowCount), screens::SCREEN_HUB_ROWS, F("hub row count"));
    test.expectEq(static_cast<uint32_t>(screen.cursor), 0, F("hub cursor boots on QUESTS"));
    test.expectEq(static_cast<uint32_t>(g.over), OVER_NONE, F("no hunt started yet"));

    // ---------------------------- hub MAP row + quest marker (imx)
    // The hub MAP row (row 1) routes to SCREEN_MAP; the quest roomHint table
    // resolves the marker room. Room indices are the sorted map.json order
    // (area 0, camp 1, cavern 2, ridge 3); no active quest -> no marker.
    ScreenRow mapRow;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_HUB, 1), mapRow);
    test.expectEq(static_cast<uint32_t>(mapRow.action), screens::ACTION_OPEN_MAP, F("hub MAP row action"));
    test.expectEq(static_cast<uint32_t>(appScreenAccept(screens::SCREEN_HUB, mapRow)), APP_NAV_MAP, F("MAP row routes to the map"));
    appNavApply(APP_NAV_MAP, screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_MAP, F("MAP screen open"));
    test.expectEq(static_cast<uint32_t>(screen.rowCount), screens::SCREEN_MAP_ROWS, F("MAP row count"));
    test.expectEq(static_cast<uint32_t>(appScreenBack(screens::SCREEN_MAP)), APP_NAV_HUB, F("MAP B backs to the hub"));
    appNavApply(APP_NAV_HUB, screen, save, g, H_A);
    save.activeQuest = SAVE_QUEST_NONE;
    test.expectEq(static_cast<uint32_t>(screenMapQuestRoom(save)), quests::QUEST_ROOM_HINT_NONE, F("no quest -> no marker"));
    save.activeQuest = quests::QUEST_GATHER_ORE;
    test.expectEq(static_cast<uint32_t>(screenMapQuestRoom(save)), zone::ROOM_CAVERN, F("gather ore marks the cavern"));
    save.activeQuest = quests::QUEST_SLAY_LUNGE;
    test.expectEq(static_cast<uint32_t>(screenMapQuestRoom(save)), zone::ROOM_AREA, F("slay lunge marks the area"));
    save.activeQuest = quests::QUEST_CRUSH_HEAVY;
    test.expectEq(static_cast<uint32_t>(screenMapQuestRoom(save)), zone::ROOM_RIDGE, F("crush heavy marks the ridge"));
    save.activeQuest = quests::QUEST_COUNT;   // out-of-range stays inert
    test.expectEq(static_cast<uint32_t>(screenMapQuestRoom(save)), quests::QUEST_ROOM_HINT_NONE, F("bad quest index inert"));
    save.activeQuest = SAVE_QUEST_NONE;
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("back on the hub after MAP"));

    // ------------------- hub QUESTS -> board -> quest card launches (087)
    // The hub boots on QUESTS (row 0); A opens the board, A on the take row
    // opens the quest card from the cart, and the card A takes the contract and
    // launches the hunt (mirrors the sketch's card branch).
    test.expectEq(static_cast<uint32_t>(screen.cursor), 0, F("hub cursor on QUESTS"));
    appNavApply(pressA(screen, save), screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_QUESTS, F("hub QUESTS opens the board"));
    test.expectEq(static_cast<uint32_t>(screen.rowCount), screens::SCREEN_QUESTS_ROWS, F("quests row count"));
    test.expectEq(static_cast<uint32_t>(screen.cursor), 0, F("board cursor on the take row"));

    ScreenRow qrow;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_QUESTS, 0), qrow);
    test.expectEq(static_cast<uint32_t>(qrow.action), screens::ACTION_TAKE_QUEST, F("board row0 take action"));
    test.expectEq(static_cast<uint32_t>(cardRowIndex(qrow)), cards::CARD_QUEST_SLAY_LUNGE, F("take row card index"));
    cardOpenRow(qrow, save);
    test.expectEq(static_cast<uint32_t>(dState.active), 1, F("quest card open"));
    test.expectEq(static_cast<uint32_t>(dState.kind), cards::KIND_QUEST, F("quest card kind"));
    // The card A runs the row action (cardApply's quest default): take writes
    // the save, then appQuestCardLaunch reports the launch.
    test.expectEq(static_cast<uint32_t>(screenApplyAction(save, dRow)), 1, F("card take applies"));
    saveStore(save, REAL_BACKEND);
    test.expectEq(static_cast<uint32_t>(save.activeQuest), 0, F("quest 0 active"));
    test.expectEq(static_cast<uint32_t>(saveQuestGet(save, 0, 0)), 1, F("quest 0 taken bit"));
    test.expectEq(static_cast<uint32_t>(appQuestCardLaunch(save, dRow)), 1, F("active card -> launch"));
    test.expectEq(static_cast<uint32_t>(cardLaunch(screen, save, g, H_A)), 1, F("card A closes + launches"));
    test.expectEq(static_cast<uint32_t>(screen.active), 0, F("board closed for the hunt"));
    test.expectEq(static_cast<uint32_t>(g.weapon), W_SWORD, F("hunt weapon from the save"));
    test.expectEq(static_cast<uint32_t>(g.monsterKind), MON_LUNGE, F("quest kill-target beast"));
    test.expectEq(static_cast<uint32_t>(g.roomId), zone::ROOM_CAMP, F("hunt starts in camp"));
    test.expectEq(static_cast<uint32_t>(g.questGoalKind), quests::GOAL_KILL, F("quest kill goal armed"));
    test.expectEq(static_cast<uint32_t>(g.questTarget), MON_LUNGE, F("quest target armed"));
    test.expectEq(static_cast<uint32_t>(g.questNeed), 3, F("quest need armed"));
    test.expectEq(static_cast<uint32_t>(g.over), OVER_NONE, F("hunt starts live"));
    test.expectEq(static_cast<uint32_t>(g.projN), 0, F("fresh projectile ring"));
    test.expectEq(static_cast<uint32_t>(g.fxN), 0, F("fresh effect ring"));
    // ui.4: the equipped sword root carries 100/100 -> identity multipliers.
    test.expectEq(static_cast<uint32_t>(g.dmgMul), 100, F("sword root identity multiplier"));
    test.expectEq(static_cast<uint32_t>(g.items[ITEM_HERB]), 4, F("inventory restored from the save"));

    // ------------------- resume: camp hold-B -> hub -> QUESTS -> active card
    // Camp hold-B (sheathed) requests the hub; the board's card for the still
    // active quest shows A GO and relaunches with no save write.
    g.player.sheathed = true;
    g.player.sheatheLatch = false;
    g.menuRequest = false;
    for (int16_t i = 0; i < HOLD_TICKS; i++)
        stepGame(g, H_B);
    test.expectEq(static_cast<uint32_t>(g.menuRequest), 1, F("camp hold-B requests the hub"));
    test.expectEq(static_cast<uint32_t>(appHubRequest(g)), APP_NAV_HUB, F("request routes to the hub"));
    test.expectEq(static_cast<uint32_t>(g.menuRequest), 0, F("request consumed once"));
    appNavApply(APP_NAV_HUB, screen, save, g, H_B);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("camp exit opens the hub"));
    test.expectEq(static_cast<uint32_t>(screen.cursor), 0, F("hub cursor reset on QUESTS"));
    appNavApply(pressA(screen, save), screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_QUESTS, F("board again"));
    cardLoad(dState, dCard, cardRowIndex(qrow), save, false);
    dRow = qrow;
    test.expectEq(static_cast<uint32_t>(dState.active), 1, F("resume card open"));
    test.expectEq(static_cast<uint32_t>(appQuestCardLaunch(save, dRow)), 1, F("resume card -> launch"));
    test.expectEq(static_cast<uint32_t>(screenApplyAction(save, dRow)), 0, F("resume take is a no-op"));
    test.expectEq(static_cast<uint32_t>(cardLaunch(screen, save, g, H_A)), 1, F("resume card A relaunches"));
    test.expectEq(static_cast<uint32_t>(g.over), OVER_NONE, F("resumed hunt live"));
    test.expectEq(static_cast<uint32_t>(g.monsterKind), MON_LUNGE, F("resumed quest beast"));
    test.expectEq(static_cast<uint32_t>(g.roomId), zone::ROOM_CAMP, F("resumed hunt starts in camp"));

    // ------------------------------- fight a few ticks, then win + commit
    for (uint8_t i = 0; i < 3; i++)
        stepGame(g, H_IDLE);
    test.expectEq(static_cast<uint32_t>(g.over), OVER_NONE, F("still fighting"));
    damageMonster(g, 2000, g.monster.x, g.monster.y);
    test.expectEq(static_cast<uint32_t>(g.over), OVER_WIN, F("hunt won"));
    test.expectEq(static_cast<uint32_t>(g.questProgress), 1, F("one kill counted"));
    // A carved scale + a gathered ore land in RAM only; the hunt-end commit
    // folds them into the save (coalesced, not per item).
    g.items[ITEM_SCALE] = 2;
    g.items[ITEM_ORE] = 1;

    bool huntLatched = false;
    test.expectEq(static_cast<uint32_t>(appHuntCommit(g.over != OVER_NONE, huntLatched, save, g)), 1, F("end commit fires"));
    saveStore(save, REAL_BACKEND);
    test.expectEq(static_cast<uint32_t>(appHuntCommit(g.over != OVER_NONE, huntLatched, save, g)), 0, F("end commit latched"));
    test.expectEq(static_cast<uint32_t>(save.progress), 1, F("progress written"));
    SaveBlock reloaded;
    test.expectEq(static_cast<uint32_t>(saveLoad(reloaded, REAL_BACKEND)), 1, F("progress reloads"));
    test.expectEq(static_cast<uint32_t>(reloaded.progress), 1, F("reloaded progress"));
    test.expectEq(reloaded.zenny, 500, F("reloaded zenny"));
    test.expectEq(static_cast<uint32_t>(reloaded.items[ITEM_HERB]), 4, F("pantry herb persisted"));
    test.expectEq(static_cast<uint32_t>(reloaded.items[ITEM_SCALE]), 2, F("carved scale persisted"));
    test.expectEq(static_cast<uint32_t>(reloaded.items[ITEM_ORE]), 3, F("gathered ore folds over the pantry (max)"));

    // Over screen: the sketch runs appOverReturnStep each tick; a fresh A
    // returns to the hub (dlp.3) so the quest can be turned in.
    bool prevA = false;
    appOverReturnStep(true, H_IDLE, prevA);
    test.expectEq(static_cast<uint32_t>(appOverReturnStep(true, H_A, prevA)), 1, F("over + A returns"));
    test.expectEq(static_cast<uint32_t>(appHuntReturn()), APP_NAV_HUB, F("hunt end routes to hub"));
    test.expectEq(static_cast<uint32_t>(appNavApply(appHuntReturn(), screen, save, g, H_A)), 0, F("return is not a hunt start"));
    test.expectEq(static_cast<uint32_t>(screen.active), 1, F("hunt end -> hub"));
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("hub after hunt"));
    test.expectEq(save.zenny, 500, F("hub zenny updated"));
    test.expectEq(static_cast<uint32_t>(save.progress), 1, F("hub progress updated"));

    // hub B is a root no-op: the hub stays up (isp.1 deleted the menu).
    appNavApply(pressB(screen, save), screen, save, g, H_B);
    test.expectEq(static_cast<uint32_t>(screen.active), 1, F("hub B keeps the hub active"));
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("hub B stays on the hub"));

    // The hub QUESTS row (row 0) reaches the board from the live hub; B backs.
    test.expectEq(static_cast<uint32_t>(screen.cursor), 0, F("hub cursor on QUESTS"));
    appNavApply(pressA(screen, save), screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_QUESTS, F("hub QUESTS row reaches the board"));
    test.expectEq(static_cast<uint32_t>(screen.rowCount), screens::SCREEN_QUESTS_ROWS, F("board row count"));
    appNavApply(pressB(screen, save), screen, save, g, H_B);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("board B -> hub"));

    // Fresh launch from the card resets the fight state (newGame path).
    g.projN = 2;
    g.fxN = 1;
    g.tick = 50;
    appNavApply(pressA(screen, save), screen, save, g, H_A);   // hub QUESTS
    cardLoad(dState, dCard, cardRowIndex(qrow), save, false);
    dRow = qrow;
    test.expectEq(static_cast<uint32_t>(cardLaunch(screen, save, g, H_A)), 1, F("card relaunch"));
    test.expectEq(static_cast<uint32_t>(g.tick), 0, F("fresh tick"));
    test.expectEq(static_cast<uint32_t>(g.projN), 0, F("fresh projectiles"));
    test.expectEq(static_cast<uint32_t>(g.fxN), 0, F("fresh effects"));
    test.expectEq(static_cast<uint32_t>(g.monsterKind), MON_LUNGE, F("fresh quest beast"));

    // ---------------------- hub GEAR: equip FLAIL, next hunt uses it (hbk.12)
    // Return to the hub, open GEAR (row 3), and press A on the WEAPON slot row:
    // it rotates to the next owned candidate (the flail root) and equips it in
    // place; the next card launch starts the hunt with the equipped flail.
    appNavApply(APP_NAV_HUB, screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("hub again"));
    tap(screen, H_DOWN);
    tap(screen, H_DOWN);
    tap(screen, H_DOWN);
    test.expectEq(static_cast<uint32_t>(screen.cursor), 3, F("cursor on GEAR"));
    appNavApply(pressA(screen, save), screen, save, g, H_A);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_GEAR, F("gear screen"));
    test.expectEq(static_cast<uint32_t>(screen.rowCount), screens::SCREEN_GEAR_ROWS, F("gear row count"));
    test.expectEq(static_cast<uint32_t>(screen.cursor), 0, F("gear cursor on the weapon slot"));
    screenGearSlotDefaults(screen, save);   // the sketch's GEAR entry default
    test.expectEq(static_cast<uint32_t>(screen.slotSel[0]), 0, F("weapon slot defaults to the sword"));
    test.expectEq(screenGearSlotCycle(save, screen, 0), 1, F("gear slot A equips the flail root"));
    test.expectEq(static_cast<uint32_t>(save.equippedNode), forge::NODE_FLAIL_BASE, F("flail root equipped"));
    saveStore(save, REAL_BACKEND);
    SaveBlock geareload;
    test.expectEq(static_cast<uint32_t>(saveLoad(geareload, REAL_BACKEND)), 1, F("gear save reloads"));
    test.expectEq(static_cast<uint32_t>(geareload.equippedNode), forge::NODE_FLAIL_BASE, F("equipped node persisted"));
    appNavApply(pressB(screen, save), screen, save, g, H_B);
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("gear B -> hub"));
    appNavApply(pressA(screen, save), screen, save, g, H_A);   // hub QUESTS
    cardLoad(dState, dCard, cardRowIndex(qrow), save, false);
    dRow = qrow;
    test.expectEq(static_cast<uint32_t>(cardLaunch(screen, save, g, H_A)), 1, F("card launch with the flail"));
    test.expectEq(static_cast<uint32_t>(g.weapon), W_FLAIL, F("hunt started with the equipped flail"));
}

}   // namespace hubfx
