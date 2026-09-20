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
    MENU_ACCEPT   // A: launch the picked loadout (demo flow, monhun-ardu-5r1)
};

struct MenuState {
    int8_t weapon = 0;    // WeaponId 0..2 (SWD/FLS/GUN)
    int8_t target = 0;    // 0..3 beast kind (lunge/sweep/heavy/ravager)
    bool active = true;   // boot into the menu
    bool prevA = false;   // menu-owned edges (see menuReturnStep)
    bool prevB = false;
    int8_t navX = 0;         // last x direction (-1/0/1); 0 = axis released
    int8_t navY = 0;         // last y direction
    uint8_t navXTimer = 0;   // ticks until the next x repeat (0 = disarmed)
    uint8_t navYTimer = 0;   // ticks until the next y repeat
};

constexpr int8_t MENU_WEAPON_COUNT = 3;
// Targets: 0..3 the beast roster (prg.8 removed the training-pole target).
constexpr int8_t MENU_TARGET_COUNT = 4;

// D-pad repeat: a fresh direction steps immediately, a held one waits
// MENU_NAV_DELAY ticks (~300 ms at the measured 52 Hz logic rate) before the
// next step, then repeats every MENU_NAV_REPEAT ticks (~115 ms). uint8 timers,
// counted down one per menu tick; no modulo, no floats.
constexpr uint8_t MENU_NAV_DELAY = 16;
constexpr uint8_t MENU_NAV_REPEAT = 6;

// Wrap a pick by delta within [0, count). Two compares beat a runtime modulo.
inline int8_t menuCycle(int8_t v, int8_t delta, int8_t count) {
    v = static_cast<int8_t>(v + delta);
    if (v < 0)
        v = static_cast<int8_t>(v + count);
    else if (v >= count)
        v = static_cast<int8_t>(v - count);
    return v;
}

// Clear both axes' hold state (last direction + timer). Called when the menu
// (re-)activates so a d-pad held across the transition cannot skip picks.
MH_NOINLINE inline void menuResetNav(MenuState &m) {
    m.navX = m.navY = 0;
    m.navXTimer = m.navYTimer = 0;
}

// One axis of debounced nav. A direction change (or fresh press) steps at once
// and arms DELAY; a continuation tick under the armed timer does nothing until
// it expires, then steps and re-arms REPEAT. Zero direction releases the axis.
inline void menuNavAxis(int8_t &pick, int8_t count, int8_t dir, int8_t &last, uint8_t &timer) {
    if (dir == 0) {
        last = 0;
        timer = 0;
        return;
    }
    if (dir != last) {
        pick = menuCycle(pick, dir, count);
        last = dir;
        timer = MENU_NAV_DELAY;
        return;
    }
    if (timer > 0) {
        timer--;
        if (timer == 0) {
            pick = menuCycle(pick, dir, count);
            timer = MENU_NAV_REPEAT;
        }
    }
}

// One menu tick: LEFT/RIGHT cycle the weapon, UP/DOWN cycle the target (both
// wrap), A rising edge reports MENU_ACCEPT exactly once per press (the caller
// launches the picked loadout via app_state.hpp; APP_NAV_HUNT -> menuStart).
// Navigation is debounced per axis (menuNavAxis): one step per tap, hold delays
// then repeats. Navigation works while A is held too. B is not a menu action.
inline MenuAction menuStep(MenuState &m, const Input &in) {
    menuNavAxis(m.weapon, MENU_WEAPON_COUNT, in.mx, m.navX, m.navXTimer);
    menuNavAxis(m.target, MENU_TARGET_COUNT, in.my, m.navY, m.navYTimer);

    bool aP, bP, bR;
    inputEdges(in, m.prevA, m.prevB, aP, bP, bR);
    (void)bP;
    (void)bR;
    if (aP)
        return MENU_ACCEPT;
    return MENU_NONE;
}

// Pick -> sim mapping: every target 0..3 selects the beast kind (prg.8 removed
// the training-pole target, so mode is always hunt). Single source of truth for
// the sketch and both suites.
inline int8_t menuMode(const MenuState &) {
    return MODE_HUNT;
}

inline int8_t menuMonsterKind(const MenuState &m) {
    return m.target;
}

// Start the chosen scene from the menu picks into its demo room (fie.6): a
// beast pick spawns in the camp (safe; its door leads to the area hunt).
// newGame() resets the world first; the room load re-arms the target for the
// arrival room.
inline void menuStart(Game &g, const MenuState &m) {
    newGame(g, m.weapon, MODE_HUNT, menuMonsterKind(m));
    loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
}

// One input tick while the sim runs: keeps the menu-owned edge flags current and
// reports the post-game return edge (A rising edge after win/lose only, exactly
// once per press). `over` is `g.over != OVER_NONE`. On a return it also clears
// the d-pad hold state so a direction held during the over screen cannot skip
// picks when the menu re-opens.
inline bool menuReturnStep(MenuState &m, bool over, const Input &in) {
    const bool aP = in.a && !m.prevA;
    m.prevA = in.a;
    m.prevB = in.b;
    if (over && aP)
        menuResetNav(m);
    return over && aP;
}

}   // namespace mh
