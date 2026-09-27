#pragma once
// On-device perf bench (bead monhun-ardu-8v7).
//
// Runs the shipping loop shape on the real target (Ardens cycle-accurate
// ATmega32u4 model) and gates the bead's acceptance budgets:
//   plane rate >= 135 Hz, logic >= 45 Hz, free RAM >= 300 B, render/logic/FX
//   each inside the frame budget, and the moving-room render (nx9) inside the
//   same 1/135 s plane floor.
//
// Why the bench is so lean: the full render stack + core sim + the harness's
// Serial already consume ~27.7 KB of the 29.7 KB flash, leaving <2 KB. So the
// loop is timed inline (no extra measurement framework) and only the final
// numbers are printed. Timebase is micros() (Timer0 /64), cycle-modelled and
// independent of ArduboyG's TIMER1 ISR and the beeper's TIMER3, so the real
// audio cue path can stay compiled in. Integer math only; 1 us == 16 cycles.

#include "harness/fxtest.hpp"
#include "src/core/world.hpp"
// Muted for the bench image only: the full render stack + core sim + Serial use
// ~27.7 KB of the 29.7 KB flash, so ArduboyTones would not fit. audioUpdate()
// still runs its real edge-detect path (that cost is measured); only the rare
// one-shot tone() arming is excluded. Cue playback is gated by test_audio.
#define MH_AUDIO 0
#include "src/audio.hpp"
#include "src/render.hpp"

#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>

extern "C" {
extern uint8_t __bss_end;
}

namespace perf {

using namespace mh;

// ------------------------------------------------------------- budgets
constexpr uint16_t PLANE_HZ_FLOOR = 135;           // bead design: planes stable ~135
constexpr uint16_t LOGIC_HZ_FLOOR = 45;            // bead acceptance: logic >= 45 Hz
constexpr uint16_t RAM_FREE_MIN = 300;             // bead acceptance
constexpr uint32_t PLANE_US = 1000000UL / 156UL;   // nominal plane period
constexpr uint32_t PLANE_FLOOR_US = 1000000UL / PLANE_HZ_FLOOR;
constexpr uint32_t LOGIC_FRAME_US = 3UL * PLANE_US;   // logic runs 1:3 planes

// Loop count: 36 planes = 12 logic frames, ~0.23 s simulated.
constexpr uint16_t PLANE_ITERS = 36;

// Moving-room phase (monhun-ardu-nx9, owner playtest): 108 planes = 36 logic
// frames (~0.7 s) of holding RIGHT, enough to walk the 512 px area room's
// middle past the first camera clamp and make the ground blit scroll.
constexpr uint16_t MOVING_PLANE_ITERS = 108;

static inline uint32_t now() {
    return micros();
}

struct Stat {
    uint32_t sum;
    uint32_t max;
    uint16_t n;
};

#define MH_NI __attribute__((noinline))

__attribute__((noinline)) static void hit(Stat &s, uint32_t v) {
    s.sum += v;
    if (v > s.max)
        s.max = v;
    ++s.n;
}
__attribute__((noinline)) static uint32_t avg(const Stat &s) {
    return s.n ? s.sum / s.n : 0;
}

static Input scriptedInput(int16_t tick) {
    Input in;
    in.mx = 1;
    in.my = ((tick / 8) & 1) ? 1 : -1;
    in.a = (tick % 5) == 0;
    in.b = (tick % 37) < 11;
    return in;
}

// 12 live effects (6 sparks, 6 damage numbers): the transient pressure the
// worst-case scene carries. The 3 shell/projectile entries were retired with
// the gun hitscan rework; the parked proj slots stay zeroed.
MH_NI static void addPressure(Game &g) {
    for (uint8_t i = 0; i < 12; i++) {
        Effect &e = g.fx[g.fxN++];
        e.x = 40 + i * 6;
        e.y = 20 + i * 3;
        e.t = 1;
        e.life = 20;
        e.crit = (i & 1) != 0;
    }
}

// Worst-case hunt plane: flail whirl ring + ball, beast mid-attack with stun
// sparkle and hit-flash shake, full HUD monster bar, then the shared pressure.
MH_NI static void primeHunt(Game &g) {
    newGame(g, W_FLAIL, MODE_HUNT);
    g.camX = 64;
    g.camY = 28;
    g.tick = 100;
    Player &p = g.player;
    p.x = 96;
    p.y = 48;
    p.subX = 8;
    p.subY = 8;
    p.hp = 80;
    p.stam = 90;
    p.state = PS_STUN;
    p.stance = ST_WHIRL;
    p.whirlTick = 3;
    p.iT = 8;
    Monster &m = g.monster;
    m.x = 150;
    m.y = 40;
    m.subX = 4;
    m.subY = 4;
    m.hp = 120;
    m.state = MS_ATTACK;
    monsterAttackSet(g, combat::ATTACK_LUNGE_LEAP);
    m.stun = 12;
    m.hitFlash = 4;
    m.fx = 16;
    m.t = 6;
    m.windupMax = 40;
    addPressure(g);
}

// The exact shipping loop body (minus pollButtons; input is scripted). The live
// scene evolves naturally so logic costs are real; render is measured on the
// same plane it ships on. Phases accumulate into the caller's stats.
MH_NI static void runBench(Game &g, AudioState &s, Stat &wait, Stat &logic, Stat &render) {
    for (uint16_t i = 0; i < PLANE_ITERS; i++) {
        uint32_t a = now();
        FX::enableOLED();
        arduboy.waitForNextPlane();
        FX::disableOLED();
        hit(wait, now() - a);
        if (arduboy.needsUpdate()) {
            a = now();
            stepGame(g, scriptedInput(g.tick));
            audioUpdate(s, g);
            hit(logic, now() - a);
        }
        a = now();
        renderScene(g, false);
        hit(render, now() - a);
    }
}

// Moving-room phase (monhun-ardu-nx9): the owner playtest scenario -- a hunt in
// the real 512x112 area room, the hunter walking (RIGHT held) from mid-room with
// the beast parked at its home spawn off-screen -- on the exact shipping loop
// shape. Covers what the fixed-camera pressure scene cannot: the camera-follow
// scroll of the ground blit, the real room's door cart reads in logic, and its
// prop records in render. Logic and render are separately reported so a scroll
// spike is readable at a glance.
MH_NI static void runMoving(Game &g, Stat &logic, Stat &render) {
    newGame(g, W_SWORD, MODE_HUNT, MON_LUNGE);
    beastHomeSpawn(g, MON_LUNGE);   // area home spawn (320,72), off-screen
    loadRoom(g, zone::ROOM_AREA, zone::SPAWN_AREA_FROM_CAMP);
    g.player.sheathed = true;   // demo hunts start stowed (1du)
    g.player.x = 200;           // mid-room: camX is past its clamp and scrolls
    g.player.y = 40;            // camY = 20 -> the split reader's v != 0 path
    updateCamera(g);
    Input in;
    in.mx = 1;
    in.my = 0;
    in.a = false;
    in.b = false;
    for (uint16_t i = 0; i < MOVING_PLANE_ITERS; i++) {
        FX::enableOLED();
        arduboy.waitForNextPlane();
        FX::disableOLED();
        if (arduboy.needsUpdate()) {
            const uint32_t a = now();
            stepGame(g, in);
            hit(logic, now() - a);
        }
        const uint32_t a = now();
        renderScene(g, false);
        hit(render, now() - a);
    }
}

// ------------------------------------------------------------- ram watermark
static inline uint16_t getSP() {
    uint16_t sp;
    asm volatile("in %A0, 0x3d\n\tin %B0, 0x3e" : "=r"(sp));
    return sp;
}
static inline uint16_t ramLow() {
    return reinterpret_cast<uint16_t>(&__bss_end);
}
static inline uint16_t ramHigh() {
    return static_cast<uint16_t>(RAMEND);
}

MH_NI static void paintStack() {
    const uint8_t sreg = SREG;
    cli();
    const uint16_t stop = static_cast<uint16_t>(getSP() - 24);
    for (uint16_t a = ramLow(); a < stop; a++)
        *reinterpret_cast<uint8_t *>(a) = 0xA5;
    SREG = sreg;
}

// First sentinel byte clobbered == deepest SP reached; free RAM there is
// deepest_SP - end_of_globals (not the shallow loop() frame).
MH_NI static uint16_t scanStack() {
    const uint8_t sreg = SREG;
    cli();
    uint16_t a = ramLow();
    const uint16_t top = ramHigh();
    while (a < top && *reinterpret_cast<uint8_t *>(a) == 0xA5)
        a++;
    SREG = sreg;
    return a;
}

// ------------------------------------------------------------- suite
static Game s_g;
static AudioState s_s;

inline void test_perf(FxTest &test) {
    arduboy.startGray();   // plane ISR drives waitForNextPlane/needsUpdate

    Stat wait, logic, render;
    wait.sum = logic.sum = render.sum = 0;
    wait.max = logic.max = render.max = 0;
    wait.n = logic.n = render.n = 0;

    primeHunt(s_g);
    s_s.inited = false;
    runBench(s_g, s_s, wait, logic, render);
    primeHunt(s_g);   // reset the pressure, gun loadout
    s_g.weapon = W_GUN;
    s_s.inited = false;
    runBench(s_g, s_s, wait, logic, render);

    // Moving-room phase (nx9): the owner playtest's walking scene.
    Stat mLogic, mRender;
    mLogic.sum = mRender.sum = 0;
    mLogic.max = mRender.max = 0;
    mLogic.n = mRender.n = 0;
    runMoving(s_g, mLogic, mRender);

    // One iteration = bracket wait + render, plus logic on 1 of every 3 planes.
    const uint32_t planeUs = avg(wait) + avg(render) + avg(logic) / 3;
    const uint32_t planeHz = 1000000UL / planeUs;
    const uint32_t logicHz = planeHz / 3;

    // Deepest-stack free RAM, measured inside the render call tree.
    primeHunt(s_g);
    paintStack();
    for (uint16_t i = 0; i < 16; i++) {
        s_g.tick++;
        renderScene(s_g, false);
    }
    const uint16_t deepest = scanStack();
    const uint32_t freeRam = deepest - ramLow();

    // ---- numbers (us / Hz / B; keys documented in output.md) ------------
    Serial.print(F("B pUs="));
    Serial.print((unsigned)planeUs);
    Serial.print(F(" pHz="));
    Serial.print((unsigned)planeHz);
    Serial.print(F(" lHz="));
    Serial.print((unsigned)logicHz);
    Serial.print(F(" lTk="));
    Serial.print((unsigned)logic.max);
    Serial.print(F(" rMx="));
    Serial.print((unsigned)render.max);
    Serial.print(F(" rAv="));
    Serial.print((unsigned)avg(render));
    Serial.print(F(" mRrMx="));
    Serial.print((unsigned)mRender.max);
    Serial.print(F(" mRrAv="));
    Serial.print((unsigned)avg(mRender));
    Serial.print(F(" mLgMx="));
    Serial.print((unsigned)mLogic.max);
    Serial.print(F(" ram="));
    Serial.println((unsigned)freeRam);

    // ---- gates (bitmask so one FAIL line documents which budget blew) ---
    uint8_t mask = 0;
    if (render.max >= PLANE_FLOOR_US)
        mask |= 1;   // render > 1/135 s
    if (logic.max >= LOGIC_FRAME_US)
        mask |= 2;   // logic > 3 plane periods
    if (planeHz < PLANE_HZ_FLOOR)
        mask |= 4;
    if (logicHz < LOGIC_HZ_FLOOR)
        mask |= 8;
    if (freeRam < RAM_FREE_MIN)
        mask |= 16;
    if (mRender.max >= PLANE_FLOOR_US)
        mask |= 32;   // moving-room render > 1/135 s
    if (mask == 0) {
        test.passCount += 6;
    } else {
        test.failCount += 6;
        Serial.print(F("F "));
        Serial.println(mask);
    }
}

}   // namespace perf
