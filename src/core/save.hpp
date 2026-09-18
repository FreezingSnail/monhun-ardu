#pragma once
// Persistent save block (bead monhun-ardu-cgz, docs/quests-shops.md).
//
// 13-byte packed little-endian record in EEPROM:
//
//   0..1  magic  u16 0x484D ("MH")
//   2     version u8
//   3..4  zenny  u16
//   5..8  quest  u8[4]  (16 quests x 2 bits: taken, done)
//   9..11 tier   u8[3]  (per weapon upgrade tier)
//   12    checksum u8 (sum of bytes 0..11)
//
// Load runs once on boot; anything but a good magic + version + checksum falls
// back to defaults. saveStore() only writes bytes that differ and verifies the
// read-back, so a power cut cannot leave a half-written record. Nothing here is
// called during a hunt: only screen actions commit (write-cycle hygiene).
//
// Host-testable: the logic takes a SaveBackend of three-address read/write
// functions rather than touching Arduino.h. On AVR save.hpp also provides the
// EEPROM-backed functions (EEPROM.update == write-if-different).

#include <stdint.h>

namespace mh {

constexpr uint16_t SAVE_MAGIC = 0x484D;   // 'M','H' little-endian
constexpr uint8_t SAVE_VERSION = 1;
constexpr uint8_t SAVE_QUEST_BYTES = 4;
constexpr uint8_t SAVE_TIER_COUNT = 3;   // N_WEAPONS, matches screens::TIER_COUNT
constexpr uint8_t SAVE_CHECKSUM_OFF = 12;
constexpr uint8_t SAVE_BYTES = 13;
// Arduboy2 reserves EEPROM 0..15 for system settings (EEPROM_STORAGE_SPACE_START).
constexpr uint16_t SAVE_EEPROM_ADDR = 16;

struct SaveBlock {
    uint16_t zenny;
    uint8_t quest[SAVE_QUEST_BYTES];
    uint8_t tier[SAVE_TIER_COUNT];
};

inline uint8_t saveChecksum(const uint8_t *bytes) {
    uint8_t sum = 0;
    for (uint8_t i = 0; i < SAVE_CHECKSUM_OFF; i++)
        sum = static_cast<uint8_t>(sum + bytes[i]);
    return sum;
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
    for (uint8_t i = 0; i < SAVE_TIER_COUNT; i++)
        out[9 + i] = s.tier[i];
    out[SAVE_CHECKSUM_OFF] = saveChecksum(out);
}

// Validate + unpack. False leaves `s` unspecified: callers fall back to
// saveDefaults(). Magic/version/checksum are all checked.
inline bool saveDecode(const uint8_t *in, SaveBlock &s) {
    if (in[0] != static_cast<uint8_t>(SAVE_MAGIC & 0xFF) || in[1] != static_cast<uint8_t>(SAVE_MAGIC >> 8))
        return false;
    if (in[2] != SAVE_VERSION)
        return false;
    if (in[SAVE_CHECKSUM_OFF] != saveChecksum(in))
        return false;
    s.zenny = static_cast<uint16_t>(in[3] | (static_cast<uint16_t>(in[4]) << 8));
    for (uint8_t i = 0; i < SAVE_QUEST_BYTES; i++)
        s.quest[i] = in[5 + i];
    for (uint8_t i = 0; i < SAVE_TIER_COUNT; i++)
        s.tier[i] = in[9 + i];
    return true;
}

inline void saveDefaults(SaveBlock &s) {
    s.zenny = 0;
    for (uint8_t i = 0; i < SAVE_QUEST_BYTES; i++)
        s.quest[i] = 0;
    for (uint8_t i = 0; i < SAVE_TIER_COUNT; i++)
        s.tier[i] = 0;
}

// Quest bits: 2 consecutive bits per quest id (taken, done) inside quest[4].
inline uint8_t saveQuestBit(uint8_t quest, uint8_t which) {
    return static_cast<uint8_t>((quest & 15) * 2 + (which & 1));
}
inline bool saveQuestGet(const SaveBlock &s, uint8_t quest, uint8_t which) {
    const uint8_t bit = saveQuestBit(quest, which);
    return (s.quest[(bit >> 3) & 3] & static_cast<uint8_t>(1u << (bit & 7))) != 0;
}
inline void saveQuestSet(SaveBlock &s, uint8_t quest, uint8_t which) {
    const uint8_t bit = saveQuestBit(quest, which);
    s.quest[(bit >> 3) & 3] |= static_cast<uint8_t>(1u << (bit & 7));
}
inline void saveQuestClear(SaveBlock &s, uint8_t quest, uint8_t which) {
    const uint8_t bit = saveQuestBit(quest, which);
    s.quest[(bit >> 3) & 3] &= static_cast<uint8_t>(~(1u << (bit & 7)));
}

using SaveReadFn = uint8_t (*)(uint16_t);
using SaveWriteFn = void (*)(uint16_t, uint8_t);

struct SaveBackend {
    SaveReadFn read;
    SaveWriteFn write;
};

inline bool saveLoad(SaveBlock &s, const SaveBackend &backend) {
    uint8_t bytes[SAVE_BYTES];
    for (uint8_t i = 0; i < SAVE_BYTES; i++)
        bytes[i] = backend.read(static_cast<uint16_t>(SAVE_EEPROM_ADDR + i));
    if (!saveDecode(bytes, s)) {
        saveDefaults(s);
        return false;
    }
    return true;
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
