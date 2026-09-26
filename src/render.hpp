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
#include "generated/art_sheets.hpp"   // creature art sheet address table (bih)
#include "generated/equip_meta.hpp"   // gen-art part tables (sheet/frame/anchor) for drawPlayer
#include "generated/armor_meta.hpp"   // ARMOR_* piece ids for the armor head layer (arm.2)

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
//
// The 4x8 dimensions are fixed for every font tile, so textPut passes them to
// the explicit-dimension drawPlusMaskFX overload (monhun-ardu-dx5.3): the
// one-arg form seekData()s the sheet just to read its w/h header, a wasted
// cart transaction per glyph. Pixels are unchanged -- the blitter receives the
// same w/h it would have read.
static inline int16_t textPut(uint24_t sheet, int16_t x, int16_t y, char c) {
    const uint8_t code = static_cast<uint8_t>(c);
    if (code < 128 && x > -4 && x < mh::SCREEN_W)
        SpritesU::drawPlusMaskFX(x, y, 4, 8, sheet, FRAME(code));
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
    // dx5.7: hoist the plane/color mapping for the dot shade once. The dots are
    // color 1 (dark gray), which lights only on the planes where
    // planeColor(plane, 1) != 0 -- so a single `color(1)` read replaces the
    // per-dot map/inline-drawPixel overhead (~173 dots/plane). Lit dots OR
    // their one-hot page bit straight into the framebuffer; frame layout is
    // page-major (byte = page*128 + x), same as drawPixel. The bit is written
    // as `1u << (sy & 7)` (GCC emits the same inline branch chain drawPixel's
    // asm uses) rather than mhBit8: mhPgmReadU8 is deliberately noinline
    // (progmem.hpp), and that call per dot measured +90 us/render vs the old
    // drawPixel path -- a regression. The inline shift measured -84 us rAv.
    const bool dotLit = arduboy.color(1) != 0;
    uint8_t *const fb = arduboy.getBuffer();
    for (int16_t i = 0; i < 260; i++) {
        if (phase != 0) {
            const int16_t sx = static_cast<int16_t>(wx - camX);
            const int16_t sy = static_cast<int16_t>(wy - camY + mh::HUD_H);
            if (dotLit && sx >= 0 && sx < mh::SCREEN_W && sy >= mh::HUD_H && sy < mh::SCREEN_H)
                fb[((static_cast<uint16_t>(sy) >> 3) << 7) | static_cast<uint16_t>(sx)] |= static_cast<uint8_t>(1u << (sy & 7));
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

// v == 0: byte-for-byte page copy (no mul). Same SPIF-margin padding as the
// split reader. Shared by the room-image blit and the fixed 128x64 card blit
// (bead 5co.3): card pages are page-aligned, so the copy reader alone suffices.
#if defined(__AVR__)
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
#else
static void roomAsmCopy(uint8_t *dst, uint8_t n) {
    for (uint8_t i = 0; i < n; i++)
        dst[i] = static_cast<uint8_t>(dst[i] | 0);
}
#endif

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
#endif

// Stream one plane of the room image into framebuffer pages 1..7. `camX` is an
// integer column offset; `camY&7` splits across the two source pages. The
// source page index q0+j+b must stay inside the image (camY clamp guarantees
// it: v != 0 implies camY <= h - ARENA_H - 1, so q0+7 < h/8).
// Vertical page-shift coefficient for the split reader: row-coef[v] ==
// (v == 0) ? 0 : (1u << (8 - v)) for v = camY & 7. AVR has no barrel shifter,
// so the shift -> 8-entry flash LUT (techniques.md §3). Index is already v&7.
static const uint8_t MH_PROGMEM ROOM_ROW_COEF[8] = {
    0, 128, 64, 32, 16, 8, 4, 2,
};

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
    const uint8_t coef = mhPgmReadU8(&ROOM_ROW_COEF[v & 7]);
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

// Fixed 128x64 detail-card blit (bead monhun-ardu-5co.3). A card page is exactly
// one screen: one 1024 B 1 bpp layer per plane, page-aligned, so a single bulk
// FX::readDataBytes fills framebuffer pages 0..7 for the current plane
// (img + plane * 1024). Cheaper than the camera-windowed room path and than a
// per-page asm copy (the bulk reader is already linked for the screen text).
// Reads happen in the render pass between plane blits.
constexpr uint16_t CARD_LAYER_BYTES = 1024;

static void cardBlit(uint24_t img) {
    const uint24_t layer = img + static_cast<uint24_t>(arduboy.currentPlane()) * static_cast<uint24_t>(CARD_LAYER_BYTES);
    FX::readDataBytes(layer, arduboy.getBuffer(), CARD_LAYER_BYTES);
}

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

// Door-cross black wipe (demo fix: the old 4-tick wipe was easy to miss).
// loadRoom arms Game::fade (ROOM_TOAST_TICKS) and stepGame decays it; the wipe
// covers the first FADE_TICKS of the toast: a full-arena shade-0 blk() on every
// plane, no per-tick height math and no cart traffic. The room art then names
// the place (the 24x8 room-name sheet fxroom is authored but the HUD banner is
// budget-gated; see output.md).
static inline void drawFade(const Game &g) {
    if (g.fade <= ROOM_TOAST_TICKS - FADE_TICKS)
        return;
    blk(0, HUD_H, SCREEN_W, ARENA_H, 0);
}

#endif   // MH_ROOM_BOUNDS

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
    // The hit test mirrors in this exact frame (mh::combatZoneContains,
    // ZONE_CELL_W): paint and hitbox must agree or a swing at the visible part
    // registers as body (the chicken head bug).
    static_assert(mh::ZONE_CELL_W == art_dims::monster_w, "zone cell width drift");
    const mh::CombatBox &zb = g.combat.zone[zoneIdx].box;
    const bool west = g.monster.fx < 0;
    const int16_t ox = static_cast<int16_t>(west ? art_dims::monster_w - zb.ox - zb.w : zb.ox);
    const uint8_t broken = (g.combat.zoneBroken & zoneBit) ? 1 : 0;
    const uint8_t f = mh::combatPartArtFrame(west, broken);
    sprDraw(sheet, static_cast<int16_t>(x + ox), static_cast<int16_t>(y + zb.oy), FRAME(f));
}

// Generic art-descriptor body draw (epic monhun-ardu-bih): every creature draws
// its base body from the art_sheets.hpp address table (cached art.sheet seeded
// by creatureLoad) instead of a per-kind branch. The frame resolve is the shared
// rule: dead -> dead; hitFlash/windup-flash -> flash; windup -> windup; attack ->
// attack; recover -> recover; else idle0 + (idleCount ? (tick/8) % idleCount :
// 0); a west-facing creature (fx < 0) with a mirror stride adds it. drawMonster
// checks the spin/attack whole-body sheets first; the shared tail (stun whirl +
// spin tell overlay) and the zone part overlays (data-driven, bih.5) follow.
static inline uint24_t artSheetAddr(uint8_t index) {
#if defined(__AVR__)
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&art_sheets::ART_SHEETS[index]);
    return static_cast<uint24_t>(pgm_read_byte(p)) | (static_cast<uint24_t>(pgm_read_byte(p + 1)) << 8) | (static_cast<uint24_t>(pgm_read_byte(p + 2)) << 16);
#else
    (void)index;
    return 0;
#endif
}

MH_NOINLINE static void drawMonsterBodyGeneric(const mh::Game &g, int16_t x, int16_t y) {
    const mh::Monster &m = g.monster;
    const mh::CombatArt &a = g.combat.art;
    const bool windupFlash = (m.state == mh::MS_WINDUP) && (((m.windupMax - m.t) / 4) % 2 == 0);
    uint8_t f;
    if (m.state == mh::MS_DEAD)
        f = a.dead;
    else if (m.hitFlash > 0 || windupFlash)
        f = a.flash;
    else if (m.state == mh::MS_WINDUP)
        f = a.windup;
    else if (m.state == mh::MS_ATTACK)
        f = a.attack;
    else if (m.state == mh::MS_RECOVER)
        f = a.recover;
    else
        f = static_cast<uint8_t>(a.idle0 + (a.idleCount ? (g.tick / 8) % a.idleCount : 0));
    if (m.fx < 0 && a.stride)
        f = static_cast<uint8_t>(f + a.stride);
    sprDraw(artSheetAddr(static_cast<uint8_t>(a.sheet - 1)), x, static_cast<int16_t>(y + a.anchorY), FRAME(f));
}

// Mock drawMonster(): dead heap, feet, body, head + eyes, and stun sparkle.
// Presence gate (beastHere): the beast -- carcass included -- belongs to its
// home room only, so an off-home room draws nothing at the stale coordinates.
static void drawMonster(const mh::Game &g, int16_t camX, int16_t camY) {
    if (!mh::beastHere(g))
        return;   // the beast (carcass included) belongs to its home room only
    const mh::Monster &m = g.monster;
    const int16_t x = static_cast<int16_t>(rndPx(m.x, m.subX) - camX);
    const int16_t y = static_cast<int16_t>(rndPx(m.y, m.subY) - camY + mh::HUD_H);
    const int16_t w = m.w;
    const int16_t h = m.h;

    // Body draw (epic monhun-ardu-bih phase 2): every creature carries a cached
    // art descriptor seeded by creatureLoad, so the base body -- state frame,
    // idle bob and west mirror -- comes from the art_sheets table with no
    // per-kind code. The whole-body attack replacement is checked FIRST because
    // it supersedes the descriptor: the attack record's own art (bih.4) selects
    // the sheet and pose with no per-kind branch. The shared tail (stun whirl,
    // spin tell overlay) and the breakable-zone part overlays (data-driven, bih.5)
    // follow below.
    // Locked (spin) tail attack on the longtail (beads monhun-ardu-nch.3/5):
    // MS_ATTACK draws the whole beast from the 8-frame 40x40 fxtailspin sheet,
    // rotated about the body centre in 45-deg steps synced to the active window;
    // MS_WINDUP draws the same sheet held at the locked away frame (start8), so
    // the beast visibly looks away with its tail at the hunter for all 8
    // directions -- the 2-facing E/W beast sheet cannot show N/S. The small
    // fxtail_spin overlay stays as the windup tell, and the resting fxtail_heavy
    // overlay is skipped in both phases. Trade: the spin sheet has no windup
    // flash frame (the tell overlay carries the timing).
    const bool attackPose = (m.state == mh::MS_WINDUP || m.state == mh::MS_ATTACK) && m.atkIdx != mh::COMBAT_NO_ATTACK;
    const bool spinning = attackPose && mh::combatFacingLockV(g.combat.attack.facing);
    // Attack art from the attack record (beads monhun-ardu-nch.8/nch.10, prg.12;
    // bih.4): artSheet/artFrame/artMode replace the per-kind beastAtk compare
    // chain and the MON_HEAVY spinSheet gate. During windup+attack the whole
    // beast is drawn from the cached attack's whole-body sheet instead of the
    // generic art-descriptor base frame. mode 0 draws the 2-facing pose at
    // artFrame (or the authored prg.11 tell slot during windup); mode 1 is the
    // locked spin. The art is read once at attack start into the cache, so no
    // per-tick cart read. Windup and attack share the pose (the overlays have no
    // windup-flash frame; the tell pose carries the timing). Cosmetic only: no
    // hit-test or window change.
    // Windup tell frame (prg.11): combat.attack.tell selects the bespoke windup
    // pose on the attack's sheet. prg.12 authored tells 1..3; an authored tell
    // overrides the attack's own pose, an unauthored tell (0/4) keeps the attack
    // pose (dot 0 is the generic coil). No procedural marker.
    const uint8_t tellSlot = (m.state == mh::MS_WINDUP) ? mh::tellWindupFrame(g.combat.attack.tell, mh::TELL_FRAMES_AUTHORED) : mh::TELL_WINDUP_NONE;
    const bool attackSheet = attackPose && g.combat.attack.artMode == 0 && g.combat.attack.artSheet != 0;
    // Generic zone-part-overlay skip (bih.5): whenever an attack-art sheet is
    // the active whole-body draw the sheet already carries the posed part, so
    // every zone overlay is suppressed -- mode 0 poses and the mode 1 spin sheet
    // alike (a spin always authors a non-zero artSheet).
    const bool attackArt = attackPose && g.combat.attack.artSheet != 0;
    if (attackPose && g.combat.attack.artMode == 1) {
        // Whole-beast spin sheet: frame 0 is the east silhouette. Windup holds
        // the locked away frame; the attack steps 45 deg clockwise from it each
        // active-window slice, so the beast completes one visible revolution.
        // The sheet is 40x40 with the body centre at (20,20), so it is centred
        // on the body box centre. No per-tick cart read: sheet constant + frame
        // math only. (The tell selector cannot index this 8-direction sheet; the
        // fxtail_spin overlay carries the spin tell.)
        const uint8_t start8 = static_cast<uint8_t>(fp::dirIndexFromDelta(m.fx, m.fy)) & 7;
        const uint8_t spinF = (m.state == mh::MS_WINDUP) ? start8 : mh::spinSheetFrame(start8, m.t, static_cast<int16_t>(g.combat.attack.active));
        sprDraw(artSheetAddr(static_cast<uint8_t>(g.combat.attack.artSheet - 1)), static_cast<int16_t>(x + (w >> 1) - 20), static_cast<int16_t>(y + (h >> 1) - 20), FRAME(spinF));
    } else if (attackSheet) {
        // 2-facing attack sheet: the attack's own pose frame, or the authored
        // tell slot during windup (tell 1..3 -> (tell << 1) | west). An
        // unauthored tell (0/4) falls back to the attack's artFrame.
        uint8_t f = g.combat.attack.artFrame;
        if (tellSlot != mh::TELL_WINDUP_NONE)
            f = static_cast<uint8_t>(tellSlot << 1);
        f = static_cast<uint8_t>(f | (m.fx < 0 ? 1 : 0));
        sprDraw(artSheetAddr(static_cast<uint8_t>(g.combat.attack.artSheet - 1)), x, y, FRAME(f));
    } else {
        drawMonsterBodyGeneric(g, x, y);
    }
    if (m.state == mh::MS_DEAD)
        return;

    // Breakable-zone part overlays (monhun-ardu-4t4 heavy tail; kt7.6 chicken and
    // bull; bih.5 data path). Each cached zone slot carries a 1-based art_sheets
    // index (partSheet) for the part art baked into the beast sheet, seeded from
    // the zone record at spawn; one generic loop draws every zone that authors
    // one, at the zone's cached face-relative box origin (drawZonePart), i.e.
    // the same world rect the hit test uses. No per-creature key: RAVAGER and
    // POLE simply pack partSheet 0 (the ravager's 18x10 fxtail is deliberately
    // not overlaid, the pole's hp-0 zone never breaks). The only skip is the
    // generic attack-art rule above: the whole-body attack sheet already draws
    // the posed part, spin sheet included.
    if (!attackArt) {
        for (uint8_t slot = 0; slot < mh::COMBAT_ZONE_SLOTS; slot++) {
            const uint8_t part = g.combat.zone[slot].partSheet;
            if (part == 0)
                continue;
            const uint8_t bit = (slot == mh::COMBAT_ZONE_HEAD) ? mh::COMBAT_ZONE_HEAD_BIT : mh::COMBAT_ZONE_APPENDAGE_BIT;
            drawZonePart(g, x, y, artSheetAddr(static_cast<uint8_t>(part - 1)), slot, bit);
        }
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

// Row form for the authored weapon sheets (docs/weapon-art.md): their rows ARE
// the pose table, so the caller passes the row directly. The cell (32x32) and
// anchor (16,16) are fixed by the equipment schema and the sheet offset is a
// generated constant, so the weapon path skips the cart part-record read.
static inline void weaponRowDraw(uint24_t sheet, uint8_t row, uint8_t facing, int16_t rx, int16_t ry) {
    const uint8_t frame = static_cast<uint8_t>(row * equip::FACINGS + (facing % equip::FACINGS));
    sprDraw(sheet, static_cast<int16_t>(rx - 16), static_cast<int16_t>(ry - 16), FRAME(frame));
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
// Placeholder armor paper-doll (bead monhun-ardu-arm.2): the per-piece armor
// sheets are not in the equip blob yet (the 05x art epic owns them), so an
// equipped head piece maps to the closest existing layered head sheet -- hunter
// helm -> mh_head_helm, bone cap -> mh_head_bandana -- and everything else
// (body, charm, unknown ids) falls back to the base head. Re-skinning later is
// a data/art change, not a render edit. See docs/equipment-framework.md.
static inline uint8_t armorHeadPart(uint8_t headId) {
    if (headId == static_cast<uint8_t>(armor::ARMOR_HUNTER_HELM + 1))
        return equip::PART_HEAD_HELM;
    if (headId == static_cast<uint8_t>(armor::ARMOR_BONE_CAP + 1))
        return equip::PART_HEAD_BANDANA;
    return equip::DEFAULT_HEAD;
}

// Weapon art rows (docs/weapon-art.md). All three weapon sheets share the row
// numbering: 0 idle, 1 recover, 2..21 move slots (startup/active pairs),
// 22 stance, 23 dodge, 24 tap-defense, 25 stun, 26 riposte rim. The move slot
// comes from Attack::id (AtkId) for named moves and from the combo chain for
// plain hits, so no pointer comparisons run at draw time.
namespace wpn {
constexpr uint8_t ROW_IDLE = 0;
constexpr uint8_t ROW_RECOVER = 1;
constexpr uint8_t ROW_MOVE0 = 2;
constexpr uint8_t ROW_STANCE = 22;
constexpr uint8_t ROW_DODGE = 23;
constexpr uint8_t ROW_DEFENSE = 24;
constexpr uint8_t ROW_STUN = 25;
constexpr uint8_t ROW_RIM = 26;
// AtkId -> dense move slot: NONE (combo/special) never gets here, branch-a ids
// fold to slot 4, branch-b to 5, branch2 to 6, alt 7, roll 8, charge 9.
MH_PROGMEM const uint8_t ATK_SLOT[10] = {0, 4, 5, 4, 4, 5, 7, 8, 9, 6};
// Per weapon: move slot -> startup row. Unused slots fall back to recover (1)
// so a stray state can never blit a blank cell.
MH_PROGMEM const uint8_t MOVE_ROW[3][10] = {
    {2, 4, 6, 8, 10, 12, 14, 16, 18, 1},   // sword: no charge
    {2, 4, 6, 8, 10, 1, 14, 16, 18, 20},   // flail: no branch-b
    {2, 4, 6, 8, 10, 12, 14, 16, 18, 1},   // gunshield: no charge
};
}   // namespace wpn

// Move slot for the attack being drawn: combo chain for plain hits, the id
// fold for named moves, the special slot while PS_SPECIAL runs.
static inline uint8_t weaponMoveSlot(const mh::Player &p, const mh::Attack *a) {
    const int8_t id = mh::attackId(a);
    if (id != mh::ATK_NONE)
        return mhPgmReadU8(&wpn::ATK_SLOT[id]);
    if (p.state == mh::PS_SPECIAL)
        return 3;
    return p.chain > 2 ? 2 : static_cast<uint8_t>(p.chain);
}

// Sheet offset for one weapon: generated constants, folded at compile time (no
// RAM table on AVR). The equipped forge node's kind (Game::wpnSheet, jd1/2tb)
// selects the variant sheet -- 0 = the class default, 1..4 the gunshield
// variants, 5..8 the sword beast variants, 9..12 the flail beast variants. The
// kind is resolved strictly against the weapon's own class, so a sword can
// never draw a flail sheet (or vice versa) even from a stale kind byte.
static inline uint24_t weaponSheet(const mh::Game &g) {
    switch (g.weapon) {
    case mh::W_SWORD:
        switch (g.wpnSheet) {
        case 5:
            return static_cast<uint24_t>(equip::SHEET_OFF_MH_WEAPON_SWORD_SABER);
        case 6:
            return static_cast<uint24_t>(equip::SHEET_OFF_MH_WEAPON_SWORD_CLEAVER);
        case 7:
            return static_cast<uint24_t>(equip::SHEET_OFF_MH_WEAPON_SWORD_TAILBLADE);
        case 8:
            return static_cast<uint24_t>(equip::SHEET_OFF_MH_WEAPON_SWORD_FANG);
        default:
            return static_cast<uint24_t>(equip::SHEET_OFF_MH_WEAPON_SWORD);
        }
    case mh::W_FLAIL:
        switch (g.wpnSheet) {
        case 9:
            return static_cast<uint24_t>(equip::SHEET_OFF_MH_WEAPON_FLAIL_SLING);
        case 10:
            return static_cast<uint24_t>(equip::SHEET_OFF_MH_WEAPON_FLAIL_SHELL);
        case 11:
            return static_cast<uint24_t>(equip::SHEET_OFF_MH_WEAPON_FLAIL_TAIL);
        case 12:
            return static_cast<uint24_t>(equip::SHEET_OFF_MH_WEAPON_FLAIL_SPIKE);
        default:
            return static_cast<uint24_t>(equip::SHEET_OFF_MH_WEAPON_FLAIL);
        }
    default:
        switch (g.wpnSheet) {
        case 1:
            return static_cast<uint24_t>(equip::SHEET_OFF_MH_WEAPON_GUN_BUCKLER);
        case 2:
            return static_cast<uint24_t>(equip::SHEET_OFF_MH_WEAPON_GUN_KITE);
        case 3:
            return static_cast<uint24_t>(equip::SHEET_OFF_MH_WEAPON_GUN_TOWER);
        case 4:
            return static_cast<uint24_t>(equip::SHEET_OFF_MH_WEAPON_GUN_BRACE);
        default:
            return static_cast<uint24_t>(equip::SHEET_OFF_MH_WEAPON_GUN);
        }
    }
}

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
    // The body is the default draw set and picks the dodge pose row; the head
    // comes from the equipped head piece (arm.2 armorHeadPart), base otherwise.
    partDraw(equip::DEFAULT_BODY, p.state == mh::PS_DODGE ? equip::POSE_DODGE : equip::POSE_IDLE, face, cx, cy);
    partDraw(armorHeadPart(g.armorHead), equip::POSE_IDLE, face, cx, cy);

    // Charge meter above the hunter (mock drawPlayer): 16 px bar at cx-8, y-4,
    // 2 px tall; fill fraction min(1, chargeT/CHARGE_MIN), shade 2. Charge-lite
    // (prg.11): one level, so there is no white-at-L2 state. Only the charge
    // stance draws it.
    if (p.state == mh::PS_CHARGE) {
        const int16_t fill = (p.chargeT * 16 + (mh::CHARGE_MIN >> 1)) / mh::CHARGE_MIN;
        const int16_t w = fill < 1 ? 1 : (fill > 16 ? 16 : fill);
        blk(static_cast<int16_t>(cx - 8), static_cast<int16_t>(y - 4), w, 2, 2);
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

    // Weapon overlay (docs/weapon-art.md): all three weapons share the row
    // table; per-weapon branches add only their extra parts (riposte rim, whirl
    // ring/ball, arrowshot tracer) and pick the stance rows.
    if (!p.sheathed) {
        const uint24_t sheet = weaponSheet(g);
        // Row per move slot: startup rows are hand-centred, active rows
        // box-centred on the point the melee test resolves against. The gun's
        // arrowshot is muzzle-referenced instead: its box is the hitscan reach,
        // not a hit area (docs/weapon-art.md).
        uint8_t row = wpn::ROW_IDLE;
        int16_t rx = cx;
        int16_t ry = cy;
        if (a) {
            const int16_t reach = mh::attackReach(a);
            const int16_t hx = static_cast<int16_t>(cx + ((p.fx * reach) >> 4));
            const int16_t hy = static_cast<int16_t>(cy + ((p.fy * reach) >> 4));
            const uint8_t base = mhPgmReadU8(&wpn::MOVE_ROW[g.weapon][weaponMoveSlot(p, a)]);
            if (phase == 1) {
                row = static_cast<uint8_t>(base + 1);
                if (!(g.weapon == mh::W_GUN && p.state == mh::PS_SPECIAL)) {
                    rx = hx;
                    ry = hy;
                }
            } else if (phase == 0) {
                row = base;
            } else {
                row = wpn::ROW_RECOVER;
            }
            if (g.weapon == mh::W_SWORD && p.state == mh::PS_SPECIAL && p.riposteT > 0)
                weaponRowDraw(sheet, wpn::ROW_RIM, face, hx, hy);
            if (g.weapon == mh::W_GUN && p.state == mh::PS_SPECIAL && phase == 1) {
                // Hitscan arrowshot: slug tracer at the hit reach -- the exact
                // point meleeHitbox resolves against, so the read never lies.
                sprDraw(fxball, static_cast<int16_t>(hx - 3), static_cast<int16_t>(hy - 4), FRAME(0));
            }
        } else if (p.stance != mh::ST_NONE) {
            row = wpn::ROW_STANCE;
        } else if (p.state == mh::PS_DODGE) {
            row = wpn::ROW_DODGE;
        } else if (p.state == mh::PS_DEFLECT || p.state == mh::PS_SHOVE) {
            row = wpn::ROW_DEFENSE;
            if (g.weapon == mh::W_GUN) {
                // Shove retract (player.hpp PS_SHOVE): p.t counts 10 -> 1, so the
                // thrust runs 10 -> 4 px along the facing and the plate retracts.
                const int16_t thrust = static_cast<int16_t>(4 + (p.t * 6) / 10);
                rx = static_cast<int16_t>(rx + ((p.fx * thrust) >> 4));
                ry = static_cast<int16_t>(ry + ((p.fy * thrust) >> 4));
            }
        } else if (p.state == mh::PS_STUN) {
            row = wpn::ROW_STUN;
        }
        // Guard stays up while the gun fires from the stance (B held).
        if (a && g.weapon == mh::W_GUN && p.stance != mh::ST_NONE)
            weaponRowDraw(sheet, wpn::ROW_STANCE, face, cx, cy);
        weaponRowDraw(sheet, row, face, rx, ry);
        if (g.weapon == mh::W_FLAIL && p.stance == mh::ST_WHIRL) {
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
            const uint8_t ring = static_cast<uint8_t>((static_cast<uint16_t>(ang) * art_dims::whirlring_frames) >> 8);
            const uint8_t ba = static_cast<uint8_t>(p.whirlTick * ANG_WHIRL_BALL);
            partVariantDraw(equip::PART_FLAIL_RING, ring, cx, cy);
            partDraw(equip::PART_FLAIL_BALL, equip::POSE_WHIRL, face, cx + mulQ4(cos256(ba), 20), cy + mulQ4(sin256(ba), 14));
        }
    }   // !p.sheathed

    if (p.iT > 0 && (g.tick % 4) < 2)
        partDraw(equip::PART_ERASE, equip::POSE_IDLE, face, cx, cy);
    if (p.state == mh::PS_STUN) {
        const uint8_t ang = static_cast<uint8_t>(static_cast<uint32_t>(g.tick) * ANG_PLAYER_STUN);
        partDraw(equip::PART_FLAIL_STUN, equip::POSE_STUN, face, cx + mulQ4(cos256(ang), 7), cy - 10 + mulQ4(sin256(ang), 2));
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

// Single-pixel open-square border: solid for hurt boxes, dotted (every other
// pixel) for hit boxes. Border only -- the interior is never touched.
static void wireBox(int16_t x, int16_t y, int16_t w, int16_t h, bool dotted) {
    if (w < 1 || h < 1)
        return;
    if (!dotted) {
        blk(x, y, w, 1, 3);
        blk(x, y + h - 1, w, 1, 3);
        blk(x, y, 1, h, 3);
        blk(x + w - 1, y, 1, h, 3);
        return;
    }
    for (int16_t i = 0; i < w; i += 2) {
        blk(x + i, y, 1, 1, 3);
        blk(x + i, y + h - 1, 1, 1, 3);
    }
    for (int16_t j = 0; j < h; j += 2) {
        blk(x, y + j, 1, 1, 3);
        blk(x + w - 1, y + j, 1, 1, 3);
    }
}

static void drawDebug(const mh::Game &g, int16_t camX, int16_t camY) {
    const int32_t ox = -camX;
    const int32_t oy = -camY + mh::HUD_H;
    const mh::Player &p = g.player;

    // Hurt boxes (solid): player body, then the creature's cached skeleton body
    // part (migration B: part boxes from g.combat.body, not w/h literals) and
    // the two breakable zone boxes (head, appendage) of the 3-hitzone model.
    wireBox(p.x + ox, p.y + oy, p.w, p.h, false);
    if (g.target.alive) {
        const mh::CombatBox &b = g.combat.body;
        wireBox(g.monster.x + b.ox + ox, g.monster.y + b.oy + oy, b.w, b.h, false);
        // 3-hitzone model: the two breakable zone boxes (head, appendage) from
        // the cached zone slots. Always solid: the wireframe shows the authored
        // boxes; the offsets mirror combatZoneContains (combatZoneOffsetX), so
        // the wire sits on the tested rect for both facings.
        if (g.combat.headZone != mh::COMBAT_NO_ZONE) {
            const mh::CombatBox &zb = g.combat.zone[mh::COMBAT_ZONE_HEAD].box;
            wireBox(g.monster.x + mh::combatZoneOffsetX(g.monster.fx, zb) + ox, g.monster.y + zb.oy + oy, zb.w, zb.h, false);
        }
        if (g.combat.appendZone != mh::COMBAT_NO_ZONE) {
            const mh::CombatBox &zb = g.combat.zone[mh::COMBAT_ZONE_APPENDAGE].box;
            wireBox(g.monster.x + mh::combatZoneOffsetX(g.monster.fx, zb) + ox, g.monster.y + zb.oy + oy, zb.w, zb.h, false);
        }
    }

    // Active player melee hit box (dotted): the sim's meleeHitbox() rect, so
    // the wire matches the frame the overlap test actually runs against.
    if ((p.state == mh::PS_ATTACK || p.state == mh::PS_SPECIAL) && p.atk) {
        const mh::Rect hit = mh::meleeHitbox(p, p.atk);
        wireBox(hit.x + ox, hit.y + oy, hit.w, hit.h, true);
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
        wireBox(hx - (hw >> 1) + ox, hy - (hh >> 1) + oy, hw, hh, true);
    }
}
#endif   // DEBUG_HURTBOXES

/* ------------------------------------------------------------------- hud */

// Mock drawHud(): HP + stamina bars, weapon name, gun nock state, then the
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

// Player hp/stam bar variant (dx5.2). Player::hpMax/stamMax are uint8, so
// (w - 2) * num <= 42 * 255 and the whole numerator fits 16 bits; this uses the
// already-linked 16-bit divide instead of the 32/16 one the shared hudBar pays
// for the 2800-hp monster bar. Geometry and truncation are identical: the
// caller guarantees num <= den <= 255, so the widened and 16-bit forms agree.
static void hudBar8(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t num, uint8_t den, uint8_t shade) {
    hudBlk(x, y, w, h, 1);
    if (den == 0 || num == 0)
        return;
    if (num > den)
        num = den;
    uint16_t fw = static_cast<uint16_t>((static_cast<uint16_t>(w - 2) * num + (den >> 1)) / den);
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

// Gun nock hint (gun rework): the hitscan arrowshot has a short nock after each
// shot, so the old shell lane at x=67 now reads the load state -- "RDY" (ready)
// or "LOD" plus a 1 px fill bar on row 6 while the nock runs. Only drawn for
// the gun; sword/flail leave the lane clear.
static void drawGunNock(const mh::Player &p) {
    if (p.reload > 0) {
        int16_t x = hudPut(67, 'L');
        x = hudPut(x, 'O');
        hudPut(x, 'D');
        const int16_t w = static_cast<int16_t>((8 * (mh::ARROW_NOCK_TICKS - p.reload) + (mh::ARROW_NOCK_TICKS >> 1)) / mh::ARROW_NOCK_TICKS);
        hudBlk(67, 6, w < 1 ? 1 : w, 1, 2);
        return;
    }
    int16_t x = hudPut(67, 'R');
    x = hudPut(x, 'D');
    hudPut(x, 'Y');
}

static void drawHud(const mh::Game &g) {
    const mh::Player &p = g.player;

    // No strip background fill: ArduboyG waitForNextPlane(BLACK) wipes the
    // framebuffer black before each plane, so the HUD rows only need the
    // shapes/text drawn. The strip uses the HUD-band path (rows 0..7); the
    // arena below stays y >= HUD_H clipped.
    hudBlk(0, mh::HUD_H - 1, mh::SCREEN_W, 1, 1);   // divider at the arena edge

    hudBar8(1, 2, 28, 4, p.hp, p.hpMax, 3);        // player HP (white)
    hudBar8(29, 2, 16, 4, p.stam, p.stamMax, 2);   // stamina (light gray)

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

    if (g.weapon == mh::W_GUN)
        drawGunNock(p);   // arrowshot load state (RDY / LOD + fill)

    // Monster HP bar: only when the beast is present (target.alive == alive &&
    // beastHere), so an off-home room never shows a ghost bar (demo fix).
    if (g.target.alive)
        hudBar(82, 2, 44, 3, g.monster.hp, g.monster.hpMax, 3);

    // Herb count (feel.22): a tiny 1 px plant glyph + one digit in the free
    // 5 px lane x=62..66 (after the 16-wide weapon marker at 46..61, before the
    // gun nock lane at 67). Only drawn when a herb is held; total available in
    // the demo is 9 (camp 1+2, area 1+2+3), so one digit always fits. prg.2 moved
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
