# monhun-ardu-6zc — Device: audio cues (hit, crit, parry, windup, shot)

## Bead
`monhun-ardu-6zc` (slice of epic `monhun-ardu-kt7`). Fires short, non-blocking
audio cues from combat events without touching core sim logic or the
ArduboyG/FX plane loop.

## Files
- added `src/audio.hpp` — cue detector + ArduboyTones playback. Reads `Game`
  only; no core header modified. Compile-time mute `-DMH_AUDIO=0` (drops the
  ArduboyTones dependency entirely).
- changed `monhun-ardu.ino` — include, one `mh::AudioState s_audio;` global, and
  `mh::audioUpdate(s_audio, g)` after `stepGame(g, in)` inside `run()` (i.e.
  only under `needsUpdate()`, outside the FX/OLED bracket).
- added `tst/fxdatatest/audio_test.hpp` + `tst/fxdatatest/test_audio.ino` —
  on-device cue-map assertions; picked up automatically by the
  `test_*.ino` wildcard in the Makefile.
- changed `output.md`.

## Timer / ISR safety
`src/common.hpp` sets `ABG_TIMER1`, so ArduboyG's plane ISR is TIMER1_COMPA.
ArduboyTones drives TIMER3_COMPA and toggles the speaker pins PC6/PC7 — distinct
timer and pins, no ISR conflict. `tone()` arms the timer ISR and returns
(no delay loops); cues are 12–55 ms one-shots. The audio test plays real tones
while asserting (ArduboyG's plane loop still running), and reaches `P`.

## Cue-to-event map (edge-diff of `Game` only)
| Cue | Fired when (same-tick sim edge) |
|-----|----------------------------------|
| `CUE_HIT`    | hunt beast hp drops (melee/shot landed) |
| `CUE_CRIT`   | hunt hp drop + fresh crit spark, or pole hp total rise + fresh crit damage number |
| `CUE_TRAIN`  | train pole total rises (body hit) |
| `CUE_HURT`   | player hp drops (clean hit, not guard/parry/deflect) |
| `CUE_PARRY`  | `player.riposteT` rises (sword parry armed a riposte) |
| `CUE_DEFLECT`| `monster.stun` rises while `player.state == PS_DEFLECT` |
| `CUE_GUARD`  | player hp drops while `player.stance == ST_GUARD` (chip) |
| `CUE_WINDUP` | `monster.state` enters `MS_WINDUP` (telegraph) |
| `CUE_SHOT`   | `projN` rises (gun fired) |
| `CUE_RELOAD` | `player.reload` reaches 0 |

Freeze-safe: `stepGame()` skips `updateEffects()`/sim while `freeze > 0`, so an
effect can hold `t == 1` and a transient field can hold its value across frozen
ticks. Every cue is gated on an edge that only advances on a non-frozen tick
(hp/total drop, stun set, spawn, reload→0, windup entry), so nothing refires.
A repeat of the same cue within 2 ticks is dropped (rate-limit).

## Mute flag (fxtest)
`-DMH_AUDIO=0` removes all `ArduboyTones` code and makes cues no-ops. Verified:
`arduino-cli compile ... --build-property compiler.cpp.extra_flags=-DMH_AUDIO=0`
→ 26756 B flash / 1909 B globals (no tone lib linked). The shipped device build
leaves `MH_AUDIO` at its default (1).

## Build (device, `rm -rf build && make build`)
```
Sketch uses 27672 bytes (93%) of program storage space. Maximum is 29696 bytes.
Global variables use 1941 bytes (75%) of dynamic memory, leaving 619 bytes for
local variables. Maximum is 2560 bytes.
```
- Flash 27672 / 29696 → 2024 B headroom (< 29696 ✓)
- RAM 1941 → 619 B free (>= 300 ✓)
- Baselines before this bead: 26140 B flash / 1884 B RAM (ArduboyTones + cues ≈
  +1532 B flash, +57 B RAM).

## Test tails
`make test` (host C++17):
```
========== Total Counts ==========
Total Passed: 497
Total Failed: 0
```

`make fxtest-headless` (Ardens, all four device suites):
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
=== test_parity ===
parity_test PASSED=660 FAILED=0
P
test_parity: PASS
```

No Python, no float/double, no core/mock changes; cue triggering reads state
only. All assets remain on the FX chip (tones are procedural, no score/tune
data added).
