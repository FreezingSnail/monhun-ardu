# monhun-ardu-h71 — eqf.4 bake per-facing weapon arcs, drop player trig

Status: DONE — **spike outcome: NOT WORTH IT. No art baked, tree clean at HEAD.**
No commit/push (orchestrator commits). Deliverable for this bead is the
measurement report (bead allows the "not worth it" outcome).

HEAD during the spike: `665db9d perf: bake HUD marker strip, flash 26928 (monhun-ardu-e4a)`.

## Question

Remaining `mulQ4(cos256/sin256)` consumers in the render path were thin: whirl
BALL, player STUN sparkle, monster stun dot, camera SHAKE. Bead context: bake
the ball/stun as 24-phase sheets (like the 836 ring) if the measured saving
justifies it; otherwise report and stop.

## Spike method

Scratch-edit `src/render.hpp` only, one variant per build, `make fxtest-headless
FXTEST_ONLY=test_perf`, read `rMx`. The perf bench scene (`primeHunt`) has the
player simultaneously in `PS_STUN` + `ST_WHIRL` plus a monster stun dot and
hit-flash shake, so all remaining consumers are live on the max frame. Each
build measured in full; `render.hpp` reverted between variants; all numbers
deterministic with the documented baseline `rMx=5388 rAv=5028 pUs=6501`.

## Measured rMx deltas (us, triplane frame)

| variant (only change) | rMx | Δ |
|---|---|---|
| baseline HEAD | 5388 | — |
| whirl BALL trig -> constant (blit kept) | 5348 | **-40** |
| player STUN trig -> constant (blit kept) | 5348 | **-40** |
| BALL+STUN trig -> constant (blits kept) | 5308 | **-80** |
| monster stun dot trig -> constant | 5348 | -40 |
| camera SHAKE trig -> constant | 5360 | -28 |
| all four trig consumers -> constant | 5248 | -140 |
| BALL full draw removed (partRead+8x4 blit+trig) | 5244 | -144 |
| STUN full draw removed (partRead+8x4 blit+trig) | 5276 | -112 |
| BALL+STUN full draw removed | 5124 | -264 |
| whirl RING blit removed (48x32 triplane, already baked) | 5036 | **-352** |

Player path trig alone = 80 us/frame; ball = 40, stun = 40 (≈13 us/plane for
the cos+sin pair, i.e. ~6.7 us per `sin256` cart byte fetch, paid on all 3
planes).

## Why the bake loses (the decisive number)

`partDraw` runs once per plane. A baked phase sheet must be at least the orbit
bounding box, because the blit origin is the fixed player centre reference:

- Ball orbit = `cx ± 20`, `cy ± 14`, cell 8x4 -> baked cell **48x32**.
- One 48x32 triplane sprite blit costs **~352 us/frame for the whole ring draw**
  (measured above: removing the already-baked ring blit saves 352 us). A 48x32
  `drawPlusMaskFX` streams ~1728 cart bytes (6 pages x 48 x mask+data x 3
  planes); area/bytes dominate, not the seek.
- Baking the ball replaces [8x4 blit + 2 trig = ~48 us/plane] with a
  [48x32 blit = ~117 us/plane] -> net **regression ~+200 us/frame**.

Precedent: the 836 ring bake was a win only because it collapsed **6 separate
`partDraw` seeks + 12 `sin256` reads into 1 blit** (7 blits -> 2). The ball and
stun are already single blits — there is no blit to collapse, so the bake only
swaps cheap trig for a much larger streaming blit.

Stun orbit = `cx ± 7`, `cy ± 2` -> baked cell 24x8 (1 page, 3 columns): the
per-part `partRead` seek remains, only the ~13 us/plane of trig is removed, so
the best case is well under the bead's ~50 us bar and would cost 24 art frames
plus quantization error. No measurable case clears the bar; the ball case is
negative.

## Remaining trig consumers in the player/render path (and why)

| consumer | calls/frame | why it stays |
|---|---|---|
| whirl BALL orbit | 2 (cos+sin) | single 8x4 blit; a phase sheet needs a 48x32 cell (~+200 us) |
| player STUN sparkle | 2 | single 8x4 blit; 24x8 bake nets <50 us, not worth the art |
| monster stun dot | 2 | same single-blit shape as the player sparkle |
| camera SHAKE | 2 (sin+cos) | **must stay per-frame**: the mock is `sin(tick*1.7)*shake`, `cos(tick*2.3)*0.7*shake` — a continuous, tick-dependent oscillation over the whole scene; it cannot be baked into a finite sheet without a per-tick table, and it is render-wide (this is stated explicitly per the bead acceptance) |
| whirl RING | 0 | already baked (836) |

`mulQ4` therefore stays in `render.hpp` for the ball/stun/shake/dot consumers.

## Verification (tree-clean branch of acceptance)

Tree clean at HEAD `665db9d` (`git status --short` empty; `git diff` empty).

- `make gen-check`: `fxdata_manifest: PASS (52 generated artifacts unchanged)`.
- `make test`: `Total Passed: 3119  Total Failed: 0`.
- `make test-tools`: `Ran 81 tests ... OK`.
- `make fxtest-headless` (full): all suites PASS —
  `test_assets 254/0`, `test_audio 14/0`, `test_boot 4/0`, `test_combat 195/0`,
  `test_data 221/0`, `test_hud 17/0`, `test_menu 59/0`, `test_parity 660/0`,
  `test_player_art 111/0`, `test_perf 5/0`.
  Perf line vs current baseline: `B pUs=6501 pHz=153 lHz=51 lTk=984 rMx=5388
  rAv=5028 ram=418` (matches `rMx=5388 rAv=5028 pUs=6501`).
- `make size`: `flash=26928/29696 (2768 free)  ram=2018/2560` — unchanged vs
  the 26928 HEAD baseline (no art, no code delta).

## Deviations / notes

- No source, art, data, generated file, or test changed. No goldens touched.
- Spike edits were reverted; every build in the table used the same toolchain
  and the baseline was re-confirmed after revert.
- Did not commit, stage, or push.
