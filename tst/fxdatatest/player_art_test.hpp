#pragma once
// On-device player pixel oracle (bead monhun-ardu-abr, epic monhun-ardu-05x).
//
// Pins the exact framebuffer bytes drawPlayer() produces for a fixed Game-state
// matrix covering all three weapons x the states that reach the player draw
// path (idle / attack startup-active-recover / special / parry / whirl / guard /
// shove / dodge / deflect / stun / reload / i-frames) x two or more facings.
// Each case renders drawPlayer() alone (no scene/camera/effects) into a cleared
// buffer on each of the three ArduboyG L4_Triplane planes and hashes the full
// 128x64 framebuffer (FNV-1a 32). The goldens below were captured from the
// pre-refactor build in the same change that introduced the generated part
// tables, then re-run unchanged after the refactor: any pixel drift fails.
//
// Framebuffer layout (ArduboyG L4_Triplane): 128 B/page, pixel(x,y) =
// buf[(y >> 3) * 128 + x], bit y & 7.
//
// GOLDEN REGEN (only when render intentionally changes; the diff must be
// explained in output.md):
//   flip PRINT_GOLDENS to true, then
//   `make fxtest-headless FXTEST_ONLY=test_player_art`
//   and copy the `G <case> <p0> <p1> <p2>` lines into GOLDEN below.
// PRINT_GOLDENS skips the comparisons and just emits the hashes.

#include "harness/fxtest.hpp"
#include "src/core/world.hpp"
#include "src/render.hpp"

#include <stdint.h>

namespace player_art {

using namespace mh;

constexpr bool PRINT_GOLDENS = false;

struct Case {
    uint8_t weapon;   // W_SWORD / W_FLAIL / W_GUN
    uint8_t state;    // PState
    uint8_t stance;   // Stance
    uint8_t atk;      // 0 none, 1 combo0, 2 combo1, 3 combo2, 4 special, 5 branch0, 6 branch1
    uint8_t phase;    // 0 startup, 1 active, 2 recover
    int8_t fx;
    int8_t fy;
    uint8_t tick;   // g.tick (i-frame flicker / stun angle phase)
    uint8_t reload;
    uint8_t iT;
    uint8_t riposte;
};

// State matrix. Facings are 8-way fp vectors (E = 16,0; W = -16,0; SE = 11,11).
static const Case CASES[] = {
    // sword: body normal/dodge, slash frames by attack, parry, riposte
    {W_SWORD, PS_IDLE, ST_NONE, 0, 0, 16, 0, 0, 0, 0, 0},
    {W_SWORD, PS_IDLE, ST_NONE, 0, 0, -16, 0, 0, 0, 0, 0},
    {W_SWORD, PS_ATTACK, ST_NONE, 1, 0, 16, 0, 0, 0, 0, 0},
    {W_SWORD, PS_ATTACK, ST_NONE, 1, 1, 16, 0, 0, 0, 0, 0},
    {W_SWORD, PS_ATTACK, ST_NONE, 1, 2, 16, 0, 0, 0, 0, 0},
    {W_SWORD, PS_ATTACK, ST_NONE, 2, 1, 11, 11, 0, 0, 0, 0},
    {W_SWORD, PS_ATTACK, ST_NONE, 3, 1, 16, 0, 0, 0, 0, 0},
    {W_SWORD, PS_SPECIAL, ST_NONE, 4, 1, 16, 0, 0, 0, 0, 0},
    {W_SWORD, PS_SPECIAL, ST_NONE, 4, 1, 16, 0, 0, 0, 90},    // riposte rim
    {W_SWORD, PS_ATTACK, ST_NONE, 5, 1, 16, 0, 0, 0, 0, 0},   // step-slash
    {W_SWORD, PS_ATTACK, ST_NONE, 6, 1, 16, 0, 0, 0, 0, 0},   // spin-cut
    {W_SWORD, PS_IDLE, ST_PARRY, 0, 0, 16, 0, 0, 0, 0, 0},
    {W_SWORD, PS_DODGE, ST_NONE, 0, 0, -16, 0, 0, 0, 0, 0},
    {W_SWORD, PS_STUN, ST_NONE, 0, 0, 16, 0, 5, 0, 0, 0},
    {W_SWORD, PS_IDLE, ST_NONE, 0, 0, 16, 0, 1, 0, 10, 0},   // i-frames on
    // flail: ring/chain/ball, deflect bars, body
    {W_FLAIL, PS_IDLE, ST_NONE, 0, 0, 16, 0, 0, 0, 0, 0},
    {W_FLAIL, PS_IDLE, ST_NONE, 0, 0, -16, 0, 0, 0, 0, 0},
    {W_FLAIL, PS_ATTACK, ST_NONE, 1, 0, 16, 0, 0, 0, 0, 0},
    {W_FLAIL, PS_ATTACK, ST_NONE, 1, 1, 16, 0, 0, 0, 0, 0},
    {W_FLAIL, PS_ATTACK, ST_NONE, 1, 2, 16, 0, 0, 0, 0, 0},
    {W_FLAIL, PS_SPECIAL, ST_NONE, 4, 1, 11, 11, 0, 0, 0, 0},
    {W_FLAIL, PS_IDLE, ST_WHIRL, 0, 0, 16, 0, 0, 0, 0, 0},
    {W_FLAIL, PS_IDLE, ST_WHIRL, 0, 0, -16, 0, 0, 0, 0, 0},
    {W_FLAIL, PS_DEFLECT, ST_NONE, 0, 0, 16, 0, 0, 0, 0, 0},
    {W_FLAIL, PS_DODGE, ST_NONE, 0, 0, 16, 0, 0, 0, 0, 0},
    {W_FLAIL, PS_STUN, ST_NONE, 0, 0, 16, 0, 5, 0, 0, 0},
    {W_FLAIL, PS_IDLE, ST_NONE, 0, 0, 16, 0, 2, 0, 10, 0},
    // gun: plate/white guard, shove, reload bar, body
    {W_GUN, PS_IDLE, ST_NONE, 0, 0, 16, 0, 0, 0, 0, 0},
    {W_GUN, PS_IDLE, ST_NONE, 0, 0, -16, 0, 0, 0, 0, 0},
    {W_GUN, PS_IDLE, ST_GUARD, 0, 0, 16, 0, 0, 0, 0, 0},
    {W_GUN, PS_ATTACK, ST_NONE, 1, 1, 16, 0, 0, 0, 0, 0},
    {W_GUN, PS_SPECIAL, ST_NONE, 4, 1, 16, 0, 0, 0, 0, 0},
    {W_GUN, PS_SHOVE, ST_NONE, 0, 0, 16, 0, 0, 0, 0, 0},
    {W_GUN, PS_IDLE, ST_NONE, 0, 0, 16, 0, 0, 35, 0, 0},   // reload bar
    {W_GUN, PS_DODGE, ST_NONE, 0, 0, 16, 0, 0, 0, 0, 0},
    {W_GUN, PS_STUN, ST_NONE, 0, 0, 16, 0, 5, 0, 0, 0},
    {W_GUN, PS_IDLE, ST_NONE, 0, 0, 16, 0, 3, 0, 10, 0},
};
constexpr uint8_t CASE_COUNT = static_cast<uint8_t>(sizeof(CASES) / sizeof(CASES[0]));
// Changing the matrix invalidates GOLDEN: extend both in the same change and
// capture the new hashes with the regen command above.
static_assert(CASE_COUNT == 37, "golden matrix changed; regenerate GOLDEN");

// Golden framebuffer hashes [case][plane], captured pre-refactor. See the regen
// note above; PROGMEM so the 3 x CASE_COUNT words stay in flash.
static const uint32_t MH_PROGMEM GOLDEN[CASE_COUNT][3] = {
    {0x84889850u, 0x701edb50u, 0x701edb50u}, {0x36089fd8u, 0xc7b408d8u, 0xc7b408d8u}, {0x9e96c62au, 0xc998692au, 0x17645e42u}, {0xc2b26499u, 0xe4c47799u, 0x64ee0249u},
    {0x9e96c62au, 0xc998692au, 0x17645e42u}, {0x6f1a5ff1u, 0xfd0a65f1u, 0x46dde0d1u}, {0x7619b2dfu, 0x8a836fdfu, 0x69dda221u}, {0x96fc1615u, 0x12a97015u, 0x6222c301u},
    {0xeef9f315u, 0x6aa74d15u, 0xba20a001u}, {0x2dae16b3u, 0xca6471b3u, 0x6222c301u}, {0xfc85c4cdu, 0x13a869cdu, 0x356d8145u}, {0x5898152fu, 0x3524c82fu, 0xc7e81d2fu},
    {0x36089fd8u, 0xc7b408d8u, 0x059d38e0u}, {0xc9c1437eu, 0x01d4027eu, 0x01d4027eu}, {0xdc867550u, 0xc81cb850u, 0xc81cb850u}, {0x07400feau, 0x095407eau, 0x095407eau},
    {0xea752610u, 0x09e1ec10u, 0x09e1ec10u}, {0x7dc1c79cu, 0x7fd5bf9cu, 0x7fd5bf9cu}, {0x0d1c1787u, 0x0b081f87u, 0x0b081f87u}, {0x7dc1c79cu, 0x7fd5bf9cu, 0x7fd5bf9cu},
    {0xc2f7c3b3u, 0xd76180b3u, 0xd76180b3u}, {0x56c39df1u, 0xf42fd3f1u, 0xf42fd3f1u}, {0x56c39df1u, 0xf42fd3f1u, 0xf42fd3f1u}, {0xbeb219f9u, 0xa24911f9u, 0xf7b2b94bu},
    {0x07400feau, 0x095407eau, 0x6ea44bb2u}, {0x4c15ea88u, 0x8428a988u, 0x8428a988u}, {0x07400feau, 0x095407eau, 0x095407eau}, {0xe56a836fu, 0x1d4a306fu, 0xb373bcd7u},
    {0xd9c2ad2fu, 0xedb3832fu, 0xcacdbe57u}, {0xe56a836fu, 0x1d4a306fu, 0x1d4a306fu}, {0xe56a836fu, 0x1d4a306fu, 0xb373bcd7u}, {0xe56a836fu, 0x1d4a306fu, 0xb373bcd7u},
    {0x46724db1u, 0x67d445b1u, 0xf8c93455u}, {0xc19b9b2fu, 0x1fb93e2fu, 0xb373bcd7u}, {0xe56a836fu, 0x1d4a306fu, 0x1f116dc5u}, {0x474a642du, 0xc098f32du, 0x606e0819u},
    {0xe56a836fu, 0x1d4a306fu, 0xb373bcd7u},
};
static_assert(sizeof(GOLDEN) / sizeof(GOLDEN[0]) == CASE_COUNT, "goldens must cover every case");

static void clearFb() {
    uint8_t *b = arduboy.getBuffer();
    for (uint16_t i = 0; i < 1024; i++)
        b[i] = 0;
}

// FNV-1a 32 over the whole 128x64 framebuffer.
static uint32_t fbHash() {
    const uint8_t *b = arduboy.getBuffer();
    uint32_t h = 2166136261u;
    for (uint16_t i = 0; i < 1024; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

static void setupCase(Game &g, const Case &c) {
    newGame(g, c.weapon, MODE_TRAIN);
    Player &p = g.player;
    p.x = 48;
    p.y = 24;
    p.w = 16;
    p.h = 16;
    p.subX = 0;
    p.subY = 0;
    p.fx = c.fx;
    p.fy = c.fy;
    p.state = c.state;
    p.stance = c.stance;
    p.chain = (c.atk >= 1 && c.atk <= 3) ? static_cast<uint8_t>(c.atk - 1) : 0;
    p.reload = c.reload;
    p.iT = c.iT;
    p.riposteT = c.riposte;
    p.whirlTick = 3;   // deterministic ring/ball angle
    g.tick = c.tick;

    const WeaponDef *def = &WEAPON_DEFS[c.weapon];
    const Attack *a = nullptr;
    if (c.atk == 1)
        a = weaponAttack(def, 0);
    else if (c.atk == 2)
        a = weaponAttack(def, 1);
    else if (c.atk == 3)
        a = weaponAttack(def, 2);
    else if (c.atk == 4)
        a = weaponSpecial(def);
    else if (c.atk == 5)
        a = branchAtk(weaponBranch(def, 0));
    else if (c.atk == 6)
        a = branchAtk(weaponBranch(def, 1));
    p.atk = a;
    if (a) {
        const int16_t st = attackStartup(a);
        const int16_t act = attackActive(a);
        p.t = c.phase == 0 ? 0 : (c.phase == 1 ? static_cast<int16_t>(st + 1) : static_cast<int16_t>(st + act + 1));
    } else {
        p.t = 0;
    }
}

static void printHex32(uint32_t v) {
    for (int8_t i = 7; i >= 0; i--) {
        const uint8_t nib = static_cast<uint8_t>((v >> (i * 4)) & 0xF);
        Serial.print(static_cast<char>(nib < 10 ? '0' + nib : 'a' + nib - 10));
    }
}

inline void test_player_art(FxTest &test) {
    arduboy.startGray();   // plane ISR drives waitForNextPlane (as in test_perf)

    static Game g;
    for (uint8_t ci = 0; ci < CASE_COUNT; ci++) {
        setupCase(g, CASES[ci]);
        if (PRINT_GOLDENS) {
            Serial.print(F("G "));
            Serial.print(ci);
        }
        for (uint8_t plane = 0; plane < 3; plane++) {
            while (arduboy.currentPlane() != plane) {
                FX::enableOLED();
                arduboy.waitForNextPlane();
                FX::disableOLED();
            }
            clearFb();
            drawPlayer(g, 0, 0);
            const uint32_t h = fbHash();
            if (PRINT_GOLDENS) {
                Serial.print(' ');
                printHex32(h);
            } else {
                test.expectEqIdx(h, mhPgmReadU32(&GOLDEN[ci][plane]), F("player art"), ci);
            }
        }
        if (PRINT_GOLDENS)
            Serial.println();
    }
}

}   // namespace player_art
