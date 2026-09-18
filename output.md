# monhun-ardu-abr — eqf.2 render slot loop, pixel-identical to today

Worker report. No commit/push/`git add` performed. Renders the existing gen-art
sheets with today's pixels; the blank placeholder weapon sheets are untouched.

## What changed

- `tst/fxdatatest/player_art_test.hpp` (+ `test_player_art.ino`): device pixel
  oracle. 37-case matrix (3 weapons x idle/attack phases/special/parry/whirl/
  guard/shove/dodge/deflect/stun/reload/i-frames x E/W/SE facings), each rendered
  per L4 plane into a cleared buffer and FNV-1a hashed; 111 goldens in PROGMEM.
  Goldens captured from the pre-refactor build, then re-run unchanged.
- `tools/gen-equipment.py`: new `source: "gen-art"` record form (explicit `sheet`
  validated against `fxdata/fxdata.h`, no PNG authored), new `order: "pose"`,
  optional `variants` and `flat` keys, and a generated **player part view**
  (`PART_SHEET` uint24 offsets, `PART_ANCHOR_X/Y`, `PART_FRAME[pose]`,
  `PART_VARIANT`, `PART_FLAT`, `partSheet()`).
- `data/equipment/*.json`: 16 gen-art records for today's overlays (body
  normal/dodge, slash frames 0..4, parry, chip idle/ball, riposte, flail
  ring/chain/ball/stun, gun plate/white/shove/reload, deflect, erase) with the
  anchors that reproduce current frame picks.
- `src/render.hpp`: `drawPlayer` selects sheet + frame + anchor from the
  generated tables via `partDraw`/`partVariantDraw`; the hw/hh slash if-chain and
  the per-weapon frame picks are gone. Reach scaling, whirl orbit and tick-driven
  offsets stay computed code. Dead `spr::` frame constants deleted.
- `tools/tests/test_gen_equipment.py`: 3 permanent tests for the gen-art form
  (no-PNG + part view, unknown-symbol rejection, `flat` marker).

## Verification (exact tails)

1. `make gen` twice -> `make gen-check`
   `fxdata_manifest: PASS (45 generated artifacts unchanged)`
2. `make test` -> `Total Passed: 3119` / `Total Failed: 0`
   `make test-tools` -> `Ran 72 tests ... OK` (was 69; +3 gen-art tests)
3. `make fxtest-headless` (all 10 suites)
   - asset 254/0, audio PASS, boot PASS, combat 195/0, data 221/0, hud PASS,
     menu 59/0, parity 660/0
   - `test_player_art PASSED=111 FAILED=0` -> P
   - perf `B pUs=6382 pHz=156 lHz=52 lTk=984 rMx=5120 rAv=4770 ram=434` -> 5/5 P
4. `make build` + `make size`
   `size: .text=27224 .data=58 .bss=1960`
   `size: flash=27282/29696 (2414 free)  ram=2018/2560`
   flash delta vs **26668 baseline: +614 B** (part tables ~310 B + refactor code).
   perf `rMx` 5120 us vs 5064 baseline (+56 us), still under the 7407 us budget.

## Deviations / notes

- **Pre-existing guard-plate quirk pinned by the oracle.** The old line
  `FRAME(guard ? spr::GUARD_WHITE : spr::GUARD_PLATE)` macro-expands to
  `guard ? 1 : 0 * 3 + arduboy.currentPlane()`: for `guard` it blits flat frame
  index 1 on *every* plane (the lit plate with its notch), not logical frame 1
  per plane. To stay byte-identical the catalog models this with a `flat: true`
  record `gun_guard_white` (drawn on ST_GUARD) beside the per-plane `gun_guard`
  plate. If the intent is the actual white plate frame, that is a separate
  one-frame visual change plus a golden regen — not done here per the
  "pixel-identical" gate.
- Slash frames are now selected by an attack slot (combo chain 0..2 / plain
  special / branch id) through the generated `PART_VARIANT` table
  `{0,0,1,2,3,4}`; the old `hw == art_dims::...` chain is deleted. Pixel oracle
  proves the frames match (combo 2 -> 18x14, special -> 20x16, step-slash
  14x12, spin-cut 28x26).
- `test_player_art` uses 2358 B RAM (static Game matrix); flash 14040 B (47%).
- No literal `art_dims::` frame constants were needed by the new path; the
  remaining `spr::WHIRL_DOT` is still used by the monster/effect sheets.
