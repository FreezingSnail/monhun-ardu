# monhun-ardu-prg.1 — Spike: reclaim budget for the progression wave

HEAD at start: `be9262a` (feel.23), clean tree. No commit/push (orchestrator
commits). End state: clean tree, `make test` green.

Baseline (`make size`):

```
size: .text=28548 .data=28 .bss=1599
size: flash=28576/29696 (1120 free)  ram=1627/2560
size: data facts: HAS_ENRAGE:true HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false
  HAS_GUARD_FACING:true HAS_GUARD_HP:true HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true
  HAS_HIT_STAGGER:false HAS_MULTI_STEP:true HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false
  HAS_STAGGER:true HAS_STEP_AFTER:true HAS_STEP_CHANCE:true HAS_TURN_RATE:true
  HAS_WAIT_STEPS:true HAS_ZONES:true
```

Baseline device perf (`make fxtest-headless FXTEST_ONLY=test_perf`):

```
B pUs=6366 pHz=157 lHz=52 lTk=452 rMx=4768 rAv=4540 ram=686
perf_test PASSED=5 FAILED=0
```

## Method

Every candidate was built as a whole shipping image
(`arduino-cli compile --fqbn arduboy-homemade:avr:arduboy-fx --optimize-for-debug`
with the `SIZE_FLAGS` `-mcall-prologues -mrelax -DMH_NO_USB`), one at a time,
`avr-size` on the ELF, then reverted. `flash = .text + .data`. LTO means only
whole-image deltas are meaningful; all deltas below are vs the 28576 baseline.
Candidates (a)/(b)/(c) and the existing carve flags were measured by passing
`-D` only (no source edit). (d)/(e)/(f)/(g) were prototyped behind a temporary
flag or local edit and reverted.

## Measured table

| Candidate | flag | flash | Δflash | RAM | ΔRAM | perf | what it removes | risk |
|---|---|---|---|---|---|---|---|---|
| (a) charge attacks + shells | `MH_CHARGE=0` | 28144 | **-432** | 1627 | 0 | — | held-A charge stance, `startChargeAttack`/`fireChargeShot`, charge data reads (ynb) | **HIGH — shipped combat feature (charge).** |
| (b) stage-3 finisher | `MH_STAGE3=0` | 28458 | **-118** | 1627 | 0 | — | `finWin` + stage-3 branch (B after a finisher) | **HIGH — combo finisher branch.** |
| (c) direction+A / roll-attack | `MH_ROLL_ALT=0` | 28480 | **-96** | 1627 | 0 | — | dir+A opener + A-out-of-evade roll attack (8xx) | **HIGH — shipped opener/roll attack.** |
| (d) rare audio cues | local trim | 28486 | **-90** | 1625 | -2 | n/a¹ | `CUE_BREAK`/`CUE_GATHER`/`CUE_EAT` + their edge detect (`AudioState.itemHerb`/`poleBroken`) | LOW — audio only; gather/eat verbs stay. |
| (e) training mode | `MH_TRAIN=0` | 27972 | **-604** | 1627 | 0 | — | `MODE_TRAIN`, pole target/`initPole`/`updatePole`/`armPoleTarget`/`damagePole`, `drawPole`, pole-room path; **menu POLE row disappears** | MED — removes training mode + pole; not in the progression scope. |
| (f) menu v2 sel-tile fold | local edit | 28560 | **-16** | 1627 | 0 | n/a² | two FX sel-tile blits replaced by a 1 px underline; menu art stays on the cart | NEGLIGIBLE — no reclaim; look loss. |
| (g) cosmetic attack overlays | `MH_FX_OVERLAY=0` | 27946 | **-630** | 1627 | 0 | — | bespoke chicken/bull/heavy attack pose sheets (`fxchickenatk`/`fxbullatk`/`fxtailspin` body); falls back to the generic beast sheet | LOW — cosmetic only, no sim change. |
| (g) telegraph shapes | `MH_FX_TELL=0` | 28160 | **-416** | 1627 | 0 | — | LINE/ARC/RING/ZONE windup telegraph shapes (legacy 2x2 core kept) | **MED-HIGH — breaks the documented "telegraph == hit-test window" contract** (`docs/dev-flow.md`). |
| (g) projectile trails | `MH_FX_TRAIL=0` | 28362 | **-214** | 1627 | 0 | — | 3 trail puffs per shot | LOW — cosmetic only. |
| (g) ground dots | `MH_FX_GROUND=0` | 28418 | **-158** | 1627 | 0 | — | procedural dot field (border kept) | LOW — cosmetic only. |
| (g) screen shake | `MH_FX_SHAKE=0` | 28436 | **-140** | 1627 | 0 | — | tick-derived view shake on hit flash | LOW — cosmetic only. |
| (g) damage numbers | `MH_FX_NUMBERS=0` | 28496 | **-80** | 1627 | 0 | — | rising damage-number text (sparks kept) | LOW — cosmetic only. |
| (g) all audio mute | `MH_AUDIO=0` | 28240 | **-336** | 1621 | -6 | — | whole beeper (cue table + ISR + `audioPlay`) | MED — removes all sound. |
| existing sheathe | `MH_SHEATHE=0` | 28520 | **-56** | 1627 | 0 | — | sheathe/stow (feel.17) | HIGH — shipped feature. |
| existing B-branch buffer | `MH_B_BRANCH_BUFFER=0` | 28418 | **-158** | 1627 | 0 | — | B branch tap buffer through recovery/lock | MED — input feel. |
| existing push-move | `MH_PUSH_MOVE=0` | 28458 | **-118** | 1627 | 0 | — | per-tick player-move flag (push-rule bug fix) | MED — reintroduces a body-push bug. |
| existing combat parts | `MH_COMBAT_PARTS=0` | 26692 | **-1884** | 1627 | 0 | — | breakable zones, multi-window attacks, stagger, zones-guard, part overlays | **HIGH — removes a shipped combat feature (ljj.6); not in the progression scope but a real gameplay regression.** |
| existing room bounds | `MH_ROOM_BOUNDS=0` | 26836 | **-1740** | 1627 | 0 | — | entire room runtime: camp, doors, heal, props, fade, **gather nodes**, pole room | **DO NOT SHIP — kills gather + the camp→area path + smith reachability (prg.4/prg.6/prg.7).** |

¹ (d) is audio edge-detect only; `test_perf` compiles `MH_AUDIO 0`, so the beeper
is already excluded there and the cue trim is a strict subtraction inside
`audioUpdate` — no perf regression is possible. ² (f) touches `drawMenu`, which
`test_perf` never renders (it renders `renderScene`).

### Combined sets (measured, exact)

| Set | flags | flash | Δflash | free | RAM |
|---|---|---|---|---|---|
| recA — visual only | `MH_FX_OVERLAY=0 MH_FX_NUMBERS=0 MH_FX_TRAIL=0 MH_FX_GROUND=0 MH_FX_SHAKE=0` | 27382 | **-1194** | 2314 | 1627 |
| recB — recA + training | recA `+ MH_TRAIN=0` | 26764 | **-1812** | 2932 | 1627 |
| recC — recB + telegraphs | recB `+ MH_FX_TELL=0` | 26342 | **-2234** | 3354 | 1627 |
| all six cosmetic | overlay+numbers+trail+tell+ground+shake | 26940 | **-1636** | 2756 | 1627 |

Perf for the render carve (all six cosmetic flags, `test_perf`):

```
B pUs=6330 pHz=157 lHz=52 lTk=452 rMx=2584 rAv=2173 ram=682
perf_test PASSED=5 FAILED=0
```

The carve is a strict render-work subtraction: `rMx` 4768 -> 2584 us, `rAv`
4540 -> 2173 us (budget 7407). All gates still pass (pHz 157>=135, lHz 52>=45,
ram 682>=300).

## Recommendation

**Primary — recB, `-1812 B` (flash 26764, 2932 free; RAM unchanged).** This is
the least gameplay loss for >=1.5 KB: it removes only render polish (bespoke
attack-pose art, projectile trails, ground dot field, screen shake, damage
numbers) and training mode. No sim/combat behavior changes; the
telegraph==hit-window contract is preserved. Flag: the **menu POLE row and
training mode are removed** — training is not part of the progression wave, but
it is a shipped demo extra the owner may want to keep.

**If more headroom is needed — recC, `-2234 B` (3354 free).** Adds the
telegraph-shape fold; flag that this breaks the documented
"telegraph == hit-test window" readability contract (LINE/ARC/RING/ZONE tells
collapse to the legacy 2x2 core), so it needs an explicit owner/design sign-off.

**Big hammer — `MH_COMBAT_PARTS=0`, `-1884 B` alone.** Clears the low end of the
2-4 KB wave in one flag, but removes breakable parts, stagger, multi-window
attacks and the part overlays (ljj.6). Not one of the wave's features, but a
shipped combat regression — recommend only if the wave truly needs ~4 KB, and
pair it with recA rather than recB to avoid also losing training.

**Do not use:** `MH_ROOM_BOUNDS=0` (removes gather nodes, camp/doors and the
smith path — directly conflicts with prg.4/prg.6/prg.7), and the charge / stage3
/ roll-alt / sheathe carves (all shipped combat features; collectively only
-702 B for a large gameplay loss).

If the wave needs the full ~4 KB: recB (-1812) + `MH_COMBAT_PARTS=0` (-1884)
= ~-3696 measured independently; that combination was not built as one image in
this spike, so budget the interaction with a follow-up `make size` before
committing to it.

## Notes

- All prototypes reverted; `git status --short` empty at end.
- `make test` re-run on the clean baseline: **Total Passed: 6032 / Total Failed: 0**.
- `make size` baseline unchanged: flash 28576/29696 (1120 free), ram 1627/2560.
- No generated artifacts touched; `output.md` is the only tree change.
