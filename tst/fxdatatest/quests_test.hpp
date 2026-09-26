#pragma once
// On-device end-to-end suite for quests (bead monhun-ardu-me6, qs.2).
//
// Covers the shipped cart path the host suite cannot: reading the QuestDef
// records out of the mhQuests blob, the quests board screen rows (take/reward
// pairs), the full take -> kill N -> turn-in flow through the real monster
// death hook, and the EEPROM persistence of active quest + progress + payout.

#include "harness/fxtest.hpp"
#include "src/screens.hpp"
#include "src/quest.hpp"
#include "src/core/monster.hpp"
#include "src/core/world.hpp"

#include <stdint.h>

namespace questfx {

using namespace mh;

static const SaveBackend REAL_BACKEND = {saveEepromRead, saveEepromWrite};

inline void test_quests(FxTest &test) {
    // --------------------------------------------------- cart quest records
    test.expectEq(quests::QUEST_COUNT, 5, F("quest count"));
    test.expectEq(quests::TARGET_LUNGE, MON_LUNGE, F("target lunge == roster"));
    test.expectEq(quests::TARGET_SWEEP, MON_SWEEP, F("target sweep == roster"));
    test.expectEq(quests::TARGET_HEAVY, MON_HEAVY, F("target heavy == roster"));
    test.expectEq(quests::TARGET_POLE, MON_POLE, F("target pole == roster"));

    QuestDef d0, d1, d2, d3, d4;
    questReadDef(quests::QUEST_SLAY_LUNGE, d0);
    questReadDef(quests::QUEST_SLAY_SWEEP, d1);
    questReadDef(quests::QUEST_GATHER_ORE, d2);
    questReadDef(quests::QUEST_CRUSH_HEAVY, d3);
    questReadDef(quests::QUEST_TRAIN_POLE, d4);
    test.expectEq(d0.id, 0, F("q0 id"));
    test.expectEq(d0.goalKind, quests::GOAL_KILL, F("q0 goal kill"));
    test.expectEq(d0.target, MON_LUNGE, F("q0 target"));
    test.expectEq(d0.need, 3, F("q0 need"));
    test.expectEq(d0.rewardZenny, 150, F("q0 reward"));
    test.expectEq(d0.rewardItem, 0, F("q0 no material"));
    test.expectEq(d0.rewardCount, 0, F("q0 no material count"));
    test.expectEq(d0.unlockFlag, 0, F("q0 unlock"));
    test.expectEq(d1.id, 1, F("q1 id"));
    test.expectEq(d1.goalKind, quests::GOAL_KILL, F("q1 goal kill"));
    test.expectEq(d1.target, MON_SWEEP, F("q1 target"));
    test.expectEq(d1.need, 2, F("q1 need"));
    test.expectEq(d1.rewardZenny, 250, F("q1 reward"));
    test.expectEq(d1.rewardItem, static_cast<uint8_t>(ITEM_SHELL + 1), F("q1 shell material"));
    test.expectEq(d1.rewardCount, 1, F("q1 shell count"));
    test.expectEq(d1.unlockFlag, 1, F("q1 unlock chain"));
    test.expectEq(d2.id, 2, F("q2 id"));
    test.expectEq(d2.goalKind, quests::GOAL_GATHER, F("q2 goal gather"));
    test.expectEq(d2.target, static_cast<uint8_t>(ITEM_ORE), F("q2 target ore item"));
    test.expectEq(d2.need, 3, F("q2 need"));
    test.expectEq(d2.rewardZenny, 200, F("q2 reward"));
    test.expectEq(d2.rewardItem, static_cast<uint8_t>(ITEM_ORE + 1), F("q2 ore material"));
    test.expectEq(d2.rewardCount, 2, F("q2 ore count"));
    test.expectEq(d2.unlockFlag, 0, F("q2 unlock chain (demo: always available)"));
    test.expectEq(d3.id, 3, F("q3 id"));
    test.expectEq(d3.goalKind, quests::GOAL_KILL, F("q3 goal kill"));
    test.expectEq(d3.target, MON_HEAVY, F("q3 target"));
    test.expectEq(d3.need, 1, F("q3 need"));
    test.expectEq(d3.rewardZenny, 400, F("q3 reward"));
    test.expectEq(d3.rewardItem, static_cast<uint8_t>(ITEM_SCALE + 1), F("q3 scale material"));
    test.expectEq(d3.rewardCount, 2, F("q3 scale count"));
    test.expectEq(d3.unlockFlag, 3, F("q3 unlock chain"));
    test.expectEq(d4.id, 4, F("q4 id"));
    test.expectEq(d4.goalKind, quests::GOAL_KILL, F("q4 goal kill"));
    test.expectEq(d4.target, MON_POLE, F("q4 target pole"));
    test.expectEq(d4.need, 1, F("q4 need"));
    test.expectEq(d4.rewardZenny, 0, F("q4 no zenny reward"));
    test.expectEq(d4.rewardItem, 0, F("q4 no material"));
    test.expectEq(d4.rewardCount, 0, F("q4 no material count"));
    test.expectEq(d4.unlockFlag, 0, F("q4 unlock 0 (available from the start)"));

    // Board rows: take/reward pairs, reward in cost, need nibble in param.
    test.expectEq(screenRowCount(screens::SCREEN_QUESTS), 10, F("board rows"));
    ScreenRow take0, turn0;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_QUESTS, 0), take0);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_QUESTS, 1), turn0);
    test.expectEq(take0.action, screens::ACTION_TAKE_QUEST, F("board take action"));
    test.expectEq(take0.cond, screens::COND_QUEST, F("board take cond"));
    test.expectEq(take0.unlock, d0.unlockFlag, F("take unlock from def"));
    test.expectEq(turn0.action, screens::ACTION_TURN_IN_QUEST, F("board turn action"));
    test.expectEq(turn0.cost, d0.rewardZenny, F("turn cost == reward"));
    test.expectEq(turn0.recipe[0].item, d0.rewardItem, F("turn material item from def"));
    test.expectEq(turn0.recipe[0].count, d0.rewardCount, F("turn material count from def"));
    test.expectEq((turn0.param >> 4) & 15, d0.need, F("turn need nibble"));

    // ------------------------------------------------ E2E: take -> kills -> turn-in
    SaveBlock save;
    saveDefaults(save);
    saveStore(save, REAL_BACKEND);   // clear any previous device run

    test.expectEq(screenCondOk(save, take0), 1, F("take row live"));
    test.expectEq(screenCondOk(save, turn0), 0, F("turn row dead before take"));
    test.expectEq(screenApplyAction(save, take0), 1, F("take applies"));
    test.expectEq(save.activeQuest, 0, F("active quest set"));
    test.expectEq(saveQuestGet(save, 0, 0), 1, F("taken bit set"));
    saveStore(save, REAL_BACKEND);

    SaveBlock loaded;
    test.expectEq(saveLoad(loaded, REAL_BACKEND), 1, F("take persisted"));
    test.expectEq(loaded.activeQuest, 0, F("active quest persisted"));

    // Kill three lunge beasts through the real death hook (one hunt each), and
    // commit progress at each hunt end exactly like the sketch does.
    Game g;
    for (uint8_t i = 0; i < 3; i++) {
        initGame(g, W_SWORD);
        initMonster(g, MON_LUNGE);
        g.questTarget = static_cast<int8_t>(loaded.activeQuest == 0 ? d0.target : -1);
        g.questGoalKind = static_cast<int8_t>(loaded.activeQuest == 0 ? d0.goalKind : -1);
        g.questNeed = d0.need;
        g.questProgress = loaded.progress;
        // 2000 respects damageMonster's int16 contract (dmg*14 <= 32767) and the
        // crit multiply lands 2800, lethal for every shipped beast pool.
        damageMonster(g, 2000, g.monster.x, g.monster.y);
        test.expectEq(g.over, OVER_WIN, F("hunt win"));
        loaded.progress = g.questProgress;
        saveStore(loaded, REAL_BACKEND);
    }
    test.expectEq(loaded.progress, 3, F("three kills persisted"));
    test.expectEq(saveLoad(loaded, REAL_BACKEND), 1, F("progress reloads"));
    test.expectEq(loaded.progress, 3, F("progress == need"));

    // Off-kind kill must not count (sweep while the lunge quest is active).
    initGame(g, W_SWORD);
    initMonster(g, MON_SWEEP);
    g.questTarget = d0.target;
    g.questGoalKind = d0.goalKind;
    g.questProgress = loaded.progress;
    damageMonster(g, 2000, g.monster.x, g.monster.y);
    test.expectEq(g.questProgress, 3, F("off-kind kill not counted"));

    // Turn in: payout + done bit, persisted.
    test.expectEq(screenCondOk(loaded, turn0), 1, F("turn row live at need"));
    test.expectEq(screenApplyAction(loaded, turn0), 1, F("turn-in applies"));
    test.expectEq(loaded.zenny, 150, F("reward paid"));
    test.expectEq(saveQuestGet(loaded, 0, 1), 1, F("done bit set"));
    test.expectEq(loaded.activeQuest, SAVE_QUEST_NONE, F("active cleared"));
    test.expectEq(loaded.progress, 0, F("progress reset"));
    saveStore(loaded, REAL_BACKEND);

    SaveBlock out;
    test.expectEq(saveLoad(out, REAL_BACKEND), 1, F("turn-in persisted"));
    test.expectEq(out.zenny, 150, F("zenny persisted"));
    test.expectEq(saveQuestGet(out, 0, 1), 1, F("done persisted"));
    test.expectEq(out.activeQuest, SAVE_QUEST_NONE, F("active none persisted"));

    // ------------------------- E2E: gather quest + chain (monhun-ardu-dlp.3)
    // Quest 2 (gather_ore) is chained behind quest 1: mark 1 done, take 2,
    // gather three ore through the real applyGather hook, then turn it in for
    // the zenny + material reward and confirm quest 3 unlocks.
    saveDefaults(save);
    saveQuestSet(save, 1, 1);   // prior quest done -> the gather quest is unlocked
    ScreenRow take2, turn2, take3;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_QUESTS, 4), take2);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_QUESTS, 5), turn2);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_QUESTS, 6), take3);
    test.expectEq(take2.action, screens::ACTION_TAKE_QUEST, F("gather take action"));
    test.expectEq(take2.unlock, d2.unlockFlag, F("gather take unlock from def"));
    test.expectEq(turn2.cost, d2.rewardZenny, F("gather turn cost == reward"));
    test.expectEq(turn2.recipe[0].item, d2.rewardItem, F("gather material item from def"));
    test.expectEq(screenCondOk(save, take2), 1, F("gather take live after unlock"));
    test.expectEq(screenApplyAction(save, take2), 1, F("gather take applies"));
    test.expectEq(save.activeQuest, 2, F("gather active quest set"));

    newGame(g, W_SWORD, MODE_HUNT);
    g.questGoalKind = static_cast<int8_t>(d2.goalKind);
    g.questTarget = static_cast<int8_t>(d2.target);
    g.questNeed = d2.need;
    g.questProgress = save.progress;
    for (uint8_t i = 0; i < 3; i++) {
        g.gatherMask = 0;                        // re-pick the node each pass
        g.player.itemNode = zone::PROP_AREA_5;   // ore node, yield 1
        applyGather(g, g.player);
    }
    test.expectEq(static_cast<uint32_t>(g.items[ITEM_ORE]), 3, F("three ore banked"));
    test.expectEq(static_cast<uint32_t>(g.questProgress), 3, F("gather progress == need"));
    save.progress = g.questProgress;
    test.expectEq(screenCondOk(save, turn2), 1, F("gather turn row live at need"));
    test.expectEq(screenApplyAction(save, turn2), 1, F("gather turn-in applies"));
    test.expectEq(save.zenny, 200, F("gather reward paid"));
    test.expectEq(static_cast<uint32_t>(save.items[ITEM_ORE]), 2, F("ore material reward granted"));
    test.expectEq(saveQuestGet(save, 2, 1), 1, F("gather done bit set"));
    test.expectEq(screenCondOk(save, take3), 1, F("chain unlock: quest 3 live after gather done"));

    // --------------------------- E2E: training-pole quest (bih.2)
    // Quest 4 is unlocked from the start (unlockFlag 0), targets the pole, and
    // needs one kill. Take it from the board (rows 8/9), kill a real pole, then
    // turn it in for 0 zenny (no economy effect).
    saveDefaults(save);
    ScreenRow take4, turn4;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_QUESTS, 8), take4);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_QUESTS, 9), turn4);
    test.expectEq(take4.action, screens::ACTION_TAKE_QUEST, F("pole take action"));
    test.expectEq(take4.param, 4, F("pole take param is quest 4"));
    test.expectEq(take4.unlock, d4.unlockFlag, F("pole take unlock from def"));
    test.expectEq(turn4.action, screens::ACTION_TURN_IN_QUEST, F("pole turn action"));
    test.expectEq(turn4.param, 20, F("pole turn param (need 1 << 4 | quest 4)"));
    test.expectEq(turn4.cost, d4.rewardZenny, F("pole turn cost == reward (0)"));
    test.expectEq(screenCondOk(save, take4), 1, F("pole take row live from the start"));
    test.expectEq(screenApplyAction(save, take4), 1, F("pole take applies"));
    test.expectEq(save.activeQuest, 4, F("pole active quest set"));

    newGame(g, W_SWORD, MODE_HUNT, MON_POLE);
    g.questGoalKind = static_cast<int8_t>(d4.goalKind);
    g.questTarget = static_cast<int8_t>(d4.target);
    g.questNeed = d4.need;
    g.questProgress = save.progress;
    damageMonster(g, 2000, g.monster.x, g.monster.y);
    test.expectEq(g.over, OVER_WIN, F("pole hunt win"));
    test.expectEq(g.questProgress, 1, F("pole kill counted"));
    save.progress = g.questProgress;
    test.expectEq(screenCondOk(save, turn4), 1, F("pole turn row live at need"));
    test.expectEq(screenApplyAction(save, turn4), 1, F("pole turn-in applies"));
    test.expectEq(save.zenny, 0, F("pole reward pays no zenny"));
    test.expectEq(saveQuestGet(save, 4, 1), 1, F("pole done bit set"));
    test.expectEq(save.activeQuest, SAVE_QUEST_NONE, F("pole active cleared"));

    // Corrupt magic -> safe defaults (active none).
    saveEepromWrite(SAVE_EEPROM_ADDR, 0x00);
    test.expectEq(saveLoad(out, REAL_BACKEND), 0, F("bad magic rejected"));
    test.expectEq(out.zenny, 0, F("bad magic defaults zenny"));
    test.expectEq(out.activeQuest, SAVE_QUEST_NONE, F("bad magic defaults active"));
}

}   // namespace questfx
