# monhun-ardu-feel.1 — Wave-B engine add budget spike

Baseline: HEAD `44a5596`, clean tree. All numbers are whole-image deltas
measured with `make size` (LTO; per-symbol math meaningless). Every prototype
was reverted; end tree is clean and `make test` is green (5391 passed).

## Baseline (measured first)

```
size: .text=26940 .data=40 .bss=1692
size: flash=26980/29696 (2716 free)  ram=1732/2560
size: data facts: HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false HAS_GUARD_HP:false
  HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true HAS_HIT_STAGGER:false HAS_MULTI_STEP:false
  HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false HAS_STAGGER:true HAS_STEP_AFTER:false
  HAS_STEP_CHANCE:false HAS_WAIT_STEPS:false HAS_ZONES:true
```

Perf baseline `make fxtest-headless FXTEST_ONLY=test_perf` (Ardens at
`/Users/connorfranc/code/Ardens/.../Ardens`, present):

```
B pUs=6372 pHz=156 lHz=52 lTk=452 rMx=4772 rAv=4587 ram=595
perf_test PASSED=5 FAILED=0
```
Render-max gate floor is `1000000/135 = 7407 us`; baseline rMx 4772 leaves
2635 us of headroom.

## Per-item results

| item | flash delta B | RAM delta B | notes / risk |
| --- | --- | --- | --- |
| 1. behind guard (`facing` clause) | **+160** | +0 | GUARD_SIZE 8→9 (cart +9 B); evaluator adds `combatGuardFacingOk` + behind dot. Under 400. |
| 2. wallStun (attack field + clamp) | **+134** | +1 | ATTACK_SIZE 22→23; CombatAttackCache +1 B. Detection folds on `HAS_ATTACK_WALLSTUN`. |
| 3. tell shapes DOT/LINE/ARC/RING | **+590** ⚠ | +1 | OVER 400. ATTACK_SIZE 22→23; CombatAttackCache +1 B; render switch. Perf rMx +148 us. |
| 4. enrage (hpPct/spdMul/faceHold/cue) | **+170** | +5 | CREATURE_SIZE 25→29; 5 cache bytes incl latch. cue stored only (audio wiring not measured). |
| 5. hop (`move.type hop`) | **+82** | +2 | ATTACK_SIZE unchanged (dx/dy already packed); cache +2 B for dx/dy. Cheapest. |

Engine-item total: **+1136 B flash / +9 B RAM**. Free budget 2716 B → fits with
~1580 B left, but item 3 alone is 52% of the total.

### Item 3 perf (prototype compiled into the perf image)

```
B pUs=6374 pHz=156 lHz=52 lTk=452 rMx=4920 rAv=4735 ram=592
perf_test PASSED=5 FAILED=0
```
Deltas vs baseline: pUs +2 us, pHz 0, lHz 0, lTk 0, **rMx +148 us**, rAv +148 us,
free ram −3 B. rMx 4920 is 2487 us below the 7407 floor → **no perf-gate risk**.
The bench's worst plane happened to draw a tell; the delta is one shape's draw.

## Pattern step facts (chicken combo data)

Each fact measured in isolation from lunge's `p_peck` pattern, then all four
together. All are RAM-neutral.

| data pattern | flash delta B | facts flipped |
| --- | --- | --- |
| 2 steps, after 0, chance 100 | **+98** | HAS_MULTI_STEP |
| 1 step, after 6 | **+124** | HAS_STEP_AFTER |
| 1 step, chance 50 | **+246** | HAS_STEP_CHANCE |
| 1 WAIT step | **+156** | HAS_WAIT_STEPS |
| 2 steps + after + chance + WAIT | **+306** | all four |

Individual costs sum to 624 B because each forces the generic runner on its
own; combined they share the runner, so the real combo price is **+306 B**.
`HAS_STEP_CHANCE` is the heavyweight (pulls in `combatChanceRoll`'s 16-bit
hash). All under the ~400 B flag individually except none; combo 306 < 400.

## Recommended implement order (feel-per-byte, lowest risk first)

1. **hop** (+82 / +2 RAM) — cheapest, no render, no perf.
2. **behind guard** (+160 / +0) — biggest positional lever, no RAM; only cart
   blob growth (GUARD_SIZE 8→9).
3. **wallStun** (+134 / +1) — small, folds cleanly on its fact.
4. **enrage** (+170 / +5) — moderate RAM; audio cue wiring still to budget.
5. **chicken step combo data** (+306 / +0) — do as its own data bead
   (`monhun-ardu-feel.8`); ships the generic runner.
6. **tell shapes** (+590 / +1, +148 us render) — last and split. Drawing only
   DOT+LINE (~2 arms) or reusing the existing debug `wireDot` rect for RING
   should cut the 590 B materially; ARC is the most bespoke arm. Flagged >400.

## Risk/flag notes

- **>400 B**: only tell shapes (+590). Everything else ≤170.
- **RAM**: total engine adds 9 B; enrage is 5 of it. CombatState static asserts
  shift for every attack-field add (83→84→85...) — update them in each bead.
- **Two-pass `make gen`**: any packed-record size change (GUARD 8→9, ATTACK
  22→23, CREATURE 25→29) shifts the FX image and the generated room/equip image
  offsets, so **`make gen` must run twice** (first pass moves `mh_map_area`,
  second converges `zone_meta.hpp`). A single pass fails the
  `zone blob stale` / `equip blob stale` static asserts. Confirmed manually.
- **Cart blob growth** (not flash): GUARD_SIZE 8→9 (+9 B all guards),
  ATTACK_SIZE 22→23 (+1 B/attack, 8 attacks = +8 B), CREATURE_SIZE 25→29
  (+4 B/creature, 8 = +32 B). Blob still far under the 64 KB pointer bound.
- **enrage cue** was stored as a byte but no audio path was compiled in; if the
  cue needs `audioUpdate` detection, budget that separately.
- **wallStun/hop MOVE_CHARGE/MOVE_HOP handling**: prototypes also taught
  `startMonsterAttack` charge/hop velocity branches; folded on their facts so
  baseline is byte-identical.

## Verification tails

```
# item 3 (largest)
size: .text=27530 .data=40 .bss=1693
size: flash=27570/29696 (2126 free)  ram=1733/2560
# perf
B pUs=6374 pHz=156 lHz=52 lTk=452 rMx=4920 rAv=4735 ram=592
perf_test PASSED=5 FAILED=0
# combined step facts
size: flash=27286/29696 (2410 free)  ram=1732/2560
# final clean baseline
size: flash=26980/29696 (2716 free)  ram=1732/2560
make test -> Total Passed: 5391  Total Failed: 0
git status --short -> (empty)
```

All prototypes reverted; no code committed. Ardens was available, so the item-3
perf numbers above are real device-model captures (not N/A).
