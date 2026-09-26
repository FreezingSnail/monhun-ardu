#pragma once
// Room-graph runtime data loader (bead monhun-ardu-fie.4).
//
// Design: docs/map-zones.md. This is the production reader shared by the game,
// the host suites and the device build:
//
//   host  -> reads src/generated/zone_data.hpp structs (identity)
//   AVR   -> reads the packed mhZones blob on the FX cart via mhFxRead* at the
//            zone_meta.hpp offsets (same pattern as core/combat.hpp)
//
// Both backends expose the same typed read functions returning plain value
// structs, so the runtime above the read layer is one code path. The Game only
// caches the active room's scalars (Game::roomW/H, door/heal ranges, monster
// kind); the door/heal rects are read from the blob on demand at the few sites
// that need them (per-tick door overlap, heal press). No per-tick room-record
// read happens in the common case.
//
// All records are little-endian, explicit u8/u16, no padding. No float.

#include <stdint.h>

#include "game.hpp"
#include "../generated/zone_meta.hpp"

#if !defined(__AVR__)
#include "../generated/zone_data.hpp"   // host mirror (identity reads)
#endif

namespace mh {

// ---------------------------------------------------------------- value structs
// Plain mirrors of the packed blob records (field order = packed ABI order).
// Tests compare these against the generated host structs / pin values.
struct ZoneRoom {
    uint16_t w, h;
    uint16_t firstDoor;
    uint8_t doorCount;
    uint16_t firstSpawn;
    uint8_t spawnCount;
    uint16_t firstProp;
    uint8_t propCount;
    uint16_t firstHeal;
    uint8_t healCount;
    // The prg.7 camp-smithy range was cached here; its only consumer (the SMITH
    // screen) is gone and the ui.4 FORGE trees live on the hub, so the runtime
    // no longer reads the smithy rects (monhun-ardu-5co.8). The cart data stays.
    uint8_t monsterKind;    // zone::MONSTER_NONE or MONSTER_KINDS index
    uint8_t monsterSpawn;   // global spawn index (0xFF when none)
};

struct ZoneDoor {
    uint16_t x, y, w, h;
    uint8_t toRoom;    // room index or zone::DOOR_MENU
    uint8_t toSpawn;   // global spawn index in toRoom
};

struct ZoneSpawn {
    uint16_t x, y;
};

struct ZoneHeal {
    uint16_t x, y;
    uint8_t w, h;
};

// Smithy interaction rect (bead monhun-ardu-prg.7): the camp forge a sheathed
// B press opens the smith screen from. One per camp/room record; the runtime
// caches the range and reads rects off the blob on demand (like heal/door).
struct ZoneSmithy {
    uint16_t x, y;
    uint8_t w, h;
};

// Prop record (bead monhun-ardu-fie.5): the render blits the sheet at (x,y).
// `type` is a zone::PROP_* kind, `sheet` an index into the zone::SHEET_* list.
// `gatherItem` is a zone::GATHER_* item (0 == zone::GATHER_NONE, not a gather
// node); `gatherYield` is 1..9 when it is one (bead monhun-ardu-feel.21).
struct ZoneProp {
    uint8_t type;
    uint16_t x, y;
    uint8_t sheet;
    uint8_t frame, w, h;
    uint8_t gatherItem;
    uint8_t gatherYield;
};

// ================================================================ read layer
#if defined(__AVR__)

namespace zdetail {

// Fake cart pointer: the blob lives below 64 KB (generator hard-fails above).
inline uint16_t zoneCartAddr(uint16_t off) {
    return static_cast<uint16_t>(static_cast<uint16_t>(mhZones) + off);
}
inline uint8_t zoneReadU8(uint16_t off) {
    return mhFxReadU8(reinterpret_cast<const uint8_t *>(zoneCartAddr(off)));
}
inline uint16_t zoneReadU16(uint16_t off) {
    return mhFxReadU16(reinterpret_cast<const uint16_t *>(zoneCartAddr(off)));
}

}   // namespace zdetail

inline ZoneRoom zoneRoomRead(uint8_t i) {
    using namespace zdetail;
    const uint16_t b = static_cast<uint16_t>(zone::ROOMS_OFF + i * zone::ROOM_SIZE);
    ZoneRoom v;
    v.w = zoneReadU16(b + zone::ROOM_W_OFF);
    v.h = zoneReadU16(b + zone::ROOM_H_OFF);
    v.firstDoor = zoneReadU16(b + zone::ROOM_FIRST_DOOR_OFF);
    v.doorCount = zoneReadU8(b + zone::ROOM_DOOR_COUNT_OFF);
    v.firstSpawn = zoneReadU16(b + zone::ROOM_FIRST_SPAWN_OFF);
    v.spawnCount = zoneReadU8(b + zone::ROOM_SPAWN_COUNT_OFF);
    v.firstProp = zoneReadU16(b + zone::ROOM_FIRST_PROP_OFF);
    v.propCount = zoneReadU8(b + zone::ROOM_PROP_COUNT_OFF);
    v.firstHeal = zoneReadU16(b + zone::ROOM_FIRST_HEAL_OFF);
    v.healCount = zoneReadU8(b + zone::ROOM_HEAL_COUNT_OFF);
    v.monsterKind = zoneReadU8(b + zone::ROOM_MONSTER_KIND_OFF);
    v.monsterSpawn = zoneReadU8(b + zone::ROOM_MONSTER_SPAWN_OFF);
    return v;
}

inline ZoneDoor zoneDoorRead(uint8_t i) {
    using namespace zdetail;
    const uint16_t b = static_cast<uint16_t>(zone::DOORS_OFF + i * zone::DOOR_SIZE);
    ZoneDoor v;
    v.x = zoneReadU16(b + zone::DOOR_X_OFF);
    v.y = zoneReadU16(b + zone::DOOR_Y_OFF);
    v.w = zoneReadU16(b + zone::DOOR_W_OFF);
    v.h = zoneReadU16(b + zone::DOOR_H_OFF);
    v.toRoom = zoneReadU8(b + zone::DOOR_TO_ROOM_OFF);
    v.toSpawn = zoneReadU8(b + zone::DOOR_TO_SPAWN_OFF);
    return v;
}

inline ZoneSpawn zoneSpawnRead(uint8_t i) {
    using namespace zdetail;
    const uint16_t b = static_cast<uint16_t>(zone::SPAWNS_OFF + i * zone::SPAWN_SIZE);
    ZoneSpawn v;
    v.x = zoneReadU16(b + zone::SPAWN_X_OFF);
    v.y = zoneReadU16(b + zone::SPAWN_Y_OFF);
    return v;
}

inline ZoneHeal zoneHealRead(uint8_t i) {
    using namespace zdetail;
    const uint16_t b = static_cast<uint16_t>(zone::HEALS_OFF + i * zone::HEAL_SIZE);
    ZoneHeal v;
    v.x = zoneReadU16(b + zone::HEAL_X_OFF);
    v.y = zoneReadU16(b + zone::HEAL_Y_OFF);
    v.w = zoneReadU8(b + zone::HEAL_W_OFF);
    v.h = zoneReadU8(b + zone::HEAL_H_OFF);
    return v;
}

inline ZoneSmithy zoneSmithyRead(uint8_t i) {
    using namespace zdetail;
    const uint16_t b = static_cast<uint16_t>(zone::SMITHIES_OFF + i * zone::SMITHY_SIZE);
    ZoneSmithy v;
    v.x = zoneReadU16(b + zone::SMITHY_X_OFF);
    v.y = zoneReadU16(b + zone::SMITHY_Y_OFF);
    v.w = zoneReadU8(b + zone::SMITHY_W_OFF);
    v.h = zoneReadU8(b + zone::SMITHY_H_OFF);
    return v;
}

// Prop records are tiny (9 B) and read together, so fetch the whole record in
// one FX transaction (one seek + 9 byte reads) instead of seven per-field
// mhFxRead* seeks with their own offset math. The unpack below is the packed
// little-endian ABI (zone_meta.hpp PROP_*_OFF); the local buffer replaces the
// per-field cart reads, so the render path does no per-field seeking.
inline ZoneProp zonePropRead(uint8_t i) {
    using namespace zdetail;
    const uint16_t b = static_cast<uint16_t>(zone::PROPS_OFF + i * zone::PROP_SIZE);
    uint8_t r[zone::PROP_SIZE];
    mhFxReadBytes(reinterpret_cast<const uint8_t *>(zoneCartAddr(b)), r, zone::PROP_SIZE);
    ZoneProp v;
    v.type = r[zone::PROP_TYPE_OFF];
    v.x = static_cast<uint16_t>(r[zone::PROP_X_OFF] | (static_cast<uint16_t>(r[zone::PROP_X_OFF + 1]) << 8));
    v.y = static_cast<uint16_t>(r[zone::PROP_Y_OFF] | (static_cast<uint16_t>(r[zone::PROP_Y_OFF + 1]) << 8));
    v.sheet = r[zone::PROP_SHEET_OFF];
    v.frame = r[zone::PROP_FRAME_OFF];
    v.w = r[zone::PROP_W_OFF];
    v.h = r[zone::PROP_H_OFF];
    v.gatherItem = r[zone::PROP_GATHER_ITEM_OFF];
    v.gatherYield = r[zone::PROP_GATHER_YIELD_OFF];
    return v;
}

#else   // ------------------------------------------------------------ host

inline ZoneRoom zoneRoomRead(uint8_t i) {
    const zone_data::Room &r = zone_data::ROOMS[i];
    ZoneRoom v;
    v.w = r.w;
    v.h = r.h;
    v.firstDoor = r.firstDoor;
    v.doorCount = r.doorCount;
    v.firstSpawn = r.firstSpawn;
    v.spawnCount = r.spawnCount;
    v.firstProp = r.firstProp;
    v.propCount = r.propCount;
    v.firstHeal = r.firstHeal;
    v.healCount = r.healCount;
    v.monsterKind = r.monsterKind;
    v.monsterSpawn = r.monsterSpawn;
    return v;
}

inline ZoneDoor zoneDoorRead(uint8_t i) {
    const zone_data::Door &d = zone_data::DOORS[i];
    ZoneDoor v;
    v.x = d.x;
    v.y = d.y;
    v.w = d.w;
    v.h = d.h;
    v.toRoom = d.toRoom;
    v.toSpawn = d.toSpawn;
    return v;
}

inline ZoneSpawn zoneSpawnRead(uint8_t i) {
    const zone_data::Spawn &s = zone_data::SPAWNS[i];
    ZoneSpawn v;
    v.x = s.x;
    v.y = s.y;
    return v;
}

inline ZoneHeal zoneHealRead(uint8_t i) {
    const zone_data::Heal &h = zone_data::HEALS[i];
    ZoneHeal v;
    v.x = h.x;
    v.y = h.y;
    v.w = h.w;
    v.h = h.h;
    return v;
}

inline ZoneSmithy zoneSmithyRead(uint8_t i) {
    const zone_data::Smithy &s = zone_data::SMITHIES[i];
    ZoneSmithy v;
    v.x = s.x;
    v.y = s.y;
    v.w = s.w;
    v.h = s.h;
    return v;
}

inline ZoneProp zonePropRead(uint8_t i) {
    const zone_data::Prop &p = zone_data::PROPS[i];
    ZoneProp v;
    v.type = p.type;
    v.x = p.x;
    v.y = p.y;
    v.sheet = p.sheet;
    v.frame = p.frame;
    v.w = p.w;
    v.h = p.h;
    v.gatherItem = p.gatherItem;
    v.gatherYield = p.gatherYield;
    return v;
}

#endif   // __AVR__

// ---------------------------------------------------------- beast home (udb)
// Per-room beast home (bead monhun-ardu-udb): the beast kind -- a MonsterKind
// value; lunge/sweep/heavy/ravager map 1:1 onto zone::MONSTER_* -- indexes the
// generated home table (heavy -> ridge, lunge -> area; roster kinds with no
// matching room fall back to the first monster room, today's single-arena
// behaviour; non-roster kinds like pole return 0xFF and keep their creature
// spawn).
// Rooms with MONSTER_NONE (camp, cavern) are never homes. 0xFF = no home, so
// the caller can skip the override. The table is baked by gen-zones, so no
// per-room cart scan runs at hunt start.
//
// Invariant: the beast's coordinates live in its home room's space and never
// migrate or simulate per room. While the hunter is in another room the normal
// distance gate keeps it idle (dormant/off-room), exactly as the single-arena
// runtime did -- this bead adds no per-room monster simulation.
inline uint8_t beastHomeRoom(int8_t kind) {
    const uint8_t k = static_cast<uint8_t>(kind);
    return k < zone::MONSTER_KIND_COUNT ? zone::MONSTER_HOME_ROOM[k] : 0xFF;
}

// Move a just-spawned beast (initMonster ran) to its home room's zone spawn
// coords. hp/spd/FSM/body stay the creature record's; only x/y move. Called by
// huntStart before loadRoom so the arrival room's target rect reads the home
// position. A home room with no monsterSpawn (0xFF) leaves the creature spawn.
inline void beastHomeSpawn(Game &g, int8_t kind) {
    const uint8_t home = beastHomeRoom(kind);
    if (home >= zone::ROOMS_COUNT)
        return;
    // The room record's monsterSpawn is validated by gen-zones (it must name a
    // spawn in the same room), so no runtime bounds check is needed here.
#if defined(__AVR__)
    const uint8_t spawn = zdetail::zoneReadU8(static_cast<uint16_t>(zone::ROOMS_OFF + home * zone::ROOM_SIZE + zone::ROOM_MONSTER_SPAWN_OFF));
#else
    const uint8_t spawn = zone_data::ROOMS[home].monsterSpawn;
#endif
    const ZoneSpawn sp = zoneSpawnRead(spawn);
    g.monster.x = static_cast<int16_t>(sp.x);
    g.monster.y = static_cast<int16_t>(sp.y);
}

// Safe room: the room record carries no monster, so stepWorldBody skips the
// monster/target updates while its door/heal/player logic stays live. Carved
// out of the parity image with the rest of the room runtime (every pre-room
// hunt is a live hunt).
inline bool roomIsSafe(const Game &g) {
    return ROOM_BOUNDS_ENABLED && g.roomMonsterKind == zone::MONSTER_NONE;
}

}   // namespace mh
