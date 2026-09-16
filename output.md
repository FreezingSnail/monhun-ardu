# monhun-ardu-6zb.3 — Gate: demo menu verified (mock+parity byte-identical, suites, flash/perf)

Independent re-run of the demo-menu wave gate at the wave tip (HEAD `5def2e7`,
clean tree), covering children `6zb.1` (core+mock MONSTER_DEFS on the FX cart)
and `6zb.2` (opening menu weapon x target select). Every command below was run
fresh by the gate; no output from the workers was trusted. Epic `6zb` acceptance
is the bar.

## Environment / HEAD

```
$ git status
On branch main
Your branch is ahead of 'origin/main' by 44 commits.
nothing to commit, working tree clean
$ git log --oneline -6
5def2e7 device: opening menu - weapon x target select (monhun-ardu-6zb.2)
2296b51 core+mock: demo monster variants on FX cart (monhun-ardu-6zb.1)
1e27a38 gate: wave verified — flash 26444 B, all suites green (monhun-ardu-42n.6)
9851902 quarter-wave SIN256 + exhaustive host check (monhun-ardu-42n.7)
d9a0a5c record hot-LUT keep decision + refreshed status (monhun-ardu-42n.5)
dbdabe2 image/fxdata manifest + make gen-check, drop orphans (monhun-ardu-42n.4)
```

Ardens present at `~/code/Ardens/build/Ardens.app/Contents/MacOS/Ardens`
(preflight checks executable + `captureserial`); no suite was skipped or faked.

## 1. `node --test mock/game.test.js` — full pass

```
ℹ tests 24
ℹ suites 0
ℹ pass 24
ℹ fail 0
ℹ cancelled 0
ℹ skipped 0
ℹ todo 0
ℹ duration_ms 101.51375
```

This includes the variant tests (`monster variants: roster + spawn stats per
def, default is legacy LUNGE`, `SWEEP never lunges, HEAVY lunges past 24`,
`weapon swap and reset keep the chosen beast`, `train mode with HEAVY keeps the
pole path intact`).

## 2. Parity fixtures byte-identical

```
$ node tools/gen-parity-fixtures.js
wrote tst/fxdatatest/parity_fixtures.hpp
scenes=20 ticks=1269 snapshots=32 cpFields=20
$ git diff --stat tst/fxdatatest/parity_fixtures.hpp
(empty)
```

Regeneration is a no-op: the 6zb.1/6zb.2 contract (default `monsterIndex = 0`
keeps every scene byte-for-byte) holds. Device `test_parity` independently
confirms 660/0 on cart-backed data (section 6).

## 3. `make gen` determinism + `gen-check` + `test-tools`

`make gen` run twice; md5 of the regenerated artifacts before / after run 1 /
after run 2:

```
fxdata/fxdata.bin   645cb6cb33ecb5223eb5b5e9f1e5da76  (all three)
src/fxdata.h        bbdce9da67c7c6fc253c37bb6b9eaaca  (all three)
fxdata/fxdata.h     bbdce9da67c7c6fc253c37bb6b9eaaca  (all three)
```

`git status --porcelain` empty after both runs — tracked generated artifacts
byte-stable (also empty immediately after the first gather, before this
three-point capture).

```
$ make gen-check
fxdata_manifest: PASS (30 generated artifacts unchanged)
exit=0
$ make test-tools
Ran 15 tests in 0.876s
OK
```

## 4. `make build` — flash / RAM vs baseline

```
arduino-cli compile --fqbn "arduboy-homemade:avr:arduboy-fx" --optimize-for-debug  --output-dir dist
Sketch uses 28116 bytes (94%) of program storage space. Maximum is 29696 bytes.
Global variables use 1946 bytes (76%) of dynamic memory, leaving 614 bytes for local variables. Maximum is 2560 bytes.
```

| metric | baseline (wave start) | measured | bar | verdict |
|---|---|---|---|---|
| flash | 28116 / 29696 (1580 free) | **28116 / 29696** | ≤ 29696 | **PASS** (1580 free, delta 0) |
| RAM (globals) | 1946 / 2560 (614 free) | **1946 / 2560** | ≤ 2560 | **PASS** (614 free, delta 0) |

Exactly on baseline: the gate adds no code, and `6zb.2`'s +1604 B flash /
+4 B RAM menu growth was already recorded in its report.

## 5. `make test` — host suites

```
========== Total Counts ==========
Total Passed: 1433
Total Failed: 0
```

1433/0 matches the expected count exactly (no drift). The menu suite sections
all pass: boot defaults 5/0, weapon wrap 6/0, target wrap 7/0, A-edge 13/0,
same-tick nav+A 2/0, mode/kind mapping 8/0, `menuStart` matrix 36/0, return
edge 9/0, return round trip 10/0.

## 6. `make fxtest-headless` — device suites (all PASS)

```
=== test_assets ===   asset_test PASSED=254 FAILED=0    P   test_assets: PASS
=== test_audio ===    test_audio PASSED=14 FAILED=0     P   test_audio: PASS
=== test_boot ===     test_boot PASSED=4 FAILED=0       P   test_boot: PASS
=== test_data ===     data_test PASSED=221 FAILED=0     P   test_data: PASS
=== test_menu ===     menu_test PASSED=39 FAILED=0      P   test_menu: PASS
=== test_parity ===   parity_test PASSED=660 FAILED=0   P   test_parity: PASS
=== test_perf ===
B pUs=6383 pHz=156 lHz=52 lTk=988 rMx=4676 rAv=4476 ram=494
perf_test PASSED=5 FAILED=0
P
test_perf: PASS
```

Sketch sizes of the test builds (flash / globals): assets 8506/1399, audio
11742/1239, boot 11308/1179, data 16486/1179, menu 10834/1884, parity
28670/1884, perf 28562/1913. Every suite ended in a bare `P`; no `F`.

## 7. Perf budgets + B-line determinism

Two independent captures (inside `fxtest-headless`, then a direct Ardens run of
`test_perf`):

```
run 1 (fxtest-headless): B pUs=6383 pHz=156 lHz=52 lTk=988 rMx=4676 rAv=4476 ram=494
run 2 (direct)         : B pUs=6383 pHz=156 lHz=52 lTk=988 rMx=4676 rAv=4476 ram=494
```

Byte-identical, and identical to the `42n.6` gate line (ram 494 recorded there).

| gate | budget | measured | verdict |
|---|---|---|---|
| render max | ≤ 7407 µs | **4676 µs** | **PASS** (2731 µs margin) |
| plane rate | ≥ 135 Hz | **156 Hz** | **PASS** |
| logic rate | ≥ 45 Hz | **52 Hz** | **PASS** |
| free RAM | ≥ 300 B | **494 B** | **PASS** |

`src/render.hpp`, `src/audio.hpp` and the perf bench are untouched by the whole
wave (`git diff 1e27a38..HEAD --` those paths is empty), which matches the
unchanged line.

## 8. Demo-flow evidence

### 8a. Device serial — `test_menu` (real AVR, cart-backed)

```
=== test_menu ===
menu_test PASSED=39 FAILED=0
P
test_menu: PASS
```

The suite drives `menuStep` through boot defaults, weapon wrap both ways,
target wrap both ways, A-edge exactly once, starts POLE/HEAVY/SWEEP through the
real AVR `newGame` and asserts cart-driven stats (HEAVY 40x28/320 hp, SWEEP
150 hp — read from the FX `MONSTER_DEFS` blob), then the post-over return edge
and pick retention. Ardens is a serial harness here; it injects no keys, so the
nav is scripted inside the suite, not played.

### 8b. Host scripted matrix — all 3x4 picks (`src/menu_state.hpp`)

Permanent host suite section `menuStart applies weapon + mode + kind to Game`
(part of `make test`, `tst/menu_test.hpp:106`) enumerates every pick and asserts
weapon + mode + monsterKind: **36/0** = 12 picks × 3 fields. Full suite
`Opening menu: nav, start edge, pick mapping` = 96 asserts, 0 failed. Mapping
verified against `src/menu_state.hpp:69-80` and `Game` constants
(`W_SWORD=0`, `W_FLAIL=1`, `W_GUNSHIELD=2`, `MODE_HUNT=0`, `MODE_TRAIN=1`):

| weapon \ target | LUNGE (0) | SWEEP (1) | HEAVY (2) | POLE (3) |
|---|---|---|---|---|
| SWD (0) | hunt / kind 0 | hunt / kind 1 | hunt / kind 2 | train / kind 0 |
| FLS (1) | hunt / kind 0 | hunt / kind 1 | hunt / kind 2 | train / kind 0 |
| GUN (2) | hunt / kind 0 | hunt / kind 1 | hunt / kind 2 | train / kind 0 |

Boot path (`monhun-ardu.ino:32,91-111`): `s_menu.active = true` at boot; while
active only `menuStep` runs (no `stepGame`/audio) and `drawMenu` replaces the
scene; `MENU_START` → `menuStart` + `active=false`; `menuReturnStep` re-opens
the menu on an over+A edge with picks kept.

### 8c. Pixels

No screen capture was taken: Ardens exposes screenshots only through the GUI
(F2 / "Take PNG Screenshot", `~/code/Ardens/src/view_debugger.cpp:130`) with no
headless CLI option, so a headless gate cannot grab the menu framebuffer. The
renderer is covered by the shipping/test builds compiling `src/menu.hpp` and by
the on-device font/asset suite (`asset_test` 254/0); the FSM-to-game mapping is
covered by 8a/8b.

## 9. `avr-nm` / cart evidence — MONSTER_DEFS on FX, perf path unchanged

`src/fxdata.h:33` — `constexpr uint24_t mhMonsterDefs = 0x005262;` (33 B blob,
ends at 21123 = FX image size). `src/core/fxmem.hpp:28` maps it to
`MH_FX_MONSTER_DEFS_ADDR`, and the AVR branch of `src/core/game.hpp:434-442`
makes `MONSTER_DEFS` an `FxMonsterDefsRom` facade that reads the cart; the
flash array at `game.hpp:444` is `#else` (host-only).

```
$ avr-nm --print-size --size-sort --radix=d dist/monhun-ardu.ino.elf | grep -E 'MONSTER_DEFS|WEAPON_DEFS|MONSTER_ATTACKS'
(no output; exit 1)
```

No `MonsterDef`/`WeaponDef` table symbol exists in any ELF section; the only
`monster*` symbols are code (`initMonster`, `damageMonster`, `clampMonster`,
`syncMonsterTarget`, `monsterOnHit/Stun/Shove`). The cart image carries the
blob verbatim at the header address:

```
$ xxd -s 0x5262 -l 33 fxdata/fxdata.bin
00005262: 0020 0018 00c8 0005 0020 0001 1c00 1600  . ....... ......
00005272: 9600 0700 ffff 0228 001c 0040 0103 0018  .......(...@....
00005282: 00                                       .
```

Decoded little-endian (`kind, w, h, hp, spd, atkDist`): LUNGE `0,32,24,200,5,32`
· SWEEP `1,28,22,150,7,-1` · HEAVY `2,40,28,320,3,24` — identical to
`mock/game.js:186-190` (source of truth) and the host table. `cmp` of the cart
slice against `fxdata/tables/monsterdefs.bin` passes (`CART_IMAGE_MATCHES_TABLE_BLOB`).

Perf path unchanged: the B line above is identical to the `42n.6` gate line,
and neither `src/render.hpp` nor the perf bench was modified by this wave.

## 10. `bd list` — wave closure

```
○ monhun-ardu-42n ● P1 [epic] EPIC: move remaining data off MCU flash ...
○ monhun-ardu-6zb ● P1 [epic] EPIC: demo opening menu ...
└── ○ monhun-ardu-6zb.3 ● P2 Gate: demo menu verified ...
○ monhun-ardu-kt7 ● P1 [epic] EPIC: mock to Arduboy device game (vertical slice)
├── ○ monhun-ardu-vx2 ● P2 Device: real 4-shade sprite art pass
├── ○ monhun-ardu-1to ● P3 Gate: device feel playtest + tuning pass
└── ○ monhun-ardu-qyb ● P4 Device: EEPROM save (deferred, out of slice)
○ monhun-ardu-7y3 ● P2 [bug] Render: HUD bars/divider clipped by blk() arena-band clamp
Total: 8 issues (8 open, 0 in progress)
```

Open items are exactly the two epics, this gate (closed at the end of this
report), and the known follow-ups `7y3`, `vx2`, `1to`, `qyb`. No new issues
were filed by this gate.

## 11. README status rows

Checked line-by-line against the fresh numbers: host 1433 (README.md:15), device
counts incl. menu 39 (16), perf line 156/52/4676/988/494 (17), shipping flash
28116 / RAM 1946 (19), FX image 21123 B (20), controls + roster incl. HEAVY
"lunges inside 24 px" (141-161) — all current. No edit made; the 6zb.2 update
already landed and nothing is stale.

## Acceptance-criteria mapping

| criterion | evidence | result |
|---|---|---|
| mock tests green | §1: 24/24, 0 failed | **PASS** |
| parity fixtures byte-identical | §2: regen no-op; §6 device parity 660/0 | **PASS** |
| host suite green, 0 failed | §5: 1433/0 | **PASS** |
| device suites green incl. test_menu | §6: 254/14/4/221/39/660/5, all `P` | **PASS** |
| perf budgets PASS | §7: rMx 4676≤7407, pHz 156≥135, lHz 52≥45, ram 494≥300 | **PASS** |
| flash/RAM recorded vs baseline | §4: 28116/29696, 1946/2560 (delta 0) | **PASS** |
| make gen deterministic + gen-check | §3: md5 x3 identical, tree clean, 30 artifacts | **PASS** |
| demo boots to menu; 3x4 picks map right | §8a device 39/0 + §8b host 36/0 + mapping table + `.ino` wiring | **PASS** |
| game over + A returns with picks kept | §8a return-edge checks; §5 return suite 9/0 + 10/0 | **PASS** |
| MONSTER_DEFS on FX cart, no flash array | §9: nm absent, cart blob at 0x5262 exact | **PASS** |
| README controls/status current | §11: verified, no edit needed | **PASS** |
| output.md gate report | this file | **PASS** |

### Deviations (documented, none gate-failing)

1. **HEAVY `atkDist` is 24, not 40 as the epic description text says.** This
   was an explicit correction applied during `6zb.1` and recorded in that
   bead's output.md ("Correction applied on orchestrator review: HEAVY
   `atkDist` 40 → 24" — the split rule `dist > atkDist ? lunge : sweep` makes
   24 the "mostly lunge" value under the `dist < 42` engage gate). Mock
   (`mock/game.js:189`), core (`src/core/game.hpp:447`), cart blob and both
   test suites pin 24 consistently; the epic prose is stale. No action needed.
2. **Pixels not captured** — headless Ardens has no screenshot option (§8c);
   renderer coverage via compile + asset/font suite. Stated, not faked.
3. **README read-only** — rows were already current, so the minimal-edit
   instruction resolves to no change (§11).

## Files changed (tree left dirty; no commit/push per gate instructions)

- `output.md` — this gate report (overwrite).

## Verdict

**PASS** on every gate bar. HEAD `5def2e7`, mock 24/0, parity fixtures
byte-identical (device parity 660/0), host 1433/0, device suites boot 4/0 ·
assets 254/0 · audio 14/0 · menu 39/0 · data 221/0 · perf 5/0 all `P`, gen
determinism + manifest green, flash 28116/29696 · RAM 1946/2560 (614 free),
perf 156 Hz / 52 Hz / 4676 µs / 494 B free within budget and byte-stable across
two runs, `MONSTER_DEFS` confirmed off MCU flash at FX `0x5262` with the cart
blob decoding to the approved roster, and the 3x4 pick matrix verified end to
end. Gate closed.
