# monhun-ardu-nch.3 — longtail: real visible spin (rotating 8-frame sheet)

Report. Epic monhun-ardu-nch. Worker spawn was cancelled mid-run after the core
implementation; the orchestrator inspected the dirty tree, finished the missing
device-art coverage + mock facing fix inline, and ran the full gate. No commit by
the worker; orchestrator commits.

## What changed

- `tools/gen-art.py`: new `fxtailspin` sheet — the longtail east silhouette
  rotated about the body centre in 45 deg steps (8 frames, 40x40 plus-mask,
  deterministic nearest-neighbour rotation re-quantized to the four authored
  shades). Cart data only; `fxtail_heavy` (rest overlay) and `fxtail_spin`
  (windup tell) unchanged.
- `src/render_math.hpp` (new, Arduino-free): `spinSheetFrame(start8, tick,
  active)` = `(start8 + tick*8/active) & 7`, frame 0 east, i*45 deg clockwise.
- `src/render.hpp`: during MS_ATTACK of the locked tail_spin on MON_HEAVY,
  `drawMonster` draws `fxtailspin` centred on the body box centre instead of the
  E/W beast sheet (start8 = DIR8 index of the locked facing, sync to `m.t` over
  the cached `active`). The fxtail_spin overlay now draws in MS_WINDUP only (the
  tell); the resting tail overlay stays skipped in both phases. `sprDraw` cull
  bound widened 32 -> 40 px for the bigger sheet.
- `mock/game.js`: mirrors the rotation (canvas rotate about the monster centre,
  frame from the locked facing + `m.t`), exports `spinSheetFrame`; duplicate
  `dirIndexFromDelta` added by the cancelled worker removed (the original
  function already existed) and the spin start now uses the real facing index.
- Tests: `tst/render_math_test.hpp` (frame math wrap/slices) registered in
  `tst/main.cpp`; host `tst/art_dims_test.hpp` pins the sheet (40x40, 8 frames,
  white-head orbit bands, frame distinctness, 4-shade quantization); device
  `tst/fxdatatest/asset_test.hpp` header check + `monster_art_test.hpp` attack
  phase now asserts the rotating sheet per frame (head right/bottom/left/top at
  t=2/5/10/15 with active 20) and that the old overlay/rest caps stay clear.

## Verification

- `make gen` x2 + `make gen-check` — exit 0, 67 artifacts unchanged (the
  cancelled worker had left `equip_meta.hpp`/`fxdata.h` out of sync; regen
  fixed it).
- `make test` -> 4759 passed / 0 failed (includes the frame-math suite).
- `make test-tools` -> 141 OK; `node --test mock/game.test.js` -> 38/38.
- Full `make fxtest-headless` -> all suites PASS; parity 660/0;
  test_monster_art 32/0 (new spin checks).
- Parity fixture regen -> empty diff (`20 scenes / 1269 ticks`).
- `make size` -> flash 25444/29696 (4252 free, +182 B vs 25262 baseline),
  RAM 1744/2560. Sheet is cart data; the +182 B is the render branch + helper.
