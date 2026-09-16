# monhun-ardu-6zb.4 — Menu: debounce d-pad nav

**Status: DONE.** All gates green. Tree left dirty, no commits.

## Files changed

| File | Change |
|---|---|
| `src/menu_state.hpp` | per-axis hold state (`navX/navY`, `navXTimer/navYTimer`), `MENU_NAV_DELAY=16` / `MENU_NAV_REPEAT=6`, `menuResetNav()`, `menuNavAxis()`, `menuStep()` rewritten to debounced nav, `menuReturnStep()` resets nav on the over+A return edge |
| `tst/menu_test.hpp` | tap-vs-hold tests: tap = 1 step, hold `<DELAY` = 1, hold `>=DELAY` = 2nd then repeats every `REPEAT`, release resets, reversal immediate, axes independent, return-to-menu resets |
| `tst/fxdatatest/menu_test.hpp` | same scripted tap/hold/reversal/return-reset sequences through the real header on device; 39 → 55 asserts |
| `README.md` | controls table: tap vs hold repeat; status table (host 1490, menu 55, flash/RAM); challenges flash/RAM history |

## Verification

### 1. `make test` — 0 failed

```
---------- return-to-menu resets nav timers (held dpad cannot skip) ----------
Passed: 21
Failed: 0
========== Total Counts ==========
Total Passed: 1490
Total Failed: 0
```

Baseline 1433 → 1490 (+57 host asserts).

### 2. `make build` — flash/RAM delta

```
Sketch uses 28132 bytes (94%) of program storage space. Maximum is 29696 bytes.
Global variables use 1950 bytes (76%) of dynamic memory, leaving 610 bytes for local variables. Maximum is 2560 bytes.
```

| Metric | Baseline | Now | Delta |
|---|---|---|---|
| Flash | 28116 B | **28132 B** | **+16 B** |
| RAM | 1946 B | **1950 B** | **+4 B** (4 bytes: `int8 navX/navY` + `uint8 navXTimer/navYTimer`) |

### 3. `make fxtest-headless` — all suites PASS (Ardens present)

Exact perf B line:

```
B pUs=6383 pHz=156 lHz=52 lTk=988 rMx=4676 rAv=4476 ram=494
```

Suite results:

```
asset_test PASSED=254 FAILED=0
P
test_assets: PASS
test_audio PASSED=14 FAILED=0
P
test_audio: PASS
test_boot PASSED=4 FAILED=0
P
test_boot: PASS
data_test PASSED=221 FAILED=0
P
test_data: PASS
menu_test PASSED=55 FAILED=0
P
test_menu: PASS
parity_test PASSED=660 FAILED=0
P
test_parity: PASS
perf_test PASSED=5 FAILED=0
P
test_perf: PASS
```

Baselines: boot 4, assets 254, audio 14, menu 39 (now **55**), parity 660, data 221, perf 5 — all PASS, none faked.

### 4. `make gen-check` — PASS

```
fxdata_manifest: fxdata/manifest.json up to date (19 images, 6 inputs, 5 outputs)
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (30 generated artifacts unchanged)
```

### 5. Quantified feel — tick math

Measured logic rate on device: `lHz=52` (perf suite above).

- **Fresh press** (direction change from neutral/released, or reversal): step **immediately** on that tick.
- **Hold**: after the initial step the axis arms `MENU_NAV_DELAY = 16` ticks = **307.7 ms** @ 52 Hz before the second step; then re-arms `MENU_NAV_REPEAT = 6` ticks = **115.4 ms**. Sustained repeat rate = 52/6 = **8.67 steps/s**.
- **1 s hold** (52 logic ticks): steps at t = 0, 16, 22, 28, 34, 40, 46, 52 → **8 pick moves** (1 tap-on-press + 7 repeats).
- **2 s hold** (104 ticks): steps at t = 0, 16 + 6k for k = 0..14 → **16 pick moves** (1 + 15 repeats; last repeat t = 100).
- **Before** this fix the same hold stepped every tick → up to **52 moves/s**, i.e. ~6x the sustained repeat rate and a full 3-weapon / 4-target wrap several times per second, which matches the demo report.

Formula used: `moves(T) = 1 + (T >= DELAY ? 1 + floor((T - DELAY) / REPEAT) : 0)`.

## Design notes

- `menuNavAxis()` is pure per-axis glue (pick ref, count, dir, last, timer): `dir==0` clears state; `dir != last` steps at once and arms delay; same dir decrements the timer, steps and re-arms repeat at zero. `uint8_t` timers, no runtime modulo, no floats.
- `menuStep()` calls it once per axis per logic tick; A edge logic untouched (single fire per press, works while a direction is held).
- `menuReturnStep()` calls `menuResetNav()` only on the actual over+A return edge; edge flags stay current so the held return A cannot restart the menu (pinned by tests).
- Re-entry with a direction held: exactly one immediate step (fresh direction), then the full 16-tick delay — no stale timer skip.

## Blockers

None. Ardens available (`~/code/Ardens/build/Ardens.app/Contents/MacOS/Ardens`), FX image present.
