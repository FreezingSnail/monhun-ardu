#pragma once
// Persistent save block (bead monhun-ardu-cgz, docs/quests-shops.md; v2 layout
// bead monhun-ardu-prg.5; v4 weapon bead monhun-ardu-isp.1).
//
// Packed little-endian record in EEPROM (version 4). The record is the v2
// prefix (bead monhun-ardu-me6) followed by the progression tail (prg.5:
// equipment + inventory counts) and the hml.1 weapon byte:
//
//   0..1   magic  u16 0x484D ("MH")
//   2      version u8 (4)
//   3..4   zenny  u16
//   5..8   quest  u8[4]  (16 quests x 2 bits: taken, done)
//   9      activeQuest u8 (0xFF = none; the quest progress is counted for)
//   10     progress    u8 (target-kind kills for the active quest)
//   11..13 tier  u8[3] (per weapon upgrade tier)
//   14..16 equip u8[3] (head/body/charm slot id; 0 = none)
//   17     flags  u8 (reserved progression bits, e.g. smithy seen)
//   18..25 items  u8[ITEM_COUNT] (inventory counts, cap 255)
//   26     weapon u8 (WeaponId 0..2; the hub's HUNT loadout, hml.1)
//   27     checksum u8 (sum of bytes 0..26)
//
// Load runs once on boot; anything but a good magic + version + checksum falls
// back to defaults. saveStore() only writes bytes that differ and verifies the
// read-back, so a power cut cannot leave a half-written record. Nothing here is
// called during a hunt: only screen actions and the hunt-end progress commit
// write (write-cycle hygiene).
//
// Migration (prg.5/isp.1): a blank/old block never crashes. saveLoad() decodes
// the shared v2 prefix from a version-2 record (inventory/equip default to
// empty), decodes the full v3 tail with weapon = 0 (hml.1 appended the weapon
// byte, so a v3 record's checksum still sits at byte 26), and rejects anything
// else into saveDefaults(). The v1 path covers the original 15-byte bead-cgz
// record (no active quest/progress) for the same reason: older EEPROM contents
// load with their zenny/quests/tiers intact.
//
// Host-testable: the logic takes a SaveBackend of three-address read/write
// functions rather than touching Arduino.h. On AVR save.hpp also provides the
// EEPROM-backed functions (EEPROM.update == write-if-different).

#include <stdint.h>
#include "progmem.hpp"                   // MH_NOINLINE
#include "bitlut.hpp"                    // mhBit8 (flash one-hot LUT)
#include "../generated/items_meta.hpp"   // item::ITEM_COUNT (inventory slot count)

namespace mh {

constexpr uint16_t SAVE_MAGIC = 0x484D;   // 'M','H' little-endian
constexpr uint8_t SAVE_VERSION = 4;
constexpr uint8_t SAVE_VERSION_V3 = 3;   // prg.5: tail, checksum at byte 26
constexpr uint8_t SAVE_VERSION_V2 = 2;   // bead me6: v2 prefix (15 B)
constexpr uint8_t SAVE_VERSION_V1 = 1;   // bead cgz: original 15 B, no quest progress
constexpr uint8_t SAVE_QUEST_BYTES = 4;
constexpr uint8_t SAVE_TIER_COUNT = 3;   // N_WEAPONS, matches screens::TIER_COUNT
constexpr uint8_t SAVE_ACTIVE_OFF = 9;   // activeQuest u8 (0xFF = none)
constexpr uint8_t SAVE_PROGRESS_OFF = 10;
constexpr uint8_t SAVE_TIER_OFF = 11;
constexpr uint8_t SAVE_EQUIP_OFF = 14;   // u8[3]: head, body, charm
constexpr uint8_t SAVE_EQUIP_COUNT = 3;
constexpr uint8_t SAVE_FLAGS_OFF = 17;                                                         // reserved progression bits
constexpr uint8_t SAVE_ITEMS_OFF = 18;                                                         // u8[item::ITEM_COUNT]
constexpr uint8_t SAVE_WEAPON_OFF = static_cast<uint8_t>(SAVE_ITEMS_OFF + item::ITEM_COUNT);   // 26
constexpr uint8_t SAVE_CHECKSUM_OFF = static_cast<uint8_t>(SAVE_WEAPON_OFF + 1);               // 27
constexpr uint8_t SAVE_BYTES = static_cast<uint8_t>(SAVE_CHECKSUM_OFF + 1);                    // 28
// Legacy records (v1..v3) kept the checksum at byte 26: v4 appended the weapon
// byte there, so a v3 decode validates against this offset.
constexpr uint8_t SAVE_V3_CHECKSUM_OFF = SAVE_WEAPON_OFF;   // 26
constexpr uint8_t SAVE_QUEST_NONE = 0xFF;
constexpr uint8_t SAVE_EQUIP_NONE = 0;   // empty equipment slot
// Arduboy2 reserves EEPROM 0..15 for system settings (EEPROM_STORAGE_SPACE_START).
constexpr uint16_t SAVE_EEPROM_ADDR = 16;

// Progression flag bits (SAVE_FLAGS_OFF). Bit 0 is the prg.7 smithy-seen
// marker; bits 1..7 are the crafted-armor bitmask (bead monhun-ardu-arm.2):
// piece i is crafted when bit (SAVE_CRAFTED_BIT_BASE + i) is set. This reuses
// the already-persisted flags byte (no save layout/version change), so an old
// record migrates with no crafted bits -- the v3 equipment slots were never
// written by a shipping build before arm.2's equip UI, so nothing is lost.
constexpr uint8_t SAVE_FLAG_SMITHY_SEEN = 0x01;
constexpr uint8_t SAVE_CRAFTED_BIT_BASE = 1;   // first crafted bit
constexpr uint8_t SAVE_CRAFTED_MAX = 7;        // pieces 0..6 fit the flags byte

struct SaveBlock {
    uint16_t zenny;
    uint8_t quest[SAVE_QUEST_BYTES];
    uint8_t activeQuest;   // quest id or SAVE_QUEST_NONE
    uint8_t progress;      // target-kind kills for the active quest
    uint8_t tier[SAVE_TIER_COUNT];
    uint8_t equip[SAVE_EQUIP_COUNT];   // head/body/charm slot id (0 = none)
    uint8_t flags;                     // SAVE_FLAG_* bits
    uint8_t items[item::ITEM_COUNT];   // inventory counts, cap 255
    uint8_t weapon;                    // WeaponId 0..2 (hml.1 hub loadout)
};

// 1u << n for the crafted-flag bits and the quest bitmap byte bits. The AVR
// shift-by-one loop is replaced by the shared flash LUT (core/bitlut.hpp);
// every index below is masked to 3 bits so the access stays in range.
inline bool saveCrafted(const SaveBlock &s, uint8_t piece) {
    return piece < SAVE_CRAFTED_MAX && (s.flags & mhBit8(static_cast<uint8_t>(SAVE_CRAFTED_BIT_BASE + piece))) != 0;
}
inline void saveSetCrafted(SaveBlock &s, uint8_t piece) {
    if (piece < SAVE_CRAFTED_MAX)
        s.flags |= mhBit8(static_cast<uint8_t>(SAVE_CRAFTED_BIT_BASE + piece));
}

// Sum of the first `count` payload bytes.
inline uint8_t saveChecksumN(const uint8_t *bytes, uint8_t count) {
    uint8_t sum = 0;
    for (uint8_t i = 0; i < count; i++)
        sum = static_cast<uint8_t>(sum + bytes[i]);
    return sum;
}

// Sum of the current (v4) payload bytes (everything before the checksum).
inline uint8_t saveChecksum(const uint8_t *bytes) {
    return saveChecksumN(bytes, SAVE_CHECKSUM_OFF);
}

inline bool saveMagicOk(const uint8_t *in) {
    return in[0] == static_cast<uint8_t>(SAVE_MAGIC & 0xFF) && in[1] == static_cast<uint8_t>(SAVE_MAGIC >> 8);
}

// Pack the save fields into the wire record (little-endian, magic/version/
// checksum derived). `out` must hold SAVE_BYTES.
inline void saveEncode(const SaveBlock &s, uint8_t *out) {
    out[0] = static_cast<uint8_t>(SAVE_MAGIC & 0xFF);
    out[1] = static_cast<uint8_t>(SAVE_MAGIC >> 8);
    out[2] = SAVE_VERSION;
    out[3] = static_cast<uint8_t>(s.zenny & 0xFF);
    out[4] = static_cast<uint8_t>(s.zenny >> 8);
    for (uint8_t i = 0; i < SAVE_QUEST_BYTES; i++)
        out[5 + i] = s.quest[i];
    out[SAVE_ACTIVE_OFF] = s.activeQuest;
    out[SAVE_PROGRESS_OFF] = s.progress;
    for (uint8_t i = 0; i < SAVE_TIER_COUNT; i++)
        out[SAVE_TIER_OFF + i] = s.tier[i];
    for (uint8_t i = 0; i < SAVE_EQUIP_COUNT; i++)
        out[SAVE_EQUIP_OFF + i] = s.equip[i];
    out[SAVE_FLAGS_OFF] = s.flags;
    for (uint8_t i = 0; i < item::ITEM_COUNT; i++)
        out[SAVE_ITEMS_OFF + i] = s.items[i];
    out[SAVE_WEAPON_OFF] = s.weapon;
    out[SAVE_CHECKSUM_OFF] = saveChecksum(out);
}

// Unpack the shared prefix fields (magic already checked; bytes 0..13 through
// tier) into `s`, leaving the equip/flags/items/weapon tail at defaults. The
// caller validates magic + the version's checksum. False leaves `s` unspecified.
inline void saveDecodePrefixFields(const uint8_t *in, SaveBlock &s) {
    s.zenny = static_cast<uint16_t>(in[3] | (static_cast<uint16_t>(in[4]) << 8));
    for (uint8_t i = 0; i < SAVE_QUEST_BYTES; i++)
        s.quest[i] = in[5 + i];
    s.activeQuest = in[SAVE_ACTIVE_OFF];
    s.progress = in[SAVE_PROGRESS_OFF];
    for (uint8_t i = 0; i < SAVE_TIER_COUNT; i++)
        s.tier[i] = in[SAVE_TIER_OFF + i];
    for (uint8_t i = 0; i < SAVE_EQUIP_COUNT; i++)
        s.equip[i] = SAVE_EQUIP_NONE;
    s.flags = 0;
    for (uint8_t i = 0; i < item::ITEM_COUNT; i++)
        s.items[i] = 0;
    s.weapon = 0;
}

// Unpack a legacy v1/v2 prefix record: validates magic + the pre-v4 checksum
// (byte 26) and decodes the shared prefix; the tail and weapon default.
inline bool saveDecodePrefix(const uint8_t *in, SaveBlock &s) {
    if (!saveMagicOk(in))
        return false;
    if (in[SAVE_V3_CHECKSUM_OFF] != saveChecksumN(in, SAVE_V3_CHECKSUM_OFF))
        return false;
    saveDecodePrefixFields(in, s);
    return true;
}

inline void saveDefaults(SaveBlock &s) {
    s.zenny = 0;
    for (uint8_t i = 0; i < SAVE_QUEST_BYTES; i++)
        s.quest[i] = 0;
    s.activeQuest = SAVE_QUEST_NONE;
    s.progress = 0;
    for (uint8_t i = 0; i < SAVE_TIER_COUNT; i++)
        s.tier[i] = 0;
    for (uint8_t i = 0; i < SAVE_EQUIP_COUNT; i++)
        s.equip[i] = SAVE_EQUIP_NONE;
    s.flags = 0;
    for (uint8_t i = 0; i < item::ITEM_COUNT; i++)
        s.items[i] = 0;
    s.weapon = 0;
}

// Unpack the v3 tail (equip/flags/items) after a validated prefix; weapon stays
// 0 (hml.1 appended it, so a v3 record has no weapon byte).
inline bool saveDecodeV3(const uint8_t *in, SaveBlock &s) {
    if (!saveDecodePrefix(in, s))
        return false;
    for (uint8_t i = 0; i < SAVE_EQUIP_COUNT; i++)
        s.equip[i] = in[SAVE_EQUIP_OFF + i];
    s.flags = in[SAVE_FLAGS_OFF];
    for (uint8_t i = 0; i < item::ITEM_COUNT; i++)
        s.items[i] = in[SAVE_ITEMS_OFF + i];
    return true;
}

// Validate + unpack the current (v4) record. False leaves `s` unspecified:
// callers fall back to saveDefaults(). Magic/version/checksum are all checked.
inline bool saveDecode(const uint8_t *in, SaveBlock &s) {
    if (in[2] != SAVE_VERSION)
        return false;
    if (!saveMagicOk(in))
        return false;
    if (in[SAVE_CHECKSUM_OFF] != saveChecksum(in))
        return false;
    saveDecodePrefixFields(in, s);
    for (uint8_t i = 0; i < SAVE_EQUIP_COUNT; i++)
        s.equip[i] = in[SAVE_EQUIP_OFF + i];
    s.flags = in[SAVE_FLAGS_OFF];
    for (uint8_t i = 0; i < item::ITEM_COUNT; i++)
        s.items[i] = in[SAVE_ITEMS_OFF + i];
    s.weapon = in[SAVE_WEAPON_OFF];
    return true;
}

// Quest bits: 2 consecutive bits per quest id (taken, done) inside quest[4].
inline uint8_t saveQuestBit(uint8_t quest, uint8_t which) {
    return static_cast<uint8_t>((quest & 15) * 2 + (which & 1));
}
inline bool saveQuestGet(const SaveBlock &s, uint8_t quest, uint8_t which) {
    const uint8_t bit = saveQuestBit(quest, which);
    return (s.quest[(bit >> 3) & 3] & mhBit8(bit)) != 0;
}
MH_NOINLINE inline void saveQuestSet(SaveBlock &s, uint8_t quest, uint8_t which) {
    const uint8_t bit = saveQuestBit(quest, which);
    s.quest[(bit >> 3) & 3] |= mhBit8(bit);
}
inline void saveQuestClear(SaveBlock &s, uint8_t quest, uint8_t which) {
    const uint8_t bit = saveQuestBit(quest, which);
    s.quest[(bit >> 3) & 3] &= static_cast<uint8_t>(~mhBit8(bit));
}

// Inventory helpers on the save block (host + device, same rules as
// core/items.hpp: id-checked, saturating at 255).
inline uint8_t saveItemCount(const SaveBlock &s, uint8_t id) {
    return id < item::ITEM_COUNT ? s.items[id] : 0;
}
inline void saveItemAdd(SaveBlock &s, uint8_t id, uint8_t n) {
    if (id >= item::ITEM_COUNT)
        return;
    const uint16_t v = static_cast<uint16_t>(s.items[id]) + n;
    s.items[id] = v > 255 ? 255 : static_cast<uint8_t>(v);
}
inline bool saveItemConsume(SaveBlock &s, uint8_t id) {
    if (id >= item::ITEM_COUNT || s.items[id] == 0)
        return false;
    s.items[id]--;
    return true;
}

// Fold the live hunt inventory into the save's counts (hunt-end commit). Each
// slot takes the larger of the two so a hunt that consumed herbs (RAM lower
// than saved) never destroys the persistent stock, while gathered/carved gains
// (RAM higher) persist. Coalesced: called once per hunt, never per item.
inline void saveFoldItems(SaveBlock &s, const uint8_t *live) {
    for (uint8_t i = 0; i < item::ITEM_COUNT; i++) {
        if (live[i] > s.items[i])
            s.items[i] = live[i];
    }
}

using SaveReadFn = uint8_t (*)(uint16_t);
using SaveWriteFn = void (*)(uint16_t, uint8_t);

struct SaveBackend {
    SaveReadFn read;
    SaveWriteFn write;
};

// Load + migrate. Reads the current SAVE_BYTES; a good v4 record decodes, a v3
// record decodes its full tail with weapon = 0, a v2 record decodes its shared
// prefix (prg.5 tail = defaults), a v1 record decodes the bead-cgz prefix, and
// anything else falls back to saveDefaults(). Returns true when a well-formed
// record was loaded (including a migrated older version); false only for
// blank/junk, where `s` holds the defaults.
inline bool saveLoad(SaveBlock &s, const SaveBackend &backend) {
    uint8_t bytes[SAVE_BYTES];
    for (uint8_t i = 0; i < SAVE_BYTES; i++)
        bytes[i] = backend.read(static_cast<uint16_t>(SAVE_EEPROM_ADDR + i));
    if (saveDecode(bytes, s))
        return true;
    // Migration: an older but well-formed record keeps its fields.
    if (bytes[2] == SAVE_VERSION_V3 && saveDecodeV3(bytes, s))
        return true;
    if (bytes[2] == SAVE_VERSION_V2 && saveDecodePrefix(bytes, s))
        return true;
    if (bytes[2] == SAVE_VERSION_V1 && saveDecodePrefix(bytes, s)) {
        s.activeQuest = SAVE_QUEST_NONE;   // v1 had no active-quest model
        s.progress = 0;
        return true;
    }
    saveDefaults(s);
    return false;
}

// Write-on-change + verify read. Returns false when the verify mismatches.
inline bool saveStore(const SaveBlock &s, const SaveBackend &backend) {
    uint8_t bytes[SAVE_BYTES];
    saveEncode(s, bytes);
    for (uint8_t i = 0; i < SAVE_BYTES; i++) {
        const uint16_t addr = static_cast<uint16_t>(SAVE_EEPROM_ADDR + i);
        if (backend.read(addr) != bytes[i])
            backend.write(addr, bytes[i]);
    }
    for (uint8_t i = 0; i < SAVE_BYTES; i++) {
        if (backend.read(static_cast<uint16_t>(SAVE_EEPROM_ADDR + i)) != bytes[i])
            return false;
    }
    return true;
}

#if defined(__AVR__)
#include <EEPROM.h>

inline uint8_t saveEepromRead(uint16_t addr) {
    return EEPROM.read(addr);
}
inline void saveEepromWrite(uint16_t addr, uint8_t value) {
    EEPROM.update(addr, value);
}
#endif

}   // namespace mh
