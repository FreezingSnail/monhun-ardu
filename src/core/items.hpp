#pragma once
// Inventory + item table + gather nodes + herb use (bead monhun-ardu-feel.22;
// item table prg.2).
//
// Design: docs/feel-design.md (data verbs + input line) and docs/map-zones.md
// (the gather fields on a prop record). The room props carry the node geometry
// (bead feel.21); this header owns the player-facing verbs:
//
//   sheathed + A inside an un-picked node -> PS_GATHER (rooted, depletes)
//   sheathed + B hold                    -> PS_ITEM  (rooted, heals hp)
//
// Both states apply their effect atomically on completion, so a move/damage
// cancel never leaves the inventory or the node half-updated. Node depletion is
// a Game bitmask reset only by newGame (a node stays picked across a room
// round-trip). Header-only, no float, no Arduino.h.
//
// The item table (data/items.json -> mhItems, prg.2) is read through the same
// host/AVR shim as core/zones.hpp: host structs (items_data.hpp) or the packed
// blob via mhFxRead* (items_meta.hpp offsets). Game::items[] is indexed by the
// generated item ids; the generic add/consume/count helpers are shared by the
// gather path and the future drop/smith beads.
//
// The node/prop readers come from core/zones.hpp (host structs / cart blob);
// addEffect is declared here (defined in projectiles.hpp) because player.hpp
// includes this header before projectiles.hpp exists.

#include <stdint.h>
#include "game.hpp"
#include "zones.hpp"
#include "../generated/quest_meta.hpp"   // quests::GOAL_GATHER (dlp.2 gather accounting)

#if !defined(__AVR__)
#include "../generated/items_data.hpp"   // host mirror (identity reads)
#endif

namespace mh {

// Defined in projectiles.hpp (included after player.hpp). Declared here so the
// gather completion can spawn the shared spark effect.
static void addEffect(Game &g, int16_t x, int16_t y, uint8_t life, bool crit);

// Node-depletion mask is a u32 (one bit per global prop record). The demo map
// has 23 props (the cavern room pushed prg.4's 12 + camp's non-gather props past
// 16, so the mask widened; the ridge arena pushed it to 23); a map that
// overflows this fires the assert instead of silently sharing bits.
static_assert(zone::PROPS_COUNT <= 32, "gatherMask is a u32: one bit per prop record");

constexpr uint8_t GATHER_SPARK_LIFE = 8;   // same spark family as the heal/hit paths

// ------------------------------------------------------------- item table
// Plain mirror of the packed mhItems record (field order = packed ABI order).
struct ItemInfo {
    uint8_t kind;   // item::KIND_CONSUMABLE / KIND_MATERIAL
    uint8_t heal;
    uint8_t stam;
    uint16_t sell;
};

#if defined(__AVR__)

namespace idetail {
// Fake cart pointer: the blob lives below 64 KB (generator hard-fails above).
inline uint16_t itemCartAddr(uint16_t off) {
    return static_cast<uint16_t>(static_cast<uint16_t>(mhItems) + off);
}
}   // namespace idetail

inline ItemInfo itemRead(uint8_t id) {
    using namespace idetail;
    const uint16_t b = static_cast<uint16_t>(item::ITEMS_OFF + id * item::ITEM_SIZE);
    ItemInfo v;
    v.kind = mhFxReadU8(reinterpret_cast<const uint8_t *>(itemCartAddr(b + item::ITEM_KIND_OFF)));
    v.heal = mhFxReadU8(reinterpret_cast<const uint8_t *>(itemCartAddr(b + item::ITEM_HEAL_OFF)));
    v.stam = mhFxReadU8(reinterpret_cast<const uint8_t *>(itemCartAddr(b + item::ITEM_STAM_OFF)));
    v.sell = mhFxReadU16(reinterpret_cast<const uint16_t *>(itemCartAddr(b + item::ITEM_SELL_OFF)));
    return v;
}

#else   // ------------------------------------------------------------ host

inline ItemInfo itemRead(uint8_t id) {
    const item_data::Item &r = item_data::ITEMS[id];
    ItemInfo v;
    v.kind = r.kind;
    v.heal = r.heal;
    v.stam = r.stam;
    v.sell = r.sell;
    return v;
}

#endif   // __AVR__

// Typed field readers; an out-of-range id reads inert (0), so a bad gather
// code or a stale slot cannot walk off the table.
inline uint8_t itemKind(uint8_t id) {
    return id < ITEM_COUNT ? itemRead(id).kind : 0;
}
inline uint8_t itemHeal(uint8_t id) {
    return id < ITEM_COUNT ? itemRead(id).heal : 0;
}
inline uint16_t itemSell(uint8_t id) {
    return id < ITEM_COUNT ? itemRead(id).sell : 0;
}

// ---------------------------------------------------------- inventory verbs
// Generic add/consume/count helpers (shared by gather + the drop/smith beads).
inline uint8_t itemCount(const Game &g, uint8_t id) {
    return id < ITEM_COUNT ? g.items[id] : 0;
}
inline void itemAdd(Game &g, uint8_t id, uint8_t n) {
    if (id >= ITEM_COUNT)
        return;
    const uint16_t v = static_cast<uint16_t>(g.items[id]) + n;
    g.items[id] = v > 255 ? 255 : static_cast<uint8_t>(v);
}
inline bool itemConsume(Game &g, uint8_t id) {
    if (id >= ITEM_COUNT || g.items[id] == 0)
        return false;
    g.items[id]--;
    return true;
}

// Was this prop record picked already this hunt? The mask is one bit per
// global prop record, byte-addressed: a 32-bit `1 << idx` would pull in AVR's
// 32-bit shift helper for every site, while the byte form only shifts a
// constant 1 within a byte (measured trim; the mask is little-endian on AVR).
static inline bool gatherNodeDepleted(const Game &g, uint8_t idx) {
    const uint8_t *m = reinterpret_cast<const uint8_t *>(&g.gatherMask);
    return (m[static_cast<uint8_t>(idx >> 3)] & static_cast<uint8_t>(1u << (idx & 7))) != 0;
}

static inline void gatherMarkPicked(Game &g, uint8_t idx) {
    uint8_t *m = reinterpret_cast<uint8_t *>(&g.gatherMask);
    m[static_cast<uint8_t>(idx >> 3)] |= static_cast<uint8_t>(1u << (idx & 7));
}

// Player body as a world rect (world.hpp's bodyRect is defined after
// player.hpp; this keeps the gather helper self-contained).
static inline Rect itemBodyRect(const Player &p) {
    Rect r;
    r.x = p.x;
    r.y = p.y;
    r.w = p.w;
    r.h = p.h;
    return r;
}

// First un-picked gather node overlapping `body`, or -1. Reads only the active
// room's prop range; folds to -1 when the room runtime is carved out.
static int16_t gatherNodeAt(const Game &g, const Rect &body) {
    if (!ROOM_BOUNDS_ENABLED)
        return -1;
    for (uint8_t i = 0; i < g.roomPropCount; i++) {
        const uint8_t idx = static_cast<uint8_t>(g.roomFirstProp + i);
        if (gatherNodeDepleted(g, idx))
            continue;
        const ZoneProp p = zonePropRead(idx);
        if (p.gatherItem == zone::GATHER_NONE)
            continue;
        Rect nr;
        nr.x = static_cast<int16_t>(p.x);
        nr.y = static_cast<int16_t>(p.y);
        nr.w = p.w;
        nr.h = p.h;
        if (body.overlaps(nr))
            return static_cast<int16_t>(idx);
    }
    return -1;
}

// Sheathed A: bind the node under the hunter and enter PS_GATHER. False when
// there is no node (the caller draws the weapon instead).
static bool tryStartGather(Game &g, Player &p) {
    if (p.state != PS_IDLE)
        return false;
    const int16_t node = gatherNodeAt(g, itemBodyRect(p));
    if (node < 0)
        return false;
    p.state = PS_GATHER;
    p.t = 0;
    p.itemNode = static_cast<uint8_t>(node);
    return true;
}

// PS_GATHER completion: deplete the bound node and add its yield to the
// matching inventory slot, then spark. A re-check keeps a stale bind inert.
static void applyGather(Game &g, Player &p) {
    const uint8_t idx = p.itemNode;
    if (idx == ITEM_NODE_NONE || idx >= zone::PROPS_COUNT)
        return;
    if (gatherNodeDepleted(g, idx))
        return;
    const ZoneProp prop = zonePropRead(idx);
    if (prop.gatherItem == zone::GATHER_NONE)
        return;
    gatherMarkPicked(g, idx);
    // The record stores the item index + 1 (zone::GATHER_*), so the inventory
    // slot is one less. itemAdd ignores an id past the table.
    const uint8_t slot = static_cast<uint8_t>(prop.gatherItem - 1);
    itemAdd(g, slot, prop.gatherYield);
    // Quest gather accounting (bead monhun-ardu-dlp.2): a GOAL_GATHER quest
    // counts the yielded item units only when the node's item matches its
    // target — an off-item node still gathers but does not advance progress.
    // Carves go through carve.hpp, never this path, so they never count.
    if (g.questGoalKind == quests::GOAL_GATHER && g.questTarget == static_cast<int8_t>(slot)) {
        const uint16_t p = static_cast<uint16_t>(g.questProgress) + prop.gatherYield;
        g.questProgress = p > 255 ? 255 : static_cast<uint8_t>(p);
    }
    addEffect(g, static_cast<int16_t>(p.x + (p.w >> 1)), static_cast<int16_t>(p.y + (p.h >> 1)), GATHER_SPARK_LIFE, false);
}

// Sheathed B hold: enter PS_ITEM when a herb is held. False = nothing (the
// caller leaves the state alone, matching the pre-feature sheathed no-op).
static bool startItemUse(Game &g, Player &p) {
    if (p.state != PS_IDLE)
        return false;
    if (g.items[ITEM_HERB] == 0)
        return false;
    p.state = PS_ITEM;
    p.t = 0;
    return true;
}

// PS_ITEM completion: consume one herb and heal the item table's heal value
// (clamped at hpMax). Herb heal is authored in data/items.json (20), so a data
// edit retunes the eat without a code change.
static void applyItemUse(Game &g, Player &p) {
    if (!itemConsume(g, ITEM_HERB))
        return;
    const uint16_t nh = static_cast<uint16_t>(p.hp) + itemHeal(ITEM_HERB);
    p.hp = nh > p.hpMax ? p.hpMax : static_cast<uint8_t>(nh);
}

}   // namespace mh
