#pragma once
// Carve the kill (bead monhun-ardu-prg.3): after MS_DEAD the carcass stays where
// the beast fell; a stowed-style A inside its last body box roots the hunter in
// PS_CARVE (~40 ticks), rolls the creature's carve table and adds the yield to
// the inventory. Move/damage cancel the window; three carves per hunt, then the
// carcass is inert.
//
// The over-screen flow guard lives in src/app_state.hpp (appHuntReturnAllowed):
// it reads Game::carveHold so an A press at the carcass never doubles as the
// "return to menu" press. Rolls reuse combatChancePasses (deterministic tick
// hash, no RNG state). Header-only, no float, no Arduino.h.

#include <stdint.h>
#include "game.hpp"
#include "projectiles.hpp"   // addEffect + the combat read layer (monster.hpp)

namespace mh {

constexpr uint8_t CARVE_SPARK_LIFE = 8;   // same spark family as gather/hit paths

// Carcass overlap: the hunter body vs the dead beast's last body box.
// updateMonster returns on MS_DEAD without touching geometry, so m.x/y/w/h
// persist after the kill. Direct integer compares keep this off the Rect path.
static inline bool carveInReach(const Game &g) {
    const Player &p = g.player;
    const Monster &m = g.monster;
    return p.x < static_cast<int16_t>(m.x + m.w) && static_cast<int16_t>(p.x + p.w) > m.x && p.y < static_cast<int16_t>(m.y + m.h) && static_cast<int16_t>(p.y + p.h) > m.y;
}

// Roll one carve against the packed creature table: every authored slot passes
// independently through combatChancePasses (chance 100 always yields, 0 never).
// Empty padding slots carry chance 0, so they never fire. The carve ordinal is
// the hash step, so the same hunt yields the same haul.
static void applyCarve(Game &g) {
    const uint8_t creature = g.combat.creature;
    for (uint8_t slot = 0; slot < CARVE_SLOTS; slot++) {
        const CombatCarve c = combatCarveRead(creature, slot);
        if (!combatChancePasses(static_cast<uint16_t>(g.tick), creature, slot, g.carvesDone, c.chance))
            continue;
        itemAdd(g, c.item, c.count);
    }
    g.carvesDone++;
    addEffect(g, static_cast<int16_t>(g.monster.x + (g.monster.w >> 1)), static_cast<int16_t>(g.monster.y + (g.monster.h >> 1)), CARVE_SPARK_LIFE, false);
}

// One over-screen tick. On a win the hunter may walk to the carcass (stowed
// speed; there is no combat left) and a fresh A inside the carcass rect starts
// the rooted carve. Move input cancels a running carve; damage cancels through
// playerHurt (which drops the state). carveHold mirrors PS_CARVE for the app
// layer's return guard.
static void stepCarve(Game &g, const Input &inp, bool aP) {
    Player &p = g.player;
    g.carveHold = false;
    if (!CARVE_ENABLED || g.over != OVER_WIN)
        return;
    if (p.state == PS_CARVE) {
        if (inp.mx || inp.my) {   // movement intent cancels before the yield
            p.state = PS_IDLE;
            p.t = 0;
            return;
        }
        p.t++;
        if (p.t >= CARVE_TICKS) {
            applyCarve(g);
            p.state = PS_IDLE;
            p.t = 0;
        }
        g.carveHold = (p.state == PS_CARVE);
        return;
    }
    if (p.state != PS_IDLE)
        return;
    if (g.carvesDone >= CARVE_MAX || !aP || !carveInReach(g))
        return;
    p.state = PS_CARVE;
    p.t = 0;
    p.sheathed = true;   // the carve is the stowed interact verb (feel.22 family)
    g.carveHold = true;
}

}   // namespace mh
