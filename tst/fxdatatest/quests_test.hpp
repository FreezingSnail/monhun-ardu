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
    test.expectEq(quests::QUEST_COUNT, 3, F("quest count"));
    test.expectEq(quests::TARGET_LUNGE, MON_LUNGE, F("target lunge == roster"));
    test.expectEq(quests::TARGET_SWEEP, MON_SWEEP, F("target sweep == roster"));
    test.expectEq(quests::TARGET_HEAVY, MON_HEAVY, F("target heavy == roster"));

    QuestDef d0, d1, d2;
    questReadDef(quests::QUEST_SLAY_LUNGE, d0);
    questReadDef(quests::QUEST_SLAY_SWEEP, d1);
    questReadDef(quests::QUEST_CRUSH_HEAVY, d2);
    test.expectEq(d0.id, 0, F("q0 id"));
    test.expectEq(d0.targetKind, MON_LUNGE, F("q0 target"));
    test.expectEq(d0.need, 3, F("q0 need"));
    test.expectEq(d0.reward, 150, F("q0 reward"));
    test.expectEq(d0.unlockFlag, 0, F("q0 unlock"));
    test.expectEq(d1.id, 1, F("q1 id"));
    test.expectEq(d1.targetKind, MON_SWEEP, F("q1 target"));
    test.expectEq(d1.need, 2, F("q1 need"));
    test.expectEq(d1.reward, 250, F("q1 reward"));
    test.expectEq(d2.id, 2, F("q2 id"));
    test.expectEq(d2.targetKind, MON_HEAVY, F("q2 target"));
    test.expectEq(d2.need, 1, F("q2 need"));
    test.expectEq(d2.reward, 400, F("q2 reward"));

    // Board rows: take/reward pairs, reward in cost, need nibble in param.
    test.expectEq(screenRowCount(screens::SCREEN_QUESTS), 6, F("board rows"));
    ScreenRow take0, turn0;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_QUESTS, 0), take0);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_QUESTS, 1), turn0);
    test.expectEq(take0.action, screens::ACTION_TAKE_QUEST, F("board take action"));
    test.expectEq(take0.cond, screens::COND_QUEST, F("board take cond"));
    test.expectEq(turn0.action, screens::ACTION_TURN_IN_QUEST, F("board turn action"));
    test.expectEq(turn0.cost, d0.reward, F("turn cost == reward"));
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
        g.questTarget = static_cast<int8_t>(loaded.activeQuest == 0 ? d0.targetKind : -1);
        g.questNeed = d0.need;
        g.questProgress = loaded.progress;
        damageMonster(g, 9999, g.monster.x, g.monster.y);
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
    g.questTarget = d0.targetKind;
    g.questProgress = loaded.progress;
    damageMonster(g, 9999, g.monster.x, g.monster.y);
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

    // Corrupt magic -> safe defaults (active none).
    saveEepromWrite(SAVE_EEPROM_ADDR, 0x00);
    test.expectEq(saveLoad(out, REAL_BACKEND), 0, F("bad magic rejected"));
    test.expectEq(out.zenny, 0, F("bad magic defaults zenny"));
    test.expectEq(out.activeQuest, SAVE_QUEST_NONE, F("bad magic defaults active"));
}

}   // namespace questfx
