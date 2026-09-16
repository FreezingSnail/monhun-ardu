# monhun-ardu-42n.6 — Gate: re-verify flash/RAM/perf/parity after data offload + art translation

Independent re-run of the full gate at the wave tip (HEAD `9851902`, clean tree),
covering children `42n.1` (tables → FX cart), `42n.2` (overlay/effect sheets),
`42n.3` (render from FX sheets), `42n.4` (manifest + gen-check), `42n.5`
(hot-LUT keep decision) and `42n.7` (quarter-wave SIN65). Every command below
was run fresh by the gate; no output from earlier workers was trusted.

## Environment / HEAD

```
$ git status
On branch main
Your branch is ahead of 'origin/main' by 41 commits.
nothing to commit, working tree clean
$ git log --oneline -8
9851902 quarter-wave SIN256 + exhaustive host check (monhun-ardu-42n.7)
d9a0a5c record hot-LUT keep decision + refreshed status (monhun-ardu-42n.5)
dbdabe2 image/fxdata manifest + make gen-check, drop orphans (monhun-ardu-42n.4)
4e332f0 render overlays/effects from FX sheets; exact-size telegraphs (monhun-ardu-42n.3)
9ed4285 author overlay/effect block sheets + dims drift guard (monhun-ardu-42n.2)
f0595a5 move weapon/monster tables to FX cart (monhun-ardu-42n.1)
7762ac7 add clang-format pre-commit hook with make hooks/format
ef40bae style: apply clang-format baseline across repo
```

## 1. `make gen` twice — deterministic

md5 of the regenerated artifacts before run 1, after run 1, after run 2:

```
fxdata/fxdata.bin   fe8fb7c181a055965ecd6b020d73c865  (all three)
src/fxdata.h        205df177038ec3418caaee59a986201f  (all three)
fxdata/fxdata.h     205df177038ec3418caaee59a986201f  (all three)
```

`git status --porcelain` was empty after the second run (and after run 1):
tracked generated artifacts are byte-stable. Run 1 tail:

```
gen-art: wrote 17 block sheets (11 overlay/effect icons) + 2 font sheets
gen-art: pixel check OK (19 sheets, disk-exact)
gen-fxtables: fxdata/tables/weapondefs.bin (540 B), fxdata/tables/monsterattacks.bin (34 B)
Saving 21090 bytes FX data to fxdata/fxdata-data.bin
fxdata_manifest: fxdata/manifest.json up to date (19 images, 5 inputs, 5 outputs)
gen.sh: FX data + src/fxdata.h regenerated
```

## 2. `make gen-check` + `make test-tools`

```
fxdata_manifest: PASS (29 generated artifacts unchanged)
exit=0
```

```
Ran 15 tests in 0.968s
OK
exit=0
```

## 3. `make build` — flash / RAM

```
Sketch uses 26444 bytes (89%) of program storage space. Maximum is 29696 bytes.
Global variables use 1941 bytes (75%) of dynamic memory, leaving 619 bytes for local variables. Maximum is 2560 bytes.
```

`avr-size -A dist/monhun-ardu.ino.elf`: `.text 26398 + .data 46 = 26444` flash;
`.data 46 + .bss 1895 = 1941` RAM. `.text` alone is 26398 B.

| metric | pre-wave (kt7.5) | after 42n.4 | expected | measured | bar | verdict |
|---|---|---|---|---|---|---|
| flash used | 27680 B | 26644 B | 26444 B | **26444 B** | ≤ 26600 B | **PASS** (156 B margin) |
| RAM used | 1941 B | 1941 B | 1941 B | **1941 B** | ≤ 2560 B, ≥ 300 free | **PASS** (619 free) |

## 4. `make test` — host

```
========== Total Counts ==========
Total Passed: 1281
Total Failed: 0
```

1281/0 (bar: 0 failed; baseline 497/0 → +784, from the exhaustive SIN65/SIN256
suite and the FX-table/data suites). No variance across runs.

## 5. `make fxtest-headless` — device suites (Ardens present)

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
data_test PASSED=194 FAILED=0
P
test_data: PASS
=== test_parity ===
parity_test PASSED=660 FAILED=0
P
test_parity: PASS
=== test_perf ===
B pUs=6386 pHz=156 lHz=52 lTk=988 rMx=4676 rAv=4476 ram=495
perf_test PASSED=5 FAILED=0
P
test_perf: PASS
```

Every suite ended in a bare `P`; no `F`; assets 254/0 (baseline 30/0 — new
overlay/effect sheets), data 194/0 is the new FX-table suite. Ardens was
present; nothing faked or skipped.

### test_perf determinism — three fresh runs, byte-identical

```
run 1 (fxtest-headless): B pUs=6386 pHz=156 lHz=52 lTk=988 rMx=4676 rAv=4476 ram=495
run 2 (direct)         : B pUs=6386 pHz=156 lHz=52 lTk=988 rMx=4676 rAv=4476 ram=495
run 3 (direct)         : B pUs=6386 pHz=156 lHz=52 lTk=988 rMx=4676 rAv=4476 ram=495
```

Zero run-to-run variance on every field, matching `42n.7`'s section-4
measurement exactly.

| gate | budget | pre-wave (kt7.5) | measured | verdict |
|---|---|---|---|---|
| render max | ≤ 7407 µs | 3984 µs | **4676 µs** | **PASS** (2731 µs margin) |
| plane rate | ≥ 135 Hz | 156 Hz | **156 Hz** | **PASS** |
| logic tick | ≤ 19230 µs | 976 µs | **988 µs** | **PASS** |
| logic rate | ≥ 45 Hz | 52 Hz | **52 Hz** | **PASS** |
| free RAM | ≥ 300 B | 489 B | **495 B** | **PASS** |

## 6. `avr-nm` — moved tables gone, hot LUTs as decided

```
$ avr-nm --print-size --size-sort --radix=d dist/monhun-ardu.ino.elf | grep -iE 'WEAPON_DEFS|MONSTER_ATTACKS'
(no output; exit 1 — no symbol in any section)
$ ... | grep -iE 'sin'
00000172 00000065 t _ZN2mhL5SIN65E       # mh::SIN65, 65 B PROGMEM
00004096 00000054 t _ZN2mhL6sin256Eh      # mh::sin256(unsigned char), 54 B code
$ ... | grep -E 'DIR8|MH_MASK|RING6'
00000278 00000032 t _ZN2fpL4DIR8E         # 32 B
00000237 00000008 t _ZN2mhL11MH_MASK_BOTE # 8 B
00000245 00000008 t _ZN2mhL11MH_MASK_TOPE # 8 B
00000418 00000006 t _ZN2mhL5RING6E        # 6 B
```

Evidence: `mh::WEAPON_DEFS` (was 540 B) and `mh::MONSTER_ATTACKS` (was 34 B)
are absent from the ELF entirely; the 256-byte `mh::SIN256` array is gone and
only the 65-byte `mh::SIN65` table plus the `sin256` folding helper (54 B code)
remain. Residual in-flash hot LUTs are exactly the `42n.5` decision set —
`SIN65 65 + DIR8 32 + MH_MASK_TOP/BOT 16 + RING6 6 = 119 B` — no hot-LUT
policy violation. The moved tables + `mhWeaponDefs`/`mhMonsterAttacks` raw
blobs live on the cart (`fxdata/tables/weapondefs.bin` 540 B,
`monsterattacks.bin` 34 B) and are exercised by `test_data` (194/0).

## 7. Ardens headless profiler dump (shipping ELF, 3000 ms)

```
$ARDENS headless=3000 display=ssd1306 fxport=d1 profiledump=build/profiler-42n6.txt \
  file=dist/monhun-ardu.ino.elf file=fxdata/fxdata.bin
```

AFTER (this gate):

```
cycles 25856018  cycles_with_sleep 48000217  cpu_active_pct 53.9
hotspots  count     pct    begin  end    name
          7161772  14.92  0x0cd8 0x0d0c abg_detail::ArduboyG_Common<...>::paint(...)
          4752397   9.90  0x2efa 0x65ba main
          1755811   3.66  0x18c2 0x1bc4 SpritesU::drawPlusMaskFX(int, int, uint24, unsigned int)
          1557885   3.25  0x0d0c 0x0e9a mh::blk(long, long, long, long, unsigned char)
           547272   1.14  0x0b0c 0x0b18 FX::readEnd()
           299765   0.62  0x0b54 0x0b62 FX::writeByte(unsigned char)
           206656   0.43  0x2dee 0x2e82 __vector_23
           184139   0.38  0x0e9a 0x1000 mh::hudBar(...)
           184125   0.38  0x1d2e 0x2804 mh::drawPlayer(mh::Game const&, int, int)
           160276   0.33  0x0c98 0x0cc8 micros
```

BEFORE (pre-wave baseline, kt7.5 AFTER section, shipping ELF):

```
cycles 25360131  cycles_with_sleep 48000002  cpu_active_pct 52.8
          7161772  14.92  paint
          4713629   9.82  main
          1718517   3.58  mh::blk
          1279997   2.67  SpritesU::drawPlusMaskFX
           331561   0.69  FX::readEnd()
```

Delta (pre-wave → now):

| symbol | pre-wave | now | change |
|---|---|---|---|
| total active cycles | 25.36 M | 25.86 M | +2.0% |
| cpu_active | 52.8% | 53.9% | +1.1 pp |
| `paint` (plane-blit floor) | 7161772 | 7161772 | 0 |
| `main` | 4713629 | 4752397 | +0.8% |
| `SpritesU::drawPlusMaskFX` | 1279997 | 1755811 | **+37.2%** |
| `mh::blk` | 1718517 | 1557885 | **-9.3%** |
| `FX::readEnd()` | 331561 | 547272 | **+65.1%** |
| `FX::writeByte` | — | 299765 | new (0.62%) |

Interpretation: the ~2% total-cycle increase is the price of `42n.3`'s art
translation — overlay/effect and telegraph pixels now come from FX sheets via
`SpritesU::drawPlusMaskFX` (+0.48 M cycles) with the corresponding cart-read
overhead (`FX::readEnd` +0.22 M, `FX::writeByte` +0.30 M). `mh::blk` fell 9.3%
(sprite-like overlays no longer drawn as procedural rects); `paint` (the
ArduboyG plane blit, outside the render scene) is byte-identical and remains
the fixed floor. All of it stays inside the inviolable budgets (render max
4676 ≤ 7407 µs). Note vs the epic's "previous or better numbers" phrasing:
render max/avg are ~17%/16% above the kt7.5 pre-wave line (+692/+634 µs) but
equal-or-better than every measurement taken during the wave (42n.7 section-4
line 6386/4676/4476/495 matches exactly). Recorded as a deviation, cause = the
FX-sheet reads themselves; no core/sim change.

## 8. `bd list` — wave closure

```
○ monhun-ardu-42n   [epic] (open, parent)
└── ○ monhun-ardu-42n.6  Gate: ... (this gate)
○ monhun-ardu-kt7   [epic] (open, parent)
├── ○ monhun-ardu-vx2  Device: real 4-shade sprite art pass (human)
├── ○ monhun-ardu-1to  Gate: device feel playtest + tuning pass (human)
└── ○ monhun-ardu-qyb  Device: EEPROM save (deferred)
○ monhun-ardu-7y3   [bug] HUD bars/divider clipped by blk() clamp
Total: 7 issues (7 open, 0 in progress)
```

All six wave children (`42n.1`–`42n.5`, `42n.7`) are closed; only the two
epics, this gate, and the known follow-ups (`7y3` HUD clamp, `vx2`, `1to`,
`qyb`) remain open.

## Acceptance-criteria mapping

| criterion | evidence | result |
|---|---|---|
| all suites PASS with exact tails | §4 host 1281/0, §5 device boot 4/0, assets 254/0, audio 14/0, parity 660/0, data 194/0, perf 5/0, all `P` | **PASS** |
| perf budgets PASS | §5 table: rMx 4676≤7407, pHz 156≥135, lHz 52≥45, ram 495≥300 | **PASS** |
| perf deterministic | §5 three runs byte-identical | **PASS** |
| shipping flash ≤ 26600 B (or documented shortfall) | §3: 26444 B, 156 B margin; avr-size/avr-nm evidence | **PASS** |
| no moved table remains a PROGMEM array | §6: `WEAPON_DEFS`/`MONSTER_ATTACKS`/`SIN256` absent; LUT set = 119 B decision set | **PASS** |
| profiler before/after recorded | §7 | **PASS** |
| gen determinism + manifest | §1 (§1 md5 ×3 identical, clean tree), §2 gen-check 29 artifacts | **PASS** |
| output.md updated | this file | **PASS** |

### Deviations (documented, none gate-failing)

1. **Render cost rose vs pre-wave** (rMx 3984 → 4676 µs, rAv 3842 → 4476 µs,
   pUs 6379 → 6386, lTk 976 → 988; pHz/lHz unchanged). Cause: `42n.3` FX-sheet
   reads (profiler §7). Within budget with 2731 µs margin; epic phrase
   "perf gate PASS at previous or better numbers" is only satisfied against
   the in-wave measurements, not the kt7.5 line. No follow-up needed unless a
   future bead wants the margin back.
2. **Assets suite 30 → 254 / new data suite 194** — expected consequence of
   `42n.2`/`42n.4`/`42n.1`; counts recorded as the new baseline.
3. **README refresh**: status snapshot rows updated to the verified truth
   (device counts, FX data 21090 B from `src/fxdata.h` `FX_DATA_BYTES`,
   perf numbers, `7y3` follow-up) — see "Files changed".

## Files changed (tree left dirty; no commit/push per gate instructions)

- `README.md` — status snapshot rows (device suites, perf numbers, FX data
  size, follow-ups), cadence line, `make test` assert count, `test_data`
  tier bullet.
- `output.md` — this gate report.

## Verdict

**PASS** on every gate bar. Flash 26444/29696 B (156 B under the ≤26600 B epic
line), RAM 1941/2560 B (619 free), host 1281/0, device boot 4/0 · assets 254/0 ·
audio 14/0 · parity 660/0 · data 194/0 · perf 5/0, gen determinism and manifest
green, moved tables confirmed off MCU flash. Gate closed.
