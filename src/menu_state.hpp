#pragma once
// Opening-menu state machine (bead monhun-ardu-6zb.2). Host-testable, no
// Arduino.h: the device loop samples one mh::Input per tick and feeds it here
// while the menu is up; the same header drives the host and device suites.
//
// The menu owns its own A/B edge flags (MenuState::prevA/prevB) so it can never
// touch Game::prevA/prevB — picking a loadout is a separate state machine, not a
// step of the hunt. While the sim runs, the loop keeps those same flags current
// through menuReturnStep(), so the post-game A edge does not need a second copy
// of the edge rule.

#include <stdint.h>
#include "core/input.hpp"
#include "core/world.hpp"   // newGame + MODE_* (menuStart)

namespace mh {

enum MenuAction : int8_t {
    MENU_NONE = 0,
    MENU_START
};

struct MenuState {
    int8_t weapon = 0;    // WeaponId 0..2 (SWD/FLS/GUN)
    int8_t target = 0;    // 0..2 beast kind, 3 = training pole
    bool active = true;   // boot into the menu
    bool prevA = false;   // menu-owned edges (see menuReturnStep)
    bool prevB = false;
};

constexpr int8_t MENU_WEAPON_COUNT = 3;
constexpr int8_t MENU_TARGET_COUNT = 4;
constexpr int8_t MENU_POLE_TARGET = 3;

// Wrap a pick by delta within [0, count). Two compares beat a runtime modulo.
inline int8_t menuCycle(int8_t v, int8_t delta, int8_t count) {
    v = static_cast<int8_t>(v + delta);
    if (v < 0)
        v = static_cast<int8_t>(v + count);
    else if (v >= count)
        v = static_cast<int8_t>(v - count);
    return v;
}

// One menu tick: LEFT/RIGHT cycle the weapon, UP/DOWN cycle the target (both
// wrap), A rising edge reports MENU_START exactly once per press. B is tracked
// but unused for now. Navigation works while A is held too.
inline MenuAction menuStep(MenuState &m, const Input &in) {
    if (in.mx < 0)
        m.weapon = menuCycle(m.weapon, -1, MENU_WEAPON_COUNT);
    else if (in.mx > 0)
        m.weapon = menuCycle(m.weapon, 1, MENU_WEAPON_COUNT);

    if (in.my < 0)
        m.target = menuCycle(m.target, -1, MENU_TARGET_COUNT);
    else if (in.my > 0)
        m.target = menuCycle(m.target, 1, MENU_TARGET_COUNT);

    bool aP, bP, bR;
    inputEdges(in, m.prevA, m.prevB, aP, bP, bR);
    (void)bP;
    (void)bR;
    return aP ? MENU_START : MENU_NONE;
}

// Pick -> sim mapping: targets 0..2 select the beast kind in hunt mode, target
// 3 the training pole in train mode. Single source of truth for the sketch and
// both suites.
inline int8_t menuMode(const MenuState &m) {
    return m.target == MENU_POLE_TARGET ? MODE_TRAIN : MODE_HUNT;
}

inline int8_t menuMonsterKind(const MenuState &m) {
    return m.target <= 2 ? m.target : 0;   // the pole has no beast
}

// Start the chosen scene from the menu picks.
inline void menuStart(Game &g, const MenuState &m) {
    newGame(g, m.weapon, menuMode(m), menuMonsterKind(m));
}

// One input tick while the sim runs: keeps the menu-owned edge flags current and
// reports the post-game return edge (A rising edge after win/lose only, exactly
// once per press). `over` is `g.over != OVER_NONE`.
inline bool menuReturnStep(MenuState &m, bool over, const Input &in) {
    const bool aP = in.a && !m.prevA;
    m.prevA = in.a;
    m.prevB = in.b;
    return over && aP;
}

}   // namespace mh
