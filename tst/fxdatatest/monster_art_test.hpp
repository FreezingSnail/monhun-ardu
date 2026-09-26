#pragma once
// On-device HEAVY long-tail overlay oracle (bead monhun-ardu-4t4).
//
// Renders the real drawMonster() through the shipping path (ArduboyG L4
// triplane, FX cart reads between plane blits) for a fixed HEAVY Game state and
// pins the tail pixels:
//
//   * east: the 24x16 fxtail_heavy overlay sits entirely LEFT of the 32x24 body
//     sprite (body at screen x=40 -> tail x=16..39), so ink at x=16 proves the
//     overlay is drawn offset at its appendage-zone anchor and x=15 stays clear
//   * the overlay snaps to the sprite facing frame, not the DIR8 hit-test
//     rotation: for west it lands at the cell mirror (monster_w - ox - w), so
//     the art stays glued to the baked part instead of detaching when the beast
//     faces west/N/S/diagonals
//   * west: the mirrored frame lands on the RIGHT of the body (x=72..95) while
//     the east tail band is empty -- the facing mirror really flips
//   * broken: the east/west stub frames keep ink at the body end and clear the
//     tip, proving combatPartArtFrame() selects the stage frame
//   * LUNGE has no appendage zone, so its east band stays clear (no overlay)
//
// Framebuffer layout (ArduboyG L4_Triplane): 128 B/page, pixel(x,y) =
// buf[(y >> 3) * 128 + x], bit y & 7. Body draw origin: rndPx(m.x)+HUD_H.
//
// Plane 0 lights BLACK/DARK/LIGHT/WHITE (mask+data), so the overlay shapes are
// checked there; plane 2 lights white only (the tip cap / root highlight).

#include "harness/fxtest.hpp"
#include "src/core/world.hpp"
#include "src/render.hpp"

#include <stdint.h>

namespace monsterart {

using namespace mh;

// Monster cell draw origin for monster x=40, y=20, cam 0: (40, 20 + HUD_H=8).
constexpr int16_t BX = 40;
constexpr int16_t BY = 20 + HUD_H;   // 28
// East intact tail band (zone origin 40-24, 28) and the west mirror snapped to
// the sprite cell: 32 - ox - w = 32+24-24 = 32 (origin 40+32), so the west
// overlay spans x=72..95 with its root (frame x0..9) against the sprite edge.
constexpr int16_t E_TAIL_X = BX - 24;   // 16
constexpr int16_t W_TAIL_X = BX + 32;   // 72
constexpr int16_t TAIL_Y = BY;          // 28
constexpr int16_t TAIL_W = 24;
constexpr int16_t TAIL_H = 16;

static void clearFb() {
    uint8_t *b = arduboy.getBuffer();
    for (uint16_t i = 0; i < 1024; i++)
        b[i] = 0;
}

static uint8_t bitAt(uint8_t x, uint8_t y) {
    return static_cast<uint8_t>((arduboy.getBuffer()[static_cast<uint16_t>(y >> 3) * 128 + x] >> (y & 7)) & 1);
}

static uint16_t countRegionBit(uint8_t xa, uint8_t ya, uint8_t w, uint8_t h) {
    uint16_t n = 0;
    for (uint8_t y = ya; y < ya + h; y++)
        for (uint8_t x = xa; x < xa + w; x++)
            n += bitAt(x, y);
    return n;
}

// Park the real drawMonster() on one plane with the OLED parked off, exactly
// like the game loop (cart reads only while OLED is disabled).
static void renderMonster(const Game &g, uint8_t plane) {
    while (arduboy.currentPlane() != plane) {
        FX::enableOLED();
        arduboy.waitForNextPlane();
        FX::disableOLED();
    }
    clearFb();
    drawMonster(g, 0, 0);
}

static void setupBeast(Game &g, int8_t kind, int8_t fx, int8_t fy) {
    newGame(g, W_SWORD, MODE_HUNT, kind);
    // Presence: the beast only exists in its home room (ridge for the heavy),
    // so the art suite loads it before parking the beast.
    loadRoom(g, beastHomeRoom(kind), zone::SPAWN_AREA_START);
    Monster &m = g.monster;
    m.x = BX;
    m.y = 20;
    m.subX = 0;
    m.subY = 0;
    m.fx = fx;
    m.fy = fy;
    m.state = MS_IDLE;
    m.stun = 0;
    m.hitFlash = 0;
    m.atkIdx = COMBAT_NO_ATTACK;
    // initGame does not clear the combat cache, and setup functions overwrite
    // state/atk without touching facing, so reset it here: a stale lock facing
    // would keep the shared spin-tail overlay / zone-skip paths live.
    g.combat.attack.facing = COMBAT_FACING_TRACK;
    g.tick = 0;
    g.combat.zoneBroken = 0;
}

// nch.3: park HEAVY in the locked tail_spin ACTIVE phase so drawMonster uses
// the rotating 8-frame fxtailspin body sheet (frame = dir8(lock facing) +
// t*8/active). The cached window box is shrunk to 1x1 at the body centre (ox/oy
// zeroed); the window no longer paints, so nothing lands in the centre band the
// quadrant checks exclude; `active` is pinned to 20 so t maps to known frames.
static void setupSpinAttack(Game &g, int8_t fx, int16_t t) {
    setupBeast(g, MON_HEAVY, fx, 0);
    attackLoad(g, combat::ATTACK_HEAVY_TAIL_SPIN);
    Monster &m = g.monster;
    m.state = MS_ATTACK;
    m.t = t;
    m.atkIdx = combat::ATTACK_HEAVY_TAIL_SPIN;
    g.combat.attack.facing = COMBAT_FACING_LOCK_AWAY;
    g.combat.attack.active = 20;
    g.combat.attack.win = combatWindowRead(combat::WINDOW_HEAVY_TAIL_SPIN_0);
    g.combat.attack.win.box.ox = 0;
    g.combat.attack.win.box.oy = 0;
    g.combat.attack.win.box.w = 1;
    g.combat.attack.win.box.h = 1;
}

// nch.2: park HEAVY in the locked tail_spin WINDUP with the FULL cached window
// (no shrink). The spin tell now draws in windup too, and the window box fill is
// gone, so a full-size box must not erase the plane-2 tip cap or paint any fill.
static void setupSpinWindup(Game &g, uint8_t window, int8_t fx, int8_t fy = 0) {
    setupBeast(g, MON_HEAVY, fx, fy);
    attackLoad(g, combat::ATTACK_HEAVY_TAIL_SPIN);
    Monster &m = g.monster;
    m.state = MS_WINDUP;
    m.atkIdx = combat::ATTACK_HEAVY_TAIL_SPIN;
    m.windupMax = 42;
    m.t = 38;   // (windupMax - t) / 4 == 1 -> odd -> no windup flash
    g.combat.attack.facing = COMBAT_FACING_LOCK_AWAY;
    g.combat.attack.win = combatWindowRead(window);   // full box, no shrink
}

// nch.8/prg.12: park the chicken (MON_LUNGE) in a peck/leap/wing_beat WINDUP or
// ATTACK so drawMonster swaps the generic BASE_BODY frame for the 6-frame
// fxchickenatk sheet (the attack record's artSheet/artFrame, bih.4). `tell` is
// the cached combat.attack.tell the selector reads during MS_WINDUP: an authored
// tell (1..3) picks that sheet slot, tell 0 (DOT) falls back to the attack's own
// artFrame (peck 0 / leap 2 / wing 4). The cached window box is shrunk to 1x1 at
// the body centre (like setupSpinAttack); the window no longer paints, so the
// centre band the facing checks exclude stays clear.
static void setupChickenAttack(Game &g, uint8_t atk, uint8_t state, int8_t fx, uint8_t tell = 0) {
    setupBeast(g, MON_LUNGE, fx, 0);
    attackLoad(g, atk);
    Monster &m = g.monster;
    m.state = state;
    m.atkIdx = atk;
    m.windupMax = 22;
    m.t = 20;   // windup flash phase, ignored by the overlay
    uint8_t win = combat::WINDOW_LUNGE_PECK_0;
    if (atk == combat::ATTACK_LUNGE_LEAP)
        win = combat::WINDOW_LUNGE_LEAP_0;
    else if (atk == combat::ATTACK_LUNGE_WING_BEAT)
        win = combat::WINDOW_LUNGE_WING_BEAT_0;
    g.combat.attack.tell = tell;
    g.combat.attack.win = combatWindowRead(win);
    g.combat.attack.win.box.ox = 0;
    g.combat.attack.win.box.oy = 0;
    g.combat.attack.win.box.w = 1;
    g.combat.attack.win.box.h = 1;
}

// nch.10/prg.12: park the bull (MON_SWEEP) in a stomp/gore/rear_kick WINDUP or
// ATTACK so drawMonster swaps the generic base frame for the 8-frame fxbullatk
// sheet (the attack record's artSheet/artFrame, bih.4). `tell` is the cached
// combat.attack.tell the selector reads during MS_WINDUP: authored tells pick
// their sheet slot (1 gore, 2 rear_kick, 3 stomp windup), tell 0 falls back to
// the attack's own artFrame (stomp 0 / gore 2 / rear_kick 4). The cached window
// box is shrunk to 1x1 at the body centre (the window no longer paints, so it
// stays clear of the pose checks).
static void setupBullAttack(Game &g, uint8_t atk, uint8_t state, int8_t fx, uint8_t tell = 0) {
    setupBeast(g, MON_SWEEP, fx, 0);
    attackLoad(g, atk);
    Monster &m = g.monster;
    m.state = state;
    m.atkIdx = atk;
    m.windupMax = 30;
    m.t = 20;   // windup flash phase, ignored by the overlay
    uint8_t win = combat::WINDOW_SWEEP_STOMP_0;
    if (atk == combat::ATTACK_SWEEP_GORE)
        win = combat::WINDOW_SWEEP_GORE_0;
    else if (atk == combat::ATTACK_SWEEP_REAR_KICK)
        win = combat::WINDOW_SWEEP_REAR_KICK_0;
    g.combat.attack.tell = tell;
    g.combat.attack.win = combatWindowRead(win);
    g.combat.attack.win.box.ox = 0;
    g.combat.attack.win.box.oy = 0;
    g.combat.attack.win.box.w = 1;
    g.combat.attack.win.box.h = 1;
}

// prg.12: park the longtail (MON_HEAVY) in a non-locked bite WINDUP so the
// selector swaps the generic BEAST_POSES frame for the 8-frame fxheavyatk sheet
// (the locked tail_spin/tail_slam keep the rotating fxtailspin sheet and never
// reach it). tell 1 (LINE) picks the bite windup slot; the shrunk 1x1 window no
// longer paints, so it stays out of the pose checks.
static void setupHeavyAttack(Game &g, uint8_t state, int8_t fx, uint8_t tell = 0) {
    setupBeast(g, MON_HEAVY, fx, 0);
    attackLoad(g, combat::ATTACK_HEAVY_BITE);
    Monster &m = g.monster;
    m.state = state;
    m.atkIdx = combat::ATTACK_HEAVY_BITE;
    m.windupMax = 30;
    m.t = 20;   // windup flash phase, ignored by the overlay
    g.combat.attack.tell = tell;
    g.combat.attack.win = combatWindowRead(combat::WINDOW_HEAVY_BITE_0);
    g.combat.attack.win.box.ox = 0;
    g.combat.attack.win.box.oy = 0;
    g.combat.attack.win.box.w = 1;
    g.combat.attack.win.box.h = 1;
}

// bih: the training pole is the first art-descriptor creature. Spawn the demo
// beast (to wire Game::target), then swap the combat caches to the pole record
// and park it at the standard cell origin. creatureLoad caches art.sheet = 1, so
// drawMonster routes through the generic art-table path.
static void setupPole(Game &g, int8_t fx, int8_t fy) {
    newGame(g, W_SWORD, MODE_HUNT, MON_LUNGE);
    creatureLoad(g, combat::CREATURE_POLE);
    Monster &m = g.monster;
    m.x = BX;
    m.y = 20;
    m.subX = 0;
    m.subY = 0;
    m.w = g.combat.body.w;
    m.h = g.combat.body.h;
    m.fx = fx;
    m.fy = fy;
    m.state = MS_IDLE;
    m.stun = 0;
    m.hitFlash = 0;
    m.atkIdx = COMBAT_NO_ATTACK;
    g.combat.attack.facing = COMBAT_FACING_TRACK;
    g.tick = 0;
    g.combat.zoneBroken = 0;
}

inline void test_monster_art(FxTest &test) {
    arduboy.startGray();

    static Game g;

    // ---- east intact: overlay offset left of the body, tip at x=16 row 37.
    setupBeast(g, MON_HEAVY, 16, 0);
    test.expectEq(g.combat.appendZone != COMBAT_NO_ZONE ? 1 : 0, 1, F("heavy has appendage zone"));
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 10, BY + 8), 1, F("body ink at cell"));
    test.expectEq(bitAt(E_TAIL_X, TAIL_Y + 9), 1, F("east tail tip plane0"));
    test.expectEq(bitAt(E_TAIL_X - 1, TAIL_Y + 9), 0, F("east tail offset clear one left"));
    test.expectEq(bitAt(E_TAIL_X + 23, TAIL_Y + 9), 1, F("east tail root plane0"));

    // ---- plane 2: the white tip cap and root highlight are drawn.
    renderMonster(g, 2);
    test.expectEq(bitAt(E_TAIL_X, TAIL_Y + 8), 1, F("east tail white cap plane2"));
    test.expectEq(bitAt(E_TAIL_X + 22, TAIL_Y + 5), 1, F("east tail root highlight plane2"));
    test.expectEq(bitAt(E_TAIL_X + 20, TAIL_Y + 9), 0, F("east tail body not white"));

    // ---- west intact: west frame on the right at the snapped cell mirror
    // (32 - ox - w = 32), east band empty.
    setupBeast(g, MON_HEAVY, -16, 0);
    renderMonster(g, 0);
    test.expectEq(bitAt(W_TAIL_X + 23, TAIL_Y + 9), 1, F("west tail tip plane0"));
    test.expectEq(bitAt(W_TAIL_X + 24, TAIL_Y + 9), 0, F("west tail offset clear one right"));
    test.expectEq(bitAt(W_TAIL_X, TAIL_Y + 9), 1, F("west tail root plane0"));
    test.expectEq(countRegionBit(static_cast<uint8_t>(E_TAIL_X), static_cast<uint8_t>(TAIL_Y), TAIL_W, TAIL_H), 0, F("west facing east band empty"));

    // ---- facing S (hunter under the tail): the overlay snaps to the east-frame
    // sprite (fx=0 -> east), so the tail stays behind the body instead of
    // rotating 24 px above it.
    setupBeast(g, MON_HEAVY, 0, 16);
    renderMonster(g, 0);
    test.expectEq(bitAt(E_TAIL_X, TAIL_Y + 9), 1, F("south-facing tail stays east plane0"));
    test.expectEq(countRegionBit(static_cast<uint8_t>(BX), static_cast<uint8_t>(BY - 24), TAIL_W, TAIL_H), 0, F("south-facing no tail above body"));

    // ---- broken stages: stub at the body end, tip cleared.
    setupBeast(g, MON_HEAVY, 16, 0);
    g.combat.zoneBroken = COMBAT_ZONE_APPENDAGE_BIT;
    renderMonster(g, 0);
    test.expectEq(bitAt(E_TAIL_X, TAIL_Y + 9), 0, F("east broken tip cleared"));
    test.expectEq(bitAt(E_TAIL_X + 19, TAIL_Y + 8), 1, F("east broken stub plane0"));

    setupBeast(g, MON_HEAVY, -16, 0);
    g.combat.zoneBroken = COMBAT_ZONE_APPENDAGE_BIT;
    renderMonster(g, 0);
    test.expectEq(bitAt(W_TAIL_X + 23, TAIL_Y + 9), 0, F("west broken tip cleared"));
    test.expectEq(bitAt(W_TAIL_X + 3, TAIL_Y + 8), 1, F("west broken stub plane0"));

    // ---- LUNGE declares a legs appendage zone (76y) but ships no overlay art
    // (render only draws HEAVY's tail): east band stays clear.
    setupBeast(g, MON_LUNGE, 16, 0);
    renderMonster(g, 0);
    test.expectEq(g.combat.appendZone != COMBAT_NO_ZONE ? 1 : 0, 1, F("lunge has a legs appendage zone"));
    test.expectEq(countRegionBit(static_cast<uint8_t>(E_TAIL_X), static_cast<uint8_t>(TAIL_Y), TAIL_W, TAIL_H), 0, F("lunge no overlay band"));

    // ---- nch.6 chicken legs: the LUNGE idle sheet's legs (thigh/knee/shank/
    // foot/toe) are DARK, so they light plane 0 on the device plane stack. The
    // leg band is cell x9..19, rows 18..22 (screen BX+9..BX+19, BY+18..BY+22);
    // the near shank cell x11 and far shank x16 must each carry plane-0 ink and
    // the cell x14 gap stays clear. The old shade-0 eraser legs left plane 0
    // empty, so the count would be 0 and the shank bits 0.
    setupBeast(g, MON_LUNGE, 16, 0);
    renderMonster(g, 0);
    test.expectEq(countRegionBit(BX + 9, BY + 18, 11, 5) > 0 ? 1 : 0, 1, F("lunge leg band plane0 ink"));
    test.expectEq(bitAt(BX + 11, BY + 20), 1, F("lunge near shank plane0"));
    test.expectEq(bitAt(BX + 16, BY + 20), 1, F("lunge far shank plane0"));
    test.expectEq(bitAt(BX + 14, BY + 20), 0, F("lunge leg gap clear"));

    // ---- nch.3 rotating spin body: during the locked tail_spin ACTIVE phase
    // drawMonster swaps the 32x24 E/W beast sheet for the 8-frame 40x40
    // fxtailspin sheet, centred on the body centre (60,42) with cell origin
    // (40,22). Frame = dir8(lock facing) + t*8/active, so east-facing (start8 0)
    // with active 20 maps t=2 -> frame0, t=5 -> frame2 (head bottom), t=10 ->
    // frame4 (head left), t=15 -> frame6 (head top). The WHITE head orbited
    // through the quadrants is the rotation signature (host suite pins the
    // sheet; these checks prove the device frame pick + centring).
    setupSpinAttack(g, 16, 2);   // frame0: head right
    renderMonster(g, 2);
    test.expectEq(countRegionBit(64, 22, 16, 40) > 0 ? 1 : 0, 1, F("spin t2 head right"));
    test.expectEq(countRegionBit(40, 22, 16, 40), 0, F("spin t2 head not left"));

    setupSpinAttack(g, 16, 5);   // frame2: head bottom
    renderMonster(g, 2);
    test.expectEq(countRegionBit(40, 46, 40, 16) > 0 ? 1 : 0, 1, F("spin t5 head bottom"));
    test.expectEq(countRegionBit(40, 22, 40, 16), 0, F("spin t5 head not top"));

    setupSpinAttack(g, 16, 10);   // frame4: head left
    renderMonster(g, 2);
    test.expectEq(countRegionBit(40, 22, 16, 40) > 0 ? 1 : 0, 1, F("spin t10 head left"));
    test.expectEq(countRegionBit(64, 22, 16, 40), 0, F("spin t10 head not right"));

    setupSpinAttack(g, 16, 15);   // frame6: head top
    renderMonster(g, 2);
    test.expectEq(countRegionBit(40, 22, 40, 16) > 0 ? 1 : 0, 1, F("spin t15 head top"));
    test.expectEq(countRegionBit(40, 46, 40, 16), 0, F("spin t15 head not bottom"));

    // The old 4-frame tail overlay is skipped in the attack phase and the
    // resting tail_heavy cap stays clear too (the rotating sheet carries it).
    test.expectEq(bitAt(48, 41), 0, F("spin attack old overlay cap clear"));
    test.expectEq(bitAt(E_TAIL_X, TAIL_Y + 8), 0, F("spin attack skips resting tail cap"));

    // ---- nch.2 windup tell: the spin overlay draws during MS_WINDUP too, and
    // the window box fill is gone. Window 1 (oy -22, north frame) is full size:
    // its box spans x52..67, y8..31, so the old shade-1 windup fill would ink
    // the x52..67 y10..17 region on at least one plane. It must stay clear on
    // every plane while the white spin cap survives at the body centre.
    setupSpinWindup(g, combat::WINDOW_HEAVY_TAIL_SPIN_1, 16);   // north frame
    uint16_t fillBits = 0;
    for (uint8_t plane = 0; plane < 3; plane++) {
        renderMonster(g, plane);
        fillBits = static_cast<uint16_t>(fillBits + countRegionBit(52, 10, 16, 8));
    }
    test.expectEq(fillBits, 0, F("windup no window box fill"));
    renderMonster(g, 2);
    test.expectEq(bitAt(59, 30), 1, F("windup spin north cap plane2"));
    test.expectEq(bitAt(59, 52), 0, F("windup north south cap clear"));
    // The resting tail_heavy overlay is skipped during the windup spin too.
    test.expectEq(bitAt(E_TAIL_X, TAIL_Y + 8), 0, F("windup spin skips resting tail cap"));

    // ---- nch.5 windup away-facing: during the windup the body is drawn from
    // the 8-frame fxtailspin sheet held at start8 = dir8(locked away facing), so
    // the head points away from the hunter and the tail at them for all 8
    // directions. Away facing south (start8 2) is frame 2 (east silhouette
    // rotated 90 deg clockwise): white head ink in the BOTTOM band of the 40x40
    // cell (y46..61), top band clear. The window-1 overlay's white cap sits at
    // (70,41), outside both bands. Pre-nch.5 the E/W beast sheet ignored fy and
    // put the head right, so the top band would not be clear.
    setupSpinWindup(g, combat::WINDOW_HEAVY_TAIL_SPIN_1, 0, 16);   // away south
    renderMonster(g, 2);
    test.expectEq(countRegionBit(40, 46, 40, 16) > 0 ? 1 : 0, 1, F("windup away south head bottom"));
    test.expectEq(countRegionBit(40, 22, 40, 16), 0, F("windup away south top band clear"));

    // ---- bih.4 attack-art oracle: every authored attack caches its whole-body
    // sheet/frame/mode from the attack record (attackLoad) -- the data drawMonster
    // now reads instead of the per-kind beastAtk ordinal chain. The sheet index
    // is the shared art_sheets table (6 chickenatk, 7 bullatk, 8 heavyatk,
    // 9 tailspin); the frame is the pre-doubled 2-facing pose base; mode 1 is
    // the locked spin. Proves the data switch matches the old ordinal math.
    attackLoad(g, combat::ATTACK_LUNGE_PECK);
    test.expectEq(g.combat.attack.artSheet, art_sheets::ART_SHEET_FXCHICKENATK, F("peck art sheet"));
    test.expectEq(g.combat.attack.artFrame, 0, F("peck art frame"));
    test.expectEq(g.combat.attack.artMode, 0, F("peck art mode"));
    attackLoad(g, combat::ATTACK_LUNGE_LEAP);
    test.expectEq(g.combat.attack.artSheet, art_sheets::ART_SHEET_FXCHICKENATK, F("leap art sheet"));
    test.expectEq(g.combat.attack.artFrame, 2, F("leap art frame"));
    attackLoad(g, combat::ATTACK_LUNGE_WING_BEAT);
    test.expectEq(g.combat.attack.artFrame, 4, F("wing_beat art frame"));
    attackLoad(g, combat::ATTACK_SWEEP_STOMP);
    test.expectEq(g.combat.attack.artSheet, art_sheets::ART_SHEET_FXBULLATK, F("stomp art sheet"));
    test.expectEq(g.combat.attack.artFrame, 0, F("stomp art frame"));
    attackLoad(g, combat::ATTACK_SWEEP_GORE);
    test.expectEq(g.combat.attack.artFrame, 2, F("gore art frame"));
    attackLoad(g, combat::ATTACK_SWEEP_REAR_KICK);
    test.expectEq(g.combat.attack.artFrame, 4, F("rear_kick art frame"));
    attackLoad(g, combat::ATTACK_HEAVY_BITE);
    test.expectEq(g.combat.attack.artSheet, art_sheets::ART_SHEET_FXHEAVYATK, F("bite art sheet"));
    test.expectEq(g.combat.attack.artFrame, 0, F("bite art frame"));
    test.expectEq(g.combat.attack.artMode, 0, F("bite art mode"));
    attackLoad(g, combat::ATTACK_HEAVY_TAIL_SLAM);
    test.expectEq(g.combat.attack.artSheet, art_sheets::ART_SHEET_FXHEAVYATK, F("tail_slam art sheet"));
    test.expectEq(g.combat.attack.artFrame, 4, F("tail_slam art frame"));
    attackLoad(g, combat::ATTACK_HEAVY_TAIL_SPIN);
    test.expectEq(g.combat.attack.artSheet, art_sheets::ART_SHEET_FXTAILSPIN, F("spin art sheet"));
    test.expectEq(g.combat.attack.artMode, 1, F("spin art mode"));

    // ---- nch.8/prg.12 chicken attack overlay: during the peck/leap/wing_beat
    // windup+attack drawMonster swaps the generic BEAST_POSES coil/lunge frame
    // for the 6-frame fxchickenatk sheet, frame = (ordinal << 1) | (west) with
    // ordinal 0 = peck, 1 = leap, 2 = wing_beat. During the windup an authored
    // tell (1..3) selects the slot directly; the peck's tell is dot (0), so it
    // keeps the peck ordinal. Peck and leap LOWER the head (peck to rows 4..9,
    // leap to rows 3..8) with the BLACK beak as an erase notch at the lowered
    // head's front edge. The peck keeps the feet planted (y21); the leap raises
    // the body to y2 and tucks the feet to y18 with the planted row clear. The
    // head is the only WHITE ink in the cell (tail/legs are dark/light), so plane
    // 2 reads its lowered rows, the high idle rows stay clear and the beak/eye
    // erase notches read as 0.
    setupChickenAttack(g, combat::ATTACK_LUNGE_PECK, MS_WINDUP, 16);   // peck E
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 12, BY + 21), 1, F("chicken peck east foot planted"));
    test.expectEq(bitAt(BX + 10, BY + 2), 0, F("chicken peck east body not raised"));
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 27, BY + 5), 1, F("chicken peck east lowered head white"));
    test.expectEq(bitAt(BX + 25, BY + 5), 0, F("chicken peck east eye black"));
    test.expectEq(bitAt(BX + 28, BY + 7), 0, F("chicken peck east beak notch"));
    test.expectEq(bitAt(BX + 25, BY + 1), 0, F("chicken peck east high head gone"));
    test.expectEq(countRegionBit(static_cast<uint8_t>(BX), static_cast<uint8_t>(BY), 12, 20), 0, F("chicken peck east left band clear"));

    setupChickenAttack(g, combat::ATTACK_LUNGE_LEAP, MS_ATTACK, 16);   // leap E
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 10, BY + 2), 1, F("chicken leap east body raised"));
    test.expectEq(bitAt(BX + 12, BY + 18), 1, F("chicken leap east feet tucked"));
    test.expectEq(bitAt(BX + 12, BY + 21), 0, F("chicken leap east planted row clear"));
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 27, BY + 5), 1, F("chicken leap east lowered head white"));
    test.expectEq(bitAt(BX + 28, BY + 7), 0, F("chicken leap east beak notch"));
    test.expectEq(bitAt(BX + 25, BY + 1), 0, F("chicken leap east high head gone"));

    setupChickenAttack(g, combat::ATTACK_LUNGE_PECK, MS_WINDUP, -16);   // peck W
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 18, BY + 21), 1, F("chicken peck west foot planted"));
    test.expectEq(bitAt(BX + 16, BY + 21), 0, F("chicken peck west foot gap clear"));
    renderMonster(g, 2);
    test.expectEq(countRegionBit(static_cast<uint8_t>(BX + 20), static_cast<uint8_t>(BY), 12, 20), 0, F("chicken peck west right band clear"));
    test.expectEq(countRegionBit(static_cast<uint8_t>(BX), static_cast<uint8_t>(BY), 12, 20) > 0 ? 1 : 0, 1, F("chicken peck west head left"));

    setupChickenAttack(g, combat::ATTACK_LUNGE_LEAP, MS_ATTACK, -16);   // leap W
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 18, BY + 18), 1, F("chicken leap west feet tucked"));
    test.expectEq(bitAt(BX + 18, BY + 21), 0, F("chicken leap west planted row clear"));

    // ---- prg.12 authored windup (selector): the wing_beat tell is arc (2) and
    // selects sheet slot 2 (frame 4 E). The near wing sweeps out behind as a
    // BLACK feather-row panel while the head stays level (rows 3..8) and the
    // feet stay planted. The art carries the whole tell.
    setupChickenAttack(g, combat::ATTACK_LUNGE_WING_BEAT, MS_WINDUP, 16, TELL_ARC);
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 27, BY + 5), 1, F("chicken wing windup level head white"));
    test.expectEq(bitAt(BX + 27, BY + 2), 0, F("chicken wing windup head not high"));
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 2, BY + 13), 0, F("chicken wing windup feather black eraser"));
    test.expectEq(bitAt(BX + 12, BY + 21), 1, F("chicken wing windup foot planted"));

    // ---- monhun-ardu-nup: the procedural 2x2/4x4 window-centre markers are gone
    // (telegraphs are sprite art only). The windup 2x2 rect sits at body centre
    // (56,40) -> x55..56, y39..40; the MS_ATTACK 4x4 surrounds it x54..57,
    // y38..41. Assert clear for an authored windup (arc slot 2) and a dot-tell
    // windup (attack pose), each on a plane the pose art does not reach: plane 1
    // for the dot pose (catches a stray shade-2 rect) and plane 2 for the arc pose
    // (white only; catches a stray shade-3 rect).
    setupChickenAttack(g, combat::ATTACK_LUNGE_PECK, MS_WINDUP, 16, TELL_DOT);
    renderMonster(g, 1);
    test.expectEq(countRegionBit(54, 38, 4, 4), 0, F("dot-tell windup no centre marker"));
    setupChickenAttack(g, combat::ATTACK_LUNGE_WING_BEAT, MS_WINDUP, 16, TELL_ARC);
    renderMonster(g, 2);
    test.expectEq(countRegionBit(54, 38, 4, 4), 0, F("authored windup no centre marker"));

    // ---- nch.10/prg.12 bull attack overlay: during the stomp/gore/rear_kick
    // windup+attack drawMonster swaps the generic BEAST_POSES coil/lunge frame
    // for the 8-frame fxbullatk sheet, frame = (ordinal << 1) | (west) with the
    // ordinal = tell slot during the windup (1 gore, 2 rear_kick, 3 stomp windup)
    // or the attack-order offset (0 stomp, 1 gore, 2 rear_kick) otherwise. The
    // release stomp holds the WHITE head high (plane2 at y4) with the front
    // hooves raised as BLACK erasers over the DARK chest (plane0 clear at y14 but
    // lit on the body behind); the gore lowers the head (plane2 at y17, clear
    // high), drives a white horn forward to (30,12) and raises the tail (plane0
    // at y3). Facing mirrors all of it.
    setupBullAttack(g, combat::ATTACK_SWEEP_STOMP, MS_WINDUP, 16);   // stomp E
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 25, BY + 4), 1, F("bull stomp east head high"));
    test.expectEq(bitAt(BX + 25, BY + 17), 0, F("bull stomp east head not low"));
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 16, BY + 14), 0, F("bull stomp east front hoof eraser"));
    test.expectEq(bitAt(BX + 16, BY + 8), 1, F("bull stomp east body behind hoof"));
    test.expectEq(bitAt(BX + 1, BY + 3), 0, F("bull stomp east tail not raised"));

    setupBullAttack(g, combat::ATTACK_SWEEP_GORE, MS_ATTACK, 16);   // gore E
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 25, BY + 17), 1, F("bull gore east head low"));
    test.expectEq(bitAt(BX + 25, BY + 4), 0, F("bull gore east head not high"));
    test.expectEq(bitAt(BX + 30, BY + 12), 1, F("bull gore east horn forward"));
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 16, BY + 14), 1, F("bull gore east no hoof eraser"));
    test.expectEq(bitAt(BX + 1, BY + 3), 1, F("bull gore east tail raised"));

    setupBullAttack(g, combat::ATTACK_SWEEP_STOMP, MS_WINDUP, -16);   // stomp W
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 6, BY + 4), 1, F("bull stomp west head high left"));
    test.expectEq(bitAt(BX + 25, BY + 4), 0, F("bull stomp west head right clear"));
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 16, BY + 14), 0, F("bull stomp west front hoof eraser"));

    setupBullAttack(g, combat::ATTACK_SWEEP_GORE, MS_ATTACK, -16);   // gore W
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 6, BY + 17), 1, F("bull gore west head low left"));
    test.expectEq(bitAt(BX + 1, BY + 12), 1, F("bull gore west horn forward"));
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 30, BY + 3), 1, F("bull gore west tail raised"));

    // ---- prg.12 authored windups (selector): the stomp tell is ring (3) and
    // selects sheet slot 3 (frame 6 E): reared on the planted hind legs with
    // both front hooves raised high and spread (BLACK erasers) and the head high
    // -- distinct from the release stomp's tucked, low hooves. The rear_kick
    // tell is arc (2) and selects slot 2 (frame 4 E): the hind legs kick back off
    // the ground while the front hooves stay planted and the head stays low.
    setupBullAttack(g, combat::ATTACK_SWEEP_STOMP, MS_WINDUP, 16, TELL_RING);
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 23, BY + 3), 1, F("bull stomp windup head high white"));
    test.expectEq(bitAt(BX + 25, BY + 17), 0, F("bull stomp windup head not low"));
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 13, BY + 4), 0, F("bull stomp windup near hoof eraser"));
    test.expectEq(bitAt(BX + 16, BY + 14), 1, F("bull stomp windup hoof row is body"));
    test.expectEq(bitAt(BX + 5, BY + 19), 1, F("bull stomp windup rear leg planted"));

    setupBullAttack(g, combat::ATTACK_SWEEP_REAR_KICK, MS_WINDUP, 16, TELL_ARC);
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 0, BY + 14), 0, F("bull rear_kick windup raised hoof eraser"));
    test.expectEq(bitAt(BX + 20, BY + 19), 1, F("bull rear_kick windup front leg planted"));
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 23, BY + 17), 1, F("bull rear_kick windup head low white"));
    test.expectEq(bitAt(BX + 25, BY + 4), 0, F("bull rear_kick windup head not high"));

    // ---- prg.12 heavy bite windup (selector): the non-locked bite tell is line
    // (1) and selects sheet slot 1 (frame 2 E): the head is drawn back and high
    // with the snout up (the raised head is the pose signature) while the tail
    // braces. The locked tail_spin/tail_slam keep the fxtailspin sheet.
    setupHeavyAttack(g, MS_WINDUP, 16, TELL_LINE);
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 25, BY + 6), 1, F("heavy bite windup head high white"));
    test.expectEq(bitAt(BX + 25, BY + 17), 0, F("heavy bite windup head not low"));
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 1, BY + 8), 1, F("heavy bite windup tail braced plane0"));

    // ---- kt7.6 breakable-zone part overlays: at rest (no attack sheet) each
    // breakable demo-roster zone draws its part overlay snapped to the sprite
    // facing frame (east at the authored box, west at the cell mirror
    // monster_w - ox - w), so the broken zone's shade-0 erase lands on the
    // baked part. The zone box origins are lunge head (18,0), lunge appendage
    // (9,0), sweep head (17,-4) and sweep appendage (4,12); west mirrors them
    // about the 32-px cell (32 - ox - w), never rotating with the DIR8 facing.

    // Chicken head east: overlay at (BX+18, BY), intact white head, broken
    // erases it and drops the dark stump.
    setupBeast(g, MON_LUNGE, 16, 0);
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 24, BY + 1), 1, F("chicken head east white crown"));
    test.expectEq(bitAt(BX + 24, BY - 1), 0, F("chicken head east above frame clear"));
    g.combat.zoneBroken = COMBAT_ZONE_HEAD_BIT;
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 24, BY + 1), 0, F("chicken broken head erased"));
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 19, BY + 2), 1, F("chicken broken neck stump plane0"));

    // Chicken head west: snapped to the cell mirror (origin 32-18-11 = 3), so
    // the overlay repaints the baked west head in-cell; the band left of the
    // cell the old rotation inked stays clear.
    setupBeast(g, MON_LUNGE, -16, 0);
    renderMonster(g, 2);
    test.expectEq(countRegionBit(static_cast<uint8_t>(BX + 3), static_cast<uint8_t>(BY), 11, 8) > 0 ? 1 : 0, 1, F("chicken head west overlay white"));
    test.expectEq(countRegionBit(static_cast<uint8_t>(BX - 18), static_cast<uint8_t>(BY), 11, 8), 0, F("chicken head west no rotated band"));
    g.combat.zoneBroken = COMBAT_ZONE_HEAD_BIT;
    renderMonster(g, 2);
    test.expectEq(countRegionBit(static_cast<uint8_t>(BX + 3), static_cast<uint8_t>(BY), 11, 8), 0, F("chicken broken west head erased"));
    setupBeast(g, MON_LUNGE, 16, 0);
    renderMonster(g, 2);
    test.expectEq(countRegionBit(static_cast<uint8_t>(BX - 18), static_cast<uint8_t>(BY), 11, 8), 0, F("chicken head east no west band"));

    // Chicken legs east: overlay at (BX+9, BY); broken erases the shank.
    setupBeast(g, MON_LUNGE, 16, 0);
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 9, BY + 21), 1, F("chicken legs east foot at zone origin"));
    g.combat.zoneBroken = COMBAT_ZONE_APPENDAGE_BIT;
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 11, BY + 20), 0, F("chicken broken shank erased"));
    test.expectEq(bitAt(BX + 11, BY + 13), 1, F("chicken broken thigh stump plane0"));

    // Chicken legs west: snapped mirror (origin 32-9-9 = 14); broken erases the
    // baked west shank at (BX+19, BY+20) and drops the stump at (BX+19, BY+13).
    setupBeast(g, MON_LUNGE, -16, 0);
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 19, BY + 20), 1, F("chicken legs west shank ink"));
    g.combat.zoneBroken = COMBAT_ZONE_APPENDAGE_BIT;
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 19, BY + 20), 0, F("chicken broken west shank erased"));
    test.expectEq(bitAt(BX + 19, BY + 13), 1, F("chicken broken west thigh stump plane0"));

    // Bull head east: overlay at (BX+17, BY-4) so the white horn mid lands at
    // (BX+23, BY+5); broken erases the horn band and keeps the head top.
    setupBeast(g, MON_SWEEP, 16, 0);
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 23, BY + 5), 1, F("bull head east horn white"));
    test.expectEq(bitAt(BX + 21, BY + 10), 1, F("bull head east head top white"));
    g.combat.zoneBroken = COMBAT_ZONE_HEAD_BIT;
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 23, BY + 5), 0, F("bull broken horn erased"));
    test.expectEq(bitAt(BX + 21, BY + 10), 1, F("bull broken head top stays"));
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 22, BY + 8), 1, F("bull broken horn stump plane0"));

    // Bull head west: snapped mirror (origin 32-17-12 = 3) keeps the white horn
    // at (BX+5, BY+4) and the head top at (BX+3, BY+10); the old rotated band
    // above-left of the cell stays clear.
    setupBeast(g, MON_SWEEP, -16, 0);
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 5, BY + 4), 1, F("bull head west horn white"));
    test.expectEq(bitAt(BX + 3, BY + 10), 1, F("bull head west head top white"));
    test.expectEq(countRegionBit(static_cast<uint8_t>(BX - 17), static_cast<uint8_t>(BY + 4), 12, 16), 0, F("bull head west no rotated band"));
    g.combat.zoneBroken = COMBAT_ZONE_HEAD_BIT;
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 5, BY + 4), 0, F("bull broken west horn erased"));
    test.expectEq(bitAt(BX + 3, BY + 10), 1, F("bull broken west head top stays"));
    setupBeast(g, MON_SWEEP, 16, 0);
    renderMonster(g, 2);
    test.expectEq(countRegionBit(static_cast<uint8_t>(BX - 17), static_cast<uint8_t>(BY + 4), 12, 16), 0, F("bull head east no west band"));

    // Bull hooves east: overlay at (BX+4, BY+12); broken cuts the shank.
    setupBeast(g, MON_SWEEP, 16, 0);
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 5, BY + 21), 1, F("bull hooves east leg at zone origin"));
    g.combat.zoneBroken = COMBAT_ZONE_APPENDAGE_BIT;
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 5, BY + 21), 0, F("bull broken shank erased"));
    test.expectEq(bitAt(BX + 5, BY + 18), 1, F("bull broken leg stump plane0"));

    // Bull hooves west: snapped mirror (origin 32-4-20 = 8); broken erases the
    // baked west far leg at (BX+24, BY+21) and drops the stump at (BX+24, BY+18).
    setupBeast(g, MON_SWEEP, -16, 0);
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 24, BY + 21), 1, F("bull hooves west far leg ink"));
    g.combat.zoneBroken = COMBAT_ZONE_APPENDAGE_BIT;
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 24, BY + 21), 0, F("bull broken west shank erased"));
    test.expectEq(bitAt(BX + 24, BY + 18), 1, F("bull broken west leg stump plane0"));
    test.expectEq(countRegionBit(static_cast<uint8_t>(BX - 4), static_cast<uint8_t>(BY - 12), 20, 16), 0, F("bull hooves west no rotated band"));
    setupBeast(g, MON_SWEEP, 16, 0);
    renderMonster(g, 0);
    test.expectEq(countRegionBit(static_cast<uint8_t>(BX - 4), static_cast<uint8_t>(BY - 12), 20, 16), 0, F("bull hooves east no west band"));

    // Facing N/S must not rotate the parts off the 2-facing sprite (fx=0 keeps
    // the east positions): the chicken head stays at (BX+18..29) and the bull
    // horns stay at (BX+17, BY-4), with the bands the rotation would ink clear.
    setupBeast(g, MON_LUNGE, 0, 16);
    renderMonster(g, 2);
    test.expectEq(countRegionBit(static_cast<uint8_t>(BX + 18), static_cast<uint8_t>(BY), 11, 8) > 0 ? 1 : 0, 1, F("chicken head south stays east"));
    test.expectEq(countRegionBit(static_cast<uint8_t>(BX), static_cast<uint8_t>(BY + 18), 11, 8), 0, F("chicken head south no rotated band"));

    setupBeast(g, MON_SWEEP, 0, 16);
    renderMonster(g, 2);
    test.expectEq(countRegionBit(static_cast<uint8_t>(BX + 4), static_cast<uint8_t>(BY + 17), 12, 16), 0, F("bull horns south no rotated band"));

    // ---- bih.5 zone part data: each beast's breakable zone caches its part
    // overlay sheet from the zone record (seeded by creatureLoad), so the
    // generic overlay loop selects the same art the per-kind chain did. The
    // heavy appendage, lunge head+appendage and sweep head+appendage carry a
    // 1-based art_sheets index; a zone without part art (heavy head, every
    // ravager/pole zone) packs 0 and draws no overlay.
    setupBeast(g, MON_HEAVY, 16, 0);
    test.expectEq(g.combat.zone[COMBAT_ZONE_APPENDAGE].partSheet, art_sheets::ART_SHEET_FXTAIL_HEAVY, F("heavy appendage part sheet"));
    test.expectEq(g.combat.zone[COMBAT_ZONE_HEAD].partSheet, 0, F("heavy head no part sheet"));
    setupBeast(g, MON_LUNGE, 16, 0);
    test.expectEq(g.combat.zone[COMBAT_ZONE_HEAD].partSheet, art_sheets::ART_SHEET_FXHEAD_CHICKEN, F("lunge head part sheet"));
    test.expectEq(g.combat.zone[COMBAT_ZONE_APPENDAGE].partSheet, art_sheets::ART_SHEET_FXLEGS_CHICKEN, F("lunge appendage part sheet"));
    setupBeast(g, MON_SWEEP, 16, 0);
    test.expectEq(g.combat.zone[COMBAT_ZONE_HEAD].partSheet, art_sheets::ART_SHEET_FXHEAD_BULL, F("sweep head part sheet"));
    test.expectEq(g.combat.zone[COMBAT_ZONE_APPENDAGE].partSheet, art_sheets::ART_SHEET_FXHOOVES_BULL, F("sweep appendage part sheet"));
    setupBeast(g, MON_RAVAGER, 16, 0);
    test.expectEq(g.combat.zone[COMBAT_ZONE_HEAD].partSheet, 0, F("ravager head no part sheet"));
    test.expectEq(g.combat.zone[COMBAT_ZONE_APPENDAGE].partSheet, 0, F("ravager appendage no part sheet"));
    setupPole(g, 16, 0);
    test.expectEq(g.combat.zone[COMBAT_ZONE_HEAD].partSheet, 0, F("pole head no part sheet"));

    // ---- bih.5 generic overlay skip: whenever an attack-art sheet is the
    // active whole-body draw (artSheet != 0), every zone overlay is suppressed
    // -- the sheet already carries the posed part. HEAVY's non-locked bite is
    // mode 0 art (fxheavyatk at the cell origin), so the resting tail overlay's
    // east band (left of the 32-px cell, engine x16..39) must stay clear even
    // though it inks that band at rest. (The locked-spin skip is pinned above.)
    setupHeavyAttack(g, MS_ATTACK, 16, 0);
    renderMonster(g, 0);
    test.expectEq(countRegionBit(static_cast<uint8_t>(E_TAIL_X), static_cast<uint8_t>(TAIL_Y), TAIL_W, TAIL_H), 0, F("heavy attack skips tail overlay"));

    // ---- bih phase 1 base-body oracle: the 4 beasts' BASE draw (no attack
    // sheet) must be pixel-identical before and after their sheet/layout moves
    // to art descriptors. Three facts per beast, all chosen OUTSIDE that beast's
    // zone-overlay boxes so the pin isolates the baked body frame:
    //   idle:  the DARK body lights plane 0 and not plane 2
    //   flash: hitFlash > 0 swaps in the WHITE flash frame (plane 2 ink)
    //   west:  fx < 0 selects the mirrored frame (the east mark clears and the
    //          west mark / head-vs-body shade appears)
    // LUNGE/SWEEP/HEAVY share the 7-frame layout (idle0 0, +2 idle, flash +5,
    // stride 7); RAVAGER holds idle0 for windup/attack with stride 4.
    // Chicken (LUNGE) tail plume x3: dark idle, white flash, mirror to x29.
    setupBeast(g, MON_LUNGE, 16, 0);
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 3, BY + 8), 1, F("lunge idle tail plane0"));
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 3, BY + 8), 0, F("lunge idle tail not white"));
    setupBeast(g, MON_LUNGE, 16, 0);
    g.monster.hitFlash = 4;
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 3, BY + 8), 1, F("lunge flash tail white"));
    setupBeast(g, MON_LUNGE, -16, 0);
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 3, BY + 8), 0, F("lunge idle west tail cleared"));
    test.expectEq(bitAt(BX + 29, BY + 8), 1, F("lunge idle west tail plane0"));

    // Bull (SWEEP) tail x2 y12: dark body east; the west frame's head lands over
    // the same cell (white), so plane 2 flips with the facing.
    setupBeast(g, MON_SWEEP, 16, 0);
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 2, BY + 12), 1, F("sweep idle tail plane0"));
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 2, BY + 12), 0, F("sweep idle tail not white"));
    setupBeast(g, MON_SWEEP, 16, 0);
    g.monster.hitFlash = 4;
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 2, BY + 12), 1, F("sweep flash tail white"));
    setupBeast(g, MON_SWEEP, -16, 0);
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 2, BY + 12), 1, F("sweep idle west head white"));

    // Longtail (HEAVY) tail base x1 y14: dark idle, white flash, mirror to x29
    // (x1 y14 sits below the west head/snout at y3..12, so only the east frame
    // inks it; the 24x16 appendage overlay sits left of the cell entirely).
    setupBeast(g, MON_HEAVY, 16, 0);
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 1, BY + 14), 1, F("heavy idle tail plane0"));
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 1, BY + 14), 0, F("heavy idle tail not white"));
    setupBeast(g, MON_HEAVY, 16, 0);
    g.monster.hitFlash = 4;
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 1, BY + 14), 1, F("heavy flash tail white"));
    setupBeast(g, MON_HEAVY, -16, 0);
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 1, BY + 14), 0, F("heavy idle west tail cleared"));
    test.expectEq(bitAt(BX + 29, BY + 14), 1, F("heavy idle west tail plane0"));

    // Ravager (legacy fxmonster sheet) body x10 y10: dark idle, white flash; the
    // east head (white) at x24 y8 flips to the west body (dark) under the mirror.
    setupBeast(g, MON_RAVAGER, 16, 0);
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 10, BY + 10), 1, F("ravager idle body plane0"));
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 10, BY + 10), 0, F("ravager idle body not white"));
    setupBeast(g, MON_RAVAGER, 16, 0);
    g.monster.hitFlash = 4;
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 10, BY + 10), 1, F("ravager flash body white"));
    setupBeast(g, MON_RAVAGER, 16, 0);
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 24, BY + 8), 1, F("ravager idle east head white"));
    setupBeast(g, MON_RAVAGER, -16, 0);
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 24, BY + 8), 0, F("ravager idle west body not white"));

    // ---- bih generic art-descriptor path: the pole draws from the fxpole
    // sheet (20x40 at the body origin, anchorY 0) instead of any beast sheet.
    // Idle frame 0 has a LIGHT head (rows 0..15) and a DARK shaft (rows 12..35)
    // with the BLACK ground plate at rows 34..35; the legacy 32x24 beast cell is
    // only 20 px wide for the pole, so its right band (x BX+20..BX+31) must stay
    // clear of the 32-px beast sheet, and the pole's below-cell shaft proves the
    // 40-tall sheet blitted. hitFlash > 0 selects the flash frame (WHITE head).
    setupPole(g, 16, 0);
    test.expectEq(g.combat.art.sheet, art_sheets::ART_SHEET_FXPOLE, F("pole art sheet cached"));
    renderMonster(g, 0);
    test.expectEq(bitAt(BX + 5, BY + 2), 1, F("pole idle head light plane0"));
    test.expectEq(bitAt(BX + 5, BY + 26), 1, F("pole shaft below beast cell plane0"));
    test.expectEq(countRegionBit(static_cast<uint8_t>(BX + 20), static_cast<uint8_t>(BY), 12, 24), 0, F("no beast sheet in cell"));
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 5, BY + 2), 0, F("pole idle head not white"));

    setupPole(g, 16, 0);
    g.monster.hitFlash = 4;
    renderMonster(g, 2);
    test.expectEq(bitAt(BX + 5, BY + 2), 1, F("pole hit flash white plane2"));
    renderMonster(g, 0);
    test.expectEq(countRegionBit(static_cast<uint8_t>(BX + 20), static_cast<uint8_t>(BY), 12, 24), 0, F("no beast sheet flash in cell"));
}

}   // namespace monsterart
