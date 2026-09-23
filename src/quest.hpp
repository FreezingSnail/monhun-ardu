#pragma once
// Cart readers for the quest-def table (bead monhun-ardu-me6, qs.2). Device-only
// (like src/screens.hpp): reads the QuestDef records out of the mhQuests cart
// blob through core/fxmem.hpp during the screen/render window. The host suite
// exercises src/quest_state.hpp with plain QuestDef structs instead.

#include <stddef.h>
#include "core/fxmem.hpp"
#include "generated/quest_meta.hpp"
#include "quest_state.hpp"

namespace mh {

// Fake cart pointer for a byte offset into the mhQuests raw_t section.
inline const uint8_t *questCart(uint16_t off) {
    return reinterpret_cast<const uint8_t *>(static_cast<uint16_t>(static_cast<uint16_t>(mhQuests) + off));
}

// Fixed 9 B records (v2, bead monhun-ardu-dlp.1): id u8, goalKind u8, target u8,
// need u8, rewardZenny u16, rewardItem u8 (itemIdx+1, 0 = none), rewardCount u8,
// unlockFlag u8. QuestDef mirrors that layout, so one bulk read fills the whole
// def (monhun-ardu-5co.8); the offsetof asserts pin the packed ABI (host
// padding past the last field is fine -- only RECORD_SIZE bytes are copied).
static_assert(offsetof(QuestDef, rewardZenny) == quests::DEF_REWARD_ZENNY_OFF, "QuestDef rewardZenny offset drift");
static_assert(offsetof(QuestDef, unlockFlag) == quests::DEF_UNLOCK_OFF, "QuestDef unlockFlag offset drift");
inline void questReadDef(uint8_t quest, QuestDef &def) {
    const uint16_t off = static_cast<uint16_t>(quests::HEADER_SIZE + quests::RECORD_SIZE * quest);
    mhFxReadBytes(questCart(off), reinterpret_cast<uint8_t *>(&def), quests::RECORD_SIZE);
}

}   // namespace mh
