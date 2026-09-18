#pragma once
// Host unit tests for the quest runtime (bead monhun-ardu-me6, qs.2):
// core/save.hpp v2 fields (active quest + progress), quest_state.hpp (take /
// kill / turn-in state machine, unlock chain, u16 payout clamp), the
// COND_QUEST row conditions + TAKE/TURN_IN actions in screen_state.hpp, and the
// Game kill-accounting hook in core/monster.hpp.
#include "test.hpp"
#include "../src/quest_state.hpp"
#include "../src/screen_state.hpp"
#include "../src/core/monster.hpp"
#include "../src/core/world.hpp"

using namespace mh;

namespace queststest {

inline ScreenRow questRow(uint8_t action, uint8_t param, uint16_t cost = 0) {
    ScreenRow r;
    r.cost = cost;
    r.action = action;
    r.flags = 0;
    r.cond = screens::COND_QUEST;
    r.param = param;
    return r;
}

// Full hunt: initGame + spawn a beast of `kind`, then kill it for real through
// the hit path (damageMonster) so the death hook is exercised.
inline void killBeast(Game &g, int8_t kind) {
    initGame(g, W_SWORD);
    initMonster(g, kind);
    g.questTarget = static_cast<int8_t>(kind);
    g.questNeed = 3;
    g.questProgress = 0;
    damageMonster(g, 9999, g.monster.x, g.monster.y);
}

}   // namespace queststest

using namespace queststest;

void QuestSuite(TestRunner &runner) {
    TestSuite suite("Quests: save v2, take/kill/turn-in, kill hook (qs.2)");

    // ------------------------------------------------------------- save v2
    {
        Test t("save v2 layout: active quest + progress bytes, checksum covers them");
        SaveBlock s;
        saveDefaults(s);
        t.assert(s.activeQuest, SAVE_QUEST_NONE, "default active quest is none");
        t.assert(s.progress, 0, "default progress is 0");
        s.zenny = 1000;
        s.activeQuest = 2;
        s.progress = 5;
        saveQuestSet(s, 2, 0);
        uint8_t bytes[SAVE_BYTES];
        t.assert(SAVE_BYTES, 15, "record is 15 B");
        t.assert(SAVE_CHECKSUM_OFF, 14, "checksum byte is 14");
        saveEncode(s, bytes);
        t.assert(bytes[2], SAVE_VERSION, "version 2");
        t.assert(bytes[SAVE_ACTIVE_OFF], 2, "active quest byte");
        t.assert(bytes[SAVE_PROGRESS_OFF], 5, "progress byte");
        uint8_t sum = 0;
        for (uint8_t i = 0; i < SAVE_CHECKSUM_OFF; i++)
            sum = static_cast<uint8_t>(sum + bytes[i]);
        t.assert(bytes[SAVE_CHECKSUM_OFF], sum, "checksum is the byte sum");
        SaveBlock out;
        t.assert(saveDecode(bytes, out), true, "round-trip decodes");
        t.assert(out.activeQuest, 2, "active quest round-trip");
        t.assert(out.progress, 5, "progress round-trip");
        t.assert(out.zenny, 1000, "zenny round-trip");
        t.assert(saveQuestGet(out, 2, 0), true, "taken bit round-trip");
        // Version 1 records are rejected (fields moved).
        bytes[2] = 1;
        t.assert(saveDecode(bytes, out), false, "version 1 rejected");
        suite.addTest(t);
    }

    // --------------------------------------------------------- quest state
    {
        Test t("unlock chain: flag 0 always, else the prior quest's done bit");
        SaveBlock s;
        saveDefaults(s);
        t.assert(questUnlocked(s, 0), true, "flag 0 always unlocked");
        t.assert(questUnlocked(s, 2), false, "chain gated by quest 1 done");
        saveQuestSet(s, 1, 1);
        t.assert(questUnlocked(s, 2), true, "prior done unlocks");
        suite.addTest(t);
    }

    {
        Test t("status + takeable: one active quest, done/taken guard");
        SaveBlock s;
        saveDefaults(s);
        t.assert(questStatus(s, 0), QS_AVAILABLE, "fresh quest available");
        t.assert(questTakeable(s, 0), true, "fresh quest takeable");
        t.assert(questTake(s, 0), true, "take changes save");
        t.assert(saveQuestGet(s, 0, 0), true, "taken bit set");
        t.assert(s.activeQuest, 0, "active slot set");
        t.assert(questStatus(s, 0), QS_ACTIVE, "now active");
        t.assert(questTake(s, 0), false, "double take rejected");
        t.assert(questTake(s, 1), false, "second quest blocked while active");
        t.assert(questTakeable(s, 1), false, "not takeable while active");
        suite.addTest(t);
    }

    {
        Test t("kills feed progress and saturate at 255");
        SaveBlock s;
        saveDefaults(s);
        questAddKill(s);   // no active quest: no-op
        t.assert(s.progress, 0, "no active quest counts nothing");
        questTake(s, 0);
        for (uint8_t i = 0; i < 3; i++)
            questAddKill(s);
        t.assert(s.progress, 3, "three kills counted");
        s.progress = 255;
        questAddKill(s);
        t.assert(s.progress, 255, "progress saturates");
        suite.addTest(t);
    }

    {
        Test t("turn-in requires progress >= need, pays reward, marks done");
        SaveBlock s;
        saveDefaults(s);
        s.zenny = 100;
        questTake(s, 0);
        s.progress = 2;
        t.assert(questReady(s, 0, 3), false, "under need not ready");
        t.assert(questTurnIn(s, 0, 3, 150), false, "under need rejected");
        t.assert(s.zenny, 100, "no payout when short");
        s.progress = 3;
        t.assert(questReady(s, 0, 3), true, "at need ready");
        t.assert(questTurnIn(s, 0, 3, 150), true, "turn-in changes save");
        t.assert(s.zenny, 250, "reward paid");
        t.assert(saveQuestGet(s, 0, 0), false, "taken cleared");
        t.assert(saveQuestGet(s, 0, 1), true, "done set");
        t.assert(s.activeQuest, SAVE_QUEST_NONE, "active slot cleared");
        t.assert(s.progress, 0, "progress reset");
        t.assert(questTurnIn(s, 0, 3, 150), false, "double turn-in rejected");
        t.assert(questStatus(s, 0), QS_DONE, "status done");
        suite.addTest(t);
    }

    {
        Test t("payout clamps at u16 max");
        SaveBlock s;
        saveDefaults(s);
        s.zenny = 65400;
        questTake(s, 2);
        s.progress = 1;
        t.assert(questTurnIn(s, 2, 1, 400), true, "turn-in applies");
        t.assert(s.zenny, 65535, "zenny saturates at 65535");
        t.assert(zennyAdd(65535, 1), 65535, "zennyAdd saturates");
        t.assert(zennyAdd(100, 250), 350, "zennyAdd adds normally");
        suite.addTest(t);
    }

    // ----------------------------------------------------- screen conditions
    {
        Test t("COND_QUEST: take rows gate on takeable, turn rows on ready");
        SaveBlock s;
        saveDefaults(s);
        const ScreenRow take = questRow(screens::ACTION_TAKE_QUEST, 0);
        const ScreenRow turn = questRow(screens::ACTION_TURN_IN_QUEST, 48, 150);   // need 3, quest 0
        t.assert(screenCondOk(s, take), true, "fresh take row live");
        t.assert(screenCondOk(s, turn), false, "turn row dead before take");
        t.assert(screenApplyAction(s, take), true, "take applies");
        t.assert(s.activeQuest, 0, "taken as active");
        t.assert(screenCondOk(s, take), false, "take row dead while active");
        t.assert(screenCondOk(s, turn), false, "turn row needs progress");
        s.progress = 3;
        t.assert(screenCondOk(s, turn), true, "turn row live at need");
        t.assert(screenApplyAction(s, turn), true, "turn-in applies");
        t.assert(s.zenny, 150, "row cost paid as reward");
        t.assert(saveQuestGet(s, 0, 1), true, "done bit set");
        t.assert(questRow(screens::ACTION_TURN_IN_QUEST, 33, 250).cond, screens::COND_QUEST, "row helper");
        suite.addTest(t);
    }

    {
        Test t("existing hub TAKE row takes the quest into the active model");
        SaveBlock s;
        saveDefaults(s);
        ScreenRow take = questRow(screens::ACTION_TAKE_QUEST, 1);
        t.assert(screenApplyAction(s, take), true, "hub take applies");
        t.assert(s.activeQuest, 1, "active slot is quest 1");
        t.assert(saveQuestGet(s, 1, 0), true, "taken bit set");
        suite.addTest(t);
    }

    // ------------------------------------------------------- kill hook
    {
        Test t("monster death counts only the active quest's target kind");
        Game g;
        killBeast(g, MON_LUNGE);
        t.assert(g.over, OVER_WIN, "beast died");
        t.assert(g.monster.state, MS_DEAD, "dead state");
        t.assert(g.questProgress, 1, "lunge kill counted");

        // A sweep dies while the quest targets lunge: no count.
        initGame(g, W_SWORD);
        initMonster(g, MON_SWEEP);
        g.questTarget = MON_LUNGE;
        g.questProgress = 1;
        damageMonster(g, 9999, g.monster.x, g.monster.y);
        t.assert(g.questProgress, 1, "off-kind kill not counted");

        // No active quest: no count.
        initGame(g, W_SWORD);
        initMonster(g, MON_LUNGE);
        g.questTarget = -1;
        damageMonster(g, 9999, g.monster.x, g.monster.y);
        t.assert(g.questProgress, 0, "no active quest counts nothing");
        suite.addTest(t);
    }

    {
        Test t("newGame resets quest accounting off");
        Game g;
        g.questTarget = MON_HEAVY;
        g.questProgress = 9;
        newGame(g, W_SWORD, MODE_HUNT, MON_HEAVY);
        t.assert(g.questTarget, -1, "new hunt starts unarmed");
        t.assert(g.questProgress, 0, "new hunt progress reset");
        t.assert(g.questNeed, 0, "new hunt need reset");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
