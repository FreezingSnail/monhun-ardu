# monhun-ardu-kt7.1 — Core: move constant tables to PROGMEM (RAM 97% used)

## Files
- added: `src/core/progmem.hpp` — portable shim:
  - AVR: `#include <avr/pgmspace.h>`, `MH_PROGMEM` = `PROGMEM`, typed
    `mhPgmReadU8/I8/U16/I16/U32/I32/Bool` readers (`pgm_read_byte/word/dword`).
  - Host: `MH_PROGMEM` is empty, readers are plain dereferences — identical
    values, no AVR headers.
- changed: `src/core/fp.hpp` — `fp::DIR8[8]` is `MH_PROGMEM`; added per-field
  `fp::dir8X(i)` / `fp::dir8Y(i)` (host `DIR8[i].x/.y` reads in fp_test.hpp are
  untouched).
- changed: `src/core/game.hpp` — `WEAPON_DEFS[3]` and `MONSTER_ATTACKS[2]` are
  `MH_PROGMEM`; added flash accessors: `weaponId/Spd/CanCancel/Attack/Special/
  Branch/Shell`, `attackStartup/Active/Recover/Dmg/Reach/Hw/Hh/Stam/Lunge/Push/
  Effect/Shell/Id`, `branchStage/Stance/AutoT/Atk`, `shellCount/Dmg/SpeedF/W/H/
  Reload/Stam/Pellets`, `monsterAttackKind/Windup/Active/Recover/SpeedF/Dmg/
  Reach/Hw/Hh`.
- changed: `src/core/player.hpp`, `src/core/monster.hpp`,
  `src/core/projectiles.hpp` — every table field read now goes through an
  accessor; `const WeaponDef&` / `const Attack&` / `const ShellDef&` /
  `const MonsterAttack&` bindings to flash entries were replaced by pointers so
  no reference is bound to a program-memory object. FSM/fields touched only,
  no per-tick whole-struct copies.
- changed: `output.md` (this file).
- `mock/` and `tst/` untouched; no gameplay numbers or logic changed.

## RAM / flash before -> after (`rm -rf build && make build`)
Before (commit 0d1a80a):
```
Sketch uses 17264 bytes (58%) of program storage space. Maximum is 29696 bytes.
Global variables use 2494 bytes (97%) of dynamic memory, leaving 66 bytes for local variables. Maximum is 2560 bytes.
```
After:
```
Sketch uses 17202 bytes (57%) of program storage space. Maximum is 29696 bytes.
Global variables use 1888 bytes (73%) of dynamic memory, leaving 672 bytes for local variables. Maximum is 2560 bytes.
```
- Flash: 17264 -> 17202 (fits, < 29696).
- Global RAM: 2494 -> 1888; free 66 -> **672 B** (target >= 300 B met).
- Reclaimed: 606 B = WEAPON_DEFS 540 + MONSTER_ATTACKS 34 + fp::DIR8 32.

`avr-nm` confirms the tables left SRAM (now `t` / flash symbols):
```
000000c5 00000022 t _ZN2mhL15MONSTER_ATTACKSE
000000e7 00000020 t _ZN2fpL4DIR8E
00000107 0000021c t _ZN2mhL11WEAPON_DEFSE
0080013e 00000400 b _ZN12Arduboy2Base7sBufferE
008005a8 000002b8 b g
```

## `make test` (host, C++17) — unchanged 497 asserts
```
Total Passed: 497
Total Failed: 0
```

## `make fxtest-headless` (Ardens, device serial)
```
test_boot
Sketch uses 11462 bytes (38%) of program storage space. Maximum is 29696 bytes.
Global variables use 1179 bytes (46%) of dynamic memory, leaving 1381 bytes for local variables. Maximum is 2560 bytes.
=== test_boot ===
test_boot PASSED=4 FAILED=0
P
test_boot: PASS
```

## Notes for the render bead (monhun-ardu-rze)
- MCU flash headroom is now 29696 - 17202 = 12494 B; SRAM free is 672 B.
- `WEAPON_DEFS` / `MONSTER_ATTACKS` / `fp::DIR8` are in **MCU flash** (not the
  FX cart). Render reading `Game` state is unchanged (game state still in RAM).
- If render needs table data (weapon HUD icon, facing vector, shell size), read
  it through the accessors in `game.hpp` / `fp.hpp` (`dir8X/dir8Y`,
  `weapon*`, `attack*`, `shell*`, `monsterAttack*`). Do NOT bind a reference to
  a table entry and do NOT copy a whole struct per plane.
- Any new core-level read-only table must be declared `MH_PROGMEM` with matching
  accessors, or it silently eats the reclaimed 606 B again.
