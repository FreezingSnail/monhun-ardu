#pragma once
// Persistent save block (bead monhun-ardu-cgz, docs/quests-shops.md; v2 layout
// bead monhun-ardu-prg.5; v4 weapon bead monhun-ardu-isp.1; v5 forge trees bead
// monhun-ardu-5co.4, docs/ui-design.md; migration removal bead monhun-ardu-5co.7).
//
// Packed little-endian record in EEPROM (version 5). The v5 tail replaces the
// per-class smith tier bytes + class-index weapon byte with the forge-tree
// ownership/equip model:
//
//   0..1   magic  u16 0x484D ("MH")
//   2      version u8 (5)
//   3..4   zenny  u16
//   5..8   quest  u8[4]  (16 quests x 2 bits: taken, done)
//   9      activeQuest u8 (0xFF = none)
//   10     progress    u8 (target-kind kills for the active quest)
//   11..13 reserved u8[3] (zero; formerly the v4 smith tier bytes)
//   14..16 equip u8[3] (head/body/charm slot id; 0 = none)
//   17     flags  u8 (reserved progression bits, e.g. smithy seen)
//   18..25 items  u8[ITEM_COUNT] (inventory counts, cap 255)
//   26     equippedNode u8 (forge node id, 0xFF = none; replaces the class byte)
//   27..30 weaponOwned u8[4] (32 node slots, bit n = node n owned)
//   31     armorCrafted u8[1] (8 piece slots, bit n = piece n crafted)
//   32     checksum u8 (sum of bytes 0..31)
//
// Load runs once on boot; only a good magic + version 5 + checksum decodes.
// Anything else (blank, junk, or any older version) falls back to
// saveDefaults(): pre-release, an old save is discarded on a version change.
// saveStore() only writes bytes that differ and verifies the read-back, so a
// power cut cannot leave a half-written record. Nothing here is called during a
// hunt: only screen actions and the hunt-end progress commit write (write-cycle
// hygiene).
//
// Host-testable: the logic takes a SaveBackend of three-address read/write
// functions rather than touching Arduino.h. On AVR save.hpp also provides the
// EEPROM-backed functions (EEPROM.update == write-if-different).
//
// Dev feel mode (hbk.1, core/dev.hpp): with DEV_UNLIMITED the load always
// returns fresh defaults and the store never writes, so a `make dev` session
// cannot touch the player's real save.

#include <stdint.h>
#include "dev.hpp"                       // DEV_UNLIMITED (MH_DEV; hbk.1 feel mode)
#include "progmem.hpp"                   // MH_NOINLINE
#include "bitlut.hpp"                    // mhBit8 (flash one-hot LUT)
#include "../generated/items_meta.hpp"   // item::ITEM_COUNT (inventory slot count)
#include "../generated/forge_meta.hpp"   // forge::NODE_DEPTH (default roots)

namespace mh {

constexpr uint16_t SAVE_MAGIC = 0x484D;   // 'M','H' little-endian
constexpr uint8_t SAVE_VERSION = 5;
constexpr uint8_t SAVE_QUEST_BYTES = 4;
constexpr uint8_t SAVE_ACTIVE_OFF = 9;   // activeQuest u8 (0xFF = none)
constexpr uint8_t SAVE_PROGRESS_OFF = 10;
constexpr uint8_t SAVE_EQUIP_OFF = 14;   // u8[3]: head, body, charm
constexpr uint8_t SAVE_EQUIP_COUNT = 3;
constexpr uint8_t SAVE_FLAGS_OFF = 17;                                                               // reserved progression bits
constexpr uint8_t SAVE_ITEMS_OFF = 18;                                                               // u8[item::ITEM_COUNT]
constexpr uint8_t SAVE_EQUIPPED_OFF = static_cast<uint8_t>(SAVE_ITEMS_OFF + item::ITEM_COUNT);       // 26
constexpr uint8_t SAVE_OWNED_OFF = static_cast<uint8_t>(SAVE_EQUIPPED_OFF + 1);                      // 27
constexpr uint8_t SAVE_OWNED_BYTES = 4;                                                              // 32 node slots (forge::NODE_COUNT <= 32)
constexpr uint8_t SAVE_CRAFTED_OFF = static_cast<uint8_t>(SAVE_OWNED_OFF + SAVE_OWNED_BYTES);        // 31
constexpr uint8_t SAVE_CRAFTED_BYTES = 1;                                                            // 8 piece slots (armor::PIECE_COUNT <= 8)
constexpr uint8_t SAVE_CHECKSUM_OFF = static_cast<uint8_t>(SAVE_CRAFTED_OFF + SAVE_CRAFTED_BYTES);   // 32
constexpr uint8_t SAVE_BYTES = static_cast<uint8_t>(SAVE_CHECKSUM_OFF + 1);                          // 33
// Byte offset of `equip` inside the in-RAM SaveBlock image. The wire record
// adds the 3 header bytes (magic u16 + version) and the 3 reserved bytes before
// it, so SAVE_EQUIP_OFF == SAVE_WIRE_HEADER + SAVE_BLOCK_EQUIP_OFF + 3.
constexpr uint8_t SAVE_WIRE_HEADER = 3;
constexpr uint8_t SAVE_BLOCK_EQUIP_OFF = static_cast<uint8_t>(2 + SAVE_QUEST_BYTES + 2);   // 8
constexpr uint8_t SAVE_QUEST_NONE = 0xFF;
constexpr uint8_t SAVE_EQUIP_NONE = 0;     // empty equipment slot
constexpr uint8_t SAVE_NODE_NONE = 0xFF;   // no equipped weapon node
// Arduboy2 reserves EEPROM 0..15 for system settings (EEPROM_STORAGE_SPACE_START).
constexpr uint16_t SAVE_EEPROM_ADDR = 16;

// Progression flag bits (SAVE_FLAGS_OFF). Bit 0 is the prg.7 smithy-seen marker.
constexpr uint8_t SAVE_FLAG_SMITHY_SEEN = 0x01;
constexpr uint8_t SAVE_OWNED_SLOTS = static_cast<uint8_t>(SAVE_OWNED_BYTES * 8);       // 32
constexpr uint8_t SAVE_CRAFTED_SLOTS = static_cast<uint8_t>(SAVE_CRAFTED_BYTES * 8);   // 8

struct SaveBlock {
    uint16_t zenny;
    uint8_t quest[SAVE_QUEST_BYTES];
    uint8_t activeQuest;                     // quest id or SAVE_QUEST_NONE
    uint8_t progress;                        // target-kind kills for the active quest
    uint8_t equip[SAVE_EQUIP_COUNT];         // head/body/charm slot id (0 = none)
    uint8_t flags;                           // SAVE_FLAG_* bits
    uint8_t items[item::ITEM_COUNT];         // inventory counts, cap 255
    uint8_t equippedNode;                    // forge node id or SAVE_NODE_NONE
    uint8_t weaponOwned[SAVE_OWNED_BYTES];   // bit n = forge node n owned
    uint8_t crafted[SAVE_CRAFTED_BYTES];     // bit n = armor piece n crafted
};

// The in-RAM block is the wire payload with the 3 reserved bytes elided between
// `progress` and `equip`: encode/decode copy the two byte-images around that
// gap (monhun-ardu-5co.8), so the two layouts must stay in lockstep. All members
// are byte-aligned and little-endian on every supported target (AVR + host).
static_assert(sizeof(SaveBlock) == SAVE_BLOCK_EQUIP_OFF + (SAVE_BYTES - SAVE_EQUIP_OFF - 1), "SaveBlock must be the wire image minus header/reserved/checksum");
static_assert(SAVE_EQUIP_OFF == SAVE_WIRE_HEADER + SAVE_BLOCK_EQUIP_OFF + 3, "wire = 3 header + saveblock header + 3 reserved before equip");

// 1u << n via the shared flash LUT (core/bitlut.hpp); n is masked to 3 bits by
// the LUT. Each bitset is sized to its data (see the *_SLOTS caps), so the slot
// bound is checked per set and no out-of-range slot can read past the array.
inline bool saveWeaponOwned(const SaveBlock &s, uint8_t node) {
    return node < SAVE_OWNED_SLOTS && (s.weaponOwned[node >> 3] & mhBit8(node)) != 0;
}
inline void saveSetWeaponOwned(SaveBlock &s, uint8_t node) {
    if (node < SAVE_OWNED_SLOTS)
        s.weaponOwned[node >> 3] |= mhBit8(node);
}

inline bool saveCrafted(const SaveBlock &s, uint8_t piece) {
    return piece < SAVE_CRAFTED_SLOTS && (s.crafted[0] & mhBit8(piece)) != 0;
}
inline void saveSetCrafted(SaveBlock &s, uint8_t piece) {
    if (piece < SAVE_CRAFTED_SLOTS)
        s.crafted[0] |= mhBit8(piece);
}

// Sum of the current (v5) payload bytes (everything before the checksum).
inline uint8_t saveChecksum(const uint8_t *bytes) {
    uint8_t sum = 0;
    for (uint8_t i = 0; i < SAVE_CHECKSUM_OFF; i++)
        sum = static_cast<uint8_t>(sum + bytes[i]);
    return sum;
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
    // The wire payload is the SaveBlock byte image around a 3-byte reserved gap:
    // copy the fields before `equip` straight after the header, zero the gap,
    // then copy the rest. Byte copies beat the old per-field stores by ~90 B.
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(&s);
    __builtin_memcpy(out + SAVE_WIRE_HEADER, raw, SAVE_BLOCK_EQUIP_OFF);
    out[SAVE_EQUIP_OFF - 3] = 0;
    out[SAVE_EQUIP_OFF - 2] = 0;
    out[SAVE_EQUIP_OFF - 1] = 0;
    __builtin_memcpy(out + SAVE_EQUIP_OFF, raw + SAVE_BLOCK_EQUIP_OFF, sizeof(SaveBlock) - SAVE_BLOCK_EQUIP_OFF);
    out[SAVE_CHECKSUM_OFF] = saveChecksum(out);
}

// A fresh save owns every class root and equips the sword root: the roots are
// the depth-0 nodes of the generated forge tree (linear class spines), and
// NODE_SWORD_BASE is the first.
inline void saveDefaults(SaveBlock &s) {
    s.zenny = DEV_UNLIMITED ? 9999 : 0;
    for (uint8_t i = 0; i < SAVE_QUEST_BYTES; i++)
        s.quest[i] = 0;
    s.activeQuest = SAVE_QUEST_NONE;
    s.progress = 0;
    for (uint8_t i = 0; i < SAVE_EQUIP_COUNT; i++)
        s.equip[i] = SAVE_EQUIP_NONE;
    s.flags = 0;
    for (uint8_t i = 0; i < item::ITEM_COUNT; i++)
        s.items[i] = DEV_UNLIMITED ? 99 : 0;
    s.equippedNode = forge::NODE_SWORD_BASE;
    for (uint8_t i = 0; i < SAVE_OWNED_BYTES; i++)
        s.weaponOwned[i] = 0;
    for (uint8_t i = 0; i < SAVE_CRAFTED_BYTES; i++)
        s.crafted[i] = 0;
    for (uint8_t i = 0; i < forge::NODE_COUNT; i++) {
        if (forge::NODE_DEPTH[i] == 0)
            saveSetWeaponOwned(s, i);
    }
}

// Validate + unpack the current (v5) record. False leaves `s` unspecified:
// callers fall back to saveDefaults(). Magic/version/checksum are all checked.
inline bool saveDecode(const uint8_t *in, SaveBlock &s) {
    if (in[2] != SAVE_VERSION)
        return false;
    if (!saveMagicOk(in))
        return false;
    if (in[SAVE_CHECKSUM_OFF] != saveChecksum(in))
        return false;
    // Mirror of saveEncode: two byte copies around the reserved gap.
    uint8_t *raw = reinterpret_cast<uint8_t *>(&s);
    __builtin_memcpy(raw, in + SAVE_WIRE_HEADER, SAVE_BLOCK_EQUIP_OFF);
    __builtin_memcpy(raw + SAVE_BLOCK_EQUIP_OFF, in + SAVE_EQUIP_OFF, sizeof(SaveBlock) - SAVE_BLOCK_EQUIP_OFF);
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

// Load. Reads the current SAVE_BYTES; a good v5 record decodes, anything else
// (blank, junk, or an older version) falls back to saveDefaults(). Returns true
// when a well-formed record was loaded; false for blank/junk, where `s` holds
// the defaults.
inline bool saveLoad(SaveBlock &s, const SaveBackend &backend) {
#if MH_DEV
    // Dev feel mode (hbk.1): the sandbox never reads the EEPROM, so the whole
    // load path is compiled out (dev builds only; shipping is identical).
    // NB: the macro, not the constexpr -- #if cannot fold DEV_UNLIMITED.
    (void)backend;
    saveDefaults(s);
    return false;
#else
    uint8_t bytes[SAVE_BYTES];
    for (uint8_t i = 0; i < SAVE_BYTES; i++)
        bytes[i] = backend.read(static_cast<uint16_t>(SAVE_EEPROM_ADDR + i));
    if (saveDecode(bytes, s))
        return true;
    saveDefaults(s);
    return false;
#endif
}

// Write-on-change + verify read. Returns false when the verify mismatches.
inline bool saveStore(const SaveBlock &s, const SaveBackend &backend) {
#if MH_DEV
    (void)s;
    (void)backend;
    return true;   // dev feel mode: never write the EEPROM
#else
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
#endif
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
