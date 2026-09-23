#pragma once
// Dev-only feel harness (bead monhun-ardu-hbk.1, docs/ui-design.md "Dev feel
// mode"): -DMH_DEV=1 turns the build into an unlimited-crafting sandbox for
// menu feel testing -- fresh defaults with 9999 zenny + 99 of every item,
// every bill gate passes, no debit, and the EEPROM save is never read or
// written (the player's real save is untouched). Default 0; every use is
// constant-folded so the shipping image is unchanged.
#ifndef MH_DEV
#define MH_DEV 0
#endif

namespace mh {

constexpr bool DEV_UNLIMITED = MH_DEV != 0;

}   // namespace mh
