# monhun-ardu-nch.6 — chicken legs: shade-0 eraser -> DARK + LIGHT highlights

## What changed

- `tools/gen-art.py` `_chicken_east`: near/far thigh, knee, shank, foot, rear toe
  recolored from `lo` (BLACK eraser on the idle/flash body tone) to `DARK`, plus
  1 px `LIGHT` highlights down each shank (cols 11/16, rows 18..20) and across
  the front of each foot (cols 10-11 / 18-19, row 21). BLACK kept for eye/beak/
  wattle (against the white head) and the ground shadow row. Silhouette, two-leg
  gap, 32x24 cell and 14-frame layout unchanged.
- `tst/art_dims_test.hpp` chicken block: added plane-0 visible-ink pins at the
  leg positions (near/far shank) and kept the mask + clear-gap checks. Fails on
  the old eraser art (proved below).
- `tst/fxdatatest/monster_art_test.hpp`: added a device LUNGE leg-band check
  (plane-0 ink in cell x9..19, rows 18..22; near/far shank bits set; cell x14 gap
  clear).
- Regenerated with `make gen` (PNG + Sprites.txt + fxdata.bin set).

## Before / after leg pixels (frame 0, east idle, cols 8..20)

`K` = shade-0 BLACK eraser (plane0=0, invisible on the black floor), `g` = DARK
(plane0=1), `l` = LIGHT (plane0=1, plane1=1), `.` = clear.

BEFORE (HEAD, `git show HEAD:fxdata/blocks/Sprites.txt`):
```
row13 KKKKKKKKKKKgg
row14 ...KK...KK...
row15 ...KK...KK...
row16 ..KKKK.KKKK..
row17 ..KKKK.KKKK..
row18 ...KK...KK...
row19 ...KK...KK...
row20 .K.KK...KK.K.
row21 .KKKKK.KKKKK.
row22 .............
row23 KKKKKKKKKKKKK
```

AFTER (fix):
```
row13 ...gg...gg.gg
row14 ...gg...gg...
row15 ...gg...gg...
row16 ..gggg.gggg..
row17 ..gggg.gggg..
row18 ...lg...lg...
row19 ...lg...lg...
row20 .g.lg...lg.g.
row21 .gllgg.gggll.
row22 .............
row23 KKKKKKKKKKKKK
```

Leg pixels (blob decode): `(11,20)` mask=1 p0=1 p1=1; `(12,20)` p0=1;
`(16,20)` p0=1 p1=1; `(14,20)` mask=0 (gap clear). Before: all leg pixels mask=1
p0=p1=0 (eraser).

## Fail-on-old-art proof

Reverted `tools/gen-art.py`, `make gen`, `make test` -> exit 2:
```
chicken near leg plane0 ink Assertion failed
chicken far leg plane0 ink Assertion failed
chicken near shank plane0 ink Assertion failed
chicken far shank plane0 ink Assertion failed
```
Restored the fix + `make gen` -> host suite green.

## Gate results (after fix)

1. `make gen` (x2) + `make gen-check`:
```
fxdata_manifest: PASS (68 generated artifacts unchanged)
GENCHECK=0
```
2. `make test`:
```
Total Passed: 4792
Total Failed: 0
```
3. `make test-tools`:
```
Ran 143 tests in 7.377s
OK
```
4. `node --test mock/game.test.js`:
```
tests 42  pass 42  fail 0
```
5. Device suites:
```
=== test_monster_art ===
test_monster_art PASSED=38 FAILED=0
P
test_monster_art: PASS

=== test_assets ===
asset_test PASSED=258 FAILED=0
test_assets: PASS

=== test_parity ===
parity_test PASSED=660 FAILED=0
test_parity: PASS
```
6. `make size`:
```
size: .text=25452 .data=40 .bss=1706
size: flash=25492/29696 (4204 free)  ram=1746/2560
```
Before == after (baseline 25492/29696, 4204 free; RAM 1746/2560). Art is cart
data; the plus-mask blob dimensions are unchanged so the image size is identical.

No float. No /tmp test code. No commit/push.
