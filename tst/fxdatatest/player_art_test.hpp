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
//
// Regen history: the goldens were captured pre-refactor (bead abr). The
// FRAME() precedence fix (monhun-ardu-px5) changed exactly one case: 29 = gun
// idle + ST_GUARD, where the flat guard-white workaround had blitted the white
// plate's raw frame on every plane; it now gets the per-plane stride. Every
// other case is byte-identical.
//
// Bead ikp (eqf.3) split the flat fxplayer body into shadow + body + head
// layers and added the per-facing helmet eye slot. Changed cases: 0, 1, 3, 5,
// 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
// 34 (body shade/facing split + the 5 toward-viewer head slots). Unchanged
// cases are the ones whose weapon overlay covers the head/body pixels (2, 4,
// 10 spin/0.6-arc slash, 27..33 gun plate/reload, 35 gun stun, 36 gun
// i-frames): their overlay shapes are byte-identical to before.
//
// Bead monhun-ardu-836 baked the flail whirl ring's 6 dots into one 24-phase
// sprite (src/render.hpp partVariantDraw). Changed cases: exactly 21 and 22 (both
// W_FLAIL + ST_WHIRL, E/W facing at whirlTick 3) -- the ring dot positions move
// by up to the 256/(2*24) angular quantization. Every other case is
// byte-identical (the ball blit and all non-whirl draws are untouched).
//
// Gun hitscan rework: the shells/projectiles and the reload lane were retired;
// the gun's A-special is now a long-reach arrowshot with a tracer slug. Changed
// cases: 109 (gun special active, E) and 111 (gun special active, W -- the
// retired reload-bar row, repurposed). Every other case is byte-identical.

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
    // Equipped head piece + 1 (arm.2 armorHeadPart); 0 = base head. Left off the
    // pre-arm.2 rows, so they value-initialize to 0 and keep their goldens.
    uint8_t armorHead;
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
    // gun: plate/white guard, shove, arrowshot tracer, body
    {W_GUN, PS_IDLE, ST_NONE, 0, 0, 16, 0, 0, 0, 0, 0},
    {W_GUN, PS_IDLE, ST_NONE, 0, 0, -16, 0, 0, 0, 0, 0},
    {W_GUN, PS_IDLE, ST_GUARD, 0, 0, 16, 0, 0, 0, 0, 0},
    {W_GUN, PS_ATTACK, ST_NONE, 1, 1, 16, 0, 0, 0, 0, 0},
    {W_GUN, PS_SPECIAL, ST_NONE, 4, 1, 16, 0, 0, 0, 0, 0},
    {W_GUN, PS_SHOVE, ST_NONE, 0, 0, 16, 0, 0, 0, 0, 0},
    {W_GUN, PS_SPECIAL, ST_NONE, 4, 1, -16, 0, 0, 0, 0, 0},   // arrowshot tracer (W)
    {W_GUN, PS_DODGE, ST_NONE, 0, 0, 16, 0, 0, 0, 0, 0},
    {W_GUN, PS_STUN, ST_NONE, 0, 0, 16, 0, 5, 0, 0, 0},
    {W_GUN, PS_IDLE, ST_NONE, 0, 0, 16, 0, 3, 0, 10, 0},
    // arm.2 armor head layer: the equipped head piece selects a different head
    // sheet (hunter helm -> mh_head_helm, bone cap -> mh_head_bandana). The
    // base rows above keep armorHead 0 and their pre-arm.2 goldens.
    {W_SWORD, PS_IDLE, ST_NONE, 0, 0, 16, 0, 0, 0, 0, 0, armor::ARMOR_HUNTER_HELM + 1},
    {W_SWORD, PS_IDLE, ST_NONE, 0, 0, -16, 0, 0, 0, 0, 0, armor::ARMOR_HUNTER_HELM + 1},
    {W_FLAIL, PS_IDLE, ST_NONE, 0, 0, 11, 11, 0, 0, 0, 0, armor::ARMOR_BONE_CAP + 1},
};
constexpr uint8_t CASE_COUNT = static_cast<uint8_t>(sizeof(CASES) / sizeof(CASES[0]));
// Changing the matrix invalidates GOLDEN: extend both in the same change and
// capture the new hashes with the regen command above.
static_assert(CASE_COUNT == 40, "golden matrix changed; regenerate GOLDEN");

// Golden framebuffer hashes [case][plane], captured pre-refactor. See the regen
// note above; PROGMEM so the 3 x CASE_COUNT words stay in flash.
static const uint32_t MH_PROGMEM GOLDEN[CASE_COUNT][3] = {
    {0xe490e488u, 0xccac0388u, 0xccac0388u},
    {0xa38365c0u, 0x46b905c0u, 0x46b905c0u},
    {0x9e96c62au, 0xc998692au, 0x17645e42u},
    {0x90d9bf11u, 0x7d2df311u, 0xd7728e01u},
    {0x9e96c62au, 0xc998692au, 0x17645e42u},
    {0x4fae7d31u, 0xd2f41731u, 0x34651e11u},
    {0x3e136117u, 0x55f84217u, 0xf05c2259u},
    {0x9861d60du, 0xb2e8700du, 0xf75a8eb9u},
    {0x8d024a25u, 0x19543b25u, 0xb76fbb91u},
    {0xe739e1abu, 0x8f6871abu, 0xf75a8eb9u},
    {0xfc85c4cdu, 0x13a869cdu, 0x356d8145u},
    {0x6d4eb357u, 0xa7c60857u, 0x3a895d57u},
    {0xa38365c0u, 0x46b905c0u, 0x37e7b108u},
    {0x3ec19436u, 0x3ab58236u, 0x3ab58236u},
    {0x82915ce0u, 0x411204e0u, 0x411204e0u},
    {0x93745722u, 0x7b8f7622u, 0x7b8f7622u},
    {0x69a0e3f8u, 0xd2289bf8u, 0xd2289bf8u},
    {0x8d8428d4u, 0x759f47d4u, 0x759f47d4u},
    {0x5957643fu, 0x4172833fu, 0x4172833fu},
    {0x8d8428d4u, 0x759f47d4u, 0x759f47d4u},
    {0xaf464173u, 0x3aa3ee73u, 0x3aa3ee73u},
    {0xbdcdefbdu, 0x58a536bdu, 0x83571f35u},
    {0x4649f76du, 0xd1a7a46du, 0xd12b30a5u},
    {0x9a8287e1u, 0x1f0c3be1u, 0x1e747583u},
    {0x93745722u, 0x7b8f7622u, 0x3ce8cb2au},
    {0x10a28740u, 0xf618c040u, 0xf618c040u},
    {0x93745722u, 0x7b8f7622u, 0x7b8f7622u},
    {0xe56a836fu, 0x1d4a306fu, 0xb373bcd7u},
    {0xd9c2ad2fu, 0xedb3832fu, 0xcacdbe57u},
    {0x30469935u, 0x51a89135u, 0x51a89135u},
    {0xe56a836fu, 0x1d4a306fu, 0xb373bcd7u},
    {0xda3dbf1du, 0x61f4b31du, 0xd3ec5e37u},
    {0x46724db1u, 0x67d445b1u, 0xf8c93455u},
    {0xa3cb7b5du, 0x4bf3105du, 0xa45602f7u},
    {0xe56a836fu, 0x1d4a306fu, 0xb845c6bbu},
    {0x474a642du, 0xc098f32du, 0x606e0819u},
    {0xe56a836fu, 0x1d4a306fu, 0xb373bcd7u},
    // arm.2 armor head layer (cases 37..39); captured after the head-part select.
    {0xf1a29388u, 0x8d116188u, 0xf5eff250u},
    {0xe79144c0u, 0xc3e95cc0u, 0x4d851fd8u},
    {0x18bc869du, 0x00bc5c25u, 0x00bc5c25u},
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
    newGame(g, c.weapon, MODE_HUNT);
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
    g.armorHead = c.armorHead;

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
