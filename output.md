# monhun-ardu-44z — audio: replace ArduboyTones with minimal TIMER3 beeper

Status: **DONE** (all gates green; no commit per worker protocol). Spike accepted:
**552 B** net flash recovered vs 28242 (gate >= 500 B).

## What changed

- `src/audio.hpp` only. Deleted the `ArduboyTones` dependency (include, global
  `mhTones` instance, `mhAudioEnabled` callback, `audioPlay` switch of
  `ArduboyTones::tone()` calls) and replaced it with a one-shot TIMER3 square-wave
  beeper:
  - `mhCueTable[11][4]` in PROGMEM: `{OCR3A, toggles, OCR3A2, toggles2}` per cue,
    all constants precomputed with the exact old library math
    (`OCR = F_CPU/8/freq/2 - 1`, `toggles = (ms*freq)>>9`). No runtime division,
    no float.
  - `mhPlay(cue)` disables `OCIE3A`, sets PC6/PC7 output low, loads the two
    segments, configures CTC /8, then re-enables the ISR.
  - `ISR(TIMER3_COMPA_vect)` toggles PC6; on segment exhaustion loads the queued
    second segment or clears `OCIE3A` and parks PC6 low. Max two segments, one
    shot, non-blocking (audioPlay returns immediately).
- Driver lives entirely in `src/audio.hpp`; no vendored library touched.
- `-DMH_AUDIO=0` path unchanged in shape: everything above is inside `#if
  MH_AUDIO`, so the mute build has no beeper symbols at all. `test_perf` and
  `test_parity` compile with `-DMH_AUDIO=0` and stay green.
- Test comments refreshed (`audio_test.hpp`, `perf_test.hpp`) to name the beeper
  instead of the removed library. Tests themselves unchanged and pass.

## Timer / vector (collision answer)

- Beeper: **TIMER3_COMPA = `__vector_32`** (I checked
  `avr/iom32u4.h`: `TIMER3_COMPA_vect_num 32`).
- ArduboyG plane timing: **TIMER1_COMPA = `__vector_17`** (`ABG_TIMER1` in
  `src/common.hpp`). Different timer, different vector — no collision.
- The bead's "1124 B timer ISR `vector_11`" note was a mis-attribution:
  `__vector_11` is **USB_COM** (`USB_COM_vect_num 11`), 0x464 = 1124 B, in the
  baseline ELF. ArduboyTones never used it. The real tone ISR was `__vector_32`,
  0x8E = 142 B. The beeper keeps TIMER3, so USB is untouched.

## Verification (exact tails / numbers)

1. `make gen` (x2) -> `make gen-check`:
   ```
   fxdata_manifest: PASS (53 generated artifacts unchanged)
   gen-check exit: 0
   ```
2. `make test` -> `Total Passed: 3144  Total Failed: 0`.
   `make test-tools` -> `Ran 82 tests in 4.948s ... OK`.
3. `make fxtest-headless` (full, run twice on final tree): all suites PASS —
   `test_assets 262/0, test_audio 14/0, test_boot 4/0, test_combat 184/0,
   test_data 221/0, test_hud 17/0, test_menu 59/0, test_parity 660/0,
   test_perf 5/0, test_player_art 111/0`.
   perf tail: `B pUs=6502 pHz=153 lHz=51 lTk=988 rMx=5392 rAv=5028 ram=409`
   vs reference `rMx=5392 rAv=5028 pUs=6502` — **byte-identical, no regression**
   (bench is `-DMH_AUDIO=0`, so this is unaffected by design; kept as the gate).
4. `make build` + `make size`:
   ```
   Sketch uses 27690 bytes (93%) ... Global variables use 2005 bytes ...
   size: .text=27632 .data=58 .bss=1947
   size: flash=27690/29696 (2006 free)  ram=2005/2560
   ```
   - Baseline: **28242** (1454 free). New: **27690** (2006 free).
   - **Net delta vs 28242 = -552 B** (spike gate >= 500 B: PASS).
   - `-DMH_AUDIO=0` ceiling re-measured today = **27378** (864 B). The beeper
     costs **312 B** of that ceiling; the other 552 B are recovered library code.
   - No `ArduboyTones` symbols remain in the ELF (`avr-nm | grep -c` = 0).
5. `node tools/gen-parity-fixtures.js` -> `git diff --stat` of
   `tst/fxdatatest/parity_fixtures.hpp` empty (0 diff lines).
6. Data facts unchanged (no `HAS_*` flip): `HAS_MULTI_WINDOW:true
   HAS_STAGGER:true HAS_ZONES:true HAS_GUARD_ZONES:true`, rest false.

## Per-symbol delta (`avr-nm --print-size`, baseline -> new)

| symbol | baseline | new |
|---|---|---|
| `ArduboyTones::nextTone` | 330 | — |
| `ArduboyTones::getNext` | 40 | — |
| `ArduboyTones::tone(u16,u16)` | 56 | — |
| `ArduboyTones::tone(u16,u16,u16,u16)` | 64 | — |
| `mh::mhAudioEnabled` | 4 | — |
| `__vector_32` (TIMER3_COMPA ISR) | 142 | 114 |
| `mh::audioCue(...).constprop` | 280 | 194 |
| `mh::mhCueTable` (PROGMEM) | — | 88 |
| `mh::mhPlay` | — | 0 (inlined into audioCue) |
| RAM `mhToggles/mhOcr2/mhToggles2` | — | 6 B bss |

RAM: bss 1971 -> 1947 (-24 B), `.data` unchanged; global RAM 2029 -> 2005.
The remaining whole-image delta also removes the ArduboyTones ctor, 3-tone
overload and static state (`toneSequence` 14 B + start/index/playing/silent/
highVol) that size-sort does not attribute cleanly under LTO; whole-image
measurement is authoritative (AGENTS.md "Budget first").

## Fidelity / deviations from ArduboyTones

- Pitch is exact (same `OCR = F_CPU/8/freq/2 - 1`, same /8 CTC prescaler).
- Duration is exact to within one timer compare: the old ISR advanced to the
  next segment on the *following* compare after the last toggle; the new ISR
  advances on the same compare. Max ~1 half-period (< ~3 ms) shorter per segment.
  Cues use at most two segments (all `tone()` calls in the old switch were 1 or 2
  tones), so no cue loses a segment.
- Output mechanism identical: PC6 toggled by writing `PINC`, PC7 held low
  (normal volume); the old library also played these cues with no
  `TONE_HIGH_VOLUME` bit, i.e. normal volume, PC7 low.
- Behavior unchanged: `audioCue`/`audioUpdate` edge detection, rate limit and
  `firedMask` are untouched. One-shot, non-blocking, does not delay frames.
