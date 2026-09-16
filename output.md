# monhun-ardu-6zb.1 — Core+mock: demo monster variants (`MONSTER_DEFS` on FX, `initMonster(kind)`)

Status: **DONE** — all acceptance checks green, tree left dirty (no commit/push).
HEAD at start/finish: `1e27a38` (wave 42n closed, clean tree). Working tree now
contains only this bead's changes.

Correction applied on orchestrator review: HEAVY `atkDist` 40 → **24**. The
split rule is `dist > atkDist ? lunge : sweep` (negative = never lunge), so a
lower threshold *widens* the lunge band: HEAVY lunges 25..41 px (inside the
unchanged `dist < 42` engage gate) and sweeps at 24 and below — "mostly lunge".

## What changed

Roster (mock = source of truth, core mirrors it):

| kind | variant | w | h | hp | spd | atkDist | chooseAttack |
|---|---|---|---|---|---|---|---|
| 0 | LUNGE (legacy/parity default) | 32 | 24 | 200 | 5 | 32 | lunge iff `dist > 32` |
| 1 | SWEEP | 28 | 22 | 150 | 7 | -1 | never lunges (always sweep) |
| 2 | HEAVY | 40 | 28 | 320 | 3 | 24 | lunge iff `dist > 24` → band 25..41 under the `dist < 42` gate |

- `src/core/game.hpp`: `MonsterDef { int8_t kind; int16_t w, h, hp, spd, atkDist; }`
  (AVR `static_assert(sizeof == 11)`), `MONSTER_DEFS[3]` host array + FX
  fake-pointer shim (`FxMonsterDefsRom`), six `monsterDef*` accessors via
  `mhFxRead*`. `Game` gains `int8_t monsterKind`.
- `src/core/fxmem.hpp`: `MH_FX_MONSTER_DEFS_ADDR` (reads `mhMonsterDefs` label).
- `tools/gen-fxtables.cpp`: field-by-field serializer `putMonsterDef` (asserted
  11 B), writes `fxdata/tables/monsterdefs.bin` (33 B total, asserted). No value
  assertions in the tool — it serializes whatever the table holds.
- `fxdata/fxdata.txt`: `raw_t mhMonsterDefs = "tables/monsterdefs.bin"` appended
  after the existing tables (all prior cart addresses unchanged).
- `src/core/monster.hpp`: `initMonster(g, kind = 0)` reads w/h/hp/spd from the
  def and keeps x=200, y=40, t=90, cd=140, face W, circleDir 1; out-of-range
  kinds clamp to 0. `chooseAttack(g, dist)` threshold = `monsterDefAtkDist(def)`
  (negative ⇒ sweep).
- `src/core/world.hpp`: `newGame(g, weapon, mode, monsterKind = 0)`;
  `withWeapon`/`resetHunt` preserve `g.monsterKind`.
- `mock/game.js`: `MONSTER_DEFS` object + `initMonster(g, kind)`; `newGame(weapon,
  mode, monsterIndex = 0)` returns a game with `monsterIndex`; `chooseAttack(g,
  dist)` reads the def; `withWeapon`/`resetHunt`/boot K-key preserve the index;
  exports `MONSTER_DEFS`.
- `tools/fxdump.cpp`: monster w/h read from `MONSTER_DEFS[MON_LUNGE]` (same 32/24
  numbers — no duplicated literals; art output unchanged).
- Tests: `mock/game.test.js` +4 tests (HEAVY band 25..41, sweep at 0/10/24,
  boundary 24/25); `tst/monster_test.hpp` +5 test blocks (roster values, per-kind
  spawn, threshold variants incl. 41/30/25 lunge and 24 sweep, kind
  preservation); `tst/fxdatatest/data_test.hpp` +MonsterDef
  size/stride/offset/value checks.

Default kind 0 is byte-for-byte today's spawn and `chooseAttack` split.

## 1. `node --test mock/game.test.js` — green

```
✔ monster variants: roster + spawn stats per def, default is legacy LUNGE (…)
✔ monster variants: SWEEP never lunges, HEAVY lunges past 24 (…)
✔ monster variants: weapon swap and reset keep the chosen beast (…)
✔ monster variants: train mode with HEAVY keeps the pole path intact (…)
ℹ tests 24
ℹ suites 0
ℹ pass 24
ℹ fail 0
ℹ cancelled 0
ℹ skipped 0
ℹ todo 0
ℹ duration_ms 88.338208
```

## 2. Parity fixtures byte-identical

```
$ node tools/gen-parity-fixtures.js && git diff --stat tst/fxdatatest/parity_fixtures.hpp
wrote tst/fxdatatest/parity_fixtures.hpp
scenes=20 ticks=1269 snapshots=32 cpFields=20
parity_diff_lines=0
```

Empty diff — fixture file untouched, so nothing was committed/changed there.

## 3. `make gen` deterministic + `make gen-check` PASS

Artifact md5s identical across two consecutive `make gen` runs after the
correction:

```
fxdata/tables/monsterdefs.bin   d2676200bb15844dbacd0820d97c98a8  (×2)
fxdata/fxdata.bin               645cb6cb33ecb5223eb5b5e9f1e5da76  (×2)
src/fxdata.h                    bbdce9da67c7c6fc253c37bb6b9eaaca  (×2)
```

```
gen-fxtables: fxdata/tables/weapondefs.bin (540 B), fxdata/tables/monsterattacks.bin (34 B), fxdata/tables/monsterdefs.bin (33 B)
fxdata_manifest: fxdata/manifest.json up to date (19 images, 6 inputs, 5 outputs)
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (30 generated artifacts unchanged)
```

Blob bytes (`fxdata/tables/monsterdefs.bin`, packed AVR order
`kind,w,h,hp,spd,atkDist`):

```
00000000: 0020 0018 00c8 0005 0020 0001 1c00 1600  . ....... ......
00000010: 9600 0700 ffff 0228 001c 0040 0103 0018  .......(...@....
00000020: 00
```

LUNGE 00 (kind) 0020=32 0018=24 00c8=200 0005=5 0020=32;
SWEEP 01 001c=28 0016=22 0096=150 0007=7 ffff=-1;
HEAVY 02 0028=40 001c=28 0140=320 0003=3 **0018=24**.

## 4. `make test` — host 1337/0

```
========== Total Counts ==========
Total Passed: 1337
Total Failed: 0
```

Baseline 1281/0 → +56 checks. New monster coverage: roster table values,
`initMonster` per kind (incl. clamped/legacy default), chooseAttack variants
(HEAVY lunge at 41/30/25, sweep at 24; SWEEP never lunges; LUNGE legacy 33/32
split), withWeapon/resetHunt preservation. No failures.

## 5. `make build` — flash/RAM delta

```
Sketch uses 26512 bytes (89%) of program storage space. Maximum is 29696 bytes.
Global variables use 1942 bytes (75%) of dynamic memory, leaving 618 bytes for local variables. Maximum is 2560 bytes.
```

`avr-size -A dist/monhun-ardu.ino.elf`: `.text 26466 + .data 46 = 26512` flash;
`.data 46 + .bss 1896 = 1942` RAM.

| metric | baseline (42n.6) | now | delta | budget |
|---|---|---|---|---|
| flash | 26444 B | **26512 B** | **+68 B** | 29696 (3184 free) |
| RAM | 1941 B | **1942 B** | **+1 B** | 2560 (618 free) |

The 33 B MonsterDef blob itself lives on the FX cart, not MCU flash: the +68 B
is the field-read/init/choose code. Cart `FX_DATA_BYTES` 21090 → 21123 (+33 B).
The atkDist value change (40 → 24) is data on the cart and costs no extra flash.

## 6. `make fxtest-headless` — device suites (Ardens present)

```
=== test_assets ===
asset_test PASSED=254 FAILED=0
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
data_test PASSED=221 FAILED=0
P
test_data: PASS
=== test_parity ===
parity_test PASSED=660 FAILED=0
P
test_parity: PASS
=== test_perf ===
B pUs=6383 pHz=156 lHz=52 lTk=988 rMx=4676 rAv=4476 ram=494
perf_test PASSED=5 FAILED=0
P
test_perf: PASS
```

`test_data` 194/0 → **221/0** (+27): `sizeof(MonsterDef)==11`, stride 11/22,
field offsets 0/1/3/5/7/9, and all 18 per-variant values read back through the
shipping FX accessors (HEAVY atkDist now asserts 24). `parity_test` unchanged
**660/0** (default kind). Every suite ends in a bare `P`; no `F`; Ardens was
present, nothing faked.

## 7. `avr-nm` — MonsterDef placement on device

```
$ avr-nm --print-size --size-sort --radix=d dist/monhun-ardu.ino.elf | grep -iE "MONSTER_DEFS|MonsterDef"
(no output; exit 1 — no host array/blob symbol in any section)
$ avr-nm --print-size --size-sort --radix=d dist/monhun-ardu.ino.elf | grep -E "mhFxRead|seekData|readEnd"
00002828 00000012 t _ZN2FX7readEndEv
00005460 00000012 t _ZN2mh10mhFxReadU8EPKh
00002876 00000024 t _ZN2FX8seekDataEu6uint24
00005432 00000028 t _ZN2mh11mhFxReadU16EPKj
```

`src/fxdata.h` (generated) places the blob at `mhMonsterDefs = 0x005262`
(0x005240 + 34 B of monsterattacks), and the cart image carries it verbatim:

```
$ dd if=fxdata/fxdata.bin bs=1 skip=$((0x5262)) count=33 | cmp - fxdata/tables/monsterdefs.bin
cart blob at 0x5262 == monsterdefs.bin (33 B)
```

The old host `MONSTER_DEFS` array (33 B) is absent from flash; only the 58 B of
`mhFxReadU8/U16` accessor code plus `FX::seekData`/`readEnd` touch the cart.

## Interpretation notes

- Split rule (both core and mock): `atkDist >= 0 && dist > atkDist ? lunge :
  sweep` — kind 0 keeps the literal legacy `dist > 32`; SWEEP (`-1`) never
  lunges; HEAVY (`24`) lunges across 25..41, i.e. "mostly lunge" within the
  `dist < 42` engage gate. Boundary checks: 24 → sweep, 25 → lunge.
- `tools/gen-fxtables.cpp` has no per-value expectations to update; the blob is
  serialized from the single `MONSTER_DEFS` table, so the 24 is asserted only by
  the host/device suites and the md5-verified blob.

## Blockers

None. Ardens was present and all device suites ran for real (no BLOCKED path).

## Files changed (tree left dirty; no commit/push per instructions)

- `src/core/game.hpp`, `src/core/fxmem.hpp`, `src/core/monster.hpp`,
  `src/core/world.hpp`
- `mock/game.js`, `mock/game.test.js`
- `tools/gen-fxtables.cpp`, `tools/fxdump.cpp`, `fxdata/fxdata.txt`
- `tst/monster_test.hpp`, `tst/fxdatatest/data_test.hpp`
- generated (committed artifacts regenerated by `make gen`): `src/fxdata.h`,
  `fxdata/fxdata.h`, `fxdata/fxdata.bin`, `fxdata/fxdata-data.bin`,
  `fxdata/manifest.json`, new `fxdata/tables/monsterdefs.bin`
- `output.md` (this report)
