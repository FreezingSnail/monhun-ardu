#pragma once
// Device cart glue for a hunt start (bead monhun-ardu-mgn, qs.4; hub-as-root
// huntStart bead monhun-ardu-isp.1): arms the core's quest kill counter from the
// active QuestDef and resolves the current weapon's smith tier into the
// damage/speed multipliers. Device-only (reads the mhQuests/mhSmith cart blobs),
// so the host boot-flow suite (tst/app_state_test.hpp) exercises the pure
// routing in src/app_state.hpp instead. The sketch calls these after every
// huntStart().

#include "core/world.hpp"
#include "quest.hpp"
#include "forge.hpp"   // forgeEquippedClass/Mul/Sheet: equipped node -> class + multipliers + sheet (ui.4, jd1)
#include "armor.hpp"   // armorApplyToGame: cache equipped stats at hunt start (arm.2)

namespace mh {

// Start a hunt from the hub (monhun-ardu-isp.1): the save's v4 weapon picks the
// loadout, and the active quest's goal picks the beast kind -- a GOAL_KILL quest
// spawns its target, anything else (no quest / gather goal) falls back to the
// LUNGE beast. newGame resets the world, then the camp spawn arms the room (the
// camp door leads to the area hunt). The caller still arms quest/tier/items/
// armor after this (those read the save, not the hunt start).
static void huntStart(Game &g, const SaveBlock &save) {
    int8_t kind = MON_LUNGE;
    const uint8_t quest = save.activeQuest;
    if (quest != SAVE_QUEST_NONE && quest < quests::QUEST_COUNT) {
        QuestDef def;
        questReadDef(quest, def);
        if (def.goalKind == quests::GOAL_KILL)
            kind = static_cast<int8_t>(def.target);
    }
    newGame(g, static_cast<int8_t>(forgeEquippedClass(save)), MODE_HUNT, kind);
    loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
}

static void questApplyToGame(Game &g, const SaveBlock &save) {
    g.questGoalKind = -1;
    g.questTarget = -1;
    g.questNeed = 0;
    g.questProgress = 0;
    const uint8_t quest = save.activeQuest;
    if (quest == SAVE_QUEST_NONE || quest >= quests::QUEST_COUNT)
        return;
    QuestDef def;
    questReadDef(quest, def);
    g.questGoalKind = static_cast<int8_t>(def.goalKind);
    g.questTarget = static_cast<int8_t>(def.target);
    g.questNeed = def.need;
    g.questProgress = save.progress;
}

// ui.4 (5co.4): the equipped forge node's dmgMul/spdMul replace the retired
// smith tier table. SAVE_NODE_NONE (or an out-of-range id) leaves 100/100.
// jd1: the same node's sheet kind rides along so the render draws its shield.
static void upgradeApplyToGame(Game &g, const SaveBlock &save) {
    forgeEquippedMul(save, g.dmgMul, g.spdMul);
    g.wpnSheet = forgeEquippedSheet(save);
}

// Restore the persisted inventory into the live hunt (prg.5 save v2). newGame
// clears Game::items[], so this re-seeds the pantry at hunt start; gather/carve
// then add to the RAM counts and the hunt-end commit folds them back with
// saveFoldItems (max), so a hunt can spend herbs without erasing the stock.
static void itemsApplyToGame(Game &g, const SaveBlock &save) {
    for (uint8_t i = 0; i < ITEM_COUNT; i++)
        g.items[i] = save.items[i];
}

}   // namespace mh
