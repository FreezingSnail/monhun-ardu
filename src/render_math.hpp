#pragma once
// Host-shareable render math with no Arduboy dependency (so tst/ can unit-test
// the frame selectors the device render uses). Integer only: no float, no
// Arduino headers.

#include <stdint.h>

namespace mh {

// ------------------------------------------- windup animation tells (prg.11)
// `combat.attack.tell` is a windup animation-frame selector, not a procedural
// shape. The authored pose lives on the beast's attack sheet (fxchickenatk /
// fxbullatk / fxtail_spin / BEAST_POSES); prg.12 authors the per-attack frames.
// Every telegraph is the beast's own sprite art -- there is no procedural marker.
//
// tell 0 is the generic coil (BEAST_POSES windup, always available). tell 1..N
// select bespoke per-attack windup frames when the beast carries them; a tell
// beyond the authored set (4) has no bespoke pose and keeps the attack's own
// windup frame.

// Sentinel: no bespoke windup frame for this tell (render keeps the attack pose).
constexpr uint8_t TELL_WINDUP_NONE = 0xFF;
// Number of bespoke windup frames the beast sheets carry. prg.12 authored the
// per-attack windup poses on fxchickenatk (slots 1..2), fxbullatk (slots 1..3)
// and fxheavyatk (slots 1..3), so a tell 1..3 now selects an authored pose;
// tell 0 stays the generic coil and tell 4 (ZONE) keeps the attack's windup
// frame. Host/device tests pass a synthetic count to pin the selector.
constexpr uint8_t TELL_FRAMES_AUTHORED = 3;

// True when `tell` selects a bespoke windup frame on a sheet that carries
// `authored` of them. tell 0 is the generic coil (not a bespoke frame).
inline bool tellHasAuthoredFrame(uint8_t tell, uint8_t authored) {
    return tell != 0 && tell <= authored;
}

// Bespoke windup frame index for `tell`, or TELL_WINDUP_NONE when unauthored.
inline uint8_t tellWindupFrame(uint8_t tell, uint8_t authored) {
    return tellHasAuthoredFrame(tell, authored) ? tell : TELL_WINDUP_NONE;
}

// Frame into the 8-frame 40x40 fxtailspin sheet (bead monhun-ardu-nch.3).
// Frame 0 is the east silhouette and frame i is i*45 deg clockwise about the
// body centre, so the sheet advances one 45-deg step per equal slice of the
// attack's active window and completes one revolution over it.
//
// `start8` is the DIR8 index of the locked facing (DIR8 0=E, 1=SE, ... clockwise,
// matching the clockwise frame order); `tick` is Monster::t during MS_ATTACK
// (0-based, incremented each active tick) and `active` is the cached attack's
// active tick count. progress8 = (tick * 8) / active (truncating integer
// division), wrapped into 0..7; at tick == active progress8 == 8 and the frame
// wraps back to start8. Any stale/negative inputs collapse to start8.
inline uint8_t spinSheetFrame(uint8_t start8, int16_t tick, int16_t active) {
    if (tick > 0 && active > 0) {
        const int16_t progress8 = static_cast<int16_t>((static_cast<int32_t>(tick) * 8) / active);
        return static_cast<uint8_t>((start8 + progress8) & 7);
    }
    return static_cast<uint8_t>(start8 & 7);
}

}   // namespace mh
