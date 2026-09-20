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
#include "render_math.hpp"   // spinSheetFrame (host-tested frame selector)
#include "core/world.hpp"
#include "core/sin256.hpp"            // 256 B sine LUT -> 65 B quarter wave + sign folding (42n.7)
#include "generated/art_dims.hpp"     // frame layout + core dims for the FX sheets
#include "generated/equip_meta.hpp"   // gen-art part tables (sheet/frame/anchor) for drawPlayer

#if defined(__AVR__)
#include <avr/io.h>   // SPDR / SPSR for the fused room-image reader
#endif

#ifndef DEBUG_HURTBOXES
#define DEBUG_HURTBOXES 0
#endif

// fie.8 ground carve. 0 (shipping default) fills the playfield with the
// procedural dot field + room border (drawArena), the old basic texture; 1 uses
// the fie.5 stored-room-image blit (drawRoom + per-plane FX streaming). The
// carve is look/budget only -- MH_ROOM_BOUNDS stays 1 either way, so the room
// graph, doors, spawns, heal, per-room bounds, props (tent) and fade all stay
// live. The image pipeline (PNGs, gen-zones blob/meta, mh_map_* layers) stays in
// the tree; only the render path compiles out. test_zones forces 1 to keep the
// blit pixel evidence.
#ifndef MH_ROOM_IMAGE
#define MH_ROOM_IMAGE 0
#endif
static_assert(MH_ROOM_IMAGE == 0 || MH_ROOM_IMAGE == 1, "MH_ROOM_IMAGE must be 0 (procedural dots) or 1 (stored room image)");

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
// fxtail_spin 24x24 (heavy's tail_spin overlay): the tail rooted at the body
// centre, pointing world W / N / E / S. The frame is picked from the world
// direction of the active window's face-relative offset (drawMonster).
constexpr uint8_t SPIN_WEST = 0;
constexpr uint8_t SPIN_NORTH = 1;
constexpr uint8_t SPIN_EAST = 2;
constexpr uint8_t SPIN_SOUTH = 3;
}   // namespace spr

// Cull fully off-screen sprites before paying the FX seek, then blit on the
// current plane. Max sheet size is 40x40 (fxtailspin 40x40 spin body, pole
// variants 20x40, fxmonster 32x24), so these bounds stay conservative: a
// 40 px sprite at x == -39 still has a pixel column on screen.
MH_NOINLINE static inline void sprDraw(uint24_t img, int16_t x, int16_t y, uint8_t frame) {
    if (x <= -40 || x >= mh::SCREEN_W || y <= -40 || y >= mh::SCREEN_H)
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
MH_NOINLINE static int16_t mulQ4(int16_t a, int16_t b) {
    return static_cast<int16_t>((a * b + 8) >> 4);
}

// 256-step sine, Q4 fixed point (-16..16). See core/sin256.hpp for the
// 65-entry quarter-wave table + quadrant folding (42n.7); cos(a) = sin(a+64).

// Mock radian rates folded into 256-units-per-turn steps (x40.7437/rad):
// 0.35 rad -> 14, 0.55 -> 22, 0.30 -> 12, 2.3 -> 94 units/tick.
constexpr uint8_t ANG_WHIRL_RING = 14;
constexpr uint8_t ANG_WHIRL_BALL = 22;
constexpr uint8_t ANG_PLAYER_STUN = 12;
constexpr uint8_t ANG_MONSTER_STUN = 14;
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
MH_NOINLINE static void blk(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t shade) {
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
// The mock's three per-dot signed 16-bit modulos (i*7%3, i*53%roomW,
// i*29%roomH) lower to __divmodhi4 on AVR and dominated the plane budget, so
// the dot field is walked with incremental counters instead. They produce the
// exact same dot positions: (i*7)%3 == i%3 (a 3-phase counter), and each world
// coord advances by its fixed step with a single conditional wrap (step is
// always < the modulus, so one subtract bounds it). Integer-only, no float.
// `roomW`/`roomH` are the active-room extents (legacy WORLD_W/H by default);
// fie.8 keeps this as the shipping default ground (MH_ROOM_IMAGE 0); fie.5's
// stored-image blit compiles in only when MH_ROOM_IMAGE is 1.
static void drawArena(int16_t camX, int16_t camY, int16_t roomW, int16_t roomH) {
    uint8_t phase = 0;   // i % 3
    int16_t wx = 0;      // (i * 53) % roomW
    int16_t wy = 0;      // (i * 29) % roomH
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
        if (wx >= roomW)
            wx -= roomW;
        wy += 29;
        if (wy >= roomH)
            wy -= roomH;
    }
    const int16_t lx = static_cast<int16_t>(-camX);
    const int16_t ly = static_cast<int16_t>(mh::HUD_H - camY);
    blk(lx, ly, roomW, 1, 2);
    blk(lx, ly + roomH - 1, roomW, 1, 2);
    blk(lx, ly, 1, roomH, 2);
    blk(lx + roomW - 1, ly, 1, roomH, 2);
}

/* --------------------------------------------------------- room image blit */
// fie.5: replace the procedural dot field with the active room's stored image.
// Ported from the fie.7 v2b spike (build/spike7/render-carve.patch). The stored
// layer is Arduboy/SSD1306 page-major -- one byte == one pixel column of 8
// vertical px -- so an integer camX is a pure column offset (no horizontal bit
// shift) and only camY&7 needs a vertical split. The fused inline asm reads one
// SPI byte per iteration (SPDR read + next-byte kick), splits it with
// `mul byte, 1<<(8-v)` into r0 = byte<<(8-v) (high part -> dest page q-1) and
// r1 = byte>>v (low part -> dest page q+1), ORs both into the framebuffer, and
// is cycle-padded to 17 cycles/iteration vs the 16-cycle SPI byte time (SPI2X,
// 8 MHz), so no SPIF wait is needed. Dest pages 1..7; page 0 stays HUD. Every
// read happens in the render pass between plane blits, never during the
// ArduboyG paint.
#if MH_ROOM_BOUNDS

#if MH_ROOM_IMAGE
// Per-room image base + extent from the generated meta constants. Only the
// three shipped rooms exist; the default (pre-room) scene maps to area, the
// legacy room-0 image (its baked stride 384 must come from the record, not the
// legacy roomW 256 default).
static inline void roomImageInfo(uint8_t roomId, uint24_t &img, int16_t &w, int16_t &h) {
    if (roomId == zone::ROOM_CAMP) {
        img = mh_map_camp;
        w = static_cast<int16_t>(zone::ROOM_CAMP_W);
        h = static_cast<int16_t>(zone::ROOM_CAMP_H);
    } else {
        img = mh_map_area;
        w = static_cast<int16_t>(zone::ROOM_AREA_W);
        h = static_cast<int16_t>(zone::ROOM_AREA_H);
    }
}

// Fused streaming reader (fie.7 v2b). FX::seekData prefetches the page's first
// column; a SPIF wait aligns to it, then the asm streams the rest of the row
// with the `mul b, 1<<(8-v)` split (r0 = high part -> dest page q-1, r1 = low
// part -> dest page q+1). X = high/only dest, Z = low dest.
//
// The spike's cycle padding was one byte-time short of the 16-cycle SPI byte
// at SPI2X (8 MHz): every read lagged one column and the whole window shifted.
// test_zones caught it; the loops below keep a safety margin over 16 cycles
// between SPDR writes and the next read.
#if defined(__AVR__)
static void roomAsmDual(uint8_t *dstHi, uint8_t *dstLo, uint8_t coef, uint8_t n) {
    uint8_t b, t;
    asm volatile("1:                        \n\t"
                 "in  %[b], %[spdr]         \n\t"
                 "out %[spdr], __zero_reg__ \n\t"
                 "mul %[b], %[coef]         \n\t"
                 "ld  %[t], X               \n\t"
                 "or  %[t], r0              \n\t"
                 "st  X+, %[t]              \n\t"
                 "ld  %[t], Z               \n\t"
                 "or  %[t], r1              \n\t"
                 "st  Z+, %[t]              \n\t"
                 "nop                       \n\t"
                 "nop                       \n\t"
                 "nop                       \n\t"
                 "nop                       \n\t"
                 "dec %[n]                  \n\t"
                 "brne 1b                   \n\t"
                 "clr __zero_reg__          \n\t"
                 : [b] "=&r"(b), [t] "=&r"(t), [n] "+r"(n), "+x"(dstHi), "+z"(dstLo)
                 : [coef] "r"(coef), [spdr] "I"(_SFR_IO_ADDR(SPDR))
                 : "memory");
}

// v == 0: byte-for-byte page copy (no mul). Same SPIF-margin padding.
static void roomAsmCopy(uint8_t *dst, uint8_t n) {
    uint8_t b, t;
    asm volatile("1:                        \n\t"
                 "in  %[b], %[spdr]         \n\t"
                 "out %[spdr], __zero_reg__ \n\t"
                 "ld  %[t], X               \n\t"
                 "or  %[t], %[b]            \n\t"
                 "st  X+, %[t]              \n\t"
                 "nop                       \n\t"
                 "nop                       \n\t"
                 "nop                       \n\t"
                 "nop                       \n\t"
                 "nop                       \n\t"
                 "nop                       \n\t"
                 "nop                       \n\t"
                 "nop                       \n\t"
                 "dec %[n]                  \n\t"
                 "brne 1b                   \n\t"
                 : [b] "=&r"(b), [t] "=&r"(t), [n] "+r"(n), "+x"(dst)
                 : [spdr] "I"(_SFR_IO_ADDR(SPDR))
                 : "memory");
}
#endif

// Stream one plane of the room image into framebuffer pages 1..7. `camX` is an
// integer column offset; `camY&7` splits across the two source pages. The
// source page index q0+j+b must stay inside the image (camY clamp guarantees
// it: v != 0 implies camY <= h - ARENA_H - 1, so q0+7 < h/8).
__attribute__((noinline)) static void drawRoom(const Game &g, int16_t camX, int16_t camY) {
    uint24_t img;
    int16_t rw, rh;
    roomImageInfo(g.roomId, img, rw, rh);
    int16_t rx = camX;
    int16_t ry = camY;
    if (rx < 0)
        rx = 0;
    else if (rx > rw - SCREEN_W)
        rx = static_cast<int16_t>(rw - SCREEN_W);
    if (ry < 0)
        ry = 0;
    else if (ry > rh - ARENA_H)
        ry = static_cast<int16_t>(rh - ARENA_H);

    const uint8_t v = static_cast<uint8_t>(ry & 7);
    const uint8_t q0 = static_cast<uint8_t>(ry >> 3);
    const uint16_t rw16 = static_cast<uint16_t>(rw);
    const uint16_t layerBytes = static_cast<uint16_t>(rw16 * static_cast<uint16_t>(rh >> 3));
    const uint24_t layer = img + static_cast<uint24_t>(arduboy.currentPlane()) * static_cast<uint24_t>(layerBytes);
    const uint8_t pages = (v == 0) ? 7 : 8;
    const uint8_t coef = (v == 0) ? 0 : static_cast<uint8_t>(1u << (8 - v));
    uint8_t *fb = arduboy.getBuffer();
    uint8_t dummy[128];   // unused half at the window's first/last page

    for (uint8_t q = 0; q < pages; q++) {
        const uint24_t src = layer + static_cast<uint24_t>(static_cast<uint16_t>(q0 + q) * rw16) + static_cast<uint24_t>(static_cast<uint16_t>(rx));
#if defined(__AVR__)
        FX::seekData(src);
        // Wait for the prefetched first column; the asm's paced loop then
        // covers the remaining 127. readEnd drains the last kick and releases
        // the FX bus for the next seek.
        while (!(SPSR & _BV(SPIF))) {
        }
        if (v == 0) {
            uint8_t *d = (q < 7) ? fb + static_cast<uint16_t>(1 + q) * SCREEN_W : dummy;
            roomAsmCopy(d, 128);
        } else {
            uint8_t *lo = (q < 7) ? fb + static_cast<uint16_t>(1 + q) * SCREEN_W : dummy;
            uint8_t *hi = (q >= 1) ? fb + static_cast<uint16_t>(q) * SCREEN_W : dummy;
            roomAsmDual(hi, lo, coef, 128);
        }
        FX::readEnd();
#else
        // Host fallback (render.hpp is device-only; kept compilable).
        uint8_t row[128];
        FX::readDataBytes(src, row, 128);
        if (v == 0) {
            uint8_t *d = (q < 7) ? fb + static_cast<uint16_t>(1 + q) * SCREEN_W : dummy;
            for (uint8_t x = 0; x < 128; x++)
                d[x] = static_cast<uint8_t>(d[x] | row[x]);
        } else {
            uint8_t *lo = (q < 7) ? fb + static_cast<uint16_t>(1 + q) * SCREEN_W : dummy;
            uint8_t *hi = (q >= 1) ? fb + static_cast<uint16_t>(q) * SCREEN_W : dummy;
            for (uint8_t x = 0; x < 128; x++) {
                hi[x] = static_cast<uint8_t>(hi[x] | static_cast<uint8_t>(row[x] << (8 - v)));
                lo[x] = static_cast<uint8_t>(lo[x] | static_cast<uint8_t>(row[x] >> v));
            }
        }
#endif
    }
}

#endif   // MH_ROOM_IMAGE

// Room props: the active room's prop records blitted as FX sprites over the
// ground layer (stored room image or procedural dot field) and under the
// actors. `sheet` indexes the zone::SHEET_* list; the
// two shipped sheets resolve (tent in images/blocks, the training pole in the
// blocks section). Static decoration only -- props have no hit test.
//
// Gather nodes (bead monhun-ardu-feel.22) draw a procedural 3-shade shape keyed
// off `gatherItem` instead of the sheet (no new art); a picked node draws
// nothing. prg.4 gives each item kind its own silhouette so ore/mushroom/bug
// read apart at 1x: herb keeps the stem/leaf/flower, mushroom is a light stalk
// + white cap, ore a squat dark rock with a light facet, bug a low body +
// wing highlight. While the hunter stands in the node a 2x2 white prompt marks
// the top (all kinds).
static void drawGatherNode(int16_t gx, int16_t gy, uint8_t item, bool inside) {
    switch (item) {
    case zone::GATHER_BLUE_MUSHROOM:
        blk(static_cast<int16_t>(gx + 3), static_cast<int16_t>(gy + 4), 2, 3, 2);   // stalk
        blk(static_cast<int16_t>(gx + 2), static_cast<int16_t>(gy + 2), 4, 3, 3);   // cap
        break;
    case zone::GATHER_ORE:
        blk(static_cast<int16_t>(gx + 1), static_cast<int16_t>(gy + 4), 6, 3, 1);   // rock body
        blk(static_cast<int16_t>(gx + 2), static_cast<int16_t>(gy + 3), 3, 2, 2);   // facet
        break;
    case zone::GATHER_BUG:
        blk(static_cast<int16_t>(gx + 2), static_cast<int16_t>(gy + 4), 4, 1, 2);   // wings
        blk(static_cast<int16_t>(gx + 3), static_cast<int16_t>(gy + 5), 2, 2, 1);   // body
        break;
    default:                                                                        // herb
        blk(static_cast<int16_t>(gx + 3), static_cast<int16_t>(gy + 3), 2, 4, 1);   // stem
        blk(static_cast<int16_t>(gx + 2), static_cast<int16_t>(gy + 3), 4, 2, 2);   // leaves
        blk(static_cast<int16_t>(gx + 3), static_cast<int16_t>(gy + 1), 2, 2, 3);   // flower
        break;
    }
    if (inside)
        blk(static_cast<int16_t>(gx + 3), static_cast<int16_t>(gy), 2, 2, 3);   // prompt
}

static inline uint24_t propSheet(uint8_t sheet) {
    return sheet ? fxpole : mh_map_tent;   // SHEET_MH_MAP_TENT 0, else SHEET_FXPOLE
}

static void drawProps(const Game &g, int16_t camX, int16_t camY) {
    // Hoist the camera->screen add once per call; a prop loop otherwise repeats
    // the same -camX / +HUD_H-camY per record.
    const int16_t px = static_cast<int16_t>(-camX);
    const int16_t oy = static_cast<int16_t>(HUD_H - camY);
    const Rect pr = bodyRect(g.player);
    for (uint8_t i = 0; i < g.roomPropCount; i++) {
        const uint8_t idx = static_cast<uint8_t>(g.roomFirstProp + i);
        const ZoneProp p = zonePropRead(idx);
        if (p.gatherItem != zone::GATHER_NONE) {
            const int16_t gx = static_cast<int16_t>(p.x + px);
            const int16_t gy = static_cast<int16_t>(p.y + oy);
            if (gatherNodeDepleted(g, idx))
                continue;   // picked: node is inert and draws nothing
            Rect nr;
            nr.x = static_cast<int16_t>(p.x);
            nr.y = static_cast<int16_t>(p.y);
            nr.w = p.w;
            nr.h = p.h;
            drawGatherNode(gx, gy, p.gatherItem, pr.overlaps(nr));
            continue;
        }
        sprDraw(propSheet(p.sheet), static_cast<int16_t>(p.x + px), static_cast<int16_t>(p.y + oy), FRAME(p.frame));
    }
}

// Door-cross black wipe: loadRoom arms Game::fade (FADE_TICKS) and stepGame
// decays it. Collapsed to the cheapest wipe that still covers a door cross:
// a full-arena shade-0 blk() on every plane, no per-tick height math and no
// cart traffic. The 4-tick arm/decay times the blackout; covers the scene but
// not the HUD strip (minY clip).
static inline void drawFade(const Game &g) {
    if (g.fade == 0)
        return;
    blk(0, HUD_H, SCREEN_W, ARENA_H, 0);
}
#endif   // MH_ROOM_BOUNDS

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

// Zone part-art overlay (bead monhun-ardu-kt7.6): draw one breakable zone's
// part from its 4-frame combatPartArtFrame sheet (east intact / east broken /
// west intact / west broken). The 32x24 beast sheets are 2-facing (east / west
// mirror), so the baked part art sits at the authored box for east and at the
// cell mirror (monster_w - ox - w) for west; the overlay snaps to that facing
// frame -- it must NOT rotate with the DIR8 hit-test offset (combatFaceOffset):
// rotating detaches the part from the baked body art whenever the beast faces
// west/N/S/diagonals (doubled chicken head/legs, floating bull horns/hooves,
// and the heavy tail parked above the body when the hunter passes under it).
// The fx sign picks the same frame the body sheet uses (fx >= 0 east), so the
// intact frame repaints the baked part exactly and the broken frame's shade-0
// erase lands on it. The hurt zones keep their face-relative rotation
// (combatZoneContains); only the paint is snapped. `zoneIdx` is the slot
// (COMBAT_ZONE_HEAD/APPENDAGE) whose cached box gives the offset; `zoneBit` is
// the matching zoneBroken bit. Every sheet's frame height is a multiple of 8 so
// the SpritesU plus-mask page stride is exact.
static void drawZonePart(const mh::Game &g, int16_t x, int16_t y, uint24_t sheet, uint8_t zoneIdx, uint8_t zoneBit) {
    const mh::CombatBox &zb = g.combat.zone[zoneIdx].box;
    const bool west = g.monster.fx < 0;
    const int16_t ox = static_cast<int16_t>(west ? art_dims::monster_w - zb.ox - zb.w : zb.ox);
    const uint8_t broken = (g.combat.zoneBroken & zoneBit) ? 1 : 0;
    const uint8_t f = mh::combatPartArtFrame(west, broken);
    sprDraw(sheet, static_cast<int16_t>(x + ox), static_cast<int16_t>(y + zb.oy), FRAME(f));
}

// 1 px outline reused by the RING/ZONE tells (shade 2, world clip via blk).
static void tellOutline(int16_t x, int16_t y, int16_t w, int16_t h) {
    blk(x, y, w, 1, 2);
    blk(x, static_cast<int16_t>(y + h - 1), w, 1, 2);
    blk(x, y, 1, h, 2);
    blk(static_cast<int16_t>(x + w - 1), y, 1, h, 2);
}

// Per-attack windup telegraph (feel.5): the shape comes from the cached attack
// (g.combat.attack.tell) and the area from the cached window, so no cart read
// happens during paint. `x,y` is the monster's screen top-left. tell 0 keeps the
// legacy 2x2 shade-2 core; the attack-phase 4x4 shade-3 marker is unchanged.
static void drawMonsterTell(const mh::Game &g, int16_t x, int16_t y) {
    const mh::Monster &m = g.monster;
    if (m.atkIdx == mh::COMBAT_NO_ATTACK)
        return;
    const mh::CombatBox &b = g.combat.attack.win.box;
    int16_t dx, dy;
    mh::combatFaceOffset(m.fx, m.fy, b, dx, dy);
    const int16_t cx = static_cast<int16_t>(x + (m.w >> 1));
    const int16_t cy = static_cast<int16_t>(y + (m.h >> 1));
    const int16_t ax = static_cast<int16_t>(cx + dx);
    const int16_t ay = static_cast<int16_t>(cy + dy);
    if (m.state == mh::MS_ATTACK) {
        blk(static_cast<int16_t>(ax - 2), static_cast<int16_t>(ay - 2), 4, 4, 3);
        return;
    }
    const uint8_t tell = g.combat.attack.tell;
    if (!mh::tellNeedsWindow(tell)) {
        blk(static_cast<int16_t>(ax - 1), static_cast<int16_t>(ay - 1), 2, 2, 2);
        return;
    }
    const int16_t bw = b.w;
    const int16_t bh = b.h;
    if (tell == mh::TELL_LINE) {
        for (uint8_t i = 1; i <= 3; i++) {
            int16_t ox, oy;
            mh::tellLineDash(dx, dy, i, ox, oy);
            blk(static_cast<int16_t>(cx + ox - 1), static_cast<int16_t>(cy + oy - 1), 2, 2, 2);
        }
    } else if (tell == mh::TELL_ARC) {
        int16_t rx, ry;
        mh::tellRectOrigin(ax, ay, bw, bh, rx, ry);
        for (uint8_t i = 0; i < 3; i++) {
            int16_t ox, oy;
            mh::tellArcSeg(bw, bh, i, ox, oy);
            blk(static_cast<int16_t>(rx + ox), static_cast<int16_t>(ry + oy), 4, 2, 2);
        }
    } else if (tell == mh::TELL_RING) {
        const int16_t elapsed = static_cast<int16_t>(m.windupMax - m.t);
        const int16_t hw = mh::tellRingHalf(static_cast<int16_t>(bw >> 1), elapsed);
        const int16_t hh = mh::tellRingHalf(static_cast<int16_t>(bh >> 1), elapsed);
        const int16_t rw = static_cast<int16_t>(hw << 1);
        const int16_t rh = static_cast<int16_t>(hh << 1);
        int16_t rx, ry;
        mh::tellRectOrigin(ax, ay, rw, rh, rx, ry);
        tellOutline(rx, ry, rw, rh);
    } else {   // ZONE
        int16_t rx, ry;
        mh::tellRectOrigin(ax, ay, bw, bh, rx, ry);
        tellOutline(rx, ry, bw, bh);
    }
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
    // Locked (spin) tail attack on the longtail (beads monhun-ardu-nch.3/5):
    // MS_ATTACK draws the whole beast from the 8-frame 40x40 fxtailspin sheet,
    // rotated about the body centre in 45-deg steps synced to the active window;
    // MS_WINDUP draws the same sheet held at the locked away frame (start8), so
    // the beast visibly looks away with its tail at the hunter for all 8
    // directions -- the 2-facing E/W beast sheet cannot show N/S. The small
    // fxtail_spin overlay stays as the windup tell, and the resting fxtail_heavy
    // overlay is skipped in both phases. Trade: the spin sheet has no windup
    // flash frame (the tell + telegraph core carry the timing).
    const bool spinning = (m.state == mh::MS_WINDUP || m.state == mh::MS_ATTACK) && m.atkIdx != mh::COMBAT_NO_ATTACK && mh::combatFacingLockV(g.combat.attack.facing);
    const bool spinSheet = spinning && g.monsterKind == mh::MON_HEAVY;
    // Chicken attack overlay (bead monhun-ardu-nch.8): during the peck/leap
    // windup+attack the whole chicken is drawn from the 4-frame 32x24
    // fxchickenatk sheet instead of the generic BEAST_POSES coil/lunge frame, so
    // both attacks read as bespoke art. MON_LUNGE is the chicken roster kind
    // (monsterCreatureId maps it to data/creatures/lunge.json). Frame order is
    // [peck E, peck W, leap E, leap W]: ordinal 0 = peck, 1 = leap, taken from
    // the attack index relative to the creature's first authored attack so the
    // mapping keeps following the JSON attack order without a literal index;
    // frame = (ordinal << 1) | (west). Windup and attack share the pose (the
    // overlay has no windup-flash frame; the tell + telegraph carry the timing,
    // same trade as fxtailspin). Cosmetic only: no hit-test or window change.
    // feel.8 added the third chicken attack (wing_beat), which has no pose in
    // the 4-frame sheet: only attacks whose ordinal fits the sheet (frames/2)
    // take the overlay, the rest fall through to the generic chicken sheet, so
    // the frame index can never leave the sheet.
    const bool chickenAtk = g.monsterKind == mh::MON_LUNGE && (m.state == mh::MS_WINDUP || m.state == mh::MS_ATTACK) && m.atkIdx != mh::COMBAT_NO_ATTACK &&
                            static_cast<uint8_t>(m.atkIdx - mh::combatCreatureFirstAttack(combat::CREATURE_LUNGE)) < static_cast<uint8_t>(art_dims::chickenatk_frames >> 1);
    // Bull attack overlay (bead monhun-ardu-nch.10): during the stomp/gore
    // windup+attack the whole bull is drawn from the 4-frame 32x24 fxbullatk
    // sheet instead of the generic BEAST_POSES coil/lunge frame. MON_SWEEP is
    // the bull roster kind (monsterCreatureId maps it to data/creatures/
    // sweep.json, whose authored attack order is stomp then gore). Frame order
    // is [stomp E, stomp W, gore E, gore W]: ordinal 0 = stomp, 1 = gore, taken
    // from the attack index relative to the creature's first authored attack so
    // the mapping keeps following the JSON order without a literal index; frame
    // = (ordinal << 1) | (west). Windup and attack share the pose (the overlay
    // has no windup-flash frame; the tell + telegraph carry the timing, same
    // trade as fxtailspin/fxchickenatk). Cosmetic only: no hit-test or window
    // change.
    const bool bullAtk = g.monsterKind == mh::MON_SWEEP && (m.state == mh::MS_WINDUP || m.state == mh::MS_ATTACK) && m.atkIdx != mh::COMBAT_NO_ATTACK;
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
    if (spinSheet) {
        // Whole-beast spin sheet: frame 0 is the east silhouette. Windup holds
        // the locked away frame; the attack steps 45 deg clockwise from it each
        // active-window slice, so the beast completes one visible revolution.
        // The sheet is 40x40 with the body centre at (20,20), so it is centred
        // on the body box centre. No per-tick cart read: sheet constant + frame
        // math only.
        const uint8_t start8 = static_cast<uint8_t>(fp::dirIndexFromDelta(m.fx, m.fy)) & 7;
        const uint8_t spinF = (m.state == mh::MS_WINDUP) ? start8 : mh::spinSheetFrame(start8, m.t, static_cast<int16_t>(g.combat.attack.active));
        sprDraw(fxtailspin, static_cast<int16_t>(x + (w >> 1) - 20), static_cast<int16_t>(y + (h >> 1) - 20), FRAME(spinF));
    } else if (chickenAtk) {
        // Ordinal from the creature's first attack (peck; leap is +1 in the
        // authored attack order): no literal record index, and the sheet frame
        // selects facing with the low bit.
        const uint8_t ordinal = static_cast<uint8_t>(m.atkIdx - mh::combatCreatureFirstAttack(combat::CREATURE_LUNGE));
        const uint8_t cf = static_cast<uint8_t>((ordinal << 1) | (m.fx < 0 ? 1 : 0));
        sprDraw(fxchickenatk, x, y, FRAME(cf));
    } else if (bullAtk) {
        // Ordinal from the creature's first attack (stomp; gore is +1 in the
        // authored attack order): no literal record index, and the sheet frame
        // selects facing with the low bit.
        const uint8_t ordinal = static_cast<uint8_t>(m.atkIdx - mh::combatCreatureFirstAttack(combat::CREATURE_SWEEP));
        const uint8_t bf = static_cast<uint8_t>((ordinal << 1) | (m.fx < 0 ? 1 : 0));
        sprDraw(fxbullatk, x, y, FRAME(bf));
    } else {
        sprDraw(monsterSheet(g.monsterKind), x, y, FRAME(f));
    }
    if (m.state == mh::MS_DEAD)
        return;

    // Breakable-zone part overlays (monhun-ardu-4t4 heavy tail; kt7.6 chicken and
    // bull). Each sheet draws its part at the zone's cached face-relative box
    // origin (drawZonePart), i.e. the same world rect the hit test uses. Skips:
    // HEAVY's resting tail during the locked spin (the rotating fxtailspin sheet
    // carries the posed tail); the chicken/bull parts during their whole-body
    // attack sheets (fxchickenatk / fxbullatk already draw the posed part).
    // RAVAGER keeps the legacy 18x10 fxtail unoverlaid (not a multiple-of-8
    // SpritesU page stride), exactly as before.
    if (g.monsterKind == mh::MON_HEAVY) {
        if (g.combat.appendZone != mh::COMBAT_NO_ZONE && !spinning)
            drawZonePart(g, x, y, fxtail_heavy, mh::COMBAT_ZONE_APPENDAGE, mh::COMBAT_ZONE_APPENDAGE_BIT);
    } else if (g.monsterKind == mh::MON_LUNGE && !chickenAtk) {
        if (g.combat.headZone != mh::COMBAT_NO_ZONE)
            drawZonePart(g, x, y, fxhead_chicken, mh::COMBAT_ZONE_HEAD, mh::COMBAT_ZONE_HEAD_BIT);
        if (g.combat.appendZone != mh::COMBAT_NO_ZONE)
            drawZonePart(g, x, y, fxlegs_chicken, mh::COMBAT_ZONE_APPENDAGE, mh::COMBAT_ZONE_APPENDAGE_BIT);
    } else if (g.monsterKind == mh::MON_SWEEP && !bullAtk) {
        if (g.combat.headZone != mh::COMBAT_NO_ZONE)
            drawZonePart(g, x, y, fxhead_bull, mh::COMBAT_ZONE_HEAD, mh::COMBAT_ZONE_HEAD_BIT);
        if (g.combat.appendZone != mh::COMBAT_NO_ZONE)
            drawZonePart(g, x, y, fxhooves_bull, mh::COMBAT_ZONE_APPENDAGE, mh::COMBAT_ZONE_APPENDAGE_BIT);
    }

    if (m.stun > 0) {
        const uint8_t a = static_cast<uint8_t>(static_cast<uint32_t>(g.tick) * ANG_MONSTER_STUN);
        sprDraw(fxwhirl, x + w / 2 + mulQ4(cos256(a), 9), y - 3 + mulQ4(sin256(a), 2), FRAME(spr::WHIRL_DOT));
    }

    // Spin tail overlay (heavy's tail_spin, 4-frame 24x24 sheet): during the
    // locked WINDUP only, the tail whips toward the hunter as the tell. The
    // frame is the world direction of the cached window's face-relative offset
    // (|dx| > |dy| -> E/W else S/N). Windup caches window 0, so the tail points
    // at the hunter. During MS_ATTACK this overlay is skipped (nch.3): the
    // rotating fxtailspin body sheet above carries the read. Frame origin is
    // the body centre.
    if (spinning && m.state == mh::MS_WINDUP) {
        int16_t sdx, sdy;
        mh::combatFaceOffset(m.fx, m.fy, g.combat.attack.win.box, sdx, sdy);
        const int16_t adx = static_cast<int16_t>((sdx < 0) ? -sdx : sdx);
        const int16_t ady = static_cast<int16_t>((sdy < 0) ? -sdy : sdy);
        uint8_t sf;
        if (adx > ady)
            sf = (sdx < 0) ? spr::SPIN_WEST : spr::SPIN_EAST;
        else
            sf = (sdy < 0) ? spr::SPIN_NORTH : spr::SPIN_SOUTH;
        sprDraw(fxtail_spin, static_cast<int16_t>(x + (w >> 1) - 12), static_cast<int16_t>(y + (h >> 1) - 12), FRAME(sf));
    }

    // Telegraph at the cached window (feel.5): tell 0 is the legacy 2x2 shade-2
    // core, the other shapes describe the area the attack will cover; the
    // attack-phase 4x4 shade-3 marker is unchanged. Drawn from the cache, so no
    // cart read happens during paint. The full-window box fill read as a debug
    // hurt zone on playtest (nch.2), so only the shapes above are drawn.
    if (m.state == mh::MS_WINDUP || m.state == mh::MS_ATTACK)
        drawMonsterTell(g, x, y);
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

MH_NOINLINE static inline void partRead(uint8_t part, PartRec &rec) {
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

    // Charge meter above the hunter (mock drawPlayer): 16 px bar at cx-8, y-4,
    // 2 px tall; fill fraction min(1, chargeT/CHARGE_L2); light gray normally,
    // white at/above CHARGE_L2. Only the ynb charge stance draws it.
    if (p.state == mh::PS_CHARGE) {
        const int16_t fill = (p.chargeT * 16 + (mh::CHARGE_L2 >> 1)) / mh::CHARGE_L2;
        const int16_t w = fill < 1 ? 1 : (fill > 16 ? 16 : fill);
        blk(static_cast<int16_t>(cx - 8), static_cast<int16_t>(y - 4), w, 2, p.chargeT >= mh::CHARGE_L2 ? 3 : 2);
    }

    // Shared attack timing (sword and flail read the same startup/active/reach;
    // the two weapon branches below only scale the reach differently).
    const mh::Attack *a = (p.state == mh::PS_ATTACK || p.state == mh::PS_SPECIAL) ? p.atk : nullptr;
    uint8_t phase = 0;
    if (a) {
        const int16_t startup = mh::attackStartup(a);
        const int16_t active = mh::attackActive(a);
        phase = p.t < startup ? 0 : (p.t < startup + active ? 1 : 2);
    }

    // Stowed: skip the weapon overlay entirely (body + head only), exactly like
    // mock drawPlayer()'s `if (p.sheathed) {}` branch.
    if (!p.sheathed) {
        if (g.weapon == mh::W_SWORD) {
            if (a) {
                int16_t reach = mh::attackReach(a);
                if (phase != 1)
                    reach = static_cast<int16_t>(reach * 6 / 10);   // mock 0.6 arc
                // int16 product on purpose: |p.fx|,|p.fy| <= 16 (DIR8 unit) and
                // reach is a pixel reach from the attack record (< 256), so the
                // 16-bit multiply stays inside int16 and no int32 cast is needed.
                const int16_t hx = static_cast<int16_t>(cx + ((p.fx * reach) >> 4));
                const int16_t hy = static_cast<int16_t>(cy + ((p.fy * reach) >> 4));
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
                        partDraw(equip::PART_FLAIL_CHAIN, equip::POSE_IDLE, face, static_cast<int16_t>(cx + ((p.fx * rr) >> 4)), static_cast<int16_t>(cy + ((p.fy * rr) >> 4)));
                    }
                    partDraw(equip::PART_CHIP_BALL, equip::POSE_IDLE, face, static_cast<int16_t>(cx + ((p.fx * reach) >> 4)), static_cast<int16_t>(cy + ((p.fy * reach) >> 4)));
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
    }   // !p.sheathed

    if (p.iT > 0 && (g.tick % 4) < 2)
        partDraw(equip::PART_ERASE, equip::POSE_IDLE, face, cx, cy);
    if (p.state == mh::PS_STUN) {
        const uint8_t ang = static_cast<uint8_t>(static_cast<uint32_t>(g.tick) * ANG_PLAYER_STUN);
        partDraw(equip::PART_FLAIL_STUN, equip::POSE_STUN, face, cx + mulQ4(cos256(ang), 7), cy - 10 + mulQ4(sin256(ang), 2));
    }
}

// Mock drawProjectiles(): ball (rim/core/base) or pellet. prg.8 removed the
// 3-puff trail (cosmetic; the shot sprite alone reads at 4x8).
static void drawProjectiles(const mh::Game &g, int16_t camX, int16_t camY) {
    for (int16_t i = 0; i < g.projN; i++) {
        const mh::Projectile &pr = g.proj[i];
        const int16_t x = static_cast<int16_t>(pr.x - camX);
        const int16_t y = static_cast<int16_t>(pr.y - camY + mh::HUD_H);

        const int16_t hw = static_cast<int16_t>(pr.w >> 1);
        const int16_t hh = static_cast<int16_t>(pr.h >> 1);
        // Ball (7x8) / scatter (4x8) sheets; art occupies the top 7x6 / 4x4.
        if (pr.heavy)
            sprDraw(fxball, x - hw, y - hh, FRAME(0));
        else
            sprDraw(fxscatter, x - hw, y - hh, FRAME(0));
    }
}

// Mock drawEffects(): 4-point spark. prg.8 removed the rising damage-number
// text path (it was the render bottleneck: 2-3 FX glyph reads per effect); only
// the newest MAX_FX_DRAW sparks are painted. Core sim is untouched: every
// effect still ticks and expires as before.
constexpr int16_t MAX_FX_DRAW = 6;

static void drawEffects(const mh::Game &g, int16_t camX, int16_t camY) {
    const int16_t first = g.fxN > MAX_FX_DRAW ? g.fxN - MAX_FX_DRAW : 0;
    for (int16_t i = first; i < g.fxN; i++) {
        const mh::Effect &e = g.fx[i];
        // 4x4 spark sprite centred on the effect; crit selects the white
        // plane. TODO: the mock expands the 4 dots with radius r — the FX
        // sprite is fixed size, so the spread animation is dropped.
        const uint8_t f = e.crit ? spr::SPARK_BRIGHT : spr::SPARK_LIGHT;
        const int16_t x = static_cast<int16_t>(e.x - camX);
        const int16_t y = static_cast<int16_t>(e.y - camY + mh::HUD_H);
        sprDraw(fxspark, static_cast<int16_t>(x - 2), static_cast<int16_t>(y - 2), FRAME(f));
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
    // part (migration B: part boxes from g.combat.body, not w/h literals).
    wireSolid(p.x + ox, p.y + oy, p.w, p.h);
    if (g.target.alive) {
        const mh::CombatBox &b = g.combat.body;
        wireSolid(g.monster.x + b.ox + ox, g.monster.y + b.oy + oy, b.w, b.h);
    }

    // Active player melee hit box (dotted): the sim's meleeHitbox() rect, so
    // the wire matches the frame the overlap test actually runs against.
    if ((p.state == mh::PS_ATTACK || p.state == mh::PS_SPECIAL) && p.atk) {
        const mh::Rect hit = mh::meleeHitbox(p, p.atk);
        wireDot(hit.x + ox, hit.y + oy, hit.w, hit.h);
    }

    // Monster windup/attack hit box (dotted), same face-relative window centre
    // and size the monster hit test uses; the windup outline is the telegraph.
    if (g.monster.atkIdx != mh::COMBAT_NO_ATTACK && (g.monster.state == mh::MS_WINDUP || g.monster.state == mh::MS_ATTACK)) {
        const mh::Monster &m = g.monster;
        const mh::CombatBox &b = g.combat.body;
        int16_t dx, dy;
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

    // Hit-spark markers (small white plus) at live effects.
    for (int16_t i = 0; i < g.fxN; i++) {
        const mh::Effect &e = g.fx[i];
        if (e.t >= e.life)
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
// monster HP bar (hunt; train has no bar since the plain pole has no pool). The
// mock drew this as the bottom 8 px strip; the device reserves the top 8 px, so the strip is mirrored: the
// divider sits at the arena edge (y = HUD_H-1) and the bars/text fill rows
// 0..6. Drawn untranslated (mock restores the camera transform first) and
// read-only.
//
// Text is drawn from the FX glyph sheet (fxfontw, 4x8 ASCII tiles) at row 1 so
// the 5 px cap sits in HUD rows 1..5; advance 4 px, matching mock drawText.
static inline int16_t hudPut(int16_t x, char c) {
    return textPut(fxfontw, x, 1, c);
}

MH_NOINLINE static uint8_t hudDigits(int16_t v) {
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
// 32-bit product: the beast hp pool now reaches 2800 (feel.19), so (w-2)*num
// exceeds 16 bits (44 * 2800 = 123200) and a uint16 multiply would wrap. The
// 16x16->32 multiply and the 32/16 divide are budgeted in the feel.19 ledger.
static void hudBar(int16_t x, int16_t y, int16_t w, int16_t h, int16_t num, int16_t den, uint8_t shade) {
    hudBlk(x, y, w, h, 1);
    if (den <= 0 || num <= 0)
        return;
    if (num > den)
        num = den;
    const uint16_t u16den = static_cast<uint16_t>(den);
    const uint32_t u32num = static_cast<uint16_t>(num);
    uint16_t fw = static_cast<uint16_t>((static_cast<uint32_t>(static_cast<uint16_t>(w - 2)) * u32num + u16den / 2) / u16den);
    if (fw > w - 2)
        fw = w - 2;
    if (fw > 0)
        hudBlk(x + 1, y + 1, fw, h - 2, shade);
}

// Generic held-item count readout (prg.2): a 1 px stalk glyph + the value in
// the HUD band. The herb indicator below is the first caller; the drops/smith
// beads reuse it for their own counts (no new screen yet). A zero count draws
// nothing.
static void drawItemCount(int16_t x, int16_t y, uint8_t count) {
    if (count == 0)
        return;
    hudBlk(x, y, 1, 4, 3);   // 1 px plant/stack glyph
    drawNumber(static_cast<int16_t>(x + 1), static_cast<int16_t>(y - 1), count, 3);
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

    // Weapon marker (mock's full name shortened to fit the 128 px strip): the
    // 4 glyphs (3-char weapon + 1-char mode) on the 4 px lane at x=46 are baked
    // into one 16x8 FX strip (bead monhun-ardu-e4a), drawn with a single blit
    // per plane instead of 4 textPut() cart seeks. Frame order is
    // weapon*2 + mode (SWD/FLA/GUN x hunt/train); hunt is the only mode now
    // (prg.8), so frame = weapon*2. The pixels come from the same GLYPHS table
    // as fxfontw (gen-art check_hud_identity). The else branch keeps the old
    // "anything but sword/flail reads GUN" mapping.
    const uint8_t wf = static_cast<uint8_t>(g.weapon == mh::W_SWORD ? 0 : (g.weapon == mh::W_FLAIL ? 1 : 2));
    sprDraw(fxhud, 46, 1, FRAME(static_cast<uint8_t>(wf * 2)));

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

    hudBar(82, 2, 44, 3, g.monster.hp, g.monster.hpMax, 3);   // monster HP (hunt only)

    // Herb count (feel.22): a tiny 1 px plant glyph + one digit in the free
    // 5 px lane x=62..66 (after the 16-wide weapon marker at 46..61, before the
    // gun text at 67). Only drawn when a herb is held; total available in the
    // demo is 9 (camp 1+2, area 1+2+3), so one digit always fits. prg.2 moved
    // the shape into the generic drawItemCount helper.
    drawItemCount(62, 2, g.items[mh::ITEM_HERB]);
}

/* ------------------------------------------------------------------ scene */

// Rooted-action progress bar (feel.22). A fixed 32x4 bar near the top of the
// arena, drawn from the player state timer so both the gather and herb-use
// windows read the same way; reuses hudBar's back/fill geometry. p.t == 0 at
// entry and GATHER_TICKS/ITEM_USE_TICKS at completion, so the bar fills over
// the window. Drawn only while the action runs.
constexpr int16_t ITEM_BAR_X = 48;
constexpr int16_t ITEM_BAR_Y = HUD_H + 4;
constexpr int16_t ITEM_BAR_W = 32;
constexpr int16_t ITEM_BAR_H = 4;

static void drawUseBar(const mh::Game &g) {
    const mh::Player &p = g.player;
    if (p.state != mh::PS_GATHER && p.state != mh::PS_ITEM)
        return;
    const bool gather = p.state == mh::PS_GATHER;
    hudBar(ITEM_BAR_X, ITEM_BAR_Y, ITEM_BAR_W, ITEM_BAR_H, p.t, gather ? mh::GATHER_TICKS : mh::ITEM_USE_TICKS, gather ? 3 : 2);
}

// Full block-art scene, mock draw order: arena, beast, player, shells, effects,
// then the (untracked) debug wire overlay and HUD. Read-only:
// render never mutates Game. Shapes are identical on every plane (the L4 shade
// resolves in ArduboyG::planeColor), so the three passes composite to the same
// 4-level image.
//
// `wire` only has an effect when DEBUG_HURTBOXES is compiled in; shipping builds
// pass false and the overlay is preprocessed out.
static void renderScene(const mh::Game &g, bool wire) {
    // Camera clamp to the active-room bounds; also guards against an unclamped
    // Game. camX/camY are int16 so a >256 px room can scroll.
    int16_t camX = g.camX;
    int16_t camY = g.camY;
    const int16_t camMx = mh::camMaxX(g);
    const int16_t camMy = mh::camMaxY(g);
    if (camX < 0)
        camX = 0;
    else if (camX > camMx)
        camX = camMx;
    if (camY < 0)
        camY = 0;
    else if (camY > camMy)
        camY = camMy;

    // Screen shake (prg.8) is removed: the camera is the clamped follow only,
    // so no tick-derived view offset is applied.
    const int16_t ecX = camX;
    const int16_t ecY = camY;

#if MH_ROOM_BOUNDS
    // Ground layer (fie.8 carve): the stored room-image blit when MH_ROOM_IMAGE
    // is 1, otherwise the procedural dot field + border (shipping default).
    // Props and the door fade are shared. All stay inside the render pass
    // between plane blits.
#if MH_ROOM_IMAGE
    drawRoom(g, ecX, ecY);
#else
    drawArena(ecX, ecY, mh::roomBoundW(g), mh::roomBoundH(g));
#endif
    drawProps(g, ecX, ecY);
#else
    drawArena(ecX, ecY, mh::roomBoundW(g), mh::roomBoundH(g));
#endif
    drawMonster(g, ecX, ecY);
    drawPlayer(g, ecX, ecY);
    drawProjectiles(g, ecX, ecY);
    drawEffects(g, ecX, ecY);
#if MH_ROOM_BOUNDS
    drawUseBar(g);   // rooted gather/item progress (feel.22), under the wipe
    drawFade(g);     // door-cross wipe covers the scene, drawn under the HUD
#endif
#if DEBUG_HURTBOXES
    if (wire)
        drawDebug(g, ecX, ecY);
#else
    (void)wire;
#endif
    drawHud(g);
}

}   // namespace mh
