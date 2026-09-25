# monhun-ardu-ryh.4 — migrate every beast + pole to masks

## What changed

Every creature is now mask-migrated. Sweep, heavy, ravager and the pole joined
the chicken (ryh.3): six new committed masks, their tightened zone boxes landed
through the pipeline, the bull/heavy part overlays cropped to the mask bbox, and
the redundant hand geometry keys deleted from the creature JSON.

- **`images/masks/` (new sources).**
  - `fxmonster_sweep_32x24.png` — cell 32x24 (sprite); body (0,0,28,22),
    head (21,4,8,8), appendage (4,18,20,5), collide (1,14,26,8).
  - `fxmonster_heavy_40x28.png` — cell 40x28 (body; the tail sits 24 px left of
    the cell, so the mask now carries a 24 px per-side horizontal margin); body
    (0,0,40,28), appendage (-24,4,24,11), collide (-8,3,48,22).
  - `fxmonster_32x24.png` — ravager cell 32x24, margin 16; body (0,0,32,24),
    head (20,4,12,12), appendage (-14,8,18,10), collide = body (unchanged).
  - `fxpole_20x40.png` — cell 20x40 (sprite); body (0,0,20,36), head (5,0,10,8),
    collide = body (unchanged).
  - `fxbullatk_28x22.png` — attack mask, cell = body; col0 stomp
    (0,0,36,26), col2 rear_kick (-14,4,22,14); col1 (gore, multi-window) and
    col3 (stomp-windup tell) blank.
  - `fxheavyatk_40x28.png` — attack mask; col0 bite (14,0,18,14), col2 tail_slam
    (-16,0,36,28); col1/col3 blank.
- **`tools/gen-hitboxes.py`.** The mask cell is now named in the filename
  (`<symbol>_<cellW>x<cellH>.png`) and the per-side margins are derived from the
  image dims (`W = (cellW + 2*mx) * columns`, `H = (cellH + 2*my) * 3`). The
  margins are 2D: the bull stomp window reaches 2 px above/below the 22-tall
  body and the longtail tail sits 24 px left. `source_columns` is read from the
  one-facing source PNG (`images/blocks/<sheet>_<W>x<H>.png` width / frame
  width), not from the attacks (an attack sheet authors a column per east pose,
  e.g. the bull stomp-windup tell has no `art.frame`). `windows_for_attacks`
  now *skips* multi-window kits instead of failing, so the mask owns the
  single-window attacks and the multi-window ones stay hand-authored. The body
  region must reproduce `stats.w/h` at the origin (new hard validation, replaces
  the old JSON round trip, which no longer applies now the keys are gone).
- **`tools/gen-art.py`.** `head_bull`, `hooves_bull` and `tail_heavy` join the
  chicken: blocks authored cell-absolute and cropped to the mask bbox
  (`_zone_part_defs_crop`). The bull head frame is 12x16 -> 8x8, hooves
  20x16 -> 20x8; the heavy tail frame stays 24x16 but its crop origin moved down
  4 px. Every overlay keeps the same *world* pixels (draw origin + frame-local
  coords are preserved), so the device art oracle is unchanged.
- **`data/creatures/*.json`.** Deleted the redundant geometry keys — `collide`
  (lunge, sweep, heavy) and every `zones.*.box` — for all five creatures.
  Behaviour keys (dmgMul, hp, bodyShare, breakTypes, broken.*, staggerOnHit,
  part, profile, patterns, windows) stay.
- **Tests/docs.** `tst/combat_test.hpp`, `tst/fxdatatest/combat_test.hpp`,
  `tst/art_dims_test.hpp`, `tst/fxdatatest/asset_test.hpp` updated to the
  tightened boxes/frames; `docs/feel-design.md` zone tables updated.

## Tight boxes landed (build/hitboxes.json, from a fresh `make gen`)

| creature | zone | before | after |
|---|---|---|---|
| sweep | head | (17,-4,12,10) | (21,4,8,8) |
| sweep | appendage | (4,12,20,10) | (4,18,20,5) |
| heavy | appendage | (-24,0,24,16) | (-24,4,24,11) |
| ravager | head/appendage | — | unchanged (no part art) |
| pole | head | — | unchanged (5,0,10,8) |

Bodies, collide boxes and every window record are unchanged in value: sweep
stomp (0,0,36,26), rear_kick (-14,4,22,14), heavy bite (14,0,18,14), tail_slam
(-16,0,36,28) all reproduce exactly through their masks. The multi-window kits
(sweep gore, ravager tail_sweep, heavy tail_spin) stay hand-authored.

## Mask format note (deviation from the design text)

The epic wrote `H == cellH * 3` and a single horizontal `MARGIN`. Two shipped
shapes need vertical reach/size beyond the body cell: the longtail body is
40x28 vs its 32x24 sprite cell, and the bull appendage/stomp window reach past
the 22-tall body. So the mask carries an independent x/y margin derived from the
image dims; the solid-rect validation is untouched (every region is still one
solid rect). The design's suggested filenames were the sprite cells; the masks
are named by their actual cell (heavy `40x28`, bull/heavy attack `28x22`/`40x28`)
because the cell is what the body/window conversion needs.

## Verification (exact tails)

`make gen`:
```
gen-hitboxes: 5 masked creature(s) [heavy, lunge, pole, ravager, sweep] -> build/hitboxes.json
gen-art: pixel check OK (38 sheets, disk-exact)
gen-combat: 5 creatures, 11 attacks, 16 windows, 15 patterns, 19 steps, 5 skeletons, 8 zones, 1257 B, sha256 f6520b708026a17023dd34398c521bdebe9174137c020eebbbbfcf9275b212f4
```

`make gen-check`:
```
fxdata_manifest: PASS (163 generated artifacts unchanged)
```

`make test`:
```
Total Passed: 6815
Total Failed: 0
```

`FXTEST_ONLY=test_monster_art make fxtest-headless`:
```
test_monster_art PASSED=180 FAILED=0
test_monster_art: PASS
```

`FXTEST_ONLY=test_combat make fxtest-headless`:
```
combat_test PASSED=252 FAILED=0
test_combat: PASS
```

`FXTEST_ONLY=test_assets make fxtest-headless`:
```
asset_test PASSED=264 FAILED=0
test_assets: PASS
```

Full device gate (sanity — `make fxtest-headless`, all suites): 0 failures.

`make size` / `make size-line`:
```
size: .text=29420 .data=50 .bss=1764
size: flash=29470/29696 (226 free)  ram=1814/2560
```

**Flash delta: 0 B** (29470/226 free, unchanged from the ryh.3 checkpoint).

Extras: `make test-tools` 373/373 OK. Review image:
`build/scratch/hitbox_review.png` (`make hitboxes-render`).
