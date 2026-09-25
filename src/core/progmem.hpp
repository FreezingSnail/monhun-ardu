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

// Flash-size shim: force one out-of-line copy of a small helper.
//
// LTO inlines the whole sim into a handful of AVR symbols, and a helper that is
// inlined N times costs N copies of its body. That is the right trade for
// 2-instruction accessors but not for the ones marked here: each was measured
// with `make size` and only kept the attribute when the whole image shrank.
// Cold one-shots (newGame/initGame duplicated per call site) were the largest
// single win at -546 B; the small warm helpers (tdiv, dir8X/Y, mulQ4, blk)
// traded ~30 us/logic tick for -378 B, which the perf gate absorbs.
//
// Identity on the host build so `make test` is unaffected. Never apply this
// blind: an unmeasured noinline usually grows the image (the call sequence plus
// the lost constant propagation can exceed the body it saved).
#if defined(__AVR__)
#define MH_NOINLINE __attribute__((noinline))
#else
#define MH_NOINLINE
#endif

// Typed reads from a table address. On AVR each does an LPM (flash) load; on
// the host each is a normal pointer dereference. Addresses are always formed
// with `&table[i].field`, so struct layout is whatever the target compiler
// chose and no manual offset math is needed.
//
// These are macros, not inline functions: the typed wrappers were pure
// indirection around a single pgm_read_*, and expanding them at the call site
// lets the compiler fold the address arithmetic (measured -44 B whole-image vs
// the function form). The argument is consumed exactly once and call sites pass
// a plain `&table[i].field` lvalue, so no multiple-evaluation hazard applies.
#if defined(__AVR__)
#define mhPgmReadU8(p) (pgm_read_byte(p))
#define mhPgmReadI8(p) (static_cast<int8_t>(pgm_read_byte(reinterpret_cast<const uint8_t *>(p))))
#define mhPgmReadU16(p) (pgm_read_word(p))
#define mhPgmReadI16(p) (static_cast<int16_t>(pgm_read_word(reinterpret_cast<const uint16_t *>(p))))
#define mhPgmReadU32(p) (pgm_read_dword(p))
#else
#define mhPgmReadU8(p) (*(p))
#define mhPgmReadI8(p) (*(p))
#define mhPgmReadU16(p) (*(p))
#define mhPgmReadI16(p) (*(p))
#define mhPgmReadU32(p) (*(p))
#endif
