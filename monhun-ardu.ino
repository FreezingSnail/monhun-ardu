
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
#include "src/screens.hpp"     // hub/list screens + EEPROM save (qs.1)
#include "src/cards.hpp"       // prebaked detail cards + nav (5co.3)
#include "src/app_state.hpp"   // boot-flow routing: hub <-> screens <-> hunt (isp.1)
#include "src/app_setup.hpp"   // cart-backed hunt arming + huntStart (qs.4/isp.1)
#include "src/quest.hpp"       // quest defs on cart + TAKE/TURN_IN state (qs.2)

decltype(arduboy) arduboy;

// Single game state. The core is header-only and shared verbatim with the host
// tests; the device loop only samples input, steps it, and reads it for draw.
mh::Game g;

// Audio cue edge detector. Driven from run() after stepGame(); reads Game only
// (no core changes). Muted at compile time with -DMH_AUDIO=0.
mh::AudioState s_audio;

// Persistent save + the data-driven screen state (bead monhun-ardu-cgz). The
// save loads once in setup(); it is committed only from a screen action or the
// hunt-end progress commit (never mid-hunt) so EEPROM write cycles stay low.
// The hub is the root screen (monhun-ardu-isp.1, the opening menu is gone):
// boot enters it, HUNT launches the save's weapon/active-quest hunt, and a
// finished hunt returns to it for turn-ins. The quests/smith screens are
// reachable from its rows.
mh::SaveBlock s_save;
mh::ScreenState s_screen;
// Detail-card state (bead monhun-ardu-5co.3): the open card's page machine and
// the list row that opened it (the card A reuses screenApplyAction with the row,
// so the packed action/param/recipe context travels with the card). Armor and
// quest rows open cards; a GEAR weapon row keeps its direct-equip action until
// the ui.4 forge trees land (temporary scope split, docs/ui-design.md).
mh::DetailState s_detail;
mh::ScreenRow s_detailRow;
// Decoded card record cached at open/refresh so drawCard does not re-read the
// mhCards record every frame.
mh::CardItem s_card;
static const mh::SaveBackend SAVE_BACKEND = {mh::saveEepromRead, mh::saveEepromWrite};

// Hunt-end A edge flag (monhun-ardu-isp.1): the over-screen return used to be
// owned by the deleted MenuState; the sketch keeps its own flag now.
static bool s_huntPrevA = false;

// Quest kill accounting edge (qs.2/qs.4): the hunt-end commit writes the
// progress once per hunt (never mid-hunt); appHuntCommit() owns the once-only
// latch so the save is not rewritten on every post-over tick.
static bool s_huntOver = false;

// GEAR skill readout (gs.2): refresh Game::armor from the save, then copy the
// per-skill points/tier into the GEAR screen's cache. Called when GEAR is
// entered and after every GEAR action, so equipping a piece moves the numbers
// on the very next frame.
static void refreshGearReadout() {
    mh::armorApplyToGame(g, s_save);
    mh::screenGearCache(s_screen, g.armor);
}

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

    mh::saveLoad(s_save, SAVE_BACKEND);   // first boot / bad block -> defaults
    // The hub is the root screen (monhun-ardu-isp.1): boot enters it. The world
    // is only built when the hub HUNT row starts a hunt (huntStart), so no
    // newGame/arming happens here. The armor cache is armed only for the hub
    // bottom strip (ui.5.2); a hunt re-arms it in huntStart.
    mh::armorApplyToGame(g, s_save);
    mh::screenEnter(s_screen, screens::SCREEN_HUB, s_save);
}

// One input sample per logic tick, shared by the screens and the sim. The
// screens own their own A/B edge flags (ScreenState::prevA/prevB); while the
// sim runs, appOverReturnStep() keeps the hunt-end A flag (s_huntPrevA) current,
// so the post-game return edge needs no second edge rule here.
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
// Live flow (monhun-ardu-isp.1): boot -> hub --HUNT--> camp --door--> area
// --door--> camp; hub --QUESTS/SMITH--> screen --B--> hub; camp hold-B -> hub;
// win/loss + A -> hub (turn-ins). The hub is the root: B there does nothing.
void run() {
    const mh::Input in = sampleInput();
#if DEBUG_HURTBOXES
    pollDebugToggle(in);   // observes A+B; does not consume input from stepGame
#endif
#ifndef MH_CARD_OFF
    if (s_detail.active) {
        // Card tick (5co.3): LEFT/RIGHT cycle pages (skipping pages absent from
        // the effective mask), B backs to the list, A runs the stored row's
        // context action and refreshes the card (a craft drops the PARTS page)
        // and the GEAR readout.
        const mh::DetailEvent dev = mh::detailStep(s_detail, in);
        if (dev == mh::DETAIL_BACK) {
            mh::cardClose(s_detail);
            return;
        }
        if (dev == mh::DETAIL_ACTION) {
            // One card action switch (ui.4.1): armor crafts/equips from the
            // baked bill (5co.6); weapon forge/upgrade or equip/unequip from the
            // cached node (5co.4); quest cards take/turn in through the row.
            const bool changed = mh::cardApply(s_save, s_card, s_detail.node, s_detailRow);
            if (changed)
                mh::saveStore(s_save, SAVE_BACKEND);
            else if (mh::cardDenied(s_detail))
                // ui.5.2 denied cue: a blocked card A (NEED PARTS / NEED ZENNY /
                // no action) reuses the low CUE_HURT thunk; no new cue row.
                mh::audioPlay(mh::CUE_HURT);
            mh::cardLoad(s_detail, s_card, s_detail.index, s_save, true);
            mh::cardSetHint(s_detail, s_save, s_card, s_detailRow);
            if (s_screen.screen == screens::SCREEN_GEAR)
                refreshGearReadout();
            return;
        }
        return;
    }
#endif
    if (s_screen.active) {
        // Screen tick: nav + A/B. B steps back one level (quests/gear -> hub;
        // the hub is the root, so its B is a no-op); A routes through the hub
        // map or runs the row action.
        const mh::ScreenEvent ev = mh::screenStep(s_screen, in);
        if (ev == mh::SCREEN_BACK) {
            mh::appNavApply(mh::appScreenBack(s_screen.screen), s_screen, s_save, g, in);
            return;
        }
        if (ev != mh::SCREEN_ACCEPT)
            return;
        mh::ScreenRow row;
        if (!mh::screenCursorRow(s_screen, row))
            return;
        // Armor/quest rows open their prebaked card (even when the action is
        // gated -- the card's hint line shows NEED PARTS / NEED ZENNY). Every
        // other row keeps the direct-action path below.
#ifndef MH_CARD_OFF
        const uint8_t cardIndex = mh::cardRowIndex(row);
        if (cardIndex != mh::CARD_NONE) {
            mh::cardLoad(s_detail, s_card, cardIndex, s_save, false);
            s_detailRow = row;
            mh::cardSetHint(s_detail, s_save, s_card, s_detailRow);
            return;
        }
#endif
        if (!mh::screenCondOk(s_save, row)) {
            // ui.5.2 denied cue: a blocked list A (locked/gated row) thunks.
            mh::audioPlay(mh::CUE_HURT);
            return;
        }
        const mh::AppNav nav = mh::appScreenAccept(s_screen.screen, row);
        if (nav != mh::APP_NAV_NONE) {
            // Hub destination (row, hunt): a hunt start builds the world from
            // the save (huntStart), arms the quest/upgrade/item/armor state and
            // clears the hunt-end latch.
            if (mh::appNavApply(nav, s_screen, s_save, g, in)) {
                mh::huntStart(g, s_save);
                mh::questApplyToGame(g, s_save);
                mh::upgradeApplyToGame(g, s_save);
                mh::itemsApplyToGame(g, s_save);
                mh::armorApplyToGame(g, s_save);
                s_huntOver = false;
            } else if (s_screen.active && s_screen.screen == screens::SCREEN_GEAR) {
                // gs.2: entering GEAR fills the live skill readout cache.
                refreshGearReadout();
            }
            return;
        }
        if (mh::screenApplyAction(s_save, row))
            mh::saveStore(s_save, SAVE_BACKEND);
        // gs.2: a GEAR equip action refreshes the readout so the points/letters
        // move immediately (armorApplyToGame first, then the cache copy).
        if (s_screen.screen == screens::SCREEN_GEAR)
            refreshGearReadout();
        return;
    }
    mh::stepGame(g, in);
    mh::audioUpdate(s_audio, g);
    // Camp hold-B sheathed: the core raises Game::menuRequest.
    // Consume it once (a held B cannot re-fire) and open the hub (root).
    if (mh::appHubRequest(g) != mh::APP_NAV_NONE) {
        mh::appNavApply(mh::APP_NAV_HUB, s_screen, s_save, g, in);
        return;
    }
    // Camp smithy (prg.7) note: the SMITH screen is gone (ui.3.1, 5co.6) and the
    // FORGE trees replace it in ui.4. Game::smithyRequest still latches from the
    // camp forge rect; the FORGE bead consumes it. Nothing to route here yet.
    // Hunt-end quest commit (qs.2/qs.4): persist the kill progress exactly once
    // per hunt. The save is otherwise untouched during a hunt (write-cycle
    // hygiene); appHuntCommit() owns the latch.
    if (mh::appHuntCommit(g.over != mh::OVER_NONE, s_huntOver, s_save, g))
        mh::saveStore(s_save, SAVE_BACKEND);
    // Win/lose over screen: a fresh A returns to the hub (monhun-ardu-dlp.3) so
    // the finished quest can be turned in; the hub HUNT row starts a fully reset
    // hunt (huntStart). While a carcass carve is live (prg.3) the A belongs to
    // the carve, so keep the edge current but skip the return nav; the hunt end
    // still routes out otherwise.
    const bool huntReturn = mh::appOverReturnStep(g.over != mh::OVER_NONE, in, s_huntPrevA);
    if (huntReturn && mh::appHuntReturnAllowed(g))
        mh::appNavApply(mh::appHuntReturn(), s_screen, s_save, g, in);
}

// Full block-art scene (arena, target, player, shells, effects, HUD). Read-only:
// render never mutates Game; the three plane passes composite one L4 image.
// While a screen is up it replaces the scene (same per-plane call discipline).
void render() {
#ifndef MH_CARD_OFF
    if (s_detail.active) {
        mh::drawCard(s_detail, s_card, s_save);
        return;
    }
#endif
    if (s_screen.active) {
        mh::drawScreen(s_screen, s_save, g);
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

// USB-free entry point for shipping builds (bead monhun-ardu-42n.8). Defining
// main() in the sketch keeps the core archive's main.cpp.o out of the link; that
// object is the only thing pulling USBDevice.attach()/serialEventRun and with
// them the whole CDC/PluggableUSB stack. -DMH_NO_USB is set for the shipping
// build/mini/size/debug flags only (see Makefile SIZE_FLAGS); the Ardens fxtest
// sketches keep the stock core main because their harness reads serial back.
// initVariant() is weak here exactly like the core's, so a variant override
// still wins. No serialEventRun(): the game never uses Serial.
#if defined(MH_NO_USB)
void initVariant() __attribute__((weak));
void initVariant() {
}
int __attribute__((OS_main)) main(void) {
    init();   // wiring: timers/PWM/ADC; Arduboy lib inits the OLED in setup()
    initVariant();
    setup();
    for (;;)
        loop();   // no serialEventRun
}
#endif
