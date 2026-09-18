# monhun-ardu-z6v — bake projectile trail + flail chain/idle: BLOCKED (measured)

Worker report. **No commit/push/`git add` performed. Working tree reverted to
HEAD (a750cf0) — no source/art/generated changes remain.**

The bead's premise ("each sprDraw pays a fixed cart seek ~156 us, so a bake
pays for itself") does not hold on this blitter for cells bigger than ~16 px.
Both candidate bakes were implemented, built, and measured on the real target
(Ardens cycle-accurate ATmega32u4, `test_perf`). **(a) the projectile-trail bake
regresses `rMx` at every cell size tried; (b) the flail chain+idle bake is
exactly perf-neutral.** Neither improves `rMx` vs the 5496 baseline, so per the
bead's own instruction I am reporting measured numbers + analysis instead of
claiming a win, and I did not keep the changes.

## What was implemented and measured (then reverted)

Full implementations were built and run, not estimated:

- `tools/gen-art.py` — `DIR8` parsed out of `src/core/fp.hpp`, `trail_frames()`
  (one frame per firing facing, ball-centre anchor) and `flail_idle_frames()`
  (chain dot + chip, player-centre anchor); two new block sheets + equipment
  records (`trailbake`, `flail_idle`) via the same gen-art/part-record path the
  whirl-ring bake (monhun-ardu-836) uses.
- `src/render.hpp` — `drawProjectiles` drew one `fxtrailbake` blit + ball;
  the flail idle `else` branch drew one `PART_FLAIL_IDLE` blit.
- `fxdata/fxdata.txt` — **reordered so the `raw_t` runtime tables pack before
  the sprite sections** (see "Option 2" below). Verified: `mhEquip` at 0x000525,
  i.e. all six 16-bit fake-pointer tables stay in the first 1.4 KiB of the
  137 KB image, structurally immune to any future sprite growth.

## Measured results (`test_perf`, hunt+train scenes)

Baseline (HEAD, unchanged): `B pUs=6614 pHz=151 lHz=50 lTk=984 rMx=5496 rAv=5136 ram=424`
— reproduced exactly before and after the revert, so the numbers below are
directly comparable.

| config | bake cell | `rMx` | delta vs 5496 | gate |
|---|---|---|---|---|
| (a) trail bake only | 40x40 | **6476** | **+980** | **F 12** (`pUs=7751 pHz=129 lHz=43`) |
| (a) trail bake only | 30x30 (tight union) | **5712** | **+216** | 5/5 |
| (a) trail bake only | 24x24 | **5712** | **+216** | 5/5 |
| (b) flail idle bake only, bench forced onto the flail-idle path | 24x24 | 5240 | 0 (old = 5240) | 5/5 |
| (b) flail idle bake only, same forced bench | 22x22 (tight union) | 5240 | 0 (old = 5240) | 5/5 |
| (b) flail idle bake, unmodified bench (path not exercised) | 24x24 | 5496 | 0 | 5/5 |

`test_perf` repeats are bit-stable on this model, so the 216/980 us deltas are
real signal, not noise. The (b) A/B used a *temporary* `primeHunt` stance change
(`ST_WHIRL` -> `ST_NONE`) purely to force the bench onto the idle branch, then
was reverted; with the stock bench the flail-idle branch is never executed
(hunt = `ST_WHIRL`, train = `W_GUN`), which is why (b) shows 5496 -> 5496.

## Why the bakes do not pay off

`SpritesU::drawPlusMaskFX` = one `FX::seekData()` (~156 us, the number the bead
quotes) **plus** a streamed blit whose cost is `pages x cols x 3` shade passes
(`drawBasic` in `src/external/SpritesU.hpp`). The seek is worth only ~150 us;
enlarging a sprite cell to cover a scattered ink union costs far more than the
seeks it removes:

- **Trail (a):** the three puffs' union spans 28x28 px (`bx` up to +/-5 from
  `vx = DIR8[i]*speedF>>4`, offsets `-bx*3-1 .. -bx-1`), i.e. 4 pages x 28 cols
  x 3 = 336 passes minimum. Three 4x4 puffs cost 3 seeks + 36 passes. Break-even
  would need the new cell under ~16 px, which the geometry makes impossible.
  Measured +216 us at the tightest cell that actually holds the ink (30x30).
- **Idle (b):** the chip (`(fx*9)>>4 - 1`, 3x3) plus the chain dot union to 21x21
  px -> 3 pages. That exactly cancels the one seek saved: 1 seek + 198 passes
  == 2 seeks + 48 passes, so rMx is identical at 22x22 and 24x24. Geometrically
  incapable of a win.
- **Cross-check:** the monhun-ardu-836 ring bake did win (-316 us) only because
  it collapsed **six** partDraw calls (each = record read + blit) into one, i.e.
  it had a 6-seek budget; a single 6-dot ring's ink also needs only 4 pages.
  The trail/idle bakes each have a 1-2 seek budget, which is not enough.

## Option 2 (independently useful, kept out of this bead)

The `fxdata/fxdata.txt` reorder (runtime tables first, sprite `include`s last)
removes the 64 KiB fake-pointer risk the bead flags: `mhEquip` currently sits at
**0x00E3CF = 58,319**, only ~7 KB below the 64 KiB window, and the bead's own
trail/idle sheets would have pushed it past it. With the reorder, all six
`mhWeaponDefs/mhMonsterAttacks/mhMonsterDefs/mhSin65/mhCombat/mhEquip` offsets
land at 0x000000..0x000525. It is a zero-cost structural fix and was verified
here (gen x3 deterministic, device build clean, `test_perf` 5/5, parity
unaffected), but it is not a `z6v` deliverable — worth its own bead.

## Options for the orchestrator

1. **Close z6v as won't-fix / re-scope.** The blitter's seek is cheap relative
   to a wide cell; art bakes only win when they collapse >=4-6 blits into a
   small cell. The trail and flail-idle cases cannot meet that bar.
2. **File the `fxdata.txt` reorder as its own bead** (Option 2 above) — it is
   the one real, safe improvement found while investigating the 64 KiB window.
3. **If a real win is wanted, the change is code, not art:** add a bulk path
   that seeks `fxtrail` once and emits the three puffs from explicit `(w,h,frame)`
   offsets without re-seeking (`drawBasic` already supports the header-less
   form; it needs a safe shared-seek wrapper). This is the actual ~2-seek/frame
   saving and would not touch the goldens.

## Verification performed (on the reverted tree, baseline intact)

```
make gen-check   -> fxdata_manifest: PASS (51 generated artifacts unchanged)
make test        -> Total Passed: 3119   Total Failed: 0
make build       -> Sketch uses 26642 bytes (89%) of program storage
test_perf        -> B pUs=6614 pHz=151 lHz=50 lTk=984 rMx=5496 rAv=5136 ram=424
                    perf_test PASSED=5 FAILED=0
```

(Baseline `make test` 3119/0 and `test_perf` 5/5 confirmed both before any edit
and after the revert, so the revert is byte-clean at HEAD.)

## Deviations / notes

- The reported flash/cart deltas and golden changes from the bead description
  (projectile cases, flail idle/attack cases) were **not** produced: the art
  was reverted, so `player_art_test.hpp` goldens are unchanged and flash/cart
  are at the 26642 B / 123917 B baseline.
- `bd close` was **not** run: the bead's acceptance (rMx improved vs 5496) is
  not met, and the changes that would have met it regress or are neutral.
