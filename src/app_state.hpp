#pragma once
// App-level routing between the data-driven screens (hub / quests / gear) and a
// hunt (bead monhun-ardu-mgn, docs/quests-shops.md qs.4; hub-as-root rework
// monhun-ardu-isp.1, which deleted the opening menu; ui.3.1 (5co.6) removed the
// SMITH screen -- FORGE replaces it in ui.4).
//
// Host- and device-testable: no cart reads, no Arduino.h. The caller resolves
// the cursor row off the cart (screenCursorRow in src/screens.hpp) and passes
// it to appScreenAccept(); this header only decides where to go and applies the
// screen state changes, so the sketch and the device E2E suite run the exact
// same routing code.
//
// Live flow (monhun-ardu-isp.1; the hub is the root screen, the 5r1/opening
// menu is gone; prg.8 removed the training-pole room):
//   boot --> hub --HUNT--> camp --door--> area --door--> camp
//   hub --QUESTS/GEAR--> screen --B/LEAVE--> hub
//   hub --B--> nothing (root; appScreenBack(HUB) == APP_NAV_NONE)
//   camp hold-B --> hub                              (appHubRequest)
//   hunt end + A --> hub                             (appHuntReturn; turn-ins)
//
// The picked loadout lives in the save (v4 `weapon`); the hub HUNT row launches
// it through the device glue huntStart() (src/app_setup.hpp) and a finished hunt
// returns to the hub to turn quests in.
//
// appNavApply() takes the transition Input so the new owner's A/B edge flags
// start from the button state that caused the change: a held button cannot
// re-fire through the new screen on the very next tick.

#include "screen_state.hpp"
#include "core/save.hpp"

namespace mh {

enum AppNav : int8_t {
    APP_NAV_NONE = 0,
    APP_NAV_HUB,
    APP_NAV_QUESTS,
    APP_NAV_GEAR,          // hml.3: hub GEAR row (weapon select + armor craft/equip)
    APP_NAV_FORGE,         // ui.4: hub FORGE row (the smithy submenu, hbk.10)
    APP_NAV_CRAFT,         // hbk.10: FORGE submenu WEAPON CRAFT row
    APP_NAV_UPGRADE,       // hbk.10: FORGE submenu WEAPON UPGRADE row (screen in hbk.11)
    APP_NAV_ARMOR_FORGE,   // hbk.10: FORGE submenu ARMOR FORGE row
    APP_NAV_HUNT           // the hub HUNT row: the caller starts the hunt (huntStart)
};

// B: hub is the root (no back destination); quests/gear -> hub.
inline AppNav appScreenBack(uint8_t screen) {
    return screen == screens::SCREEN_HUB ? APP_NAV_NONE : APP_NAV_HUB;
}

// A on the cursor row. Hub rows route to a screen (or the menu on LEAVE);
// the FORGE submenu rows route to the craft/upgrade/armor screens (hbk.10);
// other screens leave to the hub on a LEAVE row, else APP_NAV_NONE so the
// caller runs the row's save action (screenApplyAction).
inline AppNav appScreenAccept(uint8_t screen, const ScreenRow &row) {
    if (screen == screens::SCREEN_HUB) {
        switch (row.action) {
        case screens::ACTION_HUNT:
            return APP_NAV_HUNT;
        case screens::ACTION_OPEN_QUESTS:
            return APP_NAV_QUESTS;
        case screens::ACTION_OPEN_GEAR:
            return APP_NAV_GEAR;
        case screens::ACTION_OPEN_FORGE:
            return APP_NAV_FORGE;
        case screens::ACTION_LEAVE:
            return APP_NAV_NONE;   // hub is the root: no leave destination
        default:
            return APP_NAV_NONE;
        }
    }
    if (screen == screens::SCREEN_FORGE) {
        // hbk.10 smithy submenu: the three open rows route to their screens
        // (checked before the LEAVE fallthrough).
        switch (row.action) {
        case screens::ACTION_OPEN_CRAFT:
            return APP_NAV_CRAFT;
        case screens::ACTION_OPEN_UPGRADE:
            return APP_NAV_UPGRADE;
        case screens::ACTION_OPEN_ARMOR_FORGE:
            return APP_NAV_ARMOR_FORGE;
        default:
            break;
        }
    }
    if (row.action == screens::ACTION_LEAVE)
        return APP_NAV_HUB;
    return APP_NAV_NONE;
}

// Hunt end: A after the over screen returns to the hub (monhun-ardu-dlp.3) so
// the finished quest can be turned in and the next chain step taken. The hub's
// HUNT row starts the next hunt (huntStart -> newGame, so the fresh hunt starts
// from a fully reset world: projectiles/effects/quest counters) with the save's
// v4 weapon.
inline AppNav appHuntReturn() {
    return APP_NAV_HUB;
}

// Hunt-end A edge helper (replaces menuReturnStep, monhun-ardu-isp.1): true on
// the A rising edge only while `over`, exactly once per press. `prevA` is the
// caller's edge flag (the sketch's own, since there is no menu state to own it
// any more) and stays current on every tick so the release is not seen as a
// fresh press. Host-testable.
inline bool appOverReturnStep(bool over, const Input &in, bool &prevA) {
    const bool aP = in.a && !prevA;
    prevA = in.a;
    return over && aP;
}

// Hunt-end A gate (bead monhun-ardu-prg.3): while a carcass carve is live the
// over-screen A is the carve verb, so the caller keeps the A edge current
// (appOverReturnStep) but must not apply the return nav. True = the edge may
// leave the hunt. Game::carveHold is cleared the tick the carve ends or is
// cancelled.
inline bool appHuntReturnAllowed(const Game &g) {
    return !g.carveHold;
}

// Camp hold-B (sheathed): the core raises Game::menuRequest.
// The app layer consumes it exactly once -> the hub (the root screen), so a
// held B cannot re-fire once the hub is up. Returns the nav for the caller to
// apply. (Named appHubRequest in monhun-ardu-isp.1: the opening menu it used to
// open is gone.)
inline AppNav appHubRequest(Game &g) {
    if (!g.menuRequest)
        return APP_NAV_NONE;
    g.menuRequest = false;
    return APP_NAV_HUB;
}

// Apply a nav destination to the live state. Returns true when the hub HUNT row
// requested a hunt: the caller then starts it (device glue huntStart(), which
// runs newGame + loadRoom), arms the quest/upgrade state and clears its
// hunt-end latch. Screen row counts come from the generated screen_meta.hpp
// constants, so this stays cart-free and host-testable; the device build's
// screenEnter() reads the same counts off the cart.
MH_NOINLINE inline bool appNavApply(AppNav nav, ScreenState &screen, const SaveBlock &save, Game &game, const Input &in) {
    (void)save;
    (void)game;
    switch (nav) {
    case APP_NAV_HUB:
        screenReset(screen, screens::SCREEN_HUB, screens::SCREEN_HUB_ROWS);
        screen.prevA = in.a;
        screen.prevB = in.b;
        return false;
    case APP_NAV_QUESTS:
        screenReset(screen, screens::SCREEN_QUESTS, screens::SCREEN_QUESTS_ROWS);
        screen.prevA = in.a;
        screen.prevB = in.b;
        return false;
    case APP_NAV_GEAR:
        screenReset(screen, screens::SCREEN_GEAR, screens::SCREEN_GEAR_ROWS);
        screen.prevA = in.a;
        screen.prevB = in.b;
        return false;
    case APP_NAV_FORGE:
        screenReset(screen, screens::SCREEN_FORGE, screens::SCREEN_FORGE_ROWS);
        screen.prevA = in.a;
        screen.prevB = in.b;
        return false;
    case APP_NAV_CRAFT:
        screenReset(screen, screens::SCREEN_CRAFT, screens::SCREEN_CRAFT_ROWS);
        screen.prevA = in.a;
        screen.prevB = in.b;
        return false;
    case APP_NAV_ARMOR_FORGE:
        screenReset(screen, screens::SCREEN_ARMOR_FORGE, screens::SCREEN_ARMOR_FORGE_ROWS);
        screen.prevA = in.a;
        screen.prevB = in.b;
        return false;
    case APP_NAV_UPGRADE:
        // hbk.11 adds the UPGRADE screen (SCREEN_UPGRADE); until then the
        // submenu WEAPON UPGRADE row is inert.
        return false;
    case APP_NAV_HUNT:
        // The hub HUNT row: close the screen and report the hunt request. The
        // caller starts it (huntStart reads the quest def + save weapon), so the
        // fresh hunt resets projectiles/effects/quest counters.
        screen.active = false;
        return true;
    default:
        return false;
    }
}

// Hunt-end commit (beads monhun-ardu-me6 qs.2, prg.5 save v2): folds the hunt's
// quest progress and its RAM inventory gains into the save exactly once per
// hunt, returning true on the single tick where the caller must saveStore().
// `latched` is the caller's per-hunt flag; it clears when the hunt ends (over
// false) so the next hunt commits again. A hunt that neither advanced a quest
// nor gained an item returns false (no EEPROM write), but the latch still arms
// so a later over tick cannot re-enter the write path.
//
// Inventory folding is coalesced: gather/carve update Game::items[] in RAM only
// (never per item); this once-per-hunt call takes the max of the live hunt
// counts and the saved stock, so a hunted-out herb never erases the pantry.
inline bool appHuntCommit(bool over, bool &latched, SaveBlock &save, Game &g) {
    if (!over) {
        latched = false;
        return false;
    }
    if (latched)
        return false;
    latched = true;
    bool changed = false;
    if (save.activeQuest != SAVE_QUEST_NONE) {
        save.progress = g.questProgress;
        changed = true;
    }
    for (uint8_t i = 0; i < item::ITEM_COUNT; i++) {
        if (g.items[i] > save.items[i]) {
            save.items[i] = g.items[i];
            changed = true;
        }
    }
    return changed;
}

}   // namespace mh
