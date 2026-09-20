#pragma once
// App-level routing between the opening menu, the data-driven screens (hub /
// quests / smith) and a hunt (bead monhun-ardu-mgn, docs/quests-shops.md qs.4;
// demo flow rework monhun-ardu-5r1).
//
// Host- and device-testable: no cart reads, no Arduino.h. The caller resolves
// the cursor row off the cart (screenCursorRow in src/screens.hpp) and passes
// it to appScreenAccept(); this header only decides where to go and applies the
// menu/screen state changes, so the sketch and the device E2E suite run the
// exact same routing code.
//
// Demo flow (monhun-ardu-fie.6, what the shipping sketch wires):
//   menu --A--> camp --door--> area --door--> camp   (hunt pick; camp hold-B
//   --B hold--> menu)                                -> menu; area door -> camp
//   menu --A--> pole_room --door--> menu             (pole pick, train)
//   hunt end + A --> menu                            (appHuntReturn)
//
// Shelf graph (kept compiled + unit-tested, NOT reachable from the sketch; the
// hub/quests/smith work stays in the tree for later re-enable):
//   hub --HUNT--> hunt
//   hub --QUESTS/SMITH--> screen --B/LEAVE--> hub
//   hub --B/LEAVE--> menu
//
// appNavApply() takes the transition Input so the new owner's A/B edge flags
// start from the button state that caused the change: a held button cannot
// re-fire through the new screen on the very next tick.

#include "menu_state.hpp"
#include "screen_state.hpp"
#include "core/save.hpp"

namespace mh {

enum AppNav : int8_t {
    APP_NAV_NONE = 0,
    APP_NAV_MENU,   // back to the opening menu
    APP_NAV_HUB,
    APP_NAV_QUESTS,
    APP_NAV_SMITH,
    APP_NAV_HUNT   // start the picked loadout in the sim
};

// Opening-menu A: launch the picked loadout straight into the hunt (demo flow,
// monhun-ardu-5r1). The hub graph below stays for the screen suites only.
inline AppNav appMenuAccept() {
    return APP_NAV_HUNT;
}

// B: quests/smith -> hub; hub -> menu.
inline AppNav appScreenBack(uint8_t screen) {
    return screen == screens::SCREEN_HUB ? APP_NAV_MENU : APP_NAV_HUB;
}

// A on the cursor row. Hub rows route to a screen (or the menu on LEAVE);
// other screens leave to the hub on a LEAVE row, else APP_NAV_NONE so the
// caller runs the row's save action (screenApplyAction).
inline AppNav appScreenAccept(uint8_t screen, const ScreenRow &row) {
    if (screen == screens::SCREEN_HUB) {
        switch (row.action) {
        case screens::ACTION_HUNT:
            return APP_NAV_HUNT;
        case screens::ACTION_OPEN_QUESTS:
            return APP_NAV_QUESTS;
        case screens::ACTION_OPEN_SMITH:
            return APP_NAV_SMITH;
        case screens::ACTION_LEAVE:
            return APP_NAV_MENU;
        default:
            return APP_NAV_NONE;
        }
    }
    if (row.action == screens::ACTION_LEAVE)
        return APP_NAV_HUB;
    return APP_NAV_NONE;
}

// Hunt end: A after the over screen returns to the opening menu (demo flow).
// The menu's next A re-runs menuStart -> newGame, so the fresh hunt starts from
// a fully reset world (projectiles/effects/quest counters).
inline AppNav appHuntReturn() {
    return APP_NAV_MENU;
}

// Camp hold-B (sheathed) / pole-room door: the core raises Game::menuRequest.
// The app layer consumes it exactly once -> opening menu, so a held B cannot
// re-fire once the menu is up. Returns the nav for the caller to apply.
inline AppNav appMenuRequest(Game &g) {
    if (!g.menuRequest)
        return APP_NAV_NONE;
    g.menuRequest = false;
    return APP_NAV_MENU;
}

// Apply a nav destination to the live states. Returns true when a hunt just
// started (the caller then arms the quest/upgrade state and clears its
// hunt-end latch). `menu` keeps the weapon/target picks across the hunt.
// Screen row counts come from the generated screen_meta.hpp constants, so this
// stays cart-free and host-testable; the device build's screenEnter() reads the
// same counts off the cart.
MH_NOINLINE inline bool appNavApply(AppNav nav, MenuState &menu, ScreenState &screen, const SaveBlock &save, Game &game, const Input &in) {
    (void)save;
    switch (nav) {
    case APP_NAV_MENU:
        screen.active = false;
        menu.active = true;
        menuResetNav(menu);
        menu.prevA = in.a;
        menu.prevB = in.b;
        return false;
    case APP_NAV_HUB:
        screenReset(screen, screens::SCREEN_HUB, screens::SCREEN_HUB_ROWS);
        screen.prevA = in.a;
        screen.prevB = in.b;
        menu.active = false;
        return false;
    case APP_NAV_QUESTS:
        screenReset(screen, screens::SCREEN_QUESTS, screens::SCREEN_QUESTS_ROWS);
        screen.prevA = in.a;
        screen.prevB = in.b;
        menu.active = false;
        return false;
    case APP_NAV_SMITH:
        screenReset(screen, screens::SCREEN_SMITH, screens::SCREEN_SMITH_ROWS);
        screen.prevA = in.a;
        screen.prevB = in.b;
        menu.active = false;
        return false;
    case APP_NAV_HUNT:
        menuStart(game, menu);
        screen.active = false;
        menu.active = false;   // demo flow: the menu itself launched the hunt
        menu.prevA = in.a;
        menu.prevB = in.b;
        return true;
    default:
        return false;
    }
}

// Hunt-end quest-progress commit (bead monhun-ardu-me6), exactly once per hunt:
// returns true on the single tick where the caller must saveStore(). `latched`
// is the caller's per-hunt flag; it clears when the hunt ends (over false) so
// the next hunt commits again. No active quest means nothing to persist, but
// the latch still arms so a later over tick cannot re-enter the write path.
inline bool appHuntCommit(bool over, bool &latched, SaveBlock &save, uint8_t progress) {
    if (!over) {
        latched = false;
        return false;
    }
    if (latched)
        return false;
    latched = true;
    if (save.activeQuest == SAVE_QUEST_NONE)
        return false;
    save.progress = progress;
    return true;
}

}   // namespace mh
