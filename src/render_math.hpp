#pragma once
// Host-shareable render math with no Arduboy dependency (so tst/ can unit-test
// the frame selectors the device render uses). Integer only: no float, no
// Arduino headers.

#include <stdint.h>

namespace mh {

// ---------------------------------------------------- attack telegraph (feel.5)
// Pure geometry for the per-attack windup tell shapes (src/render.hpp draws
// them from the cached window; this header stays Arduino-free so the host suite
// can pin the numbers). Shape ids mirror mh::Tell in src/core/combat.hpp:
// 0 dot (default 2x2 core), 1 line, 2 arc, 3 ring, 4 zone.

// True for every shape that draws beyond the default core dot. The render
// consults this so tell 0 keeps the legacy single 2x2 blk.
inline bool tellNeedsWindow(uint8_t tell) {
    return tell != 0;
}

// TELL_LINE (1): i-th (1..3) 2x2 dash *outer corner* offset from the body centre
// along the body-centre -> window-centre vector (dx,dy). Q2 fractions
// (dx*i)>>2 keep the dashes on the facing ray without a divide helper; the ray
// reaches the window centre, i.e. the box's reach/depth from the body.
inline void tellLineDash(int16_t dx, int16_t dy, uint8_t i, int16_t &ox, int16_t &oy) {
    ox = static_cast<int16_t>(static_cast<int16_t>(dx * i) >> 2);
    oy = static_cast<int16_t>(static_cast<int16_t>(dy * i) >> 2);
}

// TELL_RING (3): expanding outline half-extent (px). Grows ~1 px per 2 elapsed
// windup ticks from 2, clamped to the window half-extent, so the box closes in
// as the windup counts down.
inline int16_t tellRingHalf(int16_t maxHalf, int16_t elapsed) {
    const int16_t grow = static_cast<int16_t>((elapsed > 0 ? elapsed : 0) >> 1);
    const int16_t half = static_cast<int16_t>(grow + 2);
    return half < maxHalf ? half : maxHalf;
}

// TELL_ARC (2): i-th (0..2) 4x2 arc segment offset from the window rect
// top-left. Three segments span the box width (left, centre, right) across the
// vertical middle, the centre dropped 2 px so they read as a shallow arc.
inline void tellArcSeg(int16_t bw, int16_t bh, uint8_t i, int16_t &ox, int16_t &oy) {
    const int16_t base = static_cast<int16_t>((bh >> 1) - 1);
    if (i == 0) {
        ox = 0;
        oy = base;
    } else if (i == 1) {
        ox = static_cast<int16_t>((bw >> 1) - 2);
        oy = static_cast<int16_t>(base + 2);
    } else {
        ox = static_cast<int16_t>(bw - 4);
        oy = base;
    }
}

// Window rect top-left from the window centre (ax,ay) and size: the shared
// outline origin for RING/ZONE, truncating shifts exactly like the hit test.
inline void tellRectOrigin(int16_t ax, int16_t ay, int16_t bw, int16_t bh, int16_t &x, int16_t &y) {
    x = static_cast<int16_t>(ax - (bw >> 1));
    y = static_cast<int16_t>(ay - (bh >> 1));
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
