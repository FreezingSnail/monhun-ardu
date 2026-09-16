#pragma once
// Portable PROGMEM shim. On AVR, read-only core tables must live in MCU flash
// (program memory): a plain `const`/`constexpr` object lands in .rodata, which
// avr-gcc places in SRAM (copied from flash at startup) and the game has no
// room for it. On the host build (make test) everything is identity: the macro
// disappears and the readers are plain loads, so the same core source compiles
// and the exact same values are read.
//
// Hot paths read single fields through the typed helpers below (no whole-struct
// copies per tick). Table-specific accessors live next to their tables
// (fp.hpp for DIR8, game.hpp for WEAPON_DEFS / MONSTER_ATTACKS).
//
// No float, no logic. Integers only.

#include <stdint.h>

#if defined(__AVR__)
#include <avr/pgmspace.h>
#define MH_PROGMEM PROGMEM
#else
#define MH_PROGMEM
#endif

// Typed reads from a table address. On AVR each does an LPM (flash) load; on
// the host each is a normal pointer dereference. Addresses are always formed
// with `&table[i].field`, so struct layout is whatever the target compiler
// chose and no manual offset math is needed.
#if defined(__AVR__)
inline uint8_t mhPgmReadU8(const uint8_t *p) {
    return pgm_read_byte(p);
}
inline int8_t mhPgmReadI8(const int8_t *p) {
    return static_cast<int8_t>(pgm_read_byte(reinterpret_cast<const uint8_t *>(p)));
}
inline uint16_t mhPgmReadU16(const uint16_t *p) {
    return pgm_read_word(p);
}
inline int16_t mhPgmReadI16(const int16_t *p) {
    return static_cast<int16_t>(pgm_read_word(reinterpret_cast<const uint16_t *>(p)));
}
inline uint32_t mhPgmReadU32(const uint32_t *p) {
    return pgm_read_dword(p);
}
inline int32_t mhPgmReadI32(const int32_t *p) {
    return static_cast<int32_t>(pgm_read_dword(reinterpret_cast<const uint32_t *>(p)));
}
inline bool mhPgmReadBool(const bool *p) {
    return pgm_read_byte(reinterpret_cast<const uint8_t *>(p)) != 0;
}
#else
inline uint8_t mhPgmReadU8(const uint8_t *p) {
    return *p;
}
inline int8_t mhPgmReadI8(const int8_t *p) {
    return *p;
}
inline uint16_t mhPgmReadU16(const uint16_t *p) {
    return *p;
}
inline int16_t mhPgmReadI16(const int16_t *p) {
    return *p;
}
inline uint32_t mhPgmReadU32(const uint32_t *p) {
    return *p;
}
inline int32_t mhPgmReadI32(const int32_t *p) {
    return *p;
}
inline bool mhPgmReadBool(const bool *p) {
    return *p;
}
#endif
