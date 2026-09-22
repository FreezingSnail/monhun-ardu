# monhun-ardu-dx5.3 — perf: cut FX seeks (sin256 RAM copy + glyph batching)

Status: DONE. All gates green, budget fits. Tree left dirty (no commit/push).

## Files / lines

- `src/core/sin256.hpp` (site 1) — AVR-only `sin65Ram()`: one-time 65 B bulk
  copy of the cart table into a function-local static, then plain RAM loads.
  `sin256()` now reads `sin65Ram()[i]` on AVR; host path unchanged (plain
  `SIN65` array, `tst/sin_test.hpp` still walks all 256 inputs).
- `src/screens.hpp` (site 2) — `screenReadText()` bulk-reads a title/label
  string into a 16 B stack buffer (`mhFxReadBytes`, one cart transaction per
  string); `drawScreen()` draws from RAM. Titles/labels longer than 16 chars
  fall back to the old per-char `mhFxReadU8` tail, so the character mapping is
  byte-identical for any input. Longest shipped label is 11 ("HUNTER HELM").
- `src/render.hpp` (site 3) — `textPut()` now calls the explicit-dimension
  `SpritesU::drawPlusMaskFX(x, y, 4, 8, sheet, FRAME(code))` overload instead
  of the one-arg form, dropping the per-glyph `seekData()` that only read the
  sheet's 4x8 w/h header. Same w/h reaches the blitter -> pixels unchanged.

## Size (HEAD e3c0129 -> after)

- before: `flash=28558/29696 (1138 free)  ram=1638/2560`
- after:  `flash=28792/29696 (904 free)  ram=1704/2560`
- delta:  **+234 B flash, +66 B RAM** (RAM = 65 B table + 1 B `ready` flag, .bss)

Per-site flash/RAM (measured by reverting one site at a time):

| site | flash | RAM | perf |
|------|-------|-----|------|
| 3 textPut header seek | **-16 B** | 0 | rAv -17 us |
| 1 sin256 RAM cache | +46 B | +66 B | rAv -42 us (rMx +52 one-time fill) |
| 2 screens string batch | +204 B | 0 | structural (not in test_perf) |

## perf_test before/after

- before: `B pUs=6343 pHz=157 lHz=52 lTk=184 rMx=3088 rAv=2691 ram=680`
- after:  `B pUs=6342 pHz=157 lHz=52 lTk=184 rMx=3140 rAv=2632 ram=608`
- **rAv 2691 -> 2632 (-59 us/plane, -2.2%)**, pUs -1, lHz/lTk unchanged.
- rMx 3088 -> 3140 (+52 us): the one-time 65 B cache fill lands on the
  worst-case render frame (first bench render). Steady-state frames no longer
  touch the cart; the +52 is a single-frame amortized cost, well under the
  7407 us gate.

## Gate tails

1. `make test`
```
Total Passed: 6285
Total Failed: 0
EXIT=0
```
2. `make size`
```
size: flash=28792/29696 (904 free)  ram=1704/2560
```
3. `make fxtest-headless FXTEST_ONLY=test_screens`
```
test_screens PASSED=85 FAILED=0
P
test_screens: PASS
```
4. `make fxtest-headless FXTEST_ONLY=test_hud`
```
test_hud PASSED=29 FAILED=0
P
test_hud: PASS
```
5. `make fxtest-headless FXTEST_ONLY=test_perf`
```
B pUs=6342 pHz=157 lHz=52 lTk=184 rMx=3140 rAv=2632 ram=608
perf_test PASSED=5 FAILED=0
P
perf_test: PASS
```
6. `make fxtest-headless` (full)
```
test_audio: PASS   test_boot: PASS   test_combat: PASS   test_data: PASS
test_hub: PASS   test_hud PASSED=29 FAILED=0   test_items: PASS
test_menu_art: PASS   test_menu: PASS   test_monster_art: PASS
test_perf: PASS   test_player_art: PASS   test_quests: PASS
test_screens: PASS   test_smith: PASS   test_tell: PASS   test_zones: PASS
(all suites P, 0 FAILED)
```

## Copy path verification

- Host: `make test` (6285 pass) exercises `sin256`/`cos256` for all 256 inputs
  against `REF256`; the host path is untouched (plain array).
- Device (Ardens): `test_perf`/`test_hud`/`test_screens` exercise the AVR
  `sin65Ram()` bulk-fill path via the real render stack; all pass, pixels pinned.

## Dropped sites + why

- **sin256 option (b)** ("batch the three calls"): not implementable. Each call
  site shares an angle between `cos256(a)`/`sin256(a)`, but the two table
  indices are `{k, 64-k}`, never adjacent, so no single seek can stream both
  without reading the whole 65 B table per call — that is *more* expensive than
  the seeks it removes (bulk 65 B ~1270 cyc vs 6 single-byte seeks ~540 cyc).
  Option (a), the one-time RAM copy, is the measured win.
- **textPut deeper strip batching** (single seek + sequential glyph reads):
  dropped. The font sheet stores each ASCII glyph as a 3-plane record 24 B
  apart; a label's glyphs (e.g. "RDY") are not adjacent, so one sequential
  stream cannot decode them. Baking a label strip into one sprite (the weapon
  marker's fxhud pattern) needs `make gen` + art, which the bead forbids. The
  kept header-seek removal is flash-negative and pixel-identical.
- **site 2 perf number**: the screen renderer is not exercised by `test_perf`
  (hunt scene only), so its win is structural: one seek per string instead of
  one per glyph (smith page: ~50 char seeks -> 7 string reads). Kept per the
  bead scope; 204 B flash cost is within the 904 B headroom.
