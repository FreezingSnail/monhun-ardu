#pragma once
// Inventory + gather nodes + herb use (bead monhun-ardu-feel.22).
//
// Design: docs/feel-design.md (data verbs + input line) and docs/map-zones.md
// (the gather fields on a prop record). The room props carry the node geometry
// (bead feel.21); this header owns the player-facing verbs:
//
//   sheathed + A inside an un-picked node -> PS_GATHER (rooted, depletes)
//   sheathed + B hold                    -> PS_ITEM  (rooted, heals 20 hp)
//
// Both states apply their effect atomically on completion, so a move/damage
// cancel never leaves the inventory or the node half-updated. Node depletion is
// a Game bitmask reset only by newGame (a node stays picked across a room
// round-trip). Header-only, no float, no Arduino.h.
//
// The node/prop readers come from core/zones.hpp (host structs / cart blob);
// addEffect is declared here (defined in projectiles.hpp) because player.hpp
// includes this header before projectiles.hpp exists.

#include <stdint.h>
#include "game.hpp"
#include "zones.hpp"

namespace mh {

// Defined in projectiles.hpp (included after player.hpp). Declared here so the
// gather completion can spawn the shared spark effect.
static void addEffect(Game &g, int16_t x, int16_t y, uint8_t life, bool crit, int16_t text);

// Node-depletion mask is a u16 (one bit per global prop record). The demo map
// has 7 props; a future map that overflows this fires the assert instead of
// silently sharing bits.
static_assert(zone::PROPS_COUNT <= 16, "gatherMask is a u16: one bit per prop record");

constexpr uint8_t GATHER_SPARK_LIFE = 8;   // same spark family as the heal/hit paths

// Was this prop record picked already this hunt?
static inline bool gatherNodeDepleted(const Game &g, uint8_t idx) {
    return (g.gatherMask & static_cast<uint16_t>(1u << idx)) != 0;
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
    g.gatherMask |= static_cast<uint16_t>(1u << idx);
    // The record stores the item index + 1, so the inventory slot is one less
    // (herb == 0). Unknown/absent items have no slot and add nothing.
    const uint8_t slot = static_cast<uint8_t>(prop.gatherItem - 1);
    if (slot < ITEM_COUNT) {
        const uint16_t n = static_cast<uint16_t>(g.items[slot]) + prop.gatherYield;
        g.items[slot] = n > 255 ? 255 : static_cast<uint8_t>(n);
    }
    addEffect(g, static_cast<int16_t>(p.x + (p.w >> 1)), static_cast<int16_t>(p.y + (p.h >> 1)), GATHER_SPARK_LIFE, false, 0);
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

// PS_ITEM completion: heal exactly HERB_HEAL (clamped at hpMax), one herb.
static void applyItemUse(Game &g, Player &p) {
    if (g.items[ITEM_HERB] == 0)
        return;
    g.items[ITEM_HERB]--;
    const uint16_t nh = static_cast<uint16_t>(p.hp) + HERB_HEAL;
    p.hp = nh > p.hpMax ? p.hpMax : static_cast<uint8_t>(nh);
}

}   // namespace mh
