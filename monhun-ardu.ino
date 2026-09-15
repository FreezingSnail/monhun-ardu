
#define ABG_IMPLEMENTATION
#define SPRITESU_IMPLEMENTATION
#include "src/common.hpp"
#include "src/globals.hpp"
#include "src/fxdata.h"
#include "src/core/world.hpp"

decltype(arduboy) arduboy;

// Single game state. The core is header-only and shared verbatim with the host
// tests; the device loop only samples input, steps it, and reads it for draw.
mh::Game g;

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
// the arena shifts down by HUD_H px. No HUD pixels are drawn in this bead.

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

// Clip a block to the screen arena band and paint it. shade 0 clears the pixels
// on the current plane (mock black bodies mask what is under them). Nothing is
// ever written outside [0,SCREEN_W) x [HUD_H,SCREEN_H).
static inline void blk(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t shade) {
    if (w <= 0 || h <= 0) return;
    int32_t x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < mh::HUD_H) y0 = mh::HUD_H;
    if (x1 > mh::SCREEN_W) x1 = mh::SCREEN_W;
    if (y1 > mh::SCREEN_H) y1 = mh::SCREEN_H;
    if (x0 >= x1 || y0 >= y1) return;
    arduboy.fillRect(static_cast<int16_t>(x0), static_cast<int16_t>(y0),
                     static_cast<uint8_t>(x1 - x0), static_cast<uint8_t>(y1 - y0),
                     shade);
}

// 3x5 digits, bit2 = leftmost pixel. Subset of the mock FONT used by the rising
// damage numbers (mock drawText scale 1).
static const uint8_t MH_PROGMEM FONT_DIG[10][5] = {
    { 0b111, 0b101, 0b101, 0b101, 0b111 }, // 0
    { 0b010, 0b110, 0b010, 0b010, 0b111 }, // 1
    { 0b111, 0b001, 0b111, 0b100, 0b111 }, // 2
    { 0b111, 0b001, 0b011, 0b001, 0b111 }, // 3
    { 0b101, 0b101, 0b111, 0b001, 0b001 }, // 4
    { 0b111, 0b100, 0b111, 0b001, 0b111 }, // 5
    { 0b111, 0b100, 0b111, 0b101, 0b111 }, // 6
    { 0b111, 0b001, 0b001, 0b001, 0b001 }, // 7
    { 0b111, 0b101, 0b111, 0b101, 0b111 }, // 8
    { 0b111, 0b101, 0b111, 0b001, 0b111 }, // 9
};

static void drawGlyph(int32_t x, int32_t y, uint8_t d, uint8_t shade) {
    for (uint8_t row = 0; row < 5; row++) {
        const uint8_t bits = mhPgmReadU8(&FONT_DIG[d][row]);
        for (uint8_t col = 0; col < 3; col++) {
            if (bits & (4 >> col)) blk(x + col, y + row, 1, 1, shade);
        }
    }
}

// Mock drawText(number, scale 1): left-to-right, 4 px advance.
static void drawNumber(int32_t x, int32_t y, int16_t value, uint8_t shade) {
    uint16_t v = value < 0 ? 0 : static_cast<uint16_t>(value);
    uint8_t buf[5];
    uint8_t n = 0;
    if (v == 0) {
        buf[n++] = 0;
    } else {
        while (v > 0 && n < 5) { buf[n++] = static_cast<uint8_t>(v % 10); v /= 10; }
    }
    for (uint8_t i = 0; i < n; i++) drawGlyph(x + i * 4, y, buf[n - 1 - i], shade);
}

// Mock drawArena(): deterministic 1 px dots + world border.
static void drawArena(int16_t camX, int16_t camY) {
    for (int16_t i = 0; i < 260; i++) {
        if ((i * 7) % 3 == 0) continue;
        const int16_t wx = static_cast<int16_t>((i * 53) % mh::WORLD_W);
        const int16_t wy = static_cast<int16_t>((i * 29) % mh::WORLD_H);
        const int16_t sx = static_cast<int16_t>(wx - camX);
        const int16_t sy = static_cast<int16_t>(wy - camY + mh::HUD_H);
        if (sx < 0 || sx >= mh::SCREEN_W || sy < mh::HUD_H || sy >= mh::SCREEN_H) continue;
        blk(sx, sy, 1, 1, 1);
    }
    const int32_t lx = -camX;
    const int32_t ly = static_cast<int32_t>(mh::HUD_H) - camY;
    blk(lx, ly, mh::WORLD_W, 1, 2);
    blk(lx, ly + mh::WORLD_H - 1, mh::WORLD_W, 1, 2);
    blk(lx, ly, 1, mh::WORLD_H, 2);
    blk(lx + mh::WORLD_W - 1, ly, 1, mh::WORLD_H, 2);
}

// Mock drawPole(): base post, ring bands, head, eye hole, ground plate.
static void drawPole(const mh::Pole& pole, int16_t camX, int16_t camY) {
    const int32_t x = static_cast<int32_t>(pole.rect.x) - camX;
    const int32_t y = static_cast<int32_t>(pole.rect.y) - camY + mh::HUD_H;
    const int32_t w = pole.rect.w;
    const int32_t h = pole.rect.h;
    blk(x + 2, y + 12, w - 4, h - 12, 1);
    for (int32_t i = 0; i < 3; i++) blk(x + 2, y + 20 + i * 7, w - 4, 1, 0);
    blk(x, y, w, 16, pole.hitFlash > 0 ? 3 : 2);
    blk(x + 8, y + 5, 4, 4, 0);
    blk(x - 2, y + h - 2, w + 4, 2, 0);
}

// Mock drawMonster(): dead heap, feet, body, head + eyes, stun sparkle, and the
// windup/attack telegraph box.
static void drawMonster(const mh::Game& g, int16_t camX, int16_t camY) {
    const mh::Monster& m = g.monster;
    const int32_t x = rndPx(m.x, m.subX) - camX;
    const int32_t y = rndPx(m.y, m.subY) - camY + mh::HUD_H;
    const int32_t w = m.w;
    const int32_t h = m.h;

    if (m.state == mh::MS_DEAD) {
        blk(x, y + 16, w, 8, 1);
        blk(x + w / 2 - 4, y + 14, 8, 4, 2);
        return;
    }

    const bool flashing = (m.state == mh::MS_WINDUP) &&
                          (((m.windupMax - m.t) / 4) % 2 == 0);
    uint8_t body = 1;
    if (m.state == mh::MS_RECOVER) body = 2;
    if (m.hitFlash > 0) body = 3;
    if (flashing) body = 3;

    blk(x + 2, y + h - 1, w - 4, 1, 0);
    for (int32_t i = 0; i < 4; i++) blk(x + 3 + i * 8, y + h - 3, 3, 3, 0);

    blk(x + 2, y + 4, w - 4, 14, body);
    blk(x + 6, y + 1, w - 12, 6, body);

    const bool faceEast = m.fx >= 0;
    const int32_t headX = faceEast ? x + w - 10 : x;
    blk(headX, y + 6, 10, 12, m.state == mh::MS_RECOVER ? 2 : 3);
    blk(faceEast ? headX + 7 : headX + 1, y + 13, 2, 2, 0);
    blk(headX + 4, y + 9, 2, 2, 0);

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
    const uint8_t bodyShade = (p.state == mh::PS_DODGE) ? 2 : 3;

    blk(x + 2, y + p.h - 1, p.w - 4, 1, 1);

    blk(x + 5, y + 1, 6, 6, bodyShade);
    blk(x + 4, y + 7, 8, 6, bodyShade);
    blk(x + 5, y + 13, 2, 2, bodyShade);
    blk(x + 9, y + 13, 2, 2, bodyShade);

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
        if (pr.heavy) {
            blk(x - hw, y - hh, pr.w, pr.h, 2);
            blk(x - hw + 1, y - hh + 1, pr.w - 2, pr.h - 2, 3);
            blk(x - hw, y + hh - 2, pr.w, 1, 0);
        } else {
            blk(x - hw, y - hh, pr.w, pr.h, 2);
            blk(x - hw + 1, y - hh + 1, 2, 2, 3);
        }
    }
}

// Mock drawEffects(): 4-point spark, or a rising damage number.
static void drawEffects(const mh::Game& g, int16_t camX, int16_t camY) {
    for (int16_t i = 0; i < g.fxN; i++) {
        const mh::Effect& e = g.fx[i];
        const int16_t r = e.life - e.t;
        if (e.text) {
            const int32_t x = e.x - camX;
            const int32_t y = static_cast<int32_t>(e.y) - (r + 1) / 3 - camY + mh::HUD_H;
            drawNumber(x - 2, y, e.text, e.crit ? 3 : 2);
        } else {
            const uint8_t sh = e.crit ? 3 : 2;
            const int32_t x = e.x - camX;
            const int32_t y = e.y - camY + mh::HUD_H;
            blk(x - r, y, 1, 1, sh);
            blk(x + r, y - 1, 1, 1, sh);
            blk(x, y - r, 1, 1, sh);
            blk(x, y + r, 1, 1, sh);
        }
    }
}

/* ------------------------------------------------------------------ loop */

void setup() {
    // Serial.begin(9600);

    arduboy.boot();
    arduboy.startGray();
    arduboy.initRandomSeed();

    FX::begin(FX_DATA_PAGE);
    FX::setCursorRange(0, 32767);

    mh::newGame(g, mh::W_SWORD, mh::MODE_HUNT);
}

// One logic tick. Called only from needsUpdate() (never mid-plane), so the
// whole core advances atomically between planes. pollButtons() already ran.
void run() {
    mh::Input in;
    in.mx = (arduboy.pressed(RIGHT_BUTTON) ? 1 : 0)
          - (arduboy.pressed(LEFT_BUTTON)  ? 1 : 0);
    in.my = (arduboy.pressed(DOWN_BUTTON)  ? 1 : 0)
          - (arduboy.pressed(UP_BUTTON)    ? 1 : 0);
    in.a  = arduboy.pressed(A_BUTTON);
    in.b  = arduboy.pressed(B_BUTTON);
    mh::stepGame(g, in);
}

// Full block-art scene, mock draw order: arena, target (pole|beast), player,
// shells, effects. Read-only: render never mutates Game. Shapes are identical
// on every plane (the L4 shade resolves in ArduboyG::planeColor), so the three
// passes composite to the same 4-level image.
void render() {
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
    // HUD band stays black this bead; bead 8ss draws bars/weapon/ammo.
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
