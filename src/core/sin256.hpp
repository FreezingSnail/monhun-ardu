#pragma once
// 256-step sine/cosine, Q4 fixed point (-16..16). Stored as a 65-entry
// quarter-wave table (SIN65[0..64]) plus quadrant sign folding instead of the
// original 256-byte table: 191 B less MCU flash, bit-identical values
// (monhun-ardu-42n.7).
//
// sin256(a) for a in 0..255 with 256 units/turn:
//   q = a >> 6 selects the quadrant, k = a & 63 the position inside it.
//   sin is symmetric about a = 64 (sin(64-k) == sin(64+k)) and odd about
//   a = 128, so i = (q & 1) ? (64 - k) : k indexes SIN65 and the low quadrant
//   bit flips the sign. cos256(a) = sin256(a + 64), as before.
//
// Host-testable: no Arduino.h, the only dependency is the PROGMEM shim. The
// exhaustive host suite (tst/sin_test.hpp) pins every output against the
// original 256-entry table.

#include <stdint.h>
#include "progmem.hpp"
#if defined(__AVR__)
#include "fxmem.hpp"   // table lives on the FX cart (monhun-ardu-ept)
#endif

namespace mh {

// sin(0)..sin(64): index i is round(sin(i * 2*pi / 256) * 16).
//
// Host keeps the plain array (tst/sin_test.hpp walks all 256 inputs). On AVR
// the 65 B table is a raw_t blob in the FX image (fxdata/tables/sin65.bin,
// emitted by tools/gen-fxtables.cpp) and is copied into RAM once on the first
// call (sin65Ram below), so later reads are RAM loads, not cart seeks:
// sin256/cos256 are called from the render pass only (whirl sprites, stun
// sparkles), i.e. between ArduboyG plane blits, never during a paint.
#if !defined(__AVR__)
static const int8_t MH_PROGMEM SIN65[65] = {
    0,  0,  1,  1,  2,  2,  2,  3,  3,  4,  4,  4,  5,  5,  5,  6,  6,  6,  7,  7,  8,  8,  8,  9,  9,  9,  10, 10, 10, 10, 11, 11, 11,
    12, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
};
#else
// One-time 65 B RAM copy of the cart table (monhun-ardu-dx5.3). sin256/cos256
// run up to 6x/frame in the render pass (3 cos/sin pairs: whirl ball, player
// stun, monster stun); each cart byte was a ~100-cycle seek. The first call
// bulk-reads the 65 B blob (one transaction) and every later call is a plain
// RAM load. Function-local static => one shared instance across all inlined
// call sites; the guard is a single byte branch per call. The fill happens in
// the render pass between plane blits, the only window FX reads are legal.
// RAM cost: 65 B table + 1 B ready flag (.bss).
MH_NOINLINE inline const int8_t *sin65Ram() {
    static int8_t buf[65];
    static bool ready = false;
    if (!ready) {
        mhFxReadBytes(reinterpret_cast<const void *>(static_cast<uintptr_t>(MH_FX_SIN65_ADDR)), reinterpret_cast<uint8_t *>(buf), 65);
        ready = true;
    }
    return buf;
}
#endif

static inline int16_t sin256(uint8_t a) {
    const uint8_t q = static_cast<uint8_t>(a >> 6);
    const uint8_t k = static_cast<uint8_t>(a & 63);
    const uint8_t i = (q & 1) ? static_cast<uint8_t>(64 - k) : k;
#if defined(__AVR__)
    const int16_t v = sin65Ram()[i];
#else
    const int16_t v = mhPgmReadI8(&SIN65[i]);
#endif
    return (q & 2) ? static_cast<int16_t>(-v) : v;
}
static inline int16_t cos256(uint8_t a) {
    return sin256(static_cast<uint8_t>(a + 64));
}

}   // namespace mh
