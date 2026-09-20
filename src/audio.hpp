#pragma once
// Device audio cues for the vertical slice.
//
// Design (bead monhun-ardu-6zc): the core sim never touches audio. This header
// observes the Game after each stepGame() and fires a short cue on the tick a
// combat event happens, by diffing the previous tick's state. No core header is
// modified; cue triggering only *reads* Game.
//
// Timer: ArduboyG owns TIMER1 (ABG_TIMER1 in src/common.hpp); the beeper owns
// TIMER3_COMPA (the vector at __vector_32, deliberately not USB __vector_11),
// so the two ISRs do not collide. Cues are short one-shots: audioPlay() just
// programs the timer/pin and returns, so the plane loop keeps its FX/OLED
// bracket and needsUpdate() gating untouched. The driver (bead
// monhun-ardu-44z) is a minimal replacement for ArduboyTones that keeps the
// same CTC /8 prescaler, speaker pin PC6 (PC7 held low, normal volume) and
// precomputed pitch/duration constants, so the cues sound the same.
//
// Mute for device test builds: compile with -DMH_AUDIO=0. That preprocesses the
// whole module down to no-ops and drops the beeper entirely, so an fxtest sketch
// can include it and still run silent.
//
// Edge detection is freeze-safe: stepGame() skips updateEffects()/sim while
// Game::freeze > 0, so an effect can sit at t==1 for several ticks and a
// transient state (deflect, riposteT) holds its value. Every cue below is gated
// on an edge that only advances on a non-frozen tick (hp/total drop, stun set,
// projectile spawn, reload reach 0, windup entry), so nothing refires.

#include <stdint.h>
#include "core/world.hpp"

#ifndef MH_AUDIO
#define MH_AUDIO 1
#endif

#if MH_AUDIO
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#endif

namespace mh {

enum AudioCue : uint8_t {
    CUE_NONE = 0,
    CUE_HIT,       // hunt: melee/shot landed on the beast
    CUE_CRIT,      // hunt head hit / pole head hit
    CUE_TRAIN,     // train: body hit on the pole
    CUE_HURT,      // player took a clean hit
    CUE_PARRY,     // sword riposte landed
    CUE_DEFLECT,   // flail deflect absorbed a hit
    CUE_GUARD,     // gunshield guard block (chip damage)
    CUE_WINDUP,    // beast started a windup (telegraph)
    CUE_SHOT,      // gun fired a shell
    CUE_RELOAD,    // gun reload finished
    CUE_BREAK,     // hunt: a breakable beast part's zone drained (part broke)
};

// Previous-tick snapshot + one-slot retrigger guard.
struct AudioState {
    int16_t tick;
    int16_t monsterHp;
    uint8_t playerHp;
    uint8_t reload;
    uint8_t monsterStun;
    uint8_t projN;
    uint8_t riposteT;
    uint8_t poleBroken;
    int8_t monsterState;
    int8_t playerStance;
    int8_t playerState;
    bool inited;
    uint8_t lastCue;
    int16_t lastCueTick;
    // Bitset of cues fired on the most recent audioUpdate(): bit CUE_x. Test hook
    // (tst/fxdatatest/audio_test.hpp) and cheap debug; not used by playback.
    uint16_t firedMask;
};

#if MH_AUDIO
// Beeper state. ISR-touched values are volatile; the ISR only toggles PC6 and
// advances/ends the one-shot, so no library object or outEn callback is needed.
static volatile uint16_t mhToggles;    // pin toggles left in the active segment
static volatile uint16_t mhOcr2;       // queued segment-2 period (OCR3A)
static volatile uint16_t mhToggles2;   // queued segment-2 toggles (0 = none)

// Cue table: {OCR3A, toggles, OCR3A2, toggles2}, precomputed for the exact
// ArduboyTones math (OCR = F_CPU/8/freq/2 - 1, toggles = (ms*freq)>>9).
// Index 0 is CUE_NONE (all zero); every queue has toggle counts >= 1.
static const uint16_t mhCueTable[12][4] PROGMEM = {
    {0, 0, 0, 0},           // CUE_NONE
    {2023, 21, 0, 0},       // CUE_HIT     494,22
    {1516, 20, 954, 81},    // CUE_CRIT    659,16 1047,40
    {1274, 36, 0, 0},       // CUE_TRAIN   784,24
    {6450, 13, 0, 0},       // CUE_HURT    155,45
    {1135, 30, 850, 126},   // CUE_PARRY   880,18 1175,55
    {1431, 24, 1135, 58},   // CUE_DEFLECT 698,18 880,34
    {5101, 11, 3815, 23},   // CUE_GUARD   196,30 262,45
    {5713, 6, 4290, 13},    // CUE_WINDUP  175,20 233,30
    {636, 42, 954, 49},     // CUE_SHOT    1568,14 1047,24
    {954, 24, 636, 85},     // CUE_RELOAD  1047,12 1568,28
    {1431, 20, 750, 100},   // CUE_BREAK   698,28 1060,80
};

// Arm one cue. Pins are only set to output/low here (the old constructor did it
// once); PC6 toggles to make the square wave, PC7 stays low for normal volume.
static void mhPlay(uint8_t cue) {
    const uint16_t *rec = mhCueTable[cue];
    TIMSK3 = 0;   // stop the ISR while re-arming
    DDRC |= _BV(PORTC6) | _BV(PORTC7);
    PORTC &= (uint8_t)~(_BV(PORTC6) | _BV(PORTC7));
    mhToggles2 = pgm_read_word(rec + 3);
    mhOcr2 = pgm_read_word(rec + 2);
    mhToggles = pgm_read_word(rec + 1);
    TCCR3A = 0;
    TCCR3B = _BV(WGM32) | _BV(CS31);   // CTC, /8 (matches ArduboyTones)
    OCR3A = pgm_read_word(rec + 0);
    TIMSK3 = _BV(OCIE3A);
}

ISR(TIMER3_COMPA_vect) {
    PINC = _BV(PORTC6);   // toggle speaker pin
    if (--mhToggles == 0) {
        const uint16_t t2 = mhToggles2;
        if (t2) {   // start queued second segment
            OCR3A = mhOcr2;
            mhToggles = t2;
            mhToggles2 = 0;
        } else {   // one-shot done
            TIMSK3 = 0;
            PORTC &= (uint8_t)~_BV(PORTC6);
        }
    }
}

static void audioPlay(uint8_t cue) {
    mhPlay(cue);
}
#else
static void audioPlay(uint8_t) {
}
#endif

static void audioSnapshot(AudioState &s, const Game &g) {
    s.tick = g.tick;
    s.monsterHp = g.monster.hp;
    s.playerHp = g.player.hp;
    s.reload = g.player.reload;
    s.monsterStun = g.monster.stun;
    s.projN = g.projN;
    s.riposteT = g.player.riposteT;
    s.poleBroken = g.combat.zoneBroken;
    s.monsterState = g.monster.state;
    s.playerStance = g.player.stance;
    s.playerState = g.player.state;
    s.inited = true;
}

// Rate-limit: a repeat of the same cue within 2 ticks is dropped so a fast
// chain cannot muddy the mix. Distinct / higher-priority cues still play.
static void audioCue(AudioState &s, uint8_t cue) {
    if (cue == CUE_NONE)
        return;
    if (cue == s.lastCue && static_cast<int16_t>(s.tick - s.lastCueTick) < 2)
        return;
    s.lastCue = cue;
    s.lastCueTick = s.tick;
    s.firedMask |= static_cast<uint16_t>(1u << cue);
    audioPlay(cue);
}

// One tick. Call once per run(), after stepGame().
static void audioUpdate(AudioState &s, const Game &g) {
    s.firedMask = 0;
    // First frame or a reset (newGame zeroes tick): latch state, fire nothing.
    if (!s.inited || g.tick < s.tick) {
        audioSnapshot(s, g);
        return;
    }

    const Player &p = g.player;
    const Monster &m = g.monster;

    // Effects spawned this tick have t==1 (addEffect t=0, updateEffects++). A
    // crit spark (text==0) marks a hunt crit; a text effect marks a landed pole
    // hit (damagePole spawns the rising number) and its crit flag the head hit.
    // Only consulted when a same-tick hp/zone gate proves the event.
    bool critSpark = false;
    bool critText = false;
    bool trainText = false;
    for (int16_t i = 0; i < g.fxN; i++) {
        const Effect &e = g.fx[i];
        if (e.t != 1)
            continue;
        if (e.text) {
            trainText = true;
            if (e.crit)
                critText = true;
        } else if (e.crit)
            critSpark = true;
    }

    const bool monsterDrop = m.hp < s.monsterHp;
    const bool poleBroke = g.combat.zoneBroken > s.poleBroken;
    const bool playerDrop = p.hp < s.playerHp;
    const bool guarding = playerDrop && (p.stance == ST_GUARD || s.playerStance == ST_GUARD);

    // Utility cues first, so a same-tick combat reaction can overwrite them.
    if (g.projN > s.projN)
        audioCue(s, CUE_SHOT);
    if (s.reload > 0 && p.reload == 0)
        audioCue(s, CUE_RELOAD);
    if (m.state == MS_WINDUP && s.monsterState != MS_WINDUP)
        audioCue(s, CUE_WINDUP);

    // Combat reactions (break > defense > crit > hit > hurt). Break wins the
    // same tick as the train damage blip.
    if (poleBroke) {
        audioCue(s, CUE_BREAK);
    } else if (p.riposteT > s.riposteT) {
        audioCue(s, CUE_PARRY);
    } else if (m.stun > s.monsterStun && p.state == PS_DEFLECT) {
        audioCue(s, CUE_DEFLECT);
    } else if (guarding) {
        audioCue(s, CUE_GUARD);
    } else if (monsterDrop && critSpark) {
        audioCue(s, CUE_CRIT);
    } else if (monsterDrop) {
        audioCue(s, CUE_HIT);
    } else if (trainText) {
        audioCue(s, critText ? CUE_CRIT : CUE_TRAIN);
    } else if (playerDrop) {
        audioCue(s, CUE_HURT);
    }

    audioSnapshot(s, g);
}

}   // namespace mh
