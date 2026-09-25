# monhun-ardu-ryh.6 — multi-window masks: one hitbox column per packed window

## Status: DONE

## Change

The last hand-authored window rects are gone: sweep `gore` (2), ravager
`bite` + `tail_sweep` (3) and heavy `tail_spin` (4 rotating) now come from the
masks, and `data/creatures/*.json` lost every `windows[].box` key.

**Encoding (bead design, decided)**: a mask's hitbox band has ONE COLUMN PER
WINDOW in the creature's packed window order (attack source order, then window
order). A window is owned by its attack's `art.sheet`, or by the beast sheet when
the attack authors no art (ravager `bite`/`tail_sweep` ride `fxmonster`). Column
i of a mask paints the i-th window the mask owns; every owned column paints
exactly one rect; no later column paints anything; a creature's columns across
its masks total its window count. The mask width is the owned-window count (a
beast sheet that owns none still carries one column so its zone/collide bands
have a frame). Single-window attacks follow the same rule, so the chicken's 3
columns already matched.

**`tools/gen-hitboxes.py`**
- `windows_for_attacks` (mapped columns by `art.frame`, skipped multi-window
  kits) replaced by `window_sheet` / `window_assignment` / `windows_for_symbol`.
- `process` pre-computes each creature's window→mask assignment, derives each
  mask's column count from it, parses the mask at that count, and derives the
  window rects for BOTH beast and attack masks. Explicit guard: a creature's
  derived windows == its authored windows.
- Dropped `discover_blocks` / `source_columns` (the art-pose column tie is
  obsolete; hurtbox/collision are column-invariant).
- `_review_panel` takes the mask column count (was the sprite pose count).
- Docstring/palette/validation text updated: one orange rect per column.

**Masks** (`images/masks/*.png`, hand-authored sources)
- `fxbullatk` 4 cols = stomp, gore-horns, gore-trample, rear_kick (was
  art-frame order stomp/blank/rear_kick/blank).
- `fxchickenatk` unchanged (3 = peck/leap/wing_beat).
- `fxheavyatk` 2 cols = bite, tail_slam.
- `fxmonster` 3 cols = bite, tail_sweep w0, w1.
- NEW `fxtailspin_40x28.png` 4 cols = tail_spin w0..w3 (the four rotating rects).
- Beast sheets that own no window (`fxmonster_lunge/_sweep/_heavy`, `fxpole`)
  re-laid to a single column; geometry identical.
- Re-laid with a one-off helper `build/paint_masks.py` (gitignored, not
  committed); the committed PNGs are the source from here.

**Docs**: `docs/creature-framework.md` hitbox-mask section rewritten for the new
column rule and validations.

## Numbers

```
make size:      size: .text=29376 .data=50 .bss=1764
                size: flash=29426/29696 (270 free)  ram=1814/2560
make size-line: size: flash=29426/29696 (270 free)  ram=1814/2560
```

Delta vs HEAD (2333a7e): **flash 0 B, RAM 0 B**. The packed blob and every
device generated header are byte-identical — `git diff` on `fxdata/` and `src/`
touches only `fxdata/manifest.json` (the new mask input); `combat.bin`,
`combat_data.hpp`, `combat_meta.hpp`, `combat_expect.hpp` and `src/fxdata.h` show
no diff. (The task's 29470/226 checkpoint predates 2333a7e, the −44 B progmem
macro trim; HEAD already measured 29426/270.)

## Gates

| command | result |
|---|---|
| `make gen` | regen OK (manifest 117 images, 87 inputs, 33 outputs) ✔ |
| `make gen-check` | `fxdata_manifest: PASS (164 generated artifacts unchanged)` ✔ |
| `make test` | `Total Passed: 6815  Total Failed: 0` ✔ (reach guard green) |
| `make test-tools` | `Ran 373 tests ... OK` ✔ |
| `FXTEST_ONLY=test_monster_art make fxtest-headless` | `test_monster_art PASSED=180 FAILED=0` / `test_monster_art: PASS` ✔ |
| `FXTEST_ONLY=test_combat make fxtest-headless` | `combat_test PASSED=252 FAILED=0` / `test_combat: PASS` ✔ |
| `FXTEST_ONLY=test_tell make fxtest-headless` | `test_tell PASSED=18 FAILED=0` / `test_tell: PASS` ✔ |
| `make size` / `make size-line` | flash 29426 (270 free) / ram 1814 ✔ |

Shipped window rects identical (test_combat pins every value; the blob diffs
empty). Budget: 270 B free ≥ the ~150 B wave floor.

## Notes

- The rotating `tail_spin` windows are four fixed body-relative rects (one per
  time slice), not a box that rotates with the art, so they paint as four mask
  columns — not the BLOCKED case the bead anticipated.
- No git commit/push (worker protocol).
