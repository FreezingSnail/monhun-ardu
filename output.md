# monhun-ardu-6zb.2 — Device: opening menu (weapon x target select, start + return)

STATUS: **DONE** — all suites green, shipping build fits, tree left dirty
(no commit), Ardens device run complete.

- Repo/base: `/Users/connorfranc/monhun-ardu`, branch `main`, HEAD `2296b51`
  (6zb.1 landed), working tree dirty with this bead's changes only.
- Bead: `monhun-ardu-6zb.2`; parent epic `monhun-ardu-6zb`.
- Contract implemented: `src/menu_state.hpp` MenuState + menuStep; `src/menu.hpp`
  drawMenu per plane; `.ino` boot-to-menu / start / over+A return; host + device
  suites; README controls + roster.

---

## Files changed

| File | State | What |
|---|---|---|
| `src/menu_state.hpp` | new | MenuState {weapon,target,active,prevA,prevB}; `menuStep()` (nav wrap + A-edge START); `menuMode()`/`menuMonsterKind()`/`menuStart()` mapping; `menuReturnStep()` post-over edge on the same owned flags |
| `src/menu.hpp` | new | `drawMenu()` per plane: title "MONHUN DEMO", WEAPON row (SWD/FLS/GUN), TARGET row (LUNGE/SWEEP/HEAVY/POLE), footer "A START"; PROGMEM strings, FX glyphs via `textPut()`, selected option white + white underline via `blk()` |
| `monhun-ardu.ino` | modified | boot `s_menu` active; shared `sampleInput()`; while active menuTick only (no stepGame/audio); START → `menuStart(g, s_menu)`; playing ticks keep menu flags current via `menuReturnStep()`; over+A re-opens menu with picks kept; `render()` draws menu instead of scene while active |
| `tst/menu_test.hpp` | new | host suite: defaults, weapon/target wrap both ways, A edge once, same-tick nav+A, mode/kind mapping, `menuStart` over all 3x4 picks, return guard, round-trip picks |
| `tst/main.cpp` | modified | `MenuSuite(runner)` registered |
| `tst/fxdatatest/test_menu.ino` | new | device runner (final bare P/F via FxTest) |
| `tst/fxdatatest/menu_test.hpp` | new | device suite 39 asserts: scripted nav, start mapping through AVR `newGame` (cart `MONSTER_DEFS` hp/size), return edge, picks kept |
| `README.md` | modified | new **Controls** section (menu, roster table, in-game bindings) + refreshed status/flash/RAM/test counts + repo layout |

No commit / no push; tree intentionally dirty.

---

## 1. `make test` — host suites (menu checks included)

```
---------- boot defaults: SWD/LUNGE, active, idle tick is silent ----------
Passed: 5
Failed: 0
---------- LEFT/RIGHT cycle weapon 0..2 and wrap both ways ----------
Passed: 6
Failed: 0
---------- UP/DOWN cycle target 0..3 (beasts then pole) and wrap both ways ----------
Passed: 7
Failed: 0
---------- A rising edge fires START once per press, hold stays silent ----------
Passed: 13
Failed: 0
---------- same-tick nav + A applies the nav and still fires START ----------
Passed: 2
Failed: 0
---------- pick -> mode/kind mapping (targets 0..2 hunt, 3 pole) ----------
Passed: 8
Failed: 0
---------- menuStart applies weapon + mode + kind to Game ----------
Passed: 36
Failed: 0
---------- return edge only after over, exactly once per press ----------
Passed: 9
Failed: 0
---------- picks survive the return round trip; the return A does not restart ----------
Passed: 10
Failed: 0
========== Total Counts ==========
Total Passed: 1433
Total Failed: 0
```

Baseline at HEAD was 1337/0; menu suite adds 96 asserts, 0 failed.

---

## 2. `make build` — shipping sketch

```
arduino-cli compile --fqbn "arduboy-homemade:avr:arduboy-fx" --optimize-for-debug  --output-dir dist
Sketch uses 28116 bytes (94%) of program storage space. Maximum is 29696 bytes.
Global variables use 1946 bytes (76%) of dynamic memory, leaving 614 bytes for local variables. Maximum is 2560 bytes.
```

| Metric | Before (26512/1942 per bead context) | After | Delta |
|---|---|---|---|
| Flash | 26512 / 29696 (3184 free) | **28116 / 29696 (1580 free)** | **+1604 B** |
| RAM (globals) | 1942 / 2560 (618 free) | **1946 / 2560 (614 free)** | **+4 B** (5 B `s_menu`, layout rounding) |

Fits with 1580 B flash / 614 B RAM free. The delta covers the menu FSM, the
FX-glyph menu renderer (`menuOptionRow` 328 B + `menuText` 164 B + PROGMEM
tables) and the now-runtime monster-kind start path (`initMonster` indexing the
FX `MONSTER_DEFS` blob instead of only kind 0).

---

## 3. `make fxtest-headless` — Ardens device suites (all PASS, incl. new test_menu)

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
=== test_menu ===
menu_test PASSED=39 FAILED=0
P
test_menu: PASS
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

B line vs expected baseline `rMx=4676 pHz=156 lHz=52 ram=495`:

- `rMx=4676`, `pHz=156`, `lHz=52` match exactly; `pUs=6383`, `lTk=988`,
  `rAv=4476` also match.
- `ram=494`, not 495: the repo's own HEAD baseline (output.md committed at
  `2296b51`) records `ram=494` on the same full line. Perf bench source is
  untouched by this bead (it renders `renderScene`, not the menu), so this is
  the existing baseline, not a regression; the RAM gate (`RAM_FREE_MIN`) passes
  5/5. No faking: device tests were run on Ardens (present at
  `~/code/Ardens/build/Ardens.app/Contents/MacOS/Ardens`), not skipped.

First run of the new suite crashed (no serial) because the test held three
~700 B `Game` copies on the AVR stack; fixed by reusing one `static Game` for
all mapping checks — rerun PASS as above. The fix is in the test only.

test_menu sketch: 10834 B flash, 1884 B globals (the static Game).

---

## 4. `make gen-check` — FX pipeline determinism

```
Including file /Users/connorfranc/monhun-ardu/fxdata/blocks/Sprites.txt
Including file /Users/connorfranc/monhun-ardu/fxdata/fonts/Sprites.txt
Saving FX data header file /Users/connorfranc/monhun-ardu/fxdata/fxdata.h
Saving 21123 bytes FX data to /Users/connorfranc/monhun-ardu/fxdata/fxdata-data.bin
Saving FX development data to /Users/connorfranc/monhun-ardu/fxdata/fxdata.bin
fxdata_manifest: fxdata/manifest.json up to date (19 images, 6 inputs, 5 outputs)
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (30 generated artifacts unchanged)
```

No FX data changed by this bead (menu text is code-side UI, not content data).
`git status` after gen-check shows no generated-artifact churn. README FX-size
lines refreshed 21090 → 21123 B (already 21123 at HEAD; doc was stale).

---

## 5. Menu screen layout + selectable matrix

ASCII sketch (128x64; glyphs are 4x8 FX tiles, 3x5 ink; `===` = white
selection underline; not to scale, x positions exact):

```
x=  0       32        64        96       128
   +-----------------------------------------------+
 8 |                                               |
16 |                 MONHUN DEMO                   |   title  y=10, x=42 (white)
24 |  WEAPON  SWD   FLS   GUN                      |   label y=22 x=4 (gray)
32 |          ===                                  |   underline y=31, selected white
40 |  TARGET  LUNGE SWEEP HEAVY POLE               |   label y=36 x=4 (gray)
48 |          =====                                |   underline y=45
56 |  A START                                      |   footer y=54 x=4 (white)
   +-----------------------------------------------+
```

- Layout constants: title x=42/y=10; label lane x=4; options x=36; rows y=22
  (weapon) and y=36 (target); footer y=54. Underline is `len*4-1` px wide at
  `y+9` on every plane (shade 3) via `blk()`; row backgrounds need no explicit
  clear because ArduboyG wipes each plane framebuffer black before the pass.
- Feature text lives in MCU flash (PROGMEM), glyphs on the FX cart — no menu
  bitmap/string in RAM (5 B `s_menu` total).

Selectable matrix — 3 weapons x 4 targets = 12 picks, all reachable/wrapping:

| weapon \ target | LUNGE (0) | SWEEP (1) | HEAVY (2) | POLE (3) |
|---|---|---|---|---|
| SWD (0) | hunt / kind 0 | hunt / kind 1 | hunt / kind 2 | train / pole |
| FLS (1) | hunt / kind 0 | hunt / kind 1 | hunt / kind 2 | train / pole |
| GUN (2) | hunt / kind 0 | hunt / kind 1 | hunt / kind 2 | train / pole |

Mapping (one source of truth, `src/menu_state.hpp`): targets 0..2 →
`MODE_HUNT` + `MONSTER_DEFS[target]` (LUNGE 32x24/200hp, SWEEP 28x22/150hp,
HEAVY 40x28/320hp on the FX cart); target 3 → `MODE_TRAIN` + kind 0 (pole).

Boot: menu active with SWD + LUNGE selected. In play: LEFT/RIGHT cycle weapon,
UP/DOWN cycle target (both wrap), A rising edge starts exactly once; while the
menu is up `stepGame`/`audioUpdate` are skipped. After win/lose, A rising edge
returns to the menu with weapon/target kept; the held return A cannot re-start
the scene (menu-owned `prevA` is kept current during play).

---

## Blockers

None. Ardens available, all device suites ran (not skipped, not faked).
