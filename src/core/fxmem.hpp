#pragma once
// FX-cart table memory (bead monhun-ardu-42n.1).
//
// On AVR the weapon/monster content tables no longer live in MCU flash: they
// are packed into the FX data image (fxdata/tables/*.bin via fxdata.txt) and
// addressed by the uint24_t offsets fxdata-build.py emits into fxdata.h.
// game.hpp hands the accessors fake 16-bit pointers into that address space;
// mhFxRead* copies the field bytes from the cart and is the only place the
// cart is touched. On the host the same calls are plain dereferences
// (identity), so the core reads the host arrays and the accessors need no
// target ifdefs.
//
// The table blob sits far below 64 KB, so a fake cart address fits in an AVR
// data pointer.

#include <stdint.h>

#if defined(__AVR__)

#include <ArduboyFX.h>
#include "../fxdata.h"

// Optional per-access counter for the device loader test (bead ljj.2):
// compile a test TU with -DMH_FX_READ_COUNT (or #define before the first
// include) and define `uint16_t mh::mhFxReadCount` there; every mhFxReadU8 /
// mhFxReadU16 issues exactly one increment, so one typed field read counts as
// one cart access. Shipping builds leave the macro undefined: zero cost.
#if defined(MH_FX_READ_COUNT)
#define MH_FX_COUNT_READ() (++::mh::mhFxReadCount)
#else
#define MH_FX_COUNT_READ() ((void)0)
#endif

namespace mh {

#if defined(MH_FX_READ_COUNT)
extern uint16_t mhFxReadCount;
#endif

// Blob base offsets (fxdata-build.py labels); blob stays < 64 KB.
constexpr uint16_t MH_FX_WEAPON_DEFS_ADDR = static_cast<uint16_t>(mhWeaponDefs);
constexpr uint16_t MH_FX_MONSTER_ATTACKS_ADDR = static_cast<uint16_t>(mhMonsterAttacks);
constexpr uint16_t MH_FX_MONSTER_DEFS_ADDR = static_cast<uint16_t>(mhMonsterDefs);

// Typed field readers. The address is a fake pointer into the FX image; the
// value is fetched as little-endian bytes, matching the packed AVR struct
// layout that tools/gen-fxtables.cpp serializes. seekData starts a read at the
// field address; the pending byte reads then walk it in address order. This is
// leaner at the call sites than readDataBytes(address, buffer, length): no
// stack buffer pointer and no length argument to marshal.
//
// `pure` is sound here: every reader seeks to its own address first and the
// cart contents never change, so repeated calls with the same pointer return
// the same bytes. It lets the optimizer share reads and avoid re-issuing the
// same SPI transaction. With MH_FX_READ_COUNT the readers bump a counter, so
// they are no longer side-effect free and must not carry the attribute (GCC
// would otherwise reuse stale counter loads across calls).
#if defined(MH_FX_READ_COUNT)
#define MH_FX_PURE
#else
#define MH_FX_PURE __attribute__((pure))
#endif
MH_FX_PURE inline uint8_t mhFxReadU8(const uint8_t *p) {
    MH_FX_COUNT_READ();
    FX::seekData(static_cast<uint24_t>(reinterpret_cast<uintptr_t>(p)));
    return FX::readEnd();
}
MH_FX_PURE inline int8_t mhFxReadI8(const int8_t *p) {
    return static_cast<int8_t>(mhFxReadU8(reinterpret_cast<const uint8_t *>(p)));
}
MH_FX_PURE inline uint16_t mhFxReadU16(const uint16_t *p) {
    MH_FX_COUNT_READ();
    FX::seekData(static_cast<uint24_t>(reinterpret_cast<uintptr_t>(p)));
    const uint8_t lo = FX::readPendingUInt8();
    const uint8_t hi = FX::readEnd();
    return static_cast<uint16_t>(static_cast<uint16_t>(lo) | (static_cast<uint16_t>(hi) << 8));
}
MH_FX_PURE inline int16_t mhFxReadI16(const int16_t *p) {
    return static_cast<int16_t>(mhFxReadU16(reinterpret_cast<const uint16_t *>(p)));
}
MH_FX_PURE inline bool mhFxReadBool(const bool *p) {
    return mhFxReadU8(reinterpret_cast<const uint8_t *>(p)) != 0;
}
#undef MH_FX_PURE

// Bulk per-record fetch (combat loader decision reads, migration C): one seek
// transaction, n byte reads. The typed readers above stay the default for
// hot-path scalars; this exists for small fixed-size records whose RAM cache
// mirror is byte-identical to the packed blob record (one cart access, not n).
inline void mhFxReadBytes(const void *p, uint8_t *dst, uint16_t n) {
    MH_FX_COUNT_READ();
    FX::readDataBytes(static_cast<uint24_t>(reinterpret_cast<uintptr_t>(p)), dst, n);
}

}   // namespace mh

#else

namespace mh {

inline uint8_t mhFxReadU8(const uint8_t *p) {
    return *p;
}
inline int8_t mhFxReadI8(const int8_t *p) {
    return *p;
}
inline uint16_t mhFxReadU16(const uint16_t *p) {
    return *p;
}
inline int16_t mhFxReadI16(const int16_t *p) {
    return *p;
}
inline bool mhFxReadBool(const bool *p) {
    return *p;
}

inline void mhFxReadBytes(const void *p, uint8_t *dst, uint16_t n) {
    const uint8_t *src = static_cast<const uint8_t *>(p);
    for (uint16_t i = 0; i < n; i++)
        dst[i] = src[i];
}

}   // namespace mh

#endif
