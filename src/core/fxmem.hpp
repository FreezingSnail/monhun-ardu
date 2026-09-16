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

namespace mh {

// Blob base offsets (fxdata-build.py labels); blob stays < 64 KB.
constexpr uint16_t MH_FX_WEAPON_DEFS_ADDR = static_cast<uint16_t>(mhWeaponDefs);
constexpr uint16_t MH_FX_MONSTER_ATTACKS_ADDR = static_cast<uint16_t>(mhMonsterAttacks);

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
// same SPI transaction.
#define MH_FX_PURE __attribute__((pure))
MH_FX_PURE inline uint8_t mhFxReadU8(const uint8_t *p) {
    FX::seekData(static_cast<uint24_t>(reinterpret_cast<uintptr_t>(p)));
    return FX::readEnd();
}
MH_FX_PURE inline int8_t mhFxReadI8(const int8_t *p) {
    return static_cast<int8_t>(mhFxReadU8(reinterpret_cast<const uint8_t *>(p)));
}
MH_FX_PURE inline uint16_t mhFxReadU16(const uint16_t *p) {
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

}   // namespace mh

#endif
