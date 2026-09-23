#pragma once
// Quest runtime logic (bead monhun-ardu-me6, docs/quests-shops.md "Quests";
// record v2 bead monhun-ardu-dlp.1).
//
// Host-testable, no Arduino/cart: the QuestDef struct + the save-block state
// machine that TAKES a quest, counts goal progress (kills or gathered items,
// clamped at 255), and TURNS IT IN for a u16-clamped zenny payout plus an
// optional material reward. The cart read side (record -> QuestDef) lives in
// src/quest.hpp so this header compiles on the host with plain structs.
//
// One active quest at a time: TAKE writes the taken bit and sets activeQuest;
// progress feeds through questAddProgress (clamped at 255); TURN_IN requires
// progress >= need, pays rewardZenny, adds rewardItem-1 x rewardCount through
// saveItemAdd when rewardItem != 0, clears taken, sets done and clears the
// active slot. unlockFlag 0 means "always unlocked"; otherwise it names the
// (1-based) quest whose done bit gates this one, so quest chains are data.

#include <stdint.h>
#include "core/save.hpp"

namespace mh {

// A quest record, matching the packed 9 B cart layout from tools/gen-quests.py.
struct QuestDef {
    uint8_t id;
    uint8_t goalKind;   // quests::GOAL_KILL / GOAL_GATHER
    uint8_t target;     // MonsterKind (kill) or item index (gather)
    uint8_t need;
    uint16_t rewardZenny;
    uint8_t rewardItem;    // itemIdx + 1, 0 = none
    uint8_t rewardCount;   // 1..255 when rewardItem != 0
    uint8_t unlockFlag;
};

enum QuestStatus : uint8_t {
    QS_AVAILABLE = 0,   // unlocked, not taken, not done
    QS_ACTIVE,          // taken; progress counts kills
    QS_DONE,            // turned in
    QS_UNAVAILABLE      // locked behind unlockFlag or another active quest
};

inline bool questDone(const SaveBlock &save, uint8_t quest) {
    return saveQuestGet(save, quest, 1);
}
inline bool questTaken(const SaveBlock &save, uint8_t quest) {
    return saveQuestGet(save, quest, 0);
}
inline bool questIsActive(const SaveBlock &save, uint8_t quest) {
    return save.activeQuest == quest;
}

// unlockFlag: 0 = always; else the 1-based prior quest that must be done.
inline bool questUnlocked(const SaveBlock &save, uint8_t unlockFlag) {
    if (unlockFlag == 0)
        return true;
    return questDone(save, static_cast<uint8_t>(unlockFlag - 1));
}

inline QuestStatus questStatus(const SaveBlock &save, uint8_t quest, uint8_t unlockFlag = 0) {
    if (quest >= 16)
        return QS_UNAVAILABLE;
    if (questDone(save, quest))
        return QS_DONE;
    if (questIsActive(save, quest))
        return QS_ACTIVE;
    if (!questUnlocked(save, unlockFlag) || save.activeQuest != SAVE_QUEST_NONE)
        return QS_UNAVAILABLE;
    return QS_AVAILABLE;
}

// Takeable: unlocked, not taken/done, and no other quest active. The unlock
// check is done by the caller (which owns the cart def); the save-only state
// machine guards the rest.
inline bool questTakeable(const SaveBlock &save, uint8_t quest) {
    if (quest >= 16)
        return false;
    if (save.activeQuest != SAVE_QUEST_NONE)
        return false;
    return !questTaken(save, quest) && !questDone(save, quest);
}

inline bool questReady(const SaveBlock &save, uint8_t quest, uint8_t need) {
    return save.activeQuest == quest && save.progress >= need;
}

// u16 saturating add: payout clamps at 65535.
inline uint16_t zennyAdd(uint16_t balance, uint16_t amount) {
    const uint32_t sum = static_cast<uint32_t>(balance) + amount;
    return sum > 65535u ? 65535 : static_cast<uint16_t>(sum);
}

// Take: sets the taken bit + active slot, resets progress. False when not
// takeable (already taken/done or another quest active).
inline bool questTake(SaveBlock &save, uint8_t quest) {
    if (!questTakeable(save, quest))
        return false;
    saveQuestSet(save, quest, 0);
    save.activeQuest = quest;
    save.progress = 0;
    return true;
}

// Add `n` goal-progress steps to the active quest (progress saturates at 255).
// The kill path adds one per target-kind kill; a gather path adds the yielded
// item count. No active quest is a no-op.
inline void questAddProgress(SaveBlock &save, uint8_t n) {
    if (save.activeQuest == SAVE_QUEST_NONE)
        return;
    const uint16_t v = static_cast<uint16_t>(save.progress) + n;
    save.progress = v > 255 ? 255 : static_cast<uint8_t>(v);
}

// Turn in: requires the active quest at/over need; pays rewardZenny, grants
// rewardItem-1 x rewardCount (when rewardItem != 0), marks done, clears taken +
// active progress.
inline bool questTurnIn(SaveBlock &save, uint8_t quest, uint8_t need, uint16_t rewardZenny, uint8_t rewardItem, uint8_t rewardCount) {
    if (!questReady(save, quest, need))
        return false;
    save.zenny = zennyAdd(save.zenny, rewardZenny);
    if (rewardItem != 0)
        saveItemAdd(save, static_cast<uint8_t>(rewardItem - 1), rewardCount);
    saveQuestClear(save, quest, 0);
    saveQuestSet(save, quest, 1);
    save.activeQuest = SAVE_QUEST_NONE;
    save.progress = 0;
    return true;
}

}   // namespace mh
