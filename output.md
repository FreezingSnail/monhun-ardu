# monhun-ardu-42n.1 — Data: move weapon/monster tables to FX cart (fxmem fake-pointer shim)

Status: **done, with one acceptance miss reported below** (all functional gates green:
gen deterministic, avr-nm clean, host tests 497/0, device tests all PASS). Flash gate
`<=27100 B` was missed at **27282 B** (398 B freed vs 27680 B baseline, plan expected
>=580 B freed). Details and analysis in "Flash result vs gate".

- Repo: `/Users/connorfranc/monhun-ardu`
- HEAD before/after: `7762ac7441b4dd5cad858f08fbf8adffa6a84784` (unchanged; no commits made)
- Date: 2026-09-16
- Working tree: left dirty intentionally (orchestrator owns commit)

## What was implemented (per bead DESIGN)

1. **`tools/gen-fxtables.cpp`** (new): host g++ generator. Includes `src/core/game.hpp`,
   walks `WEAPON_DEFS[3]` and `MONSTER_ATTACKS[2]` field-by-field in declaration order,
   writes little-endian packed AVR-layout bytes to `fxdata/tables/weapondefs.bin`
   (540 B) and `fxdata/tables/monsterattacks.bin` (34 B). Asserts per-struct sizes:
   WeaponDef 180, Attack 23, Branch 27, ShellDef 15, MonsterAttack 17, plus totals.
   Never memcpy's host structs.
2. **`fxdata/fxdata.txt`**: added `raw_t mhWeaponDefs = "tables/weapondefs.bin"` and
   `raw_t mhMonsterAttacks = "tables/monsterattacks.bin"`. `fxdata-build.py` emits
   `constexpr uint24_t mhWeaponDefs = 0x0030B2;` / `mhMonsterAttacks = 0x0032CE;`
   into `src/fxdata.h` (blob << 64 KB). `.gitignore` un-ignores `fxdata/tables/*.bin`
   (global `*.bin` rule); bins are meant to be force-added/committed like the other
   FX bins.
3. **`src/core/fxmem.hpp`** (new): typed readers `mhFxReadU8/I8/U16/I16/Bool`.
   Host = plain dereference (identity). AVR = `#include <ArduboyFX.h>` + `../fxdata.h`,
   `FX::seekData(address)` + pending byte reads (`readPendingUInt8`/`readEnd`) in
   address order. The readers are marked `__attribute__((pure))`: each call seeks its
   own address first and cart contents are immutable, so repeated reads are shareable;
   this also removed re-issued SPI transactions. (Measured: pure saves 148 B flash.)
4. **`src/core/game.hpp`**: on AVR `WEAPON_DEFS` / `MONSTER_ATTACKS` are fake-pointer
   shims (`FxWeaponDefsRom` / `FxMonsterAttacksRom` with `const T &operator[](int16_t)`)
   over the fxdata.h blob offsets; host keeps the original PROGMEM arrays. Only the
   WeaponDef/Attack/Branch/ShellDef/MonsterAttack accessors switched from `mhPgmRead*`
   to `mhFxRead*`; accessor signatures and every call site (`&WEAPON_DEFS[i]`,
   `&MONSTER_ATTACKS[kind]`) unchanged. Static asserts guard `sizeof` against the blob.
5. **`tst/fxdatatest/data_test.hpp` + `test_data.ino`** (new): device suite reading back
   through the shipping accessors: 194 checks over sizes/strides/field offsets
   (180/23/27/15/17, offsets via pointer differences on the fake cart pointers) and the
   mock values for all 3 weapons and both monster attacks. Runs 194/0 on Ardens.
6. `tools/gen.sh`: builds + runs `gen-fxtables` before `fxdata-build.py`; single entry
   `make gen`, deterministic.

Hot LUTs (DIR8/SIN256/RING6/MASK) untouched; no core math/fixture changes; parity hash
fields unchanged (660/0).

## Files changed/added

```
 M .gitignore                       (+ !fxdata/tables/*.bin exceptions)
 M fxdata/fxdata-data.bin           (12466 -> 13040 B)
 M fxdata/fxdata.bin                (12544 -> 13056 B)
 M fxdata/fxdata.h                  (+ mhWeaponDefs / mhMonsterAttacks)
 M fxdata/fxdata.txt                (+ 2 raw_t lines)
 M src/core/game.hpp                (AVR fake-pointer shim + mhFxRead* accessors)
 M src/fxdata.h                     (generated copy)
 M tools/gen.sh                     (+ gen-fxtables step)
?? fxdata/tables/weapondefs.bin     (540 B, new)
?? fxdata/tables/monsterattacks.bin (34 B, new)
?? src/core/fxmem.hpp               (new)
?? tools/gen-fxtables.cpp           (new)
?? tst/fxdatatest/data_test.hpp     (new)
?? tst/fxdatatest/test_data.ino     (new)
```

## Verification evidence

### 1. `make gen` determinism (run twice, byte-compare)

Run 1 log tail:

```
Saving FX data header file /Users/connorfranc/monhun-ardu/fxdata/fxdata.h
Saving 13040 bytes FX data to /Users/connorfranc/monhun-ardu/fxdata/fxdata-data.bin
Saving FX development data to /Users/connorfranc/monhun-ardu/fxdata/fxdata.bin
gen.sh: FX data + src/fxdata.h regenerated
```

After run 1, snapshots taken; run 2 then compared byte-for-byte:

```
$ cmp fxdata/tables/weapondefs.bin wd1.bin && cmp fxdata/tables/monsterattacks.bin ma1.bin \
  && cmp fxdata/fxdata.bin fdb1.bin && cmp fxdata/fxdata-data.bin fdd1.bin && cmp src/fxdata.h fxdata1.h
(no output: all identical)
$ diff status_after1.txt status_after2.txt   # git status --porcelain before/after run 2
(no output)
GEN_DETERMINISTIC
```

md5 after both runs (stable):

```
e6d0107b24c100deb8360d5446187f6f  fxdata/tables/weapondefs.bin
8d33ecca5afd8f79310ebd80f44de9a4  fxdata/tables/monsterattacks.bin
2aa6201b11a7abb66887f66c5cf8f3bf  fxdata/fxdata.bin
716ded4cd984bf5e63e163e790af1f9c  fxdata/fxdata-data.bin
06ca2b82319cf27d773f3543d637d480  src/fxdata.h
```

### 2. `make build` — flash/RAM before/after

Baseline HEAD 7762ac7 (reproduced): flash **27680 B (93%)**, RAM **1941 B (75%)**.
After:

```
arduino-cli compile --fqbn "arduboy-homemade:avr:arduboy-fx" --optimize-for-debug  --output-dir dist
Sketch uses 27282 bytes (91%) of program storage space. Maximum is 29696 bytes.
Global variables use 1941 bytes (75%) of dynamic memory, leaving 619 bytes for local variables. Maximum is 2560 bytes.
```

`avr-size -A dist/monhun-ardu.ino.elf` after: `.text 27236`, `.data 46`, `.bss 1895`
(baseline `.text 27634`). Flash delta: **-398 B**; RAM unchanged.

### 3. avr-nm — no table arrays

```
$ avr-nm -C --size-sort dist/monhun-ardu.ino.elf | grep -E "mh::(WEAPON_DEFS|MONSTER_ATTACKS)"
(none; grep exit=1)
```

### 4. `make test`

```
Total Passed: 497
Total Failed: 0
```

### 5. `make fxtest-headless` (Ardens present, exit 0)

```
=== test_assets ===
asset_test PASSED=30 FAILED=0
P
test_assets: PASS
=== test_audio ===
test_audio PASSED=14 FAILED=0
P
test_audio: PASS
=== test_boot ===
test_boot PASSED=4 FAILED=0
P
test_boot: PASS
=== test_data ===
data_test PASSED=194 FAILED=0
P
test_data: PASS
=== test_parity ===
parity_test PASSED=660 FAILED=0
P
test_parity: PASS
=== test_perf ===
B pUs=6379 pHz=156 lHz=52 lTk=988 rMx=3996 rAv=3868 ram=489
perf_test PASSED=5 FAILED=0
P
test_perf: PASS
```

Perf gate numbers recorded: plane 156 Hz (floor 135), logic 52 Hz (floor 45),
render max 3996 us (budget 7407), free RAM 489 B (floor 300) — all gates pass.
test_perf sketch flash 29232/29696 B (baseline 29646, so it now fits with more headroom).

## Flash result vs gate — the one miss

Bead acceptance: `make build` flash `<=27100 B` (>=580 B freed from 27680 B).
Actual: **27282 B** (398 B freed). Miss by 182 B.

Why: the tables are 574 B total (540+34, confirmed by avr-nm). The FX read path costs
+176 B of code vs the old inline-LPM accessors (`.text` delta: -574 tables, +176 code,
see per-function: `main` +84 B, `drawPlayer` +62 B, outlined `mhFxReadU8/U16` bodies
+40 B, small wrappers +24 B). That cost is AVR-ABI call overhead per field read
(address marshalling + caller register spills around the FX library calls); 60 read
call sites exist in the shipping image. Measured variants (all faithful to the design's
per-field reader contract):

| variant                                                    | flash  |
|------------------------------------------------------------|--------|
| `readDataBytes` readers, inline (first working build)      | 27498  |
| `readDataBytes` readers, `noinline`                        | 27530  |
| `seekData`+pending-byte readers, inline                    | 27430  |
| + `__attribute__((pure))` (shipped)                        | **27282** |
| `readDataBytes` readers + pure                             | 27350  |
| `readPendingLastUInt16` + bswap + pure                     | 27284  |
| `pure, noinline`                                           | 27316  |

The plain-dereference floor (fake pointers without any cart traffic) measures 26888 B,
i.e. the cart read machinery alone costs 394 B; the design's per-field read granularity
cannot meet the 27100 B plan figure. Closing the remaining 182 B would need a row-level
read granularity (cache whole Attack/Branch/Shell/MonsterAttack rows into SRAM), which
is outside this bead's design; the epic's C6 gate (<=26600 after the C3 art translation
deletes procedural drawing code) will absorb it. Flagging for the orchestrator rather
than faking the number.

## Notes / open items

- No commit made; tree left dirty as instructed.
- `fxdata/tables/*.bin` need `git add -f` (or the new `!fxdata/tables/*.bin` rule) to be
  tracked; they are required for `make gen` determinism and for the FX image.
- The fake-pointer shim relies on the AVR packed struct layout; `static_assert`s in
  `game.hpp` (AVR) pin 180/23/27/15/17 and `data_test` re-checks strides/offsets on
  device (194/0).
- `src/fxdata.h` and `fxdata/fxdata.h` are generated; do not hand-edit.
