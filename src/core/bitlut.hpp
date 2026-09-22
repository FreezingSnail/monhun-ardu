#pragma once
// Index -> one-hot bit lookups. The AVR core has no barrel shifter, so
// `1u << n` compiles to a shift-by-one loop (~10x a table read, GCC will not
// memoize it; docs/avr-arduboy-techniques.md §3). This replaces the runtime
// shifts on the save flags/quest bitset and the audio firedMask with a flash
// LUT read.
//
// The table is a function-local static inside an `inline` accessor: C++ gives
// a function-local static in an inline function a single, linker-merged object,
// so the 8 bytes live in MCU flash exactly once no matter how many translation
// units include this header. Reads go through the progmem shim, so the host
// suites use the same code path with plain loads.
//
// `n` is masked to the table width: callers only ever pass an in-range index,
// so masking cannot change behaviour, it only keeps the access in bounds.

#include <stdint.h>
#include "progmem.hpp"

namespace mh {

// 1u << n for n in 0..7.
inline uint8_t mhBit8(uint8_t n) {
    static const uint8_t MH_PROGMEM MH_BIT8_TBL[8] = {
        0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x20u, 0x40u, 0x80u,
    };
    return mhPgmReadU8(&MH_BIT8_TBL[n & 7]);
}

}   // namespace mh
