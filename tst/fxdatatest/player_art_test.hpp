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
//
// Weapon-art regen (epic bhp): the sword now draws from mh_weapon_sword
// rows (idle/recover/startup/active/parry/dodge/stun/stun+idle) instead of
// the fxslash/fxparry/fxchip overlays. Changed cases: every W_SWORD row
// (0-14) plus the two sword armor-head rows (37, 38); flail (15-26, 39)
// and gun (27-36) are byte-identical.
// Bead monhun-ardu-z5i (feel.24) made the gunshield B-tap shove read as a bash:
// during PS_SHOVE drawPlayer now draws ONLY the shove plate at a forward offset
// that retracts as p.t counts down, instead of drawing the guard/idle plate
// plus a static shove plate. It changed exactly one case: 32 (W_GUN, PS_SHOVE,
// ST_NONE, E; the row the bead text called "case 33" -- the shove case is matrix
// index 32). Every other case is byte-identical; the new hashes are
// {0x4f202fb3, 0x670510b3, 0x670510b3} (p.t == 0 in the oracle, the fully
// retracted 4 px pose with the guard/idle plate gone).
//
// Bead monhun-ardu-ngf (owner: "gunshield sprites are messed up -- diagonal,
// stance, fire animation"): the weapon fills were rasterized by forward-sampling
// weapon space and plotting one pixel per sample, so the 45 deg DIR8 vector
// (11,11)/16 mapped many samples onto one pixel and punched the shield plate and
// flail ball into a checkerboard; the 1 px muzzle-flash rays read as specks.
// gen-equipment now inverse-maps every cell pixel to weapon space (cardinal
// facings are 1:1 and unchanged) and the shot flash outer rays draw w=2.
// Changed cases: 17, 18, 20-25 (flail attacks/special/whirl/deflect/dodge/stun:
// fractional ball centres) and 29, 31-35, 39 (gun guard/attack/arrowshot/shove/
// stun plate rows + the diagonal flail armor row). Idle/recover rows whose ball
// or plate sits on integer weapon-space coordinates (15, 16, 19, 26, 30, 36) and
// every sword case are byte-identical.
//
// Big-shield rework (owner follow-up on ngf: "should be a big shield, player
// sized with gunport in the middle"): the gun plate is now a portrait 12x16
// screen-space face with a dark rim and a 4x4 gunport (dark ring, black hole)
// on the barrel line, anchored along the facing vector instead of rotated in
// weapon space, and the shot rows fire through the port. Changed cases: the
// whole gun block 27-36 (every case draws the plate or the shot art); flail and
// sword rows are byte-identical.
//
// Bead monhun-ardu-jd1 (forge node sheet -> render): the equipped forge node's
// sheet kind (Game::wpnSheet) now selects which gun sheet renders, so the four
// gunshield variants are pinned as appended cases 40..43 (W_GUN, PS_IDLE,
// ST_GUARD, E, kinds 1..4). Only the appended rows are new; all 40 prior cases
// kept their bytes (the pre-jd1 rows carry wpnSheet 0 = the class default).

//
// Bead monhun-ardu-2tb (melee beast branches): the same equipped-node kind now
// also selects the sword (kinds 5..8: saber/cleaver/tailblade/fang) and flail
// (kinds 9..12: sling/shell/tail/spike) variant sheets, so the eight variants
// are pinned as appended cases 44..51 (W_SWORD/W_FLAIL, PS_IDLE, ST_NONE, E,
// kinds 5..12), each drawing a different idle-row weapon part. Only the
// appended rows are new; all 44 prior cases kept their bytes.
//
// RAM note: the case matrix outgrew RAM at 52 rows (the array sat in .data and
// the globals line hit 2497/2560 with 63 B for the stack, which corrupted
// earlier cases non-deterministically). CASES is now flash-resident behind
// MH_PROGMEM and read with memcpy_P per case, so the matrix can keep growing.
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
    // Equipped forge-node sheet kind (jd1); 0 = class default sheet, 1..4 = the
    // gunshield variants. Left off every pre-jd1 row so they keep their goldens.
    uint8_t wpnSheet;
};

// State matrix. Facings are 8-way fp vectors (E = 16,0; W = -16,0; SE = 11,11).
static const Case MH_PROGMEM CASES[] = {
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
    // jd1 gunshield variants: same base pose (W_GUN idle guard, E) with each
    // equipped forge-node sheet kind 1..4 -- the sheet selector must swap the
    // drawn gun sheet.
    {W_GUN, PS_IDLE, ST_GUARD, 0, 0, 16, 0, 0, 0, 0, 0, 0, 1},
    {W_GUN, PS_IDLE, ST_GUARD, 0, 0, 16, 0, 0, 0, 0, 0, 0, 2},
    {W_GUN, PS_IDLE, ST_GUARD, 0, 0, 16, 0, 0, 0, 0, 0, 0, 3},
    {W_GUN, PS_IDLE, ST_GUARD, 0, 0, 16, 0, 0, 0, 0, 0, 0, 4},
    // 2tb melee beast variants: same base pose (idle, facing E) with each
    // equipped forge-node sheet kind 5..8 (sword) / 9..12 (flail) -- the sheet
    // selector must swap the drawn sword/flail sheet. The idle row draws the
    // blade / ball, so every style pins distinct pixels.
    {W_SWORD, PS_IDLE, ST_NONE, 0, 0, 16, 0, 0, 0, 0, 0, 0, 5},
    {W_SWORD, PS_IDLE, ST_NONE, 0, 0, 16, 0, 0, 0, 0, 0, 0, 6},
    {W_SWORD, PS_IDLE, ST_NONE, 0, 0, 16, 0, 0, 0, 0, 0, 0, 7},
    {W_SWORD, PS_IDLE, ST_NONE, 0, 0, 16, 0, 0, 0, 0, 0, 0, 8},
    {W_FLAIL, PS_IDLE, ST_NONE, 0, 0, 16, 0, 0, 0, 0, 0, 0, 9},
    {W_FLAIL, PS_IDLE, ST_NONE, 0, 0, 16, 0, 0, 0, 0, 0, 0, 10},
    {W_FLAIL, PS_IDLE, ST_NONE, 0, 0, 16, 0, 0, 0, 0, 0, 0, 11},
    {W_FLAIL, PS_IDLE, ST_NONE, 0, 0, 16, 0, 0, 0, 0, 0, 0, 12},
};
constexpr uint8_t CASE_COUNT = static_cast<uint8_t>(sizeof(CASES) / sizeof(CASES[0]));
// Changing the matrix invalidates GOLDEN: extend both in the same change and
// capture the new hashes with the regen command above.
static_assert(CASE_COUNT == 52, "golden matrix changed; regenerate GOLDEN");

// Golden framebuffer hashes [case][plane], captured pre-refactor. See the regen
// note above; PROGMEM so the 3 x CASE_COUNT words stay in flash.
static const uint32_t MH_PROGMEM GOLDEN[CASE_COUNT][3] = {
    {0x16d3a3ecu, 0xb1423346u, 0xb1423346u},
    {0x0d444df2u, 0xacd44d08u, 0xacd44d08u},
    {0x1ce0909fu, 0xa1c7dce4u, 0xa1c7dce4u},
    {0x8772b478u, 0xfd364086u, 0xfd364086u},
    {0xbd548579u, 0xeeb9c403u, 0xeeb9c403u},
    {0xb8c8ec23u, 0xae836d6eu, 0xae836d6eu},
    {0xabe498d7u, 0x5dbf2a72u, 0x5dbf2a72u},
    {0x204eac15u, 0x417d01f6u, 0x417d01f6u},
    {0xc6cc098du, 0x3c91014eu, 0x3c91014eu},
    {0xdcb3a712u, 0x340f9028u, 0xa3193104u},
    {0x151c8f15u, 0xd6df7980u, 0x3fd62b70u},
    {0x99e8fb1fu, 0xbdc6761fu, 0x7aa26edeu},
    {0xf44b4a7fu, 0x3325bc7fu, 0xbecda52bu},
    {0x80e8cf85u, 0xa8b26d05u, 0x4ab1b646u},
    {0xd23c81f4u, 0x07a6637eu, 0x07a6637eu},
    {0x5588d356u, 0x2a256461u, 0x2a256461u},
    {0x34ef9fb0u, 0x1db433a1u, 0x1db433a1u},
    {0x8d3271aau, 0xc04502c6u, 0xc04502c6u},
    {0x72ffb6e8u, 0x48500171u, 0xc6071819u},
    {0x37998cb4u, 0x67fd6f44u, 0x67fd6f44u},
    {0x38a2ce3bu, 0xd53db8fdu, 0x8f3c1e8du},
    {0xa1fce3b7u, 0x0df6aeb7u, 0xa77ecd60u},
    {0x1e512eafu, 0x1fcad6afu, 0xd15b1536u},
    {0xafbeac98u, 0x5067e898u, 0x6e988ddeu},
    {0x8d9143a8u, 0xaf90a6a8u, 0xd6ace58au},
    {0x863cd611u, 0x91b922a3u, 0x8a63f356u},
    {0x5588d356u, 0x2a256461u, 0x2a256461u},
    {0xb1e00324u, 0x4fe4dd92u, 0x3d19f395u},
    {0xa6dab4b2u, 0x18bcf540u, 0x00347405u},
    {0x60f18d7fu, 0x09458678u, 0xb6d14f15u},
    {0x55c8dcffu, 0xd2160e4eu, 0x6b9e00c9u},
    {0xc3bf1d9fu, 0x139b91a5u, 0x38760c77u},
    {0xfe8e47f6u, 0x797be7c5u, 0xac6bb291u},
    {0x45f02787u, 0xf0ebd249u, 0x414ba017u},
    {0x46587adau, 0x06b04ed8u, 0x5959ea7du},
    {0x7ef6f612u, 0x4d1b04bfu, 0xc9855c92u},
    {0xb1e00324u, 0x4fe4dd92u, 0x3d19f395u},
    // arm.2 armor head layer (cases 37..39); big-shield regen.
    {0x23e552ecu, 0x71a79146u, 0xe8971baeu},
    {0x51522cf2u, 0x5e50a808u, 0x6391cf40u},
    {0x965e4d66u, 0x3c4ef065u, 0x3c4ef065u},
    // jd1 gunshield variants (cases 40..43): each equipped forge-node sheet
    // kind 1..4 draws a different gun sheet (all four hashes differ from each
    // other and from the default kind-0 guard case 29).
    {0xb8679899u, 0xb6322ac0u, 0x3a2e3ee9u},
    {0x6cf6bb00u, 0x6c89d9acu, 0x5be309deu},
    {0xaed61621u, 0x62e32a39u, 0x5d595e89u},
    {0x9d80d12fu, 0x17ccce82u, 0xa0fa3755u},
    // 2tb melee beast variants (cases 44..51): sword kinds 5..8 / flail kinds
    // 9..12, each drawing a different class sheet at the same idle pose.
    {0x15fb6d94u, 0x09802e7cu, 0x09802e7cu},
    {0x04d7ea52u, 0x363c6d64u, 0x6bcb5483u},
    {0x4f7da0ccu, 0xd62cb1ebu, 0x9e231fedu},
    {0x7fa3131du, 0xa26f6e75u, 0x239bd593u},
    {0x3382bdd5u, 0x2f9de2b3u, 0x2f9de2b3u},
    {0x8171362au, 0x6e297a02u, 0xebfab8b1u},
    {0xaf913f46u, 0x5eb2cee1u, 0x570cb801u},
    {0x5588d356u, 0x2a256461u, 0x2a256461u},
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
    g.wpnSheet = c.wpnSheet;

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
        Case c;
        memcpy_P(&c, &CASES[ci], sizeof(Case));   // flash-resident matrix (RAM note above)
        setupCase(g, c);
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
