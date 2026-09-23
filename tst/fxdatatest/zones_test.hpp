#pragma once
// On-device room-image / prop / fade suite (bead monhun-ardu-fie.5).
//
// fie.8 carve: shipping now defaults to the procedural dot ground
// (MH_ROOM_IMAGE 0) and compiles the stored-image blit out. This suite is the
// permanent blit-correctness evidence, so it forces the image path back on
// before the first include of render.hpp in this TU. Blob/doors/props/fade
// assertions below still run (props + fade are shared by both ground paths).
#define MH_ROOM_IMAGE 1
//
// The fie.7 blit spike only modelled the vertical split and never captured a
// pixel; this suite is the end-to-end evidence:
//
//   1. reads the mhZones blob header + the camp room record straight off the
//      cart (zone_meta.hpp symbolic offsets) and pins the values;
//   2. camp view (128x56, camY == 0): drawRoom must page-copy each source layer
//      page into framebuffer pages 1..7 and leave page 0 (HUD) untouched. The
//      expected bytes are read back from the same layer with a *different*
//      reader (FX::readDataBytes), so a bad asm data path cannot self-confirm;
//   3. area view (384x112, camX=64, camY=52 -> v == 4, q0 == 6): the shifted
//      window must equal the page-major decomposition
//        dst[j] = (src[q0+j-1] >> v) | (src[q0+j] << (8-v))
//      computed here in C, page by page, on plane 1 (proves per-plane seek);
//   4. fade wipe: drawFade black-clears the arena pages and spares page 0;
//   5. door-cross smoke: updateDoors routes camp -> area and arms Game::fade.
//
// Framebuffer layout (ArduboyG L4_Triplane): 128 B/page, pixel(x,y) =
// buf[(y >> 3) * 128 + x], bit y & 7.

#include "harness/fxtest.hpp"
#include "src/core/world.hpp"
#include "src/render.hpp"
#include "src/app_state.hpp"   // hub app routing (fie.6/isp.1)
#include "src/app_setup.hpp"   // huntStart: save weapon + quest kind (isp.1)

#include <stdint.h>

namespace zones {

using namespace mh;

static const Input Z_IDLE = {0, 0, false, false};
static const Input Z_B = {0, 0, false, true};
static Game s_g;
// Static comparison buffers: two Games + local rows would overrun the 2.5 KB
// AVR stack (measured), so keep them out of the frame.
static uint8_t s_page[128];
static uint8_t s_hi[128];

static void clearFb() {
    uint8_t *b = arduboy.getBuffer();
    for (uint16_t i = 0; i < 1024; i++)
        b[i] = 0;
}

static void fillFb(uint8_t v) {
    uint8_t *b = arduboy.getBuffer();
    for (uint16_t i = 0; i < 1024; i++)
        b[i] = v;
}

// Park after the OLED blit exactly like the shipping loop / asset_test.
static void park() {
    FX::enableOLED();
    arduboy.waitForNextPlane();
    FX::disableOLED();
}

static void syncPlane(uint8_t want) {
    while (arduboy.currentPlane() != want)
        park();
}

// Count bytes equal to `want` on one framebuffer page.
static uint16_t countPageEq(uint8_t page, uint8_t want) {
    const uint8_t *b = arduboy.getBuffer() + static_cast<uint16_t>(page) * 128;
    uint16_t n = 0;
    for (uint8_t x = 0; x < 128; x++) {
        if (b[x] == want)
            n++;
    }
    return n;
}

// First column where the framebuffer page differs from `want`, 0xFF when the
// page matches byte-for-byte.
static uint8_t firstDiff(const uint8_t *want, uint8_t page) {
    const uint8_t *b = arduboy.getBuffer() + static_cast<uint16_t>(page) * 128;
    for (uint8_t x = 0; x < 128; x++) {
        if (b[x] != want[x])
            return x;
    }
    return 0xFF;
}

// Pixel (x, y) lit on the current plane (page-major bit layout).
static uint8_t bitAt(uint8_t x, uint8_t y) {
    return static_cast<uint8_t>((arduboy.getBuffer()[static_cast<uint16_t>(y >> 3) * 128 + x] >> (y & 7)) & 1);
}

inline void test_zones(FxTest &test) {
    // Plane ISR drives waitForNextPlane/currentPlane (as in test_perf/hud).
    // newGame() before startGray mirrors hud_test's ordering.
    Game &g = s_g;
    arduboy.startGray();
    newGame(g, W_SWORD, MODE_HUNT);
    park();   // one bracket so every later cart read sits between plane blits

    // ------------------------------------------------ 1. mhZones blob header
    FX::seekData(mhZones);
    const uint8_t magicLo = FX::readPendingUInt8();
    const uint8_t magicHi = FX::readPendingUInt8();
    const uint8_t version = FX::readPendingUInt8();
    const uint8_t flags = FX::readEnd();
    test.expectEq(magicLo, zone::MAGIC & 0xFF, F("blob magic lo"));
    test.expectEq(magicHi, zone::MAGIC >> 8, F("blob magic hi"));
    test.expectEq(version, zone::VERSION, F("blob version"));
    test.expectEq(flags, zone::FLAGS, F("blob flags"));

    // Camp room record (w/h + prop range) at the generated blob offset.
    FX::seekData(mhZones + zone::ROOM_CAMP_OFF + zone::ROOM_W_OFF);
    const uint16_t campW = static_cast<uint16_t>(FX::readPendingUInt8()) | static_cast<uint16_t>(FX::readEnd()) << 8;
    FX::seekData(mhZones + zone::ROOM_CAMP_OFF + zone::ROOM_H_OFF);
    const uint16_t campH = static_cast<uint16_t>(FX::readPendingUInt8()) | static_cast<uint16_t>(FX::readEnd()) << 8;
    FX::seekData(mhZones + zone::ROOM_CAMP_OFF + zone::ROOM_PROP_COUNT_OFF);
    const uint8_t campProps = FX::readEnd();
    test.expectEq(campW, zone::ROOM_CAMP_W, F("camp record w"));
    test.expectEq(campH, zone::ROOM_CAMP_H, F("camp record h"));
    test.expectEq(campProps, 5, F("camp prop count"));

    // Gather nodes (beads monhun-ardu-feel.21 + prg.4): camp's tent (local 0) is
    // a plain prop, the herbs (1, 2) and the mushroom (3) carry the item + yield.
    const ZoneProp campTent = zonePropRead(zone::PROP_CAMP_0);
    test.expectEq(campTent.gatherItem, zone::GATHER_NONE, F("camp tent gather none"));
    test.expectEq(campTent.gatherYield, 0, F("camp tent gather yield 0"));
    const ZoneProp campHerb0 = zonePropRead(zone::PROP_CAMP_1);
    test.expectEq(campHerb0.gatherItem, zone::GATHER_HERB, F("camp herb item"));
    test.expectEq(campHerb0.gatherYield, 1, F("camp herb 0 yield"));
    const ZoneProp campHerb1 = zonePropRead(zone::PROP_CAMP_2);
    test.expectEq(campHerb1.gatherYield, 2, F("camp herb 1 yield"));
    const ZoneProp campShroom = zonePropRead(zone::PROP_CAMP_3);
    test.expectEq(campShroom.gatherItem, zone::GATHER_BLUE_MUSHROOM, F("camp mushroom item"));
    test.expectEq(campShroom.gatherYield, 1, F("camp mushroom yield"));
    // Area props: herbs (0..2), mushrooms (3, 4), ore (5, 6), bug (7).
    const ZoneProp areaHerb = zonePropRead(zone::PROP_AREA_0);
    test.expectEq(areaHerb.gatherItem, zone::GATHER_HERB, F("area herb item"));
    test.expectEq(areaHerb.gatherYield, 1, F("area herb yield"));
    const ZoneProp areaShroom = zonePropRead(zone::PROP_AREA_3);
    test.expectEq(areaShroom.gatherItem, zone::GATHER_BLUE_MUSHROOM, F("area mushroom item"));
    test.expectEq(areaShroom.gatherYield, 1, F("area mushroom yield"));
    const ZoneProp areaOre = zonePropRead(zone::PROP_AREA_5);
    test.expectEq(areaOre.gatherItem, zone::GATHER_ORE, F("area ore item"));
    test.expectEq(areaOre.gatherYield, 1, F("area ore yield"));
    const ZoneProp areaBug = zonePropRead(zone::PROP_AREA_7);
    test.expectEq(areaBug.gatherItem, zone::GATHER_BUG, F("area bug item"));
    test.expectEq(areaBug.gatherYield, 1, F("area bug yield"));

    // ------------------------------------------ 2. camp view: v == 0 copy
    loadRoom(g, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
    test.expectEq(g.camX, 0, F("camp camX pinned"));
    test.expectEq(g.camY, 0, F("camp camY pinned"));
    test.expectEq(g.roomPropCount, 5, F("camp prop range cached"));

    syncPlane(0);
    test.expectEq(arduboy.currentPlane(), 0, F("camp on plane 0"));
    clearFb();
    drawRoom(g, g.camX, g.camY);

    test.expectEq(countPageEq(0, 0x00), 128, F("camp page0 still HUD"));
    for (uint8_t q = 0; q < 7; q++) {
        FX::readDataBytes(mh_map_camp + static_cast<uint24_t>(q) * zone::ROOM_CAMP_W, s_page, 128);
        test.expectEq(firstDiff(s_page, static_cast<uint8_t>(1 + q)), 0xFF, F("camp plane0 page copy"));
    }

    // Props: the tent (32x24 at room 40,8) is an FX sprite over the blit. Plane
    // 0 lights every shade, so the white apex/pole and the dark body both ink.
    clearFb();
    drawProps(g, g.camX, g.camY);
    test.expectEq(bitAt(56, 17), 1, F("camp tent apex ink"));
    test.expectEq(bitAt(48, 32), 1, F("camp tent body ink"));
    test.expectEq(bitAt(0, 40), 0, F("camp prop clear off-box"));

    // Area props: the herb gather nodes draw math from the zone records, not a
    // sheet, so the tent above is the only shipped sprite prop (prg.8 removed
    // the pole room + its fxpole prop record).

    // ------------------------------------- 3. area view: v == 4 shifted window
    loadRoom(g, zone::ROOM_AREA, zone::SPAWN_AREA_START);
    const int16_t rx = 64;
    const int16_t ry = 52;   // 52 & 7 == 4, 52 >> 3 == 6
    const uint8_t v = static_cast<uint8_t>(ry & 7);
    const uint8_t q0 = static_cast<uint8_t>(ry >> 3);
    test.expectEq(v, 4, F("area probe v"));
    test.expectEq(q0, 6, F("area probe q0"));

    syncPlane(1);
    test.expectEq(arduboy.currentPlane(), 1, F("area on plane 1"));
    clearFb();
    drawRoom(g, rx, ry);

    test.expectEq(countPageEq(0, 0x00), 128, F("area page0 still HUD"));
    const uint24_t areaLayer1 = mh_map_area + static_cast<uint24_t>(zone::ROOM_AREA_IMAGE_LAYER_BYTES);
    for (uint8_t j = 1; j <= 7; j++) {
        FX::readDataBytes(areaLayer1 + static_cast<uint24_t>(q0 + j - 1) * zone::ROOM_AREA_W + static_cast<uint16_t>(rx), s_page, 128);
        FX::readDataBytes(areaLayer1 + static_cast<uint24_t>(q0 + j) * zone::ROOM_AREA_W + static_cast<uint16_t>(rx), s_hi, 128);
        for (uint8_t x = 0; x < 128; x++)
            s_page[x] = static_cast<uint8_t>(static_cast<uint8_t>(s_page[x] >> v) | static_cast<uint8_t>(s_hi[x] << (8 - v)));
        test.expectEq(firstDiff(s_page, j), 0xFF, F("area plane1 split page"));
    }

    // ------------------------------------------------------ 4. fade wipe
    fillFb(0xFF);
    g.fade = FADE_TICKS;
    drawFade(g);
    test.expectEq(countPageEq(0, 0xFF), 128, F("fade spares HUD page"));
    for (uint8_t p = 1; p <= 7; p++)
        test.expectEq(countPageEq(p, 0x00), 128, F("fade clears arena page"));
    // fie.9 collapsed the wipe to a constant full-arena shade-0 blk: any armed
    // tick clears the whole arena band (was a growing wipe), HUD still spared.
    fillFb(0xFF);
    g.fade = 1;
    drawFade(g);
    test.expectEq(countPageEq(0, 0xFF), 128, F("armed fade spares HUD page"));
    for (uint8_t p = 1; p <= 7; p++)
        test.expectEq(countPageEq(p, 0x00), 128, F("armed fade covers the arena"));
    g.fade = 0;
    fillFb(0xFF);
    drawFade(g);
    test.expectEq(countPageEq(1, 0xFF), 128, F("fade 0 is a no-op"));

    // ---------------------------------------- 5. door-cross transition smoke
    // Reuse s_g (newGame resets it) instead of a second Game on the stack/bss.
    Game &t = g;
    newGame(t, W_SWORD, MODE_HUNT);
    loadRoom(t, zone::ROOM_CAMP, zone::SPAWN_CAMP_ENTRY);
    t.player.x = 60;   // clear of every door rect: drop the arrival latch
    t.player.y = 44;
    stepGame(t, Z_IDLE);
    test.expectEq(t.doorLatch, 0, F("camp latch cleared"));
    t.player.x = 120;   // camp door (120,24,8,24) -> area
    t.player.y = 24;
    stepGame(t, Z_IDLE);
    test.expectEq(t.roomId, zone::ROOM_AREA, F("door cross lands in area"));
    test.expectEq(t.fade, FADE_TICKS, F("arrival arms the wipe"));
    stepGame(t, Z_IDLE);
    test.expectEq(t.fade, FADE_TICKS - 1, F("wipe decays per tick"));

    // -------------------------------------------- 6. live app flow (fie.6/isp.1)
    // boot -> hub -> HUNT -> camp; camp hold-B -> Game::menuRequest -> hub;
    // area door -> camp. Drives the shipping src/app_state.hpp router.
    ScreenState screen;
    SaveBlock save;
    saveDefaults(save);
    Game &d = g;
    appNavApply(APP_NAV_HUB, screen, save, d, Z_IDLE);
    test.expectEq(static_cast<uint32_t>(screen.active), 1, F("hub active"));
    test.expectEq(static_cast<uint32_t>(screen.screen), screens::SCREEN_HUB, F("on the hub"));
    ScreenRow huntRow;
    huntRow.action = screens::ACTION_HUNT;   // hub row 0 (no cart read needed)
    test.expectEq(static_cast<uint32_t>(appNavApply(appScreenAccept(screens::SCREEN_HUB, huntRow), screen, save, d, Z_IDLE)), 1, F("hub HUNT requests the hunt"));
    huntStart(d, save);   // the sketch's device glue starts the world
    test.expectEq(static_cast<uint32_t>(d.roomId), zone::ROOM_CAMP, F("hunt starts in camp"));
    test.expectEq(static_cast<uint32_t>(roomIsSafe(d)), 1, F("camp is safe"));

    d.player.sheathed = true;
    d.player.sheatheLatch = false;
    d.menuRequest = false;
    for (int16_t i = 0; i < HOLD_TICKS; i++)
        stepGame(d, Z_B);
    test.expectEq(static_cast<uint32_t>(d.menuRequest), 1, F("camp hold-B requests the hub"));
    test.expectEq(static_cast<uint32_t>(appHubRequest(d)), APP_NAV_HUB, F("request routes to the hub"));
    test.expectEq(static_cast<uint32_t>(d.menuRequest), 0, F("request consumed once"));
    appNavApply(APP_NAV_HUB, screen, save, d, Z_B);
    test.expectEq(static_cast<uint32_t>(screen.active), 1, F("camp exit opens the hub"));

    // huntStart from a save picks the loadout + kill-target beast, and the camp
    // door still leads to the area hunt (prg.8 removed the pole room).
    SaveBlock beast;
    saveDefaults(beast);
    beast.equippedNode = forge::NODE_GUN_BASE;   // save v5: equip the gun root
    beast.activeQuest = 1;                       // slay_sweep (kill, target sweep)
    huntStart(d, beast);
    test.expectEq(static_cast<uint32_t>(d.mode), MODE_HUNT, F("beast hunt mode"));
    test.expectEq(static_cast<uint32_t>(d.weapon), W_GUN, F("beast hunt weapon"));
    test.expectEq(static_cast<uint32_t>(d.monsterKind), MON_SWEEP, F("quest kill-target beast"));
    test.expectEq(static_cast<uint32_t>(d.roomId), zone::ROOM_CAMP, F("beast starts in camp"));
}

}   // namespace zones
