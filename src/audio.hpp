#pragma once
// Device audio cues for the vertical slice.
//
// Design (bead monhun-ardu-6zc): the core sim never touches audio. This header
// observes the Game after each stepGame() and fires a short cue on the tick a
// combat event happens, by diffing the previous tick's state. No core header is
// modified; cue triggering only *reads* Game.
//
// Timer: ArduboyG owns TIMER1 (ABG_TIMER1 in src/common.hpp). ArduboyTones
// drives TIMER3_COMPA and toggles the speaker pins PC6/PC7, so the two ISRs do
// not collide. Cues are short, one-shot tone() calls (ArduboyTones is
// non-blocking: tone() just arms the timer ISR and returns), so the plane loop
// keeps its FX/OLED bracket and needsUpdate() gating untouched.
//
// Mute for device test builds: compile with -DMH_AUDIO=0. That preprocesses the
// whole module down to no-ops and drops the ArduboyTones dependency entirely,
// so an fxtest sketch can include it and still run silent.
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
#include <ArduboyTones.h>
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
};

// Previous-tick snapshot + one-slot retrigger guard.
struct AudioState {
    int16_t tick;
    int16_t monsterHp;
    int16_t playerHp;
    int32_t trainTotal;
    int16_t reload;
    int16_t monsterStun;
    int16_t projN;
    int16_t riposteT;
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
// outEn is called from the tone ISR; the build is compile-time muted, so this
// only ever runs when cues are actually wanted.
static inline bool mhAudioEnabled() {
    return true;
}
// Global instance performs the speaker-pin setup in its constructor.
static ArduboyTones mhTones(mhAudioEnabled);

static void audioPlay(uint8_t cue) {
    switch (cue) {
    case CUE_HIT:
        ArduboyTones::tone(494, 22);
        break;
    case CUE_CRIT:
        ArduboyTones::tone(659, 16, 1047, 40);
        break;
    case CUE_TRAIN:
        ArduboyTones::tone(784, 24);
        break;
    case CUE_HURT:
        ArduboyTones::tone(155, 45);
        break;
    case CUE_PARRY:
        ArduboyTones::tone(880, 18, 1175, 55);
        break;
    case CUE_DEFLECT:
        ArduboyTones::tone(698, 18, 880, 34);
        break;
    case CUE_GUARD:
        ArduboyTones::tone(196, 30, 262, 45);
        break;
    case CUE_WINDUP:
        ArduboyTones::tone(175, 20, 233, 30);
        break;
    case CUE_SHOT:
        ArduboyTones::tone(1568, 14, 1047, 24);
        break;
    case CUE_RELOAD:
        ArduboyTones::tone(1047, 12, 1568, 28);
        break;
    default:
        break;
    }
}
#else
static void audioPlay(uint8_t) {
}
#endif

static void audioSnapshot(AudioState &s, const Game &g) {
    s.tick = g.tick;
    s.monsterHp = g.monster.hp;
    s.playerHp = g.player.hp;
    s.trainTotal = g.train.total;
    s.reload = g.player.reload;
    s.monsterStun = g.monster.stun;
    s.projN = g.projN;
    s.riposteT = g.player.riposteT;
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
    // crit spark (text==0) marks a hunt crit; a crit text effect marks a pole
    // head hit. Only consulted when a same-tick hp/total drop gates the event.
    bool critSpark = false;
    bool critText = false;
    for (int16_t i = 0; i < g.fxN; i++) {
        const Effect &e = g.fx[i];
        if (e.t != 1)
            continue;
        if (e.text) {
            if (e.crit)
                critText = true;
        } else if (e.crit)
            critSpark = true;
    }

    const bool monsterDrop = m.hp < s.monsterHp;
    const bool trainGain = g.train.total > s.trainTotal;
    const bool playerDrop = p.hp < s.playerHp;
    const bool guarding = playerDrop && (p.stance == ST_GUARD || s.playerStance == ST_GUARD);

    // Utility cues first, so a same-tick combat reaction can overwrite them.
    if (g.projN > s.projN)
        audioCue(s, CUE_SHOT);
    if (s.reload > 0 && p.reload == 0)
        audioCue(s, CUE_RELOAD);
    if (m.state == MS_WINDUP && s.monsterState != MS_WINDUP)
        audioCue(s, CUE_WINDUP);

    // Combat reactions (defense > crit > hit > hurt).
    if (p.riposteT > s.riposteT) {
        audioCue(s, CUE_PARRY);
    } else if (m.stun > s.monsterStun && p.state == PS_DEFLECT) {
        audioCue(s, CUE_DEFLECT);
    } else if (guarding) {
        audioCue(s, CUE_GUARD);
    } else if (monsterDrop && critSpark) {
        audioCue(s, CUE_CRIT);
    } else if (monsterDrop) {
        audioCue(s, CUE_HIT);
    } else if (trainGain) {
        audioCue(s, critText ? CUE_CRIT : CUE_TRAIN);
    } else if (playerDrop) {
        audioCue(s, CUE_HURT);
    }

    audioSnapshot(s, g);
}

}   // namespace mh
