#pragma once
// Shared block-art renderer. This is the exact draw stack the device loop runs
// (previously inlined in monhun-ardu.ino); it lives in a header so the on-device
// perf bench (tst/fxdatatest/perf_test.hpp) can time the real render path instead
// of a copy that could drift.
//
// Purely presentational: render reads a Game snapshot and never mutates it. The
// core sim (src/core/*) is untouched by this move.

#include "common.hpp"
#include "fxdata.h"
#include "core/world.hpp"
#include "core/sin256.hpp"            // 256 B sine LUT -> 65 B quarter wave + sign folding (42n.7)
#include "generated/art_dims.hpp"     // frame layout + core dims for the FX sheets
#include "generated/equip_meta.hpp"   // gen-art part tables (sheet/frame/anchor) for drawPlayer

#ifndef DEBUG_HURTBOXES
#define DEBUG_HURTBOXES 0
#endif

namespace mh {

/* ---------------------------------------------------------------- sprites */

// Frame indices into the FX sprite sheets authored by tools/gen-art.py. Sheets
// are left-to-right strips and FRAME(i) == i*3 + currentPlane() selects the
// current plane's data, so one draw call per plane composites the 4 shades.
namespace spr {
// 32x24 monster sheets (epic monhun-ardu-nch): one sheet per demo beast, all
// sharing this frame layout -- four states facing east, then the same four
// facing west. Which sheet is drawn is picked by the roster kind
// (monsterSheet() below), so the state/frame math stays single-sourced.
constexpr uint8_t MON_IDLE = 0;
constexpr uint8_t MON_RECOVER = 1;
constexpr uint8_t MON_FLASH = 2;
constexpr uint8_t MON_DEAD = 3;
constexpr uint8_t MON_WEST = 4;

// 20x40 pole (20x36 art, padded): black-eyed head normal / hit flash.
constexpr uint8_t POLE_NORMAL = 0;
constexpr uint8_t POLE_FLASH = 1;

// 4x4 spark, light gray / white.
constexpr uint8_t SPARK_LIGHT = 0;
constexpr uint8_t SPARK_BRIGHT = 1;

// Overlay/effect sheets (bead monhun-ardu-42n.2); the player overlays moved to
// the generated gen-art part tables (bead monhun-ardu-abr, see drawPlayer), so
// only the monster/effect sheets keep named frames here. Anchors are documented
// at each draw site in drawMonster/drawProjectiles.
// fxwhirl 8x4: 2x2 light orbit dot, 4x4 white ball, 1x1 light chain dot,
// 2x2 white stun sparkle.
constexpr uint8_t WHIRL_DOT = art_dims::whirl_dot_frame;
// fxtrail 4x4: 2x2 light puff (frame 0) / dark puff (frame 1).
constexpr uint8_t TRAIL_LIGHT = 0;
constexpr uint8_t TRAIL_DARK = 1;
// fxtail_spin 24x24 (heavy's tail_spin overlay): the tail rooted at the body
// centre, pointing world W / N / E / S. The frame is picked from the world
// direction of the active window's face-relative offset (drawMonster).
constexpr uint8_t SPIN_WEST = 0;
constexpr uint8_t SPIN_NORTH = 1;
constexpr uint8_t SPIN_EAST = 2;
constexpr uint8_t SPIN_SOUTH = 3;
}   // namespace spr

// Cull fully off-screen sprites before paying the FX seek, then blit on the
// current plane. Max sheet size is 32x40 (fxmonster 32x24, fxpole 20x40), so
// these bounds stay conservative.
static inline void sprDraw(uint24_t img, int16_t x, int16_t y, uint8_t frame) {
    if (x <= -32 || x >= mh::SCREEN_W || y <= -40 || y >= mh::SCREEN_H)
        return;
    SpritesU::drawPlusMaskFX(x, y, img, frame);
}

/* ------------------------------------------------------------------ art */

// Block-art render, ported from mock/game.js render section (source of truth).
// Shades 0..3 map 1:1 onto the L4 triplane levels:
//   0 BLACK       no plane
//   1 DARK_GRAY   plane 0
//   2 LIGHT_GRAY  planes 0+1
//   3 WHITE       planes 0+1+2
// ArduboyG's fillRect(x, y, w, h, shade) does the plane conversion, so the
// exact same shapes are drawn on every plane (no divergent draw) and a single
// Game snapshot feeds all three passes. Read-only: render never mutates Game.
//
// HUD is reserved at the top (y 0..HUD_H-1); world y=0 maps to screen y=HUD_H
// and world blocks are clipped to the arena band. The mock put the HUD at the
// bottom; the device flips it to the top (established by the loop bead), so
// the arena shifts down by HUD_H px. drawHud() paints the strip untranslated
// (no camera/shake) at the top, mirrored from the mock's bottom strip, and uses
// the HUD-band rect path (hudBlk) so its rows 0..HUD_H-1 are paintable.

// round(v + sub/16): sub is the 1/16 px remainder, matches mock Math.round().
static __attribute__((noinline)) int16_t rndPx(int16_t v, int16_t sub) {
    return static_cast<int16_t>((v * 16 + sub + 8) >> 4);
}

// round((a*b)/16) for Q4 vectors: matches Math.round() of the mock's float
// product for every sign (arithmetic shift floors (a*b+8)/16). Inputs are
// table values |a| <= 16 and small radii |b| <= 20, so the product fits int16.
static inline int16_t mulQ4(int16_t a, int16_t b) {
    return static_cast<int16_t>((a * b + 8) >> 4);
}

// 256-step sine, Q4 fixed point (-16..16). See core/sin256.hpp for the
// 65-entry quarter-wave table + quadrant folding (42n.7); cos(a) = sin(a+64).

// Mock radian rates folded into 256-units-per-turn steps (x40.7437/rad):
// 0.35 rad -> 14, 0.55 -> 22, 0.30 -> 12, 1.7 -> 69, 2.3 -> 94 units/tick.
constexpr uint8_t ANG_WHIRL_RING = 14;
constexpr uint8_t ANG_WHIRL_BALL = 22;
constexpr uint8_t ANG_PLAYER_STUN = 12;
constexpr uint8_t ANG_MONSTER_STUN = 14;
constexpr uint8_t ANG_SHAKE_X = 69;
constexpr uint8_t ANG_SHAKE_Y = 94;
// The 6-ring offsets (i*60deg in 256/turn units) and the whirl phase bake live
// in tools/gen-art.py: the ring is one pre-composited sprite (monhun-ardu-836),
// so the render no longer walks the ring dots.

// Page masks for the direct framebuffer rect fill. MH_MASK_TOP[top] has bits
// top..7 set, MH_MASK_BOT[bot] bits 0..bot; a page slice mask is the AND of the
// two. Tables avoid AVR variable-shift loops (`0xFF << n` lowers to a loop).
static const uint8_t MH_PROGMEM MH_MASK_TOP[8] = {0xFF, 0xFE, 0xFC, 0xF8, 0xF0, 0xE0, 0xC0, 0x80};
static const uint8_t MH_PROGMEM MH_MASK_BOT[8] = {0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF};

// Clip a block to the screen's [minY, SCREEN_H) band and paint it. shade 0
// clears the pixels on the current plane (mock black bodies mask what is under
// them). Nothing is ever written outside [0,SCREEN_W) x [minY,SCREEN_H).
//
// Callers pick the band through the wrappers below: world drawing needs the
// arena clamp y >= HUD_H so the scene never paints into the HUD strip (the
// arena's vertical borders would otherwise fill rows 0..7 whenever camY > 0),
// while the HUD strip itself must be paintable down to y=0.
//
// ArduboyG::fillRect -> Arduboy2Base::fillRect is a drawFastVLine per column,
// each with its own bounds clip; the attack telegraph rects are the largest per
// frame, so the rect is painted straight into the current plane's framebuffer
// instead. The plane byte is exactly ArduboyG's conversion (colour(): a pixel is
// lit on this plane iff shade > plane), nonzero means OR the page bits in, zero
// means AND them out (shade 0 is an eraser, not a no-op). Framebuffer layout:
// 128 bytes/page, pixel(x,y) = buf[page*128 + x], bit y&7. Clamping above is the
// only bounds work needed; writes stay inside [0,1024). Render runs between
// waitForNextPlane() calls, never during the plane blit.
__attribute__((noinline)) static void blkClamp(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t shade, int16_t minY) {
    if (w <= 0 || h <= 0)
        return;
    // Screen extents stay well inside int16: x/y come from world coords <= 256
    // plus a <= 128 px size, so x+w <= ~384 (minY is 0 or HUD_H).
    int16_t x0 = x, y0 = y, x1 = static_cast<int16_t>(x + w), y1 = static_cast<int16_t>(y + h);
    if (x0 < 0)
        x0 = 0;
    if (y0 < minY)
        y0 = minY;
    if (x1 > mh::SCREEN_W)
        x1 = mh::SCREEN_W;
    if (y1 > mh::SCREEN_H)
        y1 = mh::SCREEN_H;
    if (x0 >= x1 || y0 >= y1)
        return;

    // Clamped, so every coord now fits a byte and pages 0..7.
    const uint8_t col = arduboy.colour(shade);
    const uint8_t xa = static_cast<uint8_t>(x0);
    const uint8_t xb = static_cast<uint8_t>(x1);
    const uint8_t ya = static_cast<uint8_t>(y0);
    const uint8_t yb = static_cast<uint8_t>(y1 - 1);
    const uint8_t p0 = static_cast<uint8_t>(ya >> 3);
    const uint8_t p1 = static_cast<uint8_t>(yb >> 3);
    const uint8_t count = static_cast<uint8_t>(xb - xa);
    uint8_t *p = arduboy.getBuffer() + static_cast<uint16_t>(p0) * 128 + xa;
    uint8_t top = static_cast<uint8_t>(ya & 7);
    uint8_t page = p0;
    for (;;) {
        const uint8_t bot = (page == p1) ? static_cast<uint8_t>(yb & 7) : 7;
        uint8_t mask = mhPgmReadU8(&MH_MASK_TOP[top]) & mhPgmReadU8(&MH_MASK_BOT[bot]);
        if (col) {
            for (uint8_t i = 0; i < count; i++)
                p[i] |= mask;
        } else {
            mask = static_cast<uint8_t>(~mask);
            for (uint8_t i = 0; i < count; i++)
                p[i] &= mask;
        }
        if (page == p1)
            break;
        p += 128;
        ++page;
        top = 0;
    }
}

// World/arena rect: clipped below the 8 px HUD strip (y >= HUD_H).
static inline void blk(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t shade) {
    blkClamp(x, y, w, h, shade, mh::HUD_H);
}

// HUD-strip rect: rows 0..HUD_H-1 allowed (divider, HP/stamina/monster bars,
// gun reload bar). See drawHud()/hudBar().
static inline void hudBlk(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t shade) {
    blkClamp(x, y, w, h, shade, 0);
}

// Text comes from the FX glyph sheets (128 ASCII-ordered 4x8 tiles). The ink
// byte lives on the light-gray (fxfontg) or white (fxfontw) plane, so FRAME(c)
// makes glyph c render on its own plane. Advance 4 px == mock drawText scale 1
// (3 px glyph + 1 px gap). No glyph bitmap lives in MCU flash or RAM.
static inline int16_t textPut(uint24_t sheet, int16_t x, int16_t y, char c) {
    const uint8_t code = static_cast<uint8_t>(c);
    if (code < 128 && x > -4 && x < mh::SCREEN_W)
        SpritesU::drawPlusMaskFX(x, y, sheet, FRAME(code));
    return static_cast<int16_t>(x + 4);
}

// Mock drawText(number, scale 1): digits left-to-right, 4 px advance. crit
// (shade 3) is white, otherwise light gray, matching the mock's damage colors.
static void drawNumber(int16_t x, int16_t y, int16_t value, uint8_t shade) {
    const uint24_t sheet = (shade >= 3) ? fxfontw : fxfontg;
    uint16_t v = value < 0 ? 0 : static_cast<uint16_t>(value);
    uint8_t buf[5];
    uint8_t n = 0;
    if (v == 0) {
        buf[n++] = 0;
    } else {
        while (v > 0 && n < 5) {
            buf[n++] = static_cast<uint8_t>(v % 10);
            v /= 10;
        }
    }
    for (uint8_t i = 0; i < n; i++)
        textPut(sheet, x + i * 4, y, static_cast<char>('0' + buf[n - 1 - i]));
}

// Mock drawArena(): deterministic 1 px dots + world border.
// The mock's three per-dot signed 16-bit modulos (i*7%3, i*53%WORLD_W,
// i*29%WORLD_H) lower to __divmodhi4 on AVR and dominated the plane budget, so
// the dot field is walked with incremental counters instead. They produce the
// exact same dot positions: (i*7)%3 == i%3 (a 3-phase counter), and each world
// coord advances by its fixed step with a single conditional wrap (step is
// always < the modulus, so one subtract bounds it). Integer-only, no float.
static void drawArena(int16_t camX, int16_t camY) {
    uint8_t phase = 0;   // i % 3
    int16_t wx = 0;      // (i * 53) % WORLD_W
    int16_t wy = 0;      // (i * 29) % WORLD_H
    for (int16_t i = 0; i < 260; i++) {
        if (phase != 0) {
            const int16_t sx = static_cast<int16_t>(wx - camX);
            const int16_t sy = static_cast<int16_t>(wy - camY + mh::HUD_H);
            if (sx >= 0 && sx < mh::SCREEN_W && sy >= mh::HUD_H && sy < mh::SCREEN_H)
                arduboy.drawPixel(sx, sy, 1);   // single dark-gray dot, no blk clip
        }
        if (++phase >= 3)
            phase = 0;
        wx += 53;
        if (wx >= mh::WORLD_W)
            wx -= mh::WORLD_W;
        wy += 29;
        if (wy >= mh::WORLD_H)
            wy -= mh::WORLD_H;
    }
    const int16_t lx = static_cast<int16_t>(-camX);
    const int16_t ly = static_cast<int16_t>(mh::HUD_H - camY);
    blk(lx, ly, mh::WORLD_W, 1, 2);
    blk(lx, ly + mh::WORLD_H - 1, mh::WORLD_W, 1, 2);
    blk(lx, ly, 1, mh::WORLD_H, 2);
    blk(lx + mh::WORLD_W - 1, ly, 1, mh::WORLD_H, 2);
}

// Mock drawPole(): base post, ring bands, head, eye hole, ground plate, all
// baked into the 20x40 (20x36 art) FX sprite; hit flash selects the head plane.
static void drawPole(const mh::Pole &pole, int16_t camX, int16_t camY) {
    const int16_t x = static_cast<int16_t>(pole.rect.x - camX);
    const int16_t y = static_cast<int16_t>(pole.rect.y - camY + mh::HUD_H);
    const uint8_t f = pole.hitFlash > 0 ? spr::POLE_FLASH : spr::POLE_NORMAL;
    sprDraw(fxpole, x, y, FRAME(f));
}

// Per-creature monster sheet (epic monhun-ardu-nch): the demo roster's beast
// kind selects the fxdata sheet authored by tools/gen-art.py; RAVAGER keeps the
// legacy flat sheet its tail part overlays. Frame layout is identical across
// sheets, so only the sprite base changes -- the state/facing mapping below is
// untouched (no per-state code).
static inline uint24_t monsterSheet(int8_t kind) {
    if (kind == mh::MON_SWEEP)
        return fxmonster_sweep;
    if (kind == mh::MON_HEAVY)
        return fxmonster_heavy;
    if (kind == mh::MON_RAVAGER)
        return fxmonster;
    return fxmonster_lunge;
}

// Mock drawMonster(): dead heap, feet, body, head + eyes, stun sparkle, and the
// windup/attack telegraph box.
static void drawMonster(const mh::Game &g, int16_t camX, int16_t camY) {
    const mh::Monster &m = g.monster;
    const int16_t x = static_cast<int16_t>(rndPx(m.x, m.subX) - camX);
    const int16_t y = static_cast<int16_t>(rndPx(m.y, m.subY) - camY + mh::HUD_H);
    const int16_t w = m.w;
    const int16_t h = m.h;

    // Zone overlay art is not drawn yet: the tail sheet exists (combatPartArtFrame)
    // but the body/feet/head/eyes are baked per state/facing into the sprite, so
    // a broken tail is not overlaid here.
    // Body, feet, head and eyes are baked per state/facing into the sprite;
    // recover dims the body, windup flash and hit flash whiten it.
    const bool flashing = (m.state == mh::MS_WINDUP) && (((m.windupMax - m.t) / 4) % 2 == 0);
    uint8_t f;
    if (g.monsterKind == mh::MON_RAVAGER) {
        // Legacy fxmonster sheet: idle/recover/flash/dead x facing.
        uint8_t state = spr::MON_IDLE;
        if (m.state == mh::MS_RECOVER)
            state = spr::MON_RECOVER;
        if (m.hitFlash > 0 || flashing)
            state = spr::MON_FLASH;
        if (m.state == mh::MS_DEAD)
            state = spr::MON_DEAD;
        f = static_cast<uint8_t>(state + (m.fx >= 0 ? 0 : spr::MON_WEST));
    } else {
        // Animated demo sheets (BEAST_POSES order; west = +beast_stride). The
        // idle bob steps every 8 ticks; windup/attack carry the coil->lunge
        // pose pair. Purely cosmetic: the telegraph window math does not move.
        if (m.state == mh::MS_DEAD)
            f = art_dims::beast_dead_frame;
        else if (m.hitFlash > 0 || flashing)
            f = art_dims::beast_flash_frame;
        else if (m.state == mh::MS_WINDUP)
            f = art_dims::beast_windup_frame;
        else if (m.state == mh::MS_ATTACK)
            f = art_dims::beast_attack_frame;
        else if (m.state == mh::MS_RECOVER)
            f = art_dims::beast_recover_frame;
        else
            f = static_cast<uint8_t>(art_dims::beast_idle0_frame + ((g.tick / 8) % art_dims::beast_idle_count));
        if (m.fx < 0)
            f = static_cast<uint8_t>(f + art_dims::beast_stride);
    }
    sprDraw(monsterSheet(g.monsterKind), x, y, FRAME(f));
    if (m.state == mh::MS_DEAD)
        return;

    // Breakable appendage overlay (bead monhun-ardu-4t4): HEAVY's long tail.
    // The appendage zone cache holds the face-relative box the hit test uses
    // (combatZoneContains), so the overlay lands on the same world rect: body
    // anchor + DIR8 rotation of (ox, oy). Frames are the combatPartArtFrame
    // contract (east intact / east broken / west intact / west broken). Only the
    // heavy 24x16 sheet is drawn here; the legacy ravager fxtail is 18x10 (not a
    // multiple-of-8 SpritesU page stride) and stays unoverlaid. During a locked
    // (spin) attack the resting tail is replaced by the whipping fxtail_spin
    // overlay below, so it is skipped here.
    const bool spinning = m.state == mh::MS_ATTACK && m.atkIdx != mh::COMBAT_NO_ATTACK && g.combat.attack.facing == mh::COMBAT_FACING_LOCK;
    if (g.monsterKind == mh::MON_HEAVY && g.combat.appendZone != mh::COMBAT_NO_ZONE && !spinning) {
        const mh::CombatBox &zb = g.combat.zone[mh::COMBAT_ZONE_APPENDAGE].box;
        int32_t dx, dy;
        mh::combatFaceOffset(m.fx, m.fy, zb, dx, dy);
        const uint8_t broken = (g.combat.zoneBroken & mh::COMBAT_ZONE_APPENDAGE_BIT) ? 1 : 0;
        const uint8_t tf = mh::combatPartArtFrame(m.fx < 0, broken);
        sprDraw(fxtail_heavy, static_cast<int16_t>(x + dx), static_cast<int16_t>(y + dy), FRAME(tf));
    }

    if (m.stun > 0) {
        const uint8_t a = static_cast<uint8_t>(static_cast<uint32_t>(g.tick) * ANG_MONSTER_STUN);
        sprDraw(fxwhirl, x + w / 2 + mulQ4(cos256(a), 9), y - 3 + mulQ4(sin256(a), 2), FRAME(spr::WHIRL_DOT));
    }

    // Spin tail overlay (heavy's tail_spin, 4-frame 24x24 sheet): while the
    // locked attack is active, the tail whips 360. The frame is the world
    // direction of the active window's face-relative offset (|dx| > |dy| -> E/W
    // else S/N), so the whip leads the hit box. Frame origin is the body centre.
    if (spinning) {
        int32_t sdx, sdy;
        mh::combatFaceOffset(m.fx, m.fy, g.combat.attack.win.box, sdx, sdy);
        const int32_t adx = (sdx < 0) ? -sdx : sdx;
        const int32_t ady = (sdy < 0) ? -sdy : sdy;
        uint8_t sf;
        if (adx > ady)
            sf = (sdx < 0) ? spr::SPIN_WEST : spr::SPIN_EAST;
        else
            sf = (sdy < 0) ? spr::SPIN_NORTH : spr::SPIN_SOUTH;
        sprDraw(fxtail_spin, static_cast<int16_t>(x + (w >> 1) - 12), static_cast<int16_t>(y + (h >> 1) - 12), FRAME(sf));
    }

    // Telegraph: the cached window box itself (migration A), so the tell is the
    // real hit window for every attack (bite's small box, tail_spin's four
    // rotated boxes) instead of one fixed sprite. Same face-relative centre and
    // size the hit test uses, so no cart read happens during paint. The mock's
    // windup box is shade 1 with a 2x2 shade-2 core; the attack box shade 2
    // with a 4x4 shade-3 core.
    if (m.state == mh::MS_WINDUP || m.state == mh::MS_ATTACK) {
        if (m.atkIdx != mh::COMBAT_NO_ATTACK) {
            int32_t dx, dy;
            mh::combatFaceOffset(m.fx, m.fy, g.combat.attack.win.box, dx, dy);
            const int16_t ax = static_cast<int16_t>(x + w / 2 + dx);
            const int16_t ay = static_cast<int16_t>(y + h / 2 + dy);
            const int16_t bw = g.combat.attack.win.box.w;
            const int16_t bh = g.combat.attack.win.box.h;
            if (m.state == mh::MS_WINDUP) {
                blk(static_cast<int16_t>(ax - (bw >> 1)), static_cast<int16_t>(ay - (bh >> 1)), bw, bh, 1);
                blk(static_cast<int16_t>(ax - 1), static_cast<int16_t>(ay - 1), 2, 2, 2);
            } else {
                blk(static_cast<int16_t>(ax - (bw >> 1)), static_cast<int16_t>(ay - (bh >> 1)), bw, bh, 2);
                blk(static_cast<int16_t>(ax - 2), static_cast<int16_t>(ay - 2), 4, 4, 3);
            }
        }
    }
}

// The gen-art part records live in the mhEquip cart blob (equip_meta.hpp holds
// only their offsets); one mhFxReadBytes burst fetches a record. Every read
// happens from drawPlayer, which the render pass runs between plane blits.
struct PartRec {
    uint8_t sheet[3];   // uint24_t fx offset, little-endian
    int8_t anchorX;
    int8_t anchorY;
    uint8_t order;    // equip::ORDER_* (facing / facing*pose / pose)
    uint8_t frames;   // sheet frame count for the facing order
    uint8_t frame[equip::POSE_COUNT];
};
static_assert(sizeof(PartRec) == equip::PART_SIZE, "part record ABI drift");

// Fake cart pointer for a byte offset into the mhEquip raw_t section.
static inline const uint8_t *partCart(uint16_t off) {
    return reinterpret_cast<const uint8_t *>(static_cast<uint16_t>(static_cast<uint16_t>(mhEquip) + off));
}

static inline uint24_t partSheet(const PartRec &rec) {
    return static_cast<uint24_t>(rec.sheet[0]) | static_cast<uint24_t>(rec.sheet[1]) << 8 | static_cast<uint24_t>(rec.sheet[2]) << 16;
}

static inline void partRead(uint8_t part, PartRec &rec) {
    mhFxReadBytes(partCart(static_cast<uint16_t>(equip::PARTS_OFF + static_cast<uint16_t>(part) * equip::PART_SIZE)), reinterpret_cast<uint8_t *>(&rec), equip::PART_SIZE);
}

// Resolve the sheet frame for a (pose, facing) pair from the cart record's
// order/frames: 'pose' rows store the absolute frame, 'facing' indexes the row
// directly (single-frame sheets repeat frame 0), 'facing*pose' uses
// row * FACINGS + facing. The 8-way layer art therefore needs no render branch.
static inline uint8_t partFrame(const PartRec &rec, uint8_t pose, uint8_t facing) {
    const uint8_t row = rec.frame[pose];
    if (rec.order == equip::ORDER_FACING)
        return rec.frames == 1 ? 0 : static_cast<uint8_t>(facing % equip::FACINGS);
    if (rec.order == equip::ORDER_FACING_POSE)
        return static_cast<uint8_t>(row * equip::FACINGS + (facing % equip::FACINGS));
    return row;
}

// Draw one catalog part: the generated cart record owns the sheet, the frame
// for the resolved (pose, facing) and the frame-local anchor, so drawPlayer
// selects all three without a per-part frame if-chain. `rx/ry` is the caller's
// reference point (player centre, hit box, shield centre, ...); draw x =
// rx - anchor x.
static inline void partDraw(uint8_t part, uint8_t pose, uint8_t facing, int16_t rx, int16_t ry) {
    PartRec rec;
    partRead(part, rec);
    // The cart record owns the sheet/frame/anchor; FRAME(fr) selects the
    // current plane's pass within the logical frame (frame * 3 + plane).
    sprDraw(partSheet(rec), static_cast<int16_t>(rx - rec.anchorX), static_cast<int16_t>(ry - rec.anchorY), FRAME(partFrame(rec, pose, facing)));
}

// Variant form for parts whose frame is picked by a compact selector rather
// than a pose (the sword slash attack slot -> frames 0..4).
static inline void partVariantDraw(uint8_t part, uint8_t variant, int16_t rx, int16_t ry) {
    PartRec rec;
    partRead(part, rec);
    const uint16_t v_off = mhFxReadU16(reinterpret_cast<const uint16_t *>(partCart(static_cast<uint16_t>(equip::PART_VARIANT_OFFSETS_OFF + static_cast<uint16_t>(part) * 2))));
    const uint8_t frame = mhFxReadU8(partCart(static_cast<uint16_t>(equip::PART_VARIANT_DATA_OFF + v_off + variant)));
    sprDraw(partSheet(rec), static_cast<int16_t>(rx - rec.anchorX), static_cast<int16_t>(ry - rec.anchorY), FRAME(frame));
}

// Mock drawPlayer(): body, head, weapon overlay and effects. Every shape,
// frame and anchor comes from the generated cart part tables (beads
// monhun-ardu-abr/ikp; see src/generated/equip_meta.hpp and
// tst/fxdatatest/player_art_test.hpp) -- sheet/frame/anchor/facing are data,
// not branches. The player body is the layered default draw set
// (equip::DEFAULT_BODY/HEAD, generated from data/equipment/sets/default.json),
// so re-skinning the player is art + JSON + `make gen`. The ground shadow is
// baked into every body frame (bead monhun-ardu-3fh), so there is no separate
// shadow blit. The position math that must stay mock-exact (rndPx, reach
// scaling, whirl orbit, tick-driven offsets) is still computed here; the trig
// bake is eqf.4.
static void drawPlayer(const mh::Game &g, int16_t camX, int16_t camY) {
    const mh::Player &p = g.player;
    const int16_t x = static_cast<int16_t>(rndPx(p.x, p.subX) - camX);
    const int16_t y = static_cast<int16_t>(rndPx(p.y, p.subY) - camY + mh::HUD_H);
    const int16_t cx = static_cast<int16_t>(x + 8);
    const int16_t cy = static_cast<int16_t>(y + 8);

    // 8-way facing index from the DIR8 facing vector; partFrame() resolves it
    // to the sheet frame, so no facing branch lives in the render path.
    const uint8_t face = static_cast<uint8_t>(fp::dirIndexFromDelta(p.fx, p.fy));

    // Paper-doll slots in draw order: body (with the baked shadow) -> head.
    // Both are the default draw set; the body picks the dodge pose row.
    partDraw(equip::DEFAULT_BODY, p.state == mh::PS_DODGE ? equip::POSE_DODGE : equip::POSE_IDLE, face, cx, cy);
    partDraw(equip::DEFAULT_HEAD, equip::POSE_IDLE, face, cx, cy);

    // Shared attack timing (sword and flail read the same startup/active/reach;
    // the two weapon branches below only scale the reach differently).
    const mh::Attack *a = (p.state == mh::PS_ATTACK || p.state == mh::PS_SPECIAL) ? p.atk : nullptr;
    uint8_t phase = 0;
    if (a) {
        const int16_t startup = mh::attackStartup(a);
        const int16_t active = mh::attackActive(a);
        phase = p.t < startup ? 0 : (p.t < startup + active ? 1 : 2);
    }

    if (g.weapon == mh::W_SWORD) {
        if (a) {
            int16_t reach = mh::attackReach(a);
            if (phase != 1)
                reach = static_cast<int16_t>(reach * 6 / 10);   // mock 0.6 arc
            const int16_t hx = static_cast<int16_t>(cx + (((int32_t)p.fx * reach) >> 4));
            const int16_t hy = static_cast<int16_t>(cy + (((int32_t)p.fy * reach) >> 4));
            const int16_t hw = mh::attackHw(a);
            const int16_t hh = mh::attackHh(a);
            // Attack slot -> slash frame (VARIANT_SWORD_SLASH): combo chain
            // 0/1/2, plain special, step-slash, spin-cut. The 32x32 frames are
            // hit-box-centred with the 4x4 white core at the centre.
            const int8_t atkId = mh::attackId(a);
            const uint8_t slot = atkId == mh::ATK_NONE ? static_cast<uint8_t>(p.state == mh::PS_SPECIAL ? 3 : p.chain) : static_cast<uint8_t>(3 + atkId);
            partVariantDraw(equip::PART_SWORD_SLASH, slot, hx, hy);
            if (p.state == mh::PS_SPECIAL && p.riposteT > 0) {
                // Riposte rim: rim at the frame origin, box top-left as ref.
                partDraw(equip::PART_SWORD_RIPOSTE, equip::POSE_ATTACK_ACTIVE, face, hx - (hw >> 1), hy - (hh >> 1));
            }
        } else if (p.stance == mh::ST_PARRY) {
            // Blade frame anchored on the player centre.
            partDraw(equip::PART_SWORD_PARRY, equip::POSE_PARRY, face, cx, cy);
        } else {
            // Idle: chip sheet's 3x3 white head on the mock top-left.
            partDraw(equip::PART_SWORD_CHIP, equip::POSE_IDLE, face, cx + ((p.fx * 7) >> 4), cy + ((p.fy * 7) >> 4));
        }
    } else if (g.weapon == mh::W_FLAIL) {
        if (p.stance == mh::ST_WHIRL) {
            // Mock drawPlayer() whirl: six 2x2 light dots on the exact ellipse
            // (cx + round(cos(a)*20), cy + round(sin(a)*14)) at ring angle
            // a = tick*0.35 rad + i*60deg, then the white ball at tick*0.55 rad.
            // The rates fold to 256-units/turn as ANG_WHIRL_RING/BALL
            // (0.35 rad -> 14, 0.55 -> 22), and the ellipse radii come from
            // art_dims (fxdump), so the mock stays the orbit reference.
            //
            // The 6 dots are pre-composited into one 24-phase sprite by
            // tools/gen-art.py (bead monhun-ardu-836) using the same SIN65 Q4
            // table + mulQ4 rounding, so one ring blit replaces six. Phase =
            // the ring angle's bin: (ang * phases) >> 8; the baked frames sit
            // at the bin centres, so the worst-case angular error is
            // 256/(2*phases) units -- the intentional quantization.
            const uint8_t ang = static_cast<uint8_t>(p.whirlTick * ANG_WHIRL_RING);
            const uint8_t phase = static_cast<uint8_t>((static_cast<uint16_t>(ang) * art_dims::whirlring_frames) >> 8);
            partVariantDraw(equip::PART_FLAIL_RING, phase, cx, cy);
            const uint8_t ba = static_cast<uint8_t>(p.whirlTick * ANG_WHIRL_BALL);
            partDraw(equip::PART_FLAIL_BALL, equip::POSE_WHIRL, face, cx + mulQ4(cos256(ba), 20), cy + mulQ4(sin256(ba), 14));
        } else if (p.state == mh::PS_ATTACK || p.state == mh::PS_SPECIAL) {
            if (a) {
                int16_t reach = mh::attackReach(a);
                if (phase != 1)
                    reach = static_cast<int16_t>(reach / 2);   // mock 0.5 chain
                // 1x1 light dots from the reach/aim math (8-way facing, halved
                // startup/recovery reach, trip branch reach 12); the ball is
                // the 4x4 white chip, both at the mock's exact positions.
                for (int8_t i = 1; i <= 3; i++) {
                    const int16_t rr = static_cast<int16_t>((reach * i) >> 2);
                    partDraw(equip::PART_FLAIL_CHAIN, equip::POSE_IDLE, face, static_cast<int16_t>(cx + (((int32_t)p.fx * rr) >> 4)), static_cast<int16_t>(cy + (((int32_t)p.fy * rr) >> 4)));
                }
                partDraw(equip::PART_CHIP_BALL, equip::POSE_IDLE, face, static_cast<int16_t>(cx + (((int32_t)p.fx * reach) >> 4)), static_cast<int16_t>(cy + (((int32_t)p.fy * reach) >> 4)));
            }
        } else {
            partDraw(equip::PART_FLAIL_CHAIN, equip::POSE_IDLE, face, static_cast<int16_t>(cx + ((p.fx * 4) >> 4)), static_cast<int16_t>(cy + ((p.fy * 4) >> 4)));
            partDraw(equip::PART_SWORD_CHIP, equip::POSE_IDLE, face, static_cast<int16_t>(cx + ((p.fx * 9) >> 4)), static_cast<int16_t>(cy + ((p.fy * 9) >> 4)));
        }
        if (p.state == mh::PS_DEFLECT) {
            // Two light bars; frame centred on the 16 px body.
            partDraw(equip::PART_DEFLECT, equip::POSE_DEFLECT, face, cx, cy);
        }
    } else {   // gunshield
        const int16_t shx = static_cast<int16_t>(cx + ((p.fx * 5) >> 4));
        const int16_t shy = static_cast<int16_t>(cy + ((p.fy * 5) >> 4));
        // 12x16 plate frame, plate at frame local 1,1: shield centre as ref.
        // Guard selects the fully lit plate via the record's poseMap (frame 1),
        // the idle plate is frame 0; FRAME() applies the per-plane stride.
        partDraw(equip::PART_GUN_GUARD, p.stance == mh::ST_GUARD ? equip::POSE_GUARD : equip::POSE_IDLE, face, shx, shy);
        if (p.state == mh::PS_SHOVE) {
            // poseMap shove = frame 2 of the same plate sheet: the shove plate is
            // drawn 1 px left inside its cell (anchor 5 vs the idle/guard 6), so
            // +1 on the reference re-centres the shared record anchor.
            const int16_t shx2 = static_cast<int16_t>(shx + ((p.fx * 4) >> 4) + 1);
            const int16_t shy2 = static_cast<int16_t>(shy + ((p.fy * 4) >> 4));
            partDraw(equip::PART_GUN_GUARD, equip::POSE_SHOVE, face, shx2, shy2);
        }
        if (p.reload > 0)
            partDraw(equip::PART_GUN_RELOAD, equip::POSE_IDLE, face, cx, cy);
    }

    if (p.iT > 0 && (g.tick % 4) < 2)
        partDraw(equip::PART_ERASE, equip::POSE_IDLE, face, cx, cy);
    if (p.state == mh::PS_STUN) {
        const uint8_t ang = static_cast<uint8_t>(static_cast<uint32_t>(g.tick) * ANG_PLAYER_STUN);
        partDraw(equip::PART_FLAIL_STUN, equip::POSE_STUN, face, cx + mulQ4(cos256(ang), 7), cy - 10 + mulQ4(sin256(ang), 2));
    }
}

// Mock drawProjectiles(): 3-puff trail then ball (rim/core/base) or pellet.
static void drawProjectiles(const mh::Game &g, int16_t camX, int16_t camY) {
    for (int16_t i = 0; i < g.projN; i++) {
        const mh::Projectile &pr = g.proj[i];
        const int16_t x = static_cast<int16_t>(pr.x - camX);
        const int16_t y = static_cast<int16_t>(pr.y - camY + mh::HUD_H);
        const int16_t bx = static_cast<int16_t>((pr.vx * 2) >> 4);
        const int16_t by = static_cast<int16_t>((pr.vy * 2) >> 4);

        // 2x2 trail puffs at the mock offsets, drawn far dark -> near light.
        sprDraw(fxtrail, static_cast<int16_t>(x - bx * 2 - 1), static_cast<int16_t>(y - by * 2 - 1), FRAME(spr::TRAIL_DARK));
        sprDraw(fxtrail, static_cast<int16_t>(x - bx * 3 - 1), static_cast<int16_t>(y - by * 3 - 1), FRAME(spr::TRAIL_DARK));
        sprDraw(fxtrail, static_cast<int16_t>(x - bx - 1), static_cast<int16_t>(y - by - 1), FRAME(spr::TRAIL_LIGHT));

        const int16_t hw = static_cast<int16_t>(pr.w >> 1);
        const int16_t hh = static_cast<int16_t>(pr.h >> 1);
        // Ball (7x8) / scatter (4x8) sheets; art occupies the top 7x6 / 4x4.
        if (pr.heavy)
            sprDraw(fxball, x - hw, y - hh, FRAME(0));
        else
            sprDraw(fxscatter, x - hw, y - hh, FRAME(0));
    }
}

// Mock drawEffects(): 4-point spark, or a rising damage number.
// Render-side cap: the mock has no cap (unbounded array) and the device core
// caps at MAX_EFFECTS, but the transient worst case (several simultaneous
// damage-number glyphs + sparks) is the render bottleneck — each damage number
// is 2-3 FX glyph reads. Only the newest MAX_FX_DRAW effects are painted.
// Core sim is untouched: every effect still ticks and expires as before.
constexpr int16_t MAX_FX_DRAW = 6;

static void drawEffects(const mh::Game &g, int16_t camX, int16_t camY) {
    const int16_t first = g.fxN > MAX_FX_DRAW ? g.fxN - MAX_FX_DRAW : 0;
    for (int16_t i = first; i < g.fxN; i++) {
        const mh::Effect &e = g.fx[i];
        const int16_t r = e.life - e.t;
        if (e.text) {
            const int16_t x = static_cast<int16_t>(e.x - camX);
            const int16_t y = static_cast<int16_t>(e.y - (r + 1) / 3 - camY + mh::HUD_H);
            drawNumber(static_cast<int16_t>(x - 2), y, e.text, e.crit ? 3 : 2);
        } else {
            // 4x4 spark sprite centred on the effect; crit selects the white
            // plane. TODO: the mock expands the 4 dots with radius r — the FX
            // sprite is fixed size, so the spread animation is dropped.
            const uint8_t f = e.crit ? spr::SPARK_BRIGHT : spr::SPARK_LIGHT;
            const int16_t x = static_cast<int16_t>(e.x - camX);
            const int16_t y = static_cast<int16_t>(e.y - camY + mh::HUD_H);
            sprDraw(fxspark, static_cast<int16_t>(x - 2), static_cast<int16_t>(y - 2), FRAME(f));
        }
    }
}

/* ------------------------------------------------------------- debug wire */

#if DEBUG_HURTBOXES
// 1-bit wireframe overlay, ported from mock/game.js drawDebug() (source of
// truth). No color on device, so hurt vs hit boxes are told apart by edge
// style: hurt = solid border, hit = dotted (alternating 1 px). Drawn on every
// plane with the identical shapes, exactly like the block scene, so the L4
// triplane pass resolves to the same image. Read-only: never mutates Game.
//
// World rects are the sim's exact int rectangles (raw int coords, not the
// rndPx sub-pixel smoothing used for sprites), translated by the same camera
// and HUD offset the sprite/blk scene uses.

// Solid 1 px border (hurt boxes).
static void wireSolid(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (w < 1 || h < 1)
        return;
    blk(x, y, w, 1, 3);
    blk(x, y + h - 1, w, 1, 3);
    blk(x, y, 1, h, 3);
    blk(x + w - 1, y, 1, h, 3);
}

// Dotted 1 px border (hit boxes): every other pixel on each edge.
static void wireDot(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (w < 1 || h < 1)
        return;
    for (int32_t i = 0; i < w; i += 2) {
        blk(x + i, y, 1, 1, 3);
        blk(x + i, y + h - 1, 1, 1, 3);
    }
    for (int32_t j = 0; j < h; j += 2) {
        blk(x, y + j, 1, 1, 3);
        blk(x + w - 1, y + j, 1, 1, 3);
    }
}

static void drawDebug(const mh::Game &g, int16_t camX, int16_t camY) {
    const int32_t ox = -camX;
    const int32_t oy = -camY + mh::HUD_H;
    const mh::Player &p = g.player;

    // Hurt boxes (solid): player body, then the creature's cached skeleton body
    // part in hunt (migration B: part boxes from g.combat.body, not w/h
    // literals) or the training-pole hurt rect in train.
    wireSolid(p.x + ox, p.y + oy, p.w, p.h);
    if (g.mode == mh::MODE_HUNT) {
        if (g.target.alive) {
            const mh::CombatBox &b = g.combat.body;
            wireSolid(g.monster.x + b.ox + ox, g.monster.y + b.oy + oy, b.w, b.h);
        }
    } else if (g.target.alive) {
        wireSolid(g.target.rect.x + ox, g.target.rect.y + oy, g.target.rect.w, g.target.rect.h);
    }

    // Active player melee hit box (dotted): the sim's meleeHitbox() rect, so
    // the wire matches the frame the overlap test actually runs against.
    if ((p.state == mh::PS_ATTACK || p.state == mh::PS_SPECIAL) && p.atk) {
        const mh::Rect hit = mh::meleeHitbox(p, p.atk);
        wireDot(hit.x + ox, hit.y + oy, hit.w, hit.h);
    }

    // Monster windup/attack hit box (dotted), same face-relative window centre
    // and size the monster hit test uses; the windup outline is the telegraph.
    if (g.mode == mh::MODE_HUNT && g.monster.atkIdx != mh::COMBAT_NO_ATTACK && (g.monster.state == mh::MS_WINDUP || g.monster.state == mh::MS_ATTACK)) {
        const mh::Monster &m = g.monster;
        const mh::CombatBox &b = g.combat.body;
        int32_t dx, dy;
        mh::combatFaceOffset(m.fx, m.fy, g.combat.attack.win.box, dx, dy);
        const int32_t hx = m.x + b.ox + (b.w >> 1) + dx;
        const int32_t hy = m.y + b.oy + (b.h >> 1) + dy;
        const int32_t hw = g.combat.attack.win.box.w;
        const int32_t hh = g.combat.attack.win.box.h;
        wireDot(hx - (hw >> 1) + ox, hy - (hh >> 1) + oy, hw, hh);
    }

    // Live shell/projectile hit rects (dotted), exact pr.w x pr.h collision box.
    for (int16_t i = 0; i < g.projN; i++) {
        const mh::Projectile &pr = g.proj[i];
        wireDot(pr.x - (pr.w >> 1) + ox, pr.y - (pr.h >> 1) + oy, pr.w, pr.h);
    }

    // Flail whirl radius (dotted 48x48 box) while the whirl stance is held.
    if (p.stance == mh::ST_WHIRL) {
        const int32_t cx = p.x + (p.w >> 1);
        const int32_t cy = p.y + (p.h >> 1);
        wireDot(cx - 24 + ox, cy - 24 + oy, 48, 48);
    }

    // Hit-spark markers (small white plus) at live non-text effects.
    for (int16_t i = 0; i < g.fxN; i++) {
        const mh::Effect &e = g.fx[i];
        if (e.text || e.t >= e.life)
            continue;
        const int32_t sx = e.x + ox;
        const int32_t sy = e.y + oy;
        blk(sx, sy - 1, 1, 3, 3);
        blk(sx - 1, sy, 3, 1, 3);
    }
}
#endif   // DEBUG_HURTBOXES

/* ------------------------------------------------------------------- hud */

// Mock drawHud(): HP + stamina bars, weapon name, gun shell/reload, then the
// monster HP bar (hunt) or LAST/DPS (train). The mock drew this as the bottom
// 8 px strip; the device reserves the top 8 px, so the strip is mirrored: the
// divider sits at the arena edge (y = HUD_H-1) and the bars/text fill rows
// 0..6. Drawn untranslated (mock restores the camera transform first) and
// read-only.
//
// Text is drawn from the FX glyph sheet (fxfontw, 4x8 ASCII tiles) at row 1 so
// the 5 px cap sits in HUD rows 1..5; advance 4 px, matching mock drawText.
static inline int16_t hudPut(int16_t x, char c) {
    return textPut(fxfontw, x, 1, c);
}

static uint8_t hudDigits(int16_t v) {
    uint8_t n = 1;
    while (v >= 10) {
        v = static_cast<int16_t>(v / 10);
        n++;
    }
    return n;
}

// Print a non-negative value as exactly `digits` digits (leading zeros).
static int16_t hudNum(int16_t x, int16_t v, uint8_t digits) {
    if (digits > 5)
        digits = 5;
    char b[5];
    for (int8_t i = static_cast<int8_t>(digits - 1); i >= 0; i--) {
        b[i] = static_cast<char>('0' + v % 10);
        v = static_cast<int16_t>(v / 10);
    }
    for (uint8_t i = 0; i < digits; i++)
        x = hudPut(x, b[i]);
    return x;
}

// Mock bar(): dark back/border, inner fill width round((w-2) * ratio).
// uint16 arithmetic: (w-2) <= 44 and den <= 320 (generated hp/stam maxima), so
// the product fits int16 with room to spare; keeps the 32-bit divide helper
// out of the image.
static void hudBar(int16_t x, int16_t y, int16_t w, int16_t h, int16_t num, int16_t den, uint8_t shade) {
    hudBlk(x, y, w, h, 1);
    if (den <= 0 || num <= 0)
        return;
    if (num > den)
        num = den;
    const uint16_t u16den = static_cast<uint16_t>(den);
    uint16_t fw = static_cast<uint16_t>(((static_cast<uint16_t>(w - 2) * static_cast<uint16_t>(num)) + u16den / 2) / u16den);
    if (fw > w - 2)
        fw = w - 2;
    if (fw > 0)
        hudBlk(x + 1, y + 1, fw, h - 2, shade);
}

static void drawHud(const mh::Game &g) {
    const mh::Player &p = g.player;

    // No strip background fill: ArduboyG waitForNextPlane(BLACK) wipes the
    // framebuffer black before each plane, so the HUD rows only need the
    // shapes/text drawn. The strip uses the HUD-band path (rows 0..7); the
    // arena below stays y >= HUD_H clipped.
    hudBlk(0, mh::HUD_H - 1, mh::SCREEN_W, 1, 1);   // divider at the arena edge

    hudBar(1, 2, 28, 4, p.hp, p.hpMax, 3);        // player HP (white)
    hudBar(29, 2, 16, 4, p.stam, p.stamMax, 2);   // stamina (light gray)

    // Weapon marker (mock's full name shortened to fit the 128 px strip), then
    // the mode marker (device-only, the mock implied it via pole vs beast): the
    // 4 glyphs (3-char weapon + 1-char mode) on the 4 px lane at x=46 are baked
    // into one 16x8 FX strip (bead monhun-ardu-e4a), drawn with a single blit
    // per plane instead of 4 textPut() cart seeks. Frame order is
    // weapon*2 + mode (SWD/FLA/GUN x hunt/train); the pixels come from the same
    // GLYPHS table as fxfontw (gen-art check_hud_identity). The else branch
    // keeps the old "anything but sword/flail reads GUN" mapping.
    const uint8_t wf = static_cast<uint8_t>(g.weapon == mh::W_SWORD ? 0 : (g.weapon == mh::W_FLAIL ? 1 : 2));
    const uint8_t mf = static_cast<uint8_t>(g.mode == mh::MODE_TRAIN ? 1 : 0);
    sprDraw(fxhud, 46, 1, FRAME(static_cast<uint8_t>(wf * 2 + mf)));

    if (g.weapon == mh::W_GUN) {   // shell count + reload
        int16_t x = 67;
        if (p.reload > 0) {
            hudPut(x, 'R');
            hudPut(x, 'L');
            hudPut(x, 'D');
            const mh::ShellDef *sh = mh::weaponShell(&mh::WEAPON_DEFS[g.weapon], p.shell);
            const int16_t rmax = mh::shellReload(sh);
            if (rmax > 0) {
                // uint16 narrowing: rmax <= 70 (generated shell data).
                const uint16_t u16rmax = static_cast<uint16_t>(rmax);
                uint16_t bw = static_cast<uint16_t>((12 * (u16rmax - static_cast<uint16_t>(p.reload)) + u16rmax / 2) / u16rmax);
                if (bw < 1)
                    bw = 1;
                else if (bw > 12)
                    bw = 12;
                hudBlk(67, 6, bw, 1, 2);
            }
        } else {
            x = hudPut(x, p.shell == 0 ? 'B' : 'S');
            hudNum(x, p.shells[p.shell], hudDigits(p.shells[p.shell]));
        }
    }

    if (g.mode == mh::MODE_TRAIN) {   // train total + DPS
        const int16_t total = g.train.total > 9999 ? 9999 : static_cast<int16_t>(g.train.total);
        int16_t dps = mh::trainDps(g);
        if (dps > 999)
            dps = 999;
        const uint8_t nt = hudDigits(total);
        const uint8_t nd = hudDigits(dps);
        int16_t x = static_cast<int16_t>(127 - 4 * (nt + nd + 2));
        x = hudPut(x, 'T');
        x = hudNum(x, total, nt);
        x = hudPut(x, 'D');
        hudNum(x, dps, nd);
    } else {   // monster HP (hunt)
        hudBar(82, 2, 44, 3, g.monster.hp, g.monster.hpMax, 3);
    }
}

/* ------------------------------------------------------------------ scene */

// Full block-art scene, mock draw order: arena, target (pole|beast), player,
// shells, effects, then the (untracked) debug wire overlay and HUD. Read-only:
// render never mutates Game. Shapes are identical on every plane (the L4 shade
// resolves in ArduboyG::planeColor), so the three passes composite to the same
// 4-level image.
//
// `wire` only has an effect when DEBUG_HURTBOXES is compiled in; shipping builds
// pass false and the overlay is preprocessed out.
static void renderScene(const mh::Game &g, bool wire) {
    // Camera clamp to world bounds; also guards against an unclamped Game.
    int16_t camX = g.camX;
    int16_t camY = g.camY;
    if (camX < 0)
        camX = 0;
    else if (camX > mh::CAM_MAX_X)
        camX = mh::CAM_MAX_X;
    if (camY < 0)
        camY = 0;
    else if (camY > mh::CAM_MAX_Y)
        camY = mh::CAM_MAX_Y;

    // Mock g.shake has no Game field yet (freeze is not gated/decayed), so the
    // render derives an equivalent tick-based int offset from the decaying hit
    // indicators: the view kicks for the ~4 ticks a monster/pole hit flashes.
    // TODO(hitstop bead): replace with a real Game::shake value.
    int16_t shakeX = 0;
    int16_t shakeY = 0;
    const int16_t amp = g.monster.hitFlash > g.pole.hitFlash ? g.monster.hitFlash : g.pole.hitFlash;
    if (amp > 0) {
        const uint8_t a1 = static_cast<uint8_t>(static_cast<uint32_t>(g.tick) * ANG_SHAKE_X);
        const uint8_t a2 = static_cast<uint8_t>(static_cast<uint32_t>(g.tick) * ANG_SHAKE_Y);
        shakeX = static_cast<int16_t>(mulQ4(sin256(a1), amp));                  // mock sin(tick*1.7)*shake
        shakeY = static_cast<int16_t>(mulQ4(cos256(a2), (amp * 7 + 5) / 10));   // *0.7, round
    }
    const int16_t ecX = static_cast<int16_t>(camX - shakeX);
    const int16_t ecY = static_cast<int16_t>(camY - shakeY);

    drawArena(ecX, ecY);
    if (g.mode == mh::MODE_TRAIN)
        drawPole(g.pole, ecX, ecY);
    else
        drawMonster(g, ecX, ecY);
    drawPlayer(g, ecX, ecY);
    drawProjectiles(g, ecX, ecY);
    drawEffects(g, ecX, ecY);
#if DEBUG_HURTBOXES
    if (wire)
        drawDebug(g, ecX, ecY);
#else
    (void)wire;
#endif
    drawHud(g);
}

}   // namespace mh
