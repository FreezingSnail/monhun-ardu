# monhun-ardu-hbk.1 — dev feel mode (`make dev`, `-DMH_DEV=1`)

Status: **DONE.** All gates green. Shipping image unchanged (flash delta 0);
dev build compiles and reports its own size line.

## What changed

- **new `src/core/dev.hpp`** — `#ifndef MH_DEV / #define MH_DEV 0 / #endif` +
  `constexpr bool mh::DEV_UNLIMITED = MH_DEV != 0` (constant-folded; shipping
  delta 0).
- **`src/core/save.hpp`** — `#include "dev.hpp"`; header comment gains the hbk.1
  note. `saveDefaults()`: `s.zenny = DEV_UNLIMITED ? 9999 : 0;` and
  `s.items[i] = DEV_UNLIMITED ? 99 : 0;`. `saveLoad()`: `DEV_UNLIMITED` ->
  `saveDefaults(s); return false;` before any EEPROM read. `saveStore()`:
  `if (DEV_UNLIMITED) return true;` first line (never writes).
- **`src/forge_state.hpp`** — `billShort()` returns `BILL_OK` when
  `DEV_UNLIMITED`; `billDebit()` returns early. `save.hpp` now pulls dev.hpp,
  so no extra include.
- **`Makefile`** — `.PHONY` gains `dev`; new `dev` target: shipping FQBN + flags
  plus `-DMH_DEV=1` into `dist/`, then prints the `dev size:` line inline (no
  `size-line` dependency, so no shipping recompile).
- **`README.md`** — `make dev` added to the Commands block; device-layer note
  describing unlimited crafting + fresh sandbox defaults + never-touched EEPROM.

No EEPROM writes in the dev build: the only write path (`saveStore`) and the
read path (`saveLoad`) both short-circuit on `DEV_UNLIMITED` before touching the
backend; defaults fill RAM only.

## Command tails

```
$ make test
Total Passed: 6316
Total Failed: 0

$ make test-tools
Ran 357 tests in 21.529s
OK

$ make size-line
Sketch uses 29506 bytes (99%) of program storage space. Maximum is 29696 bytes.
Global variables use 1798 bytes (70%) of dynamic memory, leaving 762 bytes for local variables.
size: flash=29506/29696 (190 free)  ram=1798/2560

$ make dev
Sketch uses 28808 bytes (97%) of program storage space. Maximum is 29696 bytes.
Global variables use 1798 bytes (70%) of dynamic memory, leaving 762 bytes for local variables.
dev size: flash=28808/29696 (888 free)  ram=1798/2560
```

## Size lines

```
shipping (make size-line): flash=29506/29696 (190 free)  ram=1798/2560   <- unchanged vs HEAD
dev      (make dev):       flash=28808/29696 (888 free)  ram=1798/2560
```

Shipping delta: **0 B** (expected — every `DEV_UNLIMITED` use constant-folds to
0). Dev is 698 B smaller because the stripped save load/store paths drop out.

Note: `make dev` writes the dev image into `dist/`; a follow-up `make size-line`
(recompiled shipping) restores it — the shipping number above is from that run.

## Wall time (approx)

| step | time |
|---|---|
| read task/design + save.hpp/forge_state.hpp/Makefile/README | ~10 min |
| implement (dev.hpp, save, forge, Makefile, README) | ~8 min |
| gates (test, size-line, dev, test-tools) | ~5 min |
| report | ~3 min |
