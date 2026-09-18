#pragma once
// Device cart glue for a hunt start (bead monhun-ardu-mgn, qs.4): arms the
// core's quest kill counter from the active QuestDef and resolves the current
// weapon's smith tier into the damage/speed multipliers. Device-only (reads the
// mhQuests/mhSmith cart blobs), so the host boot-flow suite (tst/app_state_test.hpp)
// exercises the pure routing in src/app_state.hpp instead. The sketch calls
// these after every newGame/menuStart.

#include "core/world.hpp"
#include "quest.hpp"
#include "smith.hpp"

namespace mh {

static void questApplyToGame(Game &g, const SaveBlock &save) {
    g.questTarget = -1;
    g.questNeed = 0;
    g.questProgress = 0;
    const uint8_t quest = save.activeQuest;
    if (quest == SAVE_QUEST_NONE || quest >= quests::QUEST_COUNT)
        return;
    QuestDef def;
    questReadDef(quest, def);
    g.questTarget = static_cast<int8_t>(def.targetKind);
    g.questNeed = def.need;
    g.questProgress = save.progress;
}

static void upgradeApplyToGame(Game &g, const SaveBlock &save) {
    g.dmgMul = UPGRADE_MUL_BASE;
    g.spdMul = UPGRADE_MUL_BASE;
    const int8_t weapon = g.weapon;
    if (weapon < 0 || weapon >= smith::WEAPON_COUNT)
        return;
    const uint8_t tier = (weapon < SAVE_TIER_COUNT) ? save.tier[weapon] : 0;
    smithResolve(static_cast<uint8_t>(weapon), tier, g.dmgMul, g.spdMul);
}

}   // namespace mh
