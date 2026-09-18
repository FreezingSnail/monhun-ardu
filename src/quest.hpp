#pragma once
// Cart readers for the quest-def table (bead monhun-ardu-me6, qs.2). Device-only
// (like src/screens.hpp): reads the QuestDef records out of the mhQuests cart
// blob through core/fxmem.hpp during the screen/render window. The host suite
// exercises src/quest_state.hpp with plain QuestDef structs instead.

#include "core/fxmem.hpp"
#include "generated/quest_meta.hpp"
#include "quest_state.hpp"

namespace mh {

// Fake cart pointer for a byte offset into the mhQuests raw_t section.
inline const uint8_t *questCart(uint16_t off) {
    return reinterpret_cast<const uint8_t *>(static_cast<uint16_t>(static_cast<uint16_t>(mhQuests) + off));
}

// Fixed 6 B records: id u8, targetKind u8, need u8, reward u16, unlockFlag u8.
inline void questReadDef(uint8_t quest, QuestDef &def) {
    const uint16_t off = static_cast<uint16_t>(quests::HEADER_SIZE + quests::RECORD_SIZE * quest);
    def.id = mhFxReadU8(questCart(static_cast<uint16_t>(off + quests::DEF_ID_OFF)));
    def.targetKind = mhFxReadU8(questCart(static_cast<uint16_t>(off + quests::DEF_TARGET_OFF)));
    def.need = mhFxReadU8(questCart(static_cast<uint16_t>(off + quests::DEF_NEED_OFF)));
    def.reward = mhFxReadU16(reinterpret_cast<const uint16_t *>(questCart(static_cast<uint16_t>(off + quests::DEF_REWARD_OFF))));
    def.unlockFlag = mhFxReadU8(questCart(static_cast<uint16_t>(off + quests::DEF_UNLOCK_OFF)));
}

}   // namespace mh
