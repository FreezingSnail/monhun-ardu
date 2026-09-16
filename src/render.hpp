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

#ifndef DEBUG_HURTBOXES
#define DEBUG_HURTBOXES 0
#endif

namespace mh {

/* ---------------------------------------------------------------- sprites */

// Frame indices into the FX sprite sheets authored by tools/gen-art.py. Sheets
// are left-to-right strips and FRAME(i) == i*3 + currentPlane() selects the
// current plane's data, so one draw call per plane composites the 4 shades.
namespace spr {
// 16x16 player body. Weapon overlays stay procedural: aim (fx/fy) and reach are
// dynamic and routinely exceed the 16x16 frame.
constexpr uint8_t PLAYER_NORMAL = 0;
constexpr uint8_t PLAYER_DODGE  = 1;

// 32x24 monster: four states facing east, then the same four facing west.
constexpr uint8_t MON_IDLE    = 0;
constexpr uint8_t MON_RECOVER = 1;
constexpr uint8_t MON_FLASH   = 2;
constexpr uint8_t MON_DEAD    = 3;
constexpr uint8_t MON_WEST    = 4;

// 20x40 pole (20x36 art, padded): black-eyed head normal / hit flash.
constexpr uint8_t POLE_NORMAL = 0;
constexpr uint8_t POLE_FLASH  = 1;

// 4x4 spark, light gray / white.
constexpr uint8_t SPARK_LIGHT  = 0;
constexpr uint8_t SPARK_BRIGHT = 1;
} // namespace spr

// Cull fully off-screen sprites before paying the FX seek, then blit on the
// current plane. Max sheet size is 32x40, so these bounds are conservative.
static inline void sprDraw(uint24_t img, int32_t x, int32_t y, uint16_t frame) {
    if (x <= -32 || x >= mh::SCREEN_W || y <= -40 || y >= mh::SCREEN_H) return;
    SpritesU::drawPlusMaskFX(static_cast<int16_t>(x), static_cast<int16_t>(y),
                             img, frame);
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
// and every block is clipped to the arena band. The mock put the HUD at the
// bottom; the device flips it to the top (established by the loop bead), so
// the arena shifts down by HUD_H px. drawHud() paints the strip untranslated
// (no camera/shake) at the top, mirrored from the mock's bottom strip.

// round(v + sub/16): sub is the 1/16 px remainder, matches mock Math.round().
static inline int16_t rndPx(int16_t v, int16_t sub) {
    return static_cast<int16_t>((v * 16 + sub + 8) >> 4);
}

// round((a*b)/16) for Q4 vectors: matches Math.round() of the mock's float
// product for every sign (arithmetic shift floors (a*b+8)/16).
static inline int32_t mulQ4(int32_t a, int32_t b) {
    return (a * b + 8) >> 4;
}

// 256-step sine, Q4 fixed point (-16..16). cos(a) = SIN256[(a+64)&255].
static const int8_t MH_PROGMEM SIN256[256] = {
      0,  0,  1,  1,  2,  2,  2,  3,  3,  4,  4,  4,  5,  5,  5,  6,
      6,  6,  7,  7,  8,  8,  8,  9,  9,  9, 10, 10, 10, 10, 11, 11,
     11, 12, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 15,
     15, 15, 15, 15, 15, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 15, 15, 15, 15, 15,
     15, 15, 14, 14, 14, 14, 14, 14, 13, 13, 13, 13, 12, 12, 12, 12,
     11, 11, 11, 10, 10, 10, 10,  9,  9,  9,  8,  8,  8,  7,  7,  6,
      6,  6,  5,  5,  5,  4,  4,  4,  3,  3,  2,  2,  2,  1,  1,  0,
      0,  0, -1, -1, -2, -2, -2, -3, -3, -4, -4, -4, -5, -5, -5, -6,
     -6, -6, -7, -7, -8, -8, -8, -9, -9, -9,-10,-10,-10,-10,-11,-11,
    -11,-12,-12,-12,-12,-13,-13,-13,-13,-14,-14,-14,-14,-14,-14,-15,
    -15,-15,-15,-15,-15,-15,-16,-16,-16,-16,-16,-16,-16,-16,-16,-16,
    -16,-16,-16,-16,-16,-16,-16,-16,-16,-16,-16,-15,-15,-15,-15,-15,
    -15,-15,-14,-14,-14,-14,-14,-14,-13,-13,-13,-13,-12,-12,-12,-12,
    -11,-11,-11,-10,-10,-10,-10, -9, -9, -9, -8, -8, -8, -7, -7, -6,
     -6, -6, -5, -5, -5, -4, -4, -4, -3, -3, -2, -2, -2, -1, -1,  0,
};

static inline int16_t sin256(uint8_t a) { return mhPgmReadI8(&SIN256[a]); }
static inline int16_t cos256(uint8_t a) {
    return mhPgmReadI8(&SIN256[static_cast<uint8_t>(a + 64)]);
}

// Mock radian rates folded into 256-units-per-turn steps (x40.7437/rad):
// 0.35 rad -> 14, 0.55 -> 22, 0.30 -> 12, 1.7 -> 69, 2.3 -> 94 units/tick.
constexpr uint8_t ANG_WHIRL_RING = 14;
constexpr uint8_t ANG_WHIRL_BALL = 22;
constexpr uint8_t ANG_PLAYER_STUN = 12;
constexpr uint8_t ANG_MONSTER_STUN = 14;
constexpr uint8_t ANG_SHAKE_X = 69;
constexpr uint8_t ANG_SHAKE_Y = 94;
// 6-ring offsets: i*60deg in 256/turn units (42.667 -> rounded).
static const uint8_t MH_PROGMEM RING6[6] = { 0, 43, 85, 128, 171, 213 };

// Page masks for the direct framebuffer rect fill. MH_MASK_TOP[top] has bits
// top..7 set, MH_MASK_BOT[bot] bits 0..bot; a page slice mask is the AND of the
// two. Tables avoid AVR variable-shift loops (`0xFF << n` lowers to a loop).
static const uint8_t MH_PROGMEM MH_MASK_TOP[8] = { 0xFF,0xFE,0xFC,0xF8,0xF0,0xE0,0xC0,0x80 };
static const uint8_t MH_PROGMEM MH_MASK_BOT[8] = { 0x01,0x03,0x07,0x0F,0x1F,0x3F,0x7F,0xFF };

// Clip a block to the screen arena band and paint it. shade 0 clears the pixels
// on the current plane (mock black bodies mask what is under them). Nothing is
// ever written outside [0,SCREEN_W) x [HUD_H,SCREEN_H).
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
__attribute__((noinline)) static void blk(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t shade) {
    if (w <= 0 || h <= 0) return;
    int32_t x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < mh::HUD_H) y0 = mh::HUD_H;
    if (x1 > mh::SCREEN_W) x1 = mh::SCREEN_W;
    if (y1 > mh::SCREEN_H) y1 = mh::SCREEN_H;
    if (x0 >= x1 || y0 >= y1) return;

    // Clamped, so every coord now fits a byte and pages 0..7.
    const uint8_t col = arduboy.colour(shade);
    const uint8_t xa = static_cast<uint8_t>(x0);
    const uint8_t xb = static_cast<uint8_t>(x1);
    const uint8_t ya = static_cast<uint8_t>(y0);
    const uint8_t yb = static_cast<uint8_t>(y1 - 1);
    const uint8_t p0 = static_cast<uint8_t>(ya >> 3);
    const uint8_t p1 = static_cast<uint8_t>(yb >> 3);
    const uint8_t count = static_cast<uint8_t>(xb - xa);
    uint8_t* p = arduboy.getBuffer() + static_cast<uint16_t>(p0) * 128 + xa;
    uint8_t top = static_cast<uint8_t>(ya & 7);
    uint8_t page = p0;
    for (;;) {
        const uint8_t bot = (page == p1) ? static_cast<uint8_t>(yb & 7) : 7;
        uint8_t mask = mhPgmReadU8(&MH_MASK_TOP[top]) & mhPgmReadU8(&MH_MASK_BOT[bot]);
        if (col) {
            for (uint8_t i = 0; i < count; i++) p[i] |= mask;
        } else {
            mask = static_cast<uint8_t>(~mask);
            for (uint8_t i = 0; i < count; i++) p[i] &= mask;
        }
        if (page == p1) break;
        p += 128;
        ++page;
        top = 0;
    }
}

// Text comes from the FX glyph sheets (128 ASCII-ordered 4x8 tiles). The ink
// byte lives on the light-gray (fxfontg) or white (fxfontw) plane, so FRAME(c)
// makes glyph c render on its own plane. Advance 4 px == mock drawText scale 1
// (3 px glyph + 1 px gap). No glyph bitmap lives in MCU flash or RAM.
static inline int16_t textPut(uint24_t sheet, int32_t x, int32_t y, char c) {
    const uint8_t code = static_cast<uint8_t>(c);
    if (code < 128 && x > -4 && x < mh::SCREEN_W)
        SpritesU::drawPlusMaskFX(static_cast<int16_t>(x), static_cast<int16_t>(y),
                                 sheet, FRAME(code));
    return static_cast<int16_t>(x + 4);
}

// Mock drawText(number, scale 1): digits left-to-right, 4 px advance. crit
// (shade 3) is white, otherwise light gray, matching the mock's damage colors.
static void drawNumber(int32_t x, int32_t y, int16_t value, uint8_t shade) {
    const uint24_t sheet = (shade >= 3) ? fxfontw : fxfontg;
    uint16_t v = value < 0 ? 0 : static_cast<uint16_t>(value);
    uint8_t buf[5];
    uint8_t n = 0;
    if (v == 0) {
        buf[n++] = 0;
    } else {
        while (v > 0 && n < 5) { buf[n++] = static_cast<uint8_t>(v % 10); v /= 10; }
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
    uint8_t phase = 0;          // i % 3
    int16_t wx = 0;             // (i * 53) % WORLD_W
    int16_t wy = 0;             // (i * 29) % WORLD_H
    for (int16_t i = 0; i < 260; i++) {
        if (phase != 0) {
            const int16_t sx = static_cast<int16_t>(wx - camX);
            const int16_t sy = static_cast<int16_t>(wy - camY + mh::HUD_H);
            if (sx >= 0 && sx < mh::SCREEN_W && sy >= mh::HUD_H && sy < mh::SCREEN_H)
                arduboy.drawPixel(sx, sy, 1); // single dark-gray dot, no blk clip
        }
        if (++phase >= 3) phase = 0;
        wx += 53;
        if (wx >= mh::WORLD_W) wx -= mh::WORLD_W;
        wy += 29;
        if (wy >= mh::WORLD_H) wy -= mh::WORLD_H;
    }
    const int32_t lx = -camX;
    const int32_t ly = static_cast<int32_t>(mh::HUD_H) - camY;
    blk(lx, ly, mh::WORLD_W, 1, 2);
    blk(lx, ly + mh::WORLD_H - 1, mh::WORLD_W, 1, 2);
    blk(lx, ly, 1, mh::WORLD_H, 2);
    blk(lx + mh::WORLD_W - 1, ly, 1, mh::WORLD_H, 2);
}

// Mock drawPole(): base post, ring bands, head, eye hole, ground plate, all
// baked into the 20x40 (20x36 art) FX sprite; hit flash selects the head plane.
static void drawPole(const mh::Pole& pole, int16_t camX, int16_t camY) {
    const int32_t x = static_cast<int32_t>(pole.rect.x) - camX;
    const int32_t y = static_cast<int32_t>(pole.rect.y) - camY + mh::HUD_H;
    const uint8_t f = pole.hitFlash > 0 ? spr::POLE_FLASH : spr::POLE_NORMAL;
    sprDraw(fxpole, x, y, FRAME(f));
}

// Mock drawMonster(): dead heap, feet, body, head + eyes, stun sparkle, and the
// windup/attack telegraph box.
static void drawMonster(const mh::Game& g, int16_t camX, int16_t camY) {
    const mh::Monster& m = g.monster;
    const int32_t x = rndPx(m.x, m.subX) - camX;
    const int32_t y = rndPx(m.y, m.subY) - camY + mh::HUD_H;
    const int32_t w = m.w;
    const int32_t h = m.h;

    // Body, feet, head and eyes are baked per state/facing into the sprite;
    // recover dims the body, windup flash and hit flash whiten it.
    const bool flashing = (m.state == mh::MS_WINDUP) &&
                          (((m.windupMax - m.t) / 4) % 2 == 0);
    uint8_t state = spr::MON_IDLE;
    if (m.state == mh::MS_RECOVER) state = spr::MON_RECOVER;
    if (m.hitFlash > 0 || flashing) state = spr::MON_FLASH;
    if (m.state == mh::MS_DEAD) state = spr::MON_DEAD;
    const uint8_t f = static_cast<uint8_t>(state + (m.fx >= 0 ? 0 : spr::MON_WEST));
    sprDraw(fxmonster, x, y, FRAME(f));
    if (m.state == mh::MS_DEAD) return;

    if (m.stun > 0) {
        const uint8_t a = static_cast<uint8_t>(static_cast<uint32_t>(g.tick) * ANG_MONSTER_STUN);
        blk(x + w / 2 + mulQ4(cos256(a), 9),
            y - 3 + mulQ4(sin256(a), 2), 2, 2, 2);
    }

    if (m.state == mh::MS_WINDUP || m.state == mh::MS_ATTACK) {
        const mh::MonsterAttack* a = m.atk;
        if (a) {
            const int32_t reach = mh::monsterAttackReach(a);
            const int32_t ax = x + w / 2 + (((int32_t)m.fx * reach) >> 4);
            const int32_t ay = y + h / 2 + (((int32_t)m.fy * reach) >> 4);
            const int32_t hw = mh::monsterAttackHw(a);
            const int32_t hh = mh::monsterAttackHh(a);
            if (m.state == mh::MS_WINDUP) {
                blk(ax - hw / 2, ay - hh / 2, hw, hh, 1);
                blk(ax - 1, ay - 1, 2, 2, 2);
            } else {
                blk(ax - hw / 2, ay - hh / 2, hw, hh, 2);
                blk(ax - 2, ay - 2, 4, 4, 3);
            }
        }
    }
}

// Mock drawPlayer(): shadow, body, weapon-specific silhouette, i-frame flicker
// and stun sparkle. Sword arc / parry, flail chain + whirl ring, gun plate.
static void drawPlayer(const mh::Game& g, int16_t camX, int16_t camY) {
    const mh::Player& p = g.player;
    const int32_t x = rndPx(p.x, p.subX) - camX;
    const int32_t y = rndPx(p.y, p.subY) - camY + mh::HUD_H;
    const int32_t cx = x + 8;
    const int32_t cy = y + 8;

    // Shadow + body from the FX sheet; dodge dims the body one shade.
    const uint8_t bodyFrame = (p.state == mh::PS_DODGE) ? spr::PLAYER_DODGE
                                                        : spr::PLAYER_NORMAL;
    sprDraw(fxplayer, x, y, FRAME(bodyFrame));

    if (g.weapon == mh::W_SWORD) {
        if (p.state == mh::PS_ATTACK || p.state == mh::PS_SPECIAL) {
            const mh::Attack* a = p.atk;
            if (a) {
                const int16_t startup = mh::attackStartup(a);
                const int16_t active = mh::attackActive(a);
                const uint8_t phase = p.t < startup ? 0 : (p.t < startup + active ? 1 : 2);
                int32_t reach = mh::attackReach(a);
                if (phase != 1) reach = reach * 6 / 10; // mock 0.6 arc
                const int32_t hx = cx + (((int32_t)p.fx * reach) >> 4);
                const int32_t hy = cy + (((int32_t)p.fy * reach) >> 4);
                const int32_t hw = mh::attackHw(a);
                const int32_t hh = mh::attackHh(a);
                blk(hx - hw / 2, hy - hh / 2, hw, hh, phase == 1 ? 2 : 1);
                blk(hx - 2, hy - 2, 4, 4, 3);
                if (p.state == mh::PS_SPECIAL && p.riposteT > 0) {
                    blk(hx - hw / 2 - 2, hy - hh / 2 - 2, hw + 4, hh + 4, 2);
                }
            }
        } else if (p.stance == mh::ST_PARRY) {
            blk(cx - 1, cy - 12, 2, 14, 3);
            blk(cx - 3, cy - 14, 6, 2, 2);
        } else {
            blk(cx + ((p.fx * 7) >> 4) - 1, cy + ((p.fy * 7) >> 4) - 1, 3, 3, 3);
        }
    } else if (g.weapon == mh::W_FLAIL) {
        if (p.stance == mh::ST_WHIRL) {
            const uint8_t ang = static_cast<uint8_t>(p.whirlTick * ANG_WHIRL_RING);
            for (uint8_t i = 0; i < 6; i++) {
                const uint8_t ai = static_cast<uint8_t>(ang + mhPgmReadU8(&RING6[i]));
                blk(cx + mulQ4(cos256(ai), 20), cy + mulQ4(sin256(ai), 14), 2, 2, 2);
            }
            const uint8_t ba = static_cast<uint8_t>(p.whirlTick * ANG_WHIRL_BALL);
            blk(cx + mulQ4(cos256(ba), 20) - 2, cy + mulQ4(sin256(ba), 14) - 2, 4, 4, 3);
        } else if (p.state == mh::PS_ATTACK || p.state == mh::PS_SPECIAL) {
            const mh::Attack* a = p.atk;
            if (a) {
                const int16_t startup = mh::attackStartup(a);
                const int16_t active = mh::attackActive(a);
                const uint8_t phase = p.t < startup ? 0 : (p.t < startup + active ? 1 : 2);
                int32_t reach = mh::attackReach(a);
                if (phase != 1) reach = reach / 2; // mock 0.5 chain
                for (int32_t i = 1; i <= 3; i++) {
                    const int32_t rr = (reach * i) >> 2;
                    blk(cx + (((int32_t)p.fx * rr) >> 4), cy + (((int32_t)p.fy * rr) >> 4),
                        1, 1, 2);
                }
                blk(cx + (((int32_t)p.fx * reach) >> 4) - 2,
                    cy + (((int32_t)p.fy * reach) >> 4) - 2, 4, 4, 3);
            }
        } else {
            blk(cx + ((p.fx * 4) >> 4), cy + ((p.fy * 4) >> 4), 1, 1, 2);
            blk(cx + ((p.fx * 9) >> 4) - 1, cy + ((p.fy * 9) >> 4) - 1, 3, 3, 3);
        }
        if (p.state == mh::PS_DEFLECT) {
            blk(x - 2, y + 2, 1, 12, 2);
            blk(x + p.w + 1, y + 2, 1, 12, 2);
        }
    } else { // gunshield
        const bool guard = (p.stance == mh::ST_GUARD);
        const int32_t shx = cx + ((p.fx * 5) >> 4);
        const int32_t shy = cy + ((p.fy * 5) >> 4);
        blk(shx - 5, shy - 7, 10, 14, guard ? 3 : 2);
        blk(shx - 1, shy - 7, 2, 14, 0);
        if (p.state == mh::PS_SHOVE) {
            blk(shx + ((p.fx * 4) >> 4) - 5, shy + ((p.fy * 4) >> 4) - 7, 10, 14, 3);
        }
        if (p.reload > 0) blk(x + 3, y - 3, 10, 2, 2);
    }

    if (p.iT > 0 && (g.tick % 4) < 2) blk(x + 6, y + 3, 4, 1, 0);
    if (p.state == mh::PS_STUN) {
        const uint8_t a = static_cast<uint8_t>(static_cast<uint32_t>(g.tick) * ANG_PLAYER_STUN);
        blk(cx + mulQ4(cos256(a), 7), y - 2 + mulQ4(sin256(a), 2), 2, 2, 3);
    }
}

// Mock drawProjectiles(): 3-puff trail then ball (rim/core/base) or pellet.
static void drawProjectiles(const mh::Game& g, int16_t camX, int16_t camY) {
    for (int16_t i = 0; i < g.projN; i++) {
        const mh::Projectile& pr = g.proj[i];
        const int32_t x = pr.x - camX;
        const int32_t y = pr.y - camY + mh::HUD_H;
        const int32_t bx = (pr.vx * 2) >> 4;
        const int32_t by = (pr.vy * 2) >> 4;

        blk(x - bx * 2 - 1, y - by * 2 - 1, 2, 2, 1);
        blk(x - bx * 3 - 1, y - by * 3 - 1, 2, 2, 1);
        blk(x - bx - 1, y - by - 1, 2, 2, 2);

        const int32_t hw = pr.w >> 1;
        const int32_t hh = pr.h >> 1;
        // Ball (7x8) / scatter (4x8) sheets; art occupies the top 7x6 / 4x4.
        if (pr.heavy) sprDraw(fxball, x - hw, y - hh, FRAME(0));
        else          sprDraw(fxscatter, x - hw, y - hh, FRAME(0));
    }
}

// Mock drawEffects(): 4-point spark, or a rising damage number.
// Render-side cap: the mock has no cap (unbounded array) and the device core
// caps at MAX_EFFECTS, but the transient worst case (several simultaneous
// damage-number glyphs + sparks) is the render bottleneck — each damage number
// is 2-3 FX glyph reads. Only the newest MAX_FX_DRAW effects are painted.
// Core sim is untouched: every effect still ticks and expires as before.
constexpr int16_t MAX_FX_DRAW = 6;

static void drawEffects(const mh::Game& g, int16_t camX, int16_t camY) {
    const int16_t first = g.fxN > MAX_FX_DRAW ? g.fxN - MAX_FX_DRAW : 0;
    for (int16_t i = first; i < g.fxN; i++) {
        const mh::Effect& e = g.fx[i];
        const int16_t r = e.life - e.t;
        if (e.text) {
            const int32_t x = e.x - camX;
            const int32_t y = static_cast<int32_t>(e.y) - (r + 1) / 3 - camY + mh::HUD_H;
            drawNumber(x - 2, y, e.text, e.crit ? 3 : 2);
        } else {
            // 4x4 spark sprite centred on the effect; crit selects the white
            // plane. TODO: the mock expands the 4 dots with radius r — the FX
            // sprite is fixed size, so the spread animation is dropped.
            const uint8_t f = e.crit ? spr::SPARK_BRIGHT : spr::SPARK_LIGHT;
            const int32_t x = e.x - camX;
            const int32_t y = e.y - camY + mh::HUD_H;
            sprDraw(fxspark, x - 2, y - 2, FRAME(f));
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
    if (w < 1 || h < 1) return;
    blk(x, y, w, 1, 3);
    blk(x, y + h - 1, w, 1, 3);
    blk(x, y, 1, h, 3);
    blk(x + w - 1, y, 1, h, 3);
}

// Dotted 1 px border (hit boxes): every other pixel on each edge.
static void wireDot(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (w < 1 || h < 1) return;
    for (int32_t i = 0; i < w; i += 2) {
        blk(x + i, y, 1, 1, 3);
        blk(x + i, y + h - 1, 1, 1, 3);
    }
    for (int32_t j = 0; j < h; j += 2) {
        blk(x, y + j, 1, 1, 3);
        blk(x + w - 1, y + j, 1, 1, 3);
    }
}

static void drawDebug(const mh::Game& g, int16_t camX, int16_t camY) {
    const int32_t ox = -camX;
    const int32_t oy = -camY + mh::HUD_H;
    const mh::Player& p = g.player;

    // Hurt boxes (solid): player body, then the live target hurt rect (monster
    // body in hunt, training pole in train) as activeTarget() would report.
    wireSolid(p.x + ox, p.y + oy, p.w, p.h);
    if (g.target.alive)
        wireSolid(g.target.rect.x + ox, g.target.rect.y + oy,
                  g.target.rect.w, g.target.rect.h);

    // Active player melee hit box (dotted): the sim's meleeHitbox() rect, so
    // the wire matches the frame the overlap test actually runs against.
    if ((p.state == mh::PS_ATTACK || p.state == mh::PS_SPECIAL) && p.atk) {
        const mh::Rect hit = mh::meleeHitbox(p, p.atk);
        wireDot(hit.x + ox, hit.y + oy, hit.w, hit.h);
    }

    // Monster windup/attack hit box (dotted), same reach/facing projection and
    // hw/hh the monster hit test uses; the windup outline is the telegraph.
    if (g.mode == mh::MODE_HUNT && g.monster.atk &&
        (g.monster.state == mh::MS_WINDUP || g.monster.state == mh::MS_ATTACK)) {
        const mh::Monster& m = g.monster;
        const int32_t reach = mh::monsterAttackReach(m.atk);
        const int32_t hx = m.x + (m.w >> 1) + (((int32_t)m.fx * reach) >> 4);
        const int32_t hy = m.y + (m.h >> 1) + (((int32_t)m.fy * reach) >> 4);
        const int32_t hw = mh::monsterAttackHw(m.atk);
        const int32_t hh = mh::monsterAttackHh(m.atk);
        wireDot(hx - (hw >> 1) + ox, hy - (hh >> 1) + oy, hw, hh);
    }

    // Live shell/projectile hit rects (dotted), exact pr.w x pr.h collision box.
    for (int16_t i = 0; i < g.projN; i++) {
        const mh::Projectile& pr = g.proj[i];
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
        const mh::Effect& e = g.fx[i];
        if (e.text || e.t >= e.life) continue;
        const int32_t sx = e.x + ox;
        const int32_t sy = e.y + oy;
        blk(sx, sy - 1, 1, 3, 3);
        blk(sx - 1, sy, 3, 1, 3);
    }
}
#endif // DEBUG_HURTBOXES

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

static uint8_t hudDigits(int32_t v) {
    uint8_t n = 1;
    while (v >= 10) { v /= 10; n++; }
    return n;
}

// Print a non-negative value as exactly `digits` digits (leading zeros).
static int16_t hudNum(int16_t x, int32_t v, uint8_t digits) {
    if (digits > 5) digits = 5;
    char b[5];
    for (int8_t i = static_cast<int8_t>(digits - 1); i >= 0; i--) {
        b[i] = static_cast<char>('0' + v % 10);
        v /= 10;
    }
    for (uint8_t i = 0; i < digits; i++) x = hudPut(x, b[i]);
    return x;
}

// Mock bar(): dark back/border, inner fill width round((w-2) * ratio).
static void hudBar(int32_t x, int32_t y, int32_t w, int32_t h,
                   int32_t num, int32_t den, uint8_t shade) {
    blk(x, y, w, h, 1);
    if (den <= 0 || num <= 0) return;
    if (num > den) num = den;
    int32_t fw = ((w - 2) * num + den / 2) / den;
    if (fw > w - 2) fw = w - 2;
    if (fw > 0) blk(x + 1, y + 1, fw, h - 2, shade);
}

static void drawHud(const mh::Game& g) {
    const mh::Player& p = g.player;

    // No strip background fill: ArduboyG waitForNextPlane(BLACK) wipes the
    // framebuffer black before each plane, and blk() clamps the arena band to
    // y >= HUD_H anyway, so a y=0 HUD wipe was a no-op.
    blk(0, mh::HUD_H - 1, mh::SCREEN_W, 1, 1);    // divider at the arena edge

    hudBar(1, 2, 28, 4, p.hp, p.hpMax, 3);        // player HP (white)
    hudBar(29, 2, 16, 4, p.stam, p.stamMax, 2);   // stamina (light gray)

    // Weapon marker (mock's full name shortened to fit the 128 px strip), then
    // the mode marker (device-only, the mock implied it via pole vs beast).
    int16_t x = 46;
    if (g.weapon == mh::W_SWORD) {
        x = hudPut(x, 'S'); x = hudPut(x, 'W'); x = hudPut(x, 'D');
    } else if (g.weapon == mh::W_FLAIL) {
        x = hudPut(x, 'F'); x = hudPut(x, 'L'); x = hudPut(x, 'A');
    } else {
        x = hudPut(x, 'G'); x = hudPut(x, 'U'); x = hudPut(x, 'N');
    }
    x = hudPut(x, g.mode == mh::MODE_TRAIN ? 'T' : 'H');

    if (g.weapon == mh::W_GUN) {                  // shell count + reload
        x = 67;
        if (p.reload > 0) {
            hudPut(x, 'R'); hudPut(x, 'L'); hudPut(x, 'D');
            const mh::ShellDef* sh =
                mh::weaponShell(&mh::WEAPON_DEFS[g.weapon], p.shell);
            const int16_t rmax = mh::shellReload(sh);
            if (rmax > 0) {
                int32_t bw = (12 * (rmax - p.reload) + rmax / 2) / rmax;
                if (bw < 1) bw = 1; else if (bw > 12) bw = 12;
                blk(67, 6, bw, 1, 2);
            }
        } else {
            x = hudPut(x, p.shell == 0 ? 'B' : 'S');
            hudNum(x, p.shells[p.shell], hudDigits(p.shells[p.shell]));
        }
    }

    if (g.mode == mh::MODE_TRAIN) {               // train total + DPS
        int32_t total = g.train.total;
        if (total > 9999) total = 9999;
        int32_t dps = mh::trainDps(g);
        if (dps > 999) dps = 999;
        const uint8_t nt = hudDigits(total);
        const uint8_t nd = hudDigits(dps);
        x = static_cast<int16_t>(127 - 4 * (nt + nd + 2));
        x = hudPut(x, 'T');
        x = hudNum(x, total, nt);
        x = hudPut(x, 'D');
        hudNum(x, dps, nd);
    } else {                                      // monster HP (hunt)
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
static void renderScene(const mh::Game& g, bool wire) {
    // Camera clamp to world bounds; also guards against an unclamped Game.
    int16_t camX = g.camX;
    int16_t camY = g.camY;
    if (camX < 0) camX = 0; else if (camX > mh::CAM_MAX_X) camX = mh::CAM_MAX_X;
    if (camY < 0) camY = 0; else if (camY > mh::CAM_MAX_Y) camY = mh::CAM_MAX_Y;

    // Mock g.shake has no Game field yet (freeze is not gated/decayed), so the
    // render derives an equivalent tick-based int offset from the decaying hit
    // indicators: the view kicks for the ~4 ticks a monster/pole hit flashes.
    // TODO(hitstop bead): replace with a real Game::shake value.
    int16_t shakeX = 0;
    int16_t shakeY = 0;
    const int16_t amp = g.monster.hitFlash > g.pole.hitFlash ? g.monster.hitFlash
                                                             : g.pole.hitFlash;
    if (amp > 0) {
        const uint8_t a1 = static_cast<uint8_t>(static_cast<uint32_t>(g.tick) * ANG_SHAKE_X);
        const uint8_t a2 = static_cast<uint8_t>(static_cast<uint32_t>(g.tick) * ANG_SHAKE_Y);
        shakeX = static_cast<int16_t>(mulQ4(sin256(a1), amp));            // mock sin(tick*1.7)*shake
        shakeY = static_cast<int16_t>(mulQ4(cos256(a2), (amp * 7 + 5) / 10)); // *0.7, round
    }
    const int16_t ecX = static_cast<int16_t>(camX - shakeX);
    const int16_t ecY = static_cast<int16_t>(camY - shakeY);

    drawArena(ecX, ecY);
    if (g.mode == mh::MODE_TRAIN) drawPole(g.pole, ecX, ecY);
    else drawMonster(g, ecX, ecY);
    drawPlayer(g, ecX, ecY);
    drawProjectiles(g, ecX, ecY);
    drawEffects(g, ecX, ecY);
#if DEBUG_HURTBOXES
    if (wire) drawDebug(g, ecX, ecY);
#else
    (void)wire;
#endif
    drawHud(g);
}

} // namespace mh
