
#define ABG_IMPLEMENTATION
#define SPRITESU_IMPLEMENTATION
#include "src/common.hpp"
#include "src/globals.hpp"
#include "src/fxdata.h"
#include "src/core/world.hpp"
#include "src/audio.hpp"

// Compile-time gate for the 1-bit wireframe debug overlay (hurt/hit boxes).
// 0 = release: every debug symbol below is preprocessed out (zero flash/RAM).
// 1 = debug: world-space wire boxes drawn after the scene, before the HUD.
#ifndef DEBUG_HURTBOXES
#define DEBUG_HURTBOXES 0
#endif

// Shared block-art renderer (arena, target, player, shells, effects, HUD). The
// exact same header is compiled into the on-device perf bench, so the numbers
// there describe this loop's real render path.
#include "src/render.hpp"
#include "src/menu.hpp"        // draws through render.hpp (textPut/blk) + MenuState
#include "src/screens.hpp"     // hub/list screens + EEPROM save (qs.1)
#include "src/app_state.hpp"   // boot-flow routing: menu <-> hub <-> screens <-> hunt (qs.4)
#include "src/app_setup.hpp"   // cart-backed hunt arming: quest def + smith tier (qs.4)
#include "src/quest.hpp"       // quest defs on cart + TAKE/TURN_IN state (qs.2)
#include "src/smith.hpp"       // smith upgrade defs on cart + tier multipliers (qs.3)

decltype(arduboy) arduboy;

// Single game state. The core is header-only and shared verbatim with the host
// tests; the device loop only samples input, steps it, and reads it for draw.
mh::Game g;

// Opening menu (bead monhun-ardu-6zb.2): boot lands here. While active it owns
// every input edge; the sim and audio are not stepped. A launches the picked
// weapon/target straight into the hunt (demo flow, monhun-ardu-5r1); a win/loss
// + A returns here. The picks stay live across the menu -> hunt -> menu loop.
mh::MenuState s_menu;

// Audio cue edge detector. Driven from run() after stepGame(); reads Game only
// (no core changes). Muted at compile time with -DMH_AUDIO=0.
mh::AudioState s_audio;

// Persistent save + the data-driven screen state (bead monhun-ardu-cgz). The
// save loads once in setup(); it is committed only from a screen action or the
// hunt-end progress commit (never mid-hunt) so EEPROM write cycles stay low.
// The hub/quests/smith screens are off the demo path (monhun-ardu-5r1): the
// routing code stays compiled and tested, but the sketch never enters the hub.
// If a save already carries an active quest/tier it still applies at hunt start
// and the hunt-end commit runs once per hunt, exactly as before.
mh::SaveBlock s_save;
mh::ScreenState s_screen;
static const mh::SaveBackend SAVE_BACKEND = {mh::saveEepromRead, mh::saveEepromWrite};

// Quest kill accounting edge (qs.2/qs.4): the hunt-end commit writes the
// progress once per hunt (never mid-hunt); appHuntCommit() owns the once-only
// latch so the save is not rewritten on every post-over tick.
static bool s_huntOver = false;

#if DEBUG_HURTBOXES
// Runtime toggle inside the debug build: hold A+B for 30 ticks to flip. The
// buttons still reach the sim unchanged (run() never consumes them); A+B is
// only *observed* here, so normal input cannot be eaten by the overlay.
static bool s_wire = true;
static uint8_t s_wireHold = 0;

static void pollDebugToggle(const mh::Input &in) {
    if (in.a && in.b) {
        if (s_wireHold < 30)
            s_wireHold++;
        if (s_wireHold == 30)
            s_wire = !s_wire;
    } else {
        s_wireHold = 0;
    }
}
#endif   // DEBUG_HURTBOXES

void setup() {
    // Serial.begin(9600);

    arduboy.boot();
    arduboy.startGray();
    // initRandomSeed() dropped: the core is fully deterministic and never calls
    // random(), so seeding only pulled the AVR random/random_r code into flash.

    FX::begin(FX_DATA_PAGE);
    FX::setCursorRange(0, 32767);

    mh::newGame(g, mh::W_SWORD, mh::MODE_HUNT);
    mh::saveLoad(s_save, SAVE_BACKEND);   // first boot / bad block -> defaults
    mh::questApplyToGame(g, s_save);
    mh::upgradeApplyToGame(g, s_save);
}

// One input sample per logic tick, shared by the menu and the sim. The menu
// owns its own edge flags (MenuState::prevA/prevB); while the sim runs,
// menuReturnStep() keeps those same flags current, so the post-game A edge
// needs no second edge rule here.
static mh::Input sampleInput() {
    mh::Input in;
    in.mx = (arduboy.pressed(RIGHT_BUTTON) ? 1 : 0) - (arduboy.pressed(LEFT_BUTTON) ? 1 : 0);
    in.my = (arduboy.pressed(DOWN_BUTTON) ? 1 : 0) - (arduboy.pressed(UP_BUTTON) ? 1 : 0);
    in.a = arduboy.pressed(A_BUTTON);
    in.b = arduboy.pressed(B_BUTTON);
    return in;
}

// One logic tick. Called only from needsUpdate() (never mid-plane), so the
// whole core advances atomically between planes. pollButtons() already ran.
// Demo flow (monhun-ardu-5r1): menu --A--> hunt --end+A--> menu. The screen
// branch below still handles the hub graph if a screen ever becomes active
// (shelf code kept in tree), but nothing on the demo path sets it.
void run() {
    const mh::Input in = sampleInput();
#if DEBUG_HURTBOXES
    pollDebugToggle(in);   // observes A+B; does not consume input from stepGame
#endif
    if (s_menu.active) {
        // Menu tick: no stepGame, no audio (the new game re-latches the audio
        // snapshot on its tick 0). A launches the picked loadout directly
        // (APP_NAV_HUNT -> menuStart -> newGame, so projectiles/effects/quest
        // counters reset for the fresh hunt); then re-arm the quest/tier from
        // the save and clear the hunt-end latch, exactly like the screen path.
        if (mh::menuStep(s_menu, in) == mh::MENU_ACCEPT) {
            if (mh::appNavApply(mh::appMenuAccept(), s_menu, s_screen, s_save, g, in)) {
                mh::questApplyToGame(g, s_save);
                mh::upgradeApplyToGame(g, s_save);
                s_huntOver = false;
            }
        }
        return;
    }
    if (s_screen.active) {
        // Screen tick: nav + A/B. B steps back one level (quests/smith -> hub,
        // hub -> menu); A routes through the hub map or runs the row action.
        const mh::ScreenEvent ev = mh::screenStep(s_screen, in);
        if (ev == mh::SCREEN_BACK) {
            mh::appNavApply(mh::appScreenBack(s_screen.screen), s_menu, s_screen, s_save, g, in);
            return;
        }
        if (ev != mh::SCREEN_ACCEPT)
            return;
        mh::ScreenRow row;
        if (!mh::screenCursorRow(s_screen, row) || !mh::screenCondOk(s_save, row))
            return;
        const mh::AppNav nav = mh::appScreenAccept(s_screen.screen, row);
        if (nav != mh::APP_NAV_NONE) {
            // Boot-flow destination (hub row, leave, hunt): a hunt start arms
            // the quest/upgrade state and clears the hunt-end latch.
            if (mh::appNavApply(nav, s_menu, s_screen, s_save, g, in)) {
                mh::questApplyToGame(g, s_save);
                mh::upgradeApplyToGame(g, s_save);
                s_huntOver = false;
            }
            return;
        }
        if (mh::screenApplyAction(s_save, row))
            mh::saveStore(s_save, SAVE_BACKEND);
        return;
    }
    mh::stepGame(g, in);
    mh::audioUpdate(s_audio, g);
    // Hunt-end quest commit (qs.2/qs.4): persist the kill progress exactly once
    // per hunt. The save is otherwise untouched during a hunt (write-cycle
    // hygiene); appHuntCommit() owns the latch.
    if (mh::appHuntCommit(g.over != mh::OVER_NONE, s_huntOver, s_save, g.questProgress))
        mh::saveStore(s_save, SAVE_BACKEND);
    // Win/lose over screen: a fresh A returns to the opening menu (picks
    // preserved); the menu's next A starts a fully reset hunt.
    if (mh::menuReturnStep(s_menu, g.over != mh::OVER_NONE, in))
        mh::appNavApply(mh::appHuntReturn(), s_menu, s_screen, s_save, g, in);
}

// Full block-art scene (arena, target, player, shells, effects, HUD). Read-only:
// render never mutates Game; the three plane passes composite one L4 image.
// While the menu is up it replaces the scene (same per-plane call discipline).
void render() {
    if (s_screen.active) {
        mh::drawScreen(s_screen, s_save);
        return;
    }
    if (s_menu.active) {
        mh::drawMenu(s_menu);
        return;
    }
#if DEBUG_HURTBOXES
    mh::renderScene(g, s_wire);
#else
    mh::renderScene(g, false);
#endif
}

void loop() {
    FX::enableOLED();
    arduboy.waitForNextPlane();
    FX::disableOLED();
    if (arduboy.needsUpdate()) {
        arduboy.pollButtons();
        run();
    }
    render();
}
