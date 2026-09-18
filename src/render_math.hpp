#pragma once
// Host-shareable render math with no Arduboy dependency (so tst/ can unit-test
// the frame selectors the device render uses). Integer only: no float, no
// Arduino headers.

#include <stdint.h>

namespace mh {

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
