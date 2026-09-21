# monhun-ardu-prg.10 — Spike: charge-lite + animation tells + cue trim budget

HEAD at start: `2d5040d` (prg.7), clean tree. No commit/push (orchestrator
commits). End state: clean tree, `make test` green, all prototypes reverted.

Method: each candidate is a whole-image `make build` + `avr-size` (LTO makes
per-symbol math meaningless), one prototype at a time, reverted before the
next. Deltas are vs the baseline below. Render-touching variants also get a
`test_perf` line. Flag-only candidates were measured by passing `-D` through
`SIZE_FLAGS` (no source edit).

## Baseline (`make size`)

```
size: .text=29116 .data=20 .bss=1590
size: flash=29136/29696 (560 free)  ram=1610/2560
```

Baseline perf (`make fxtest-headless FXTEST_ONLY=test_perf`):

```
B pUs=6348 pHz=157 lHz=52 lTk=476 rMx=3344 rAv=3074 ram=709
perf_test PASSED=5 FAILED=0
```

## Measured table (all vs 29136/1610)

| Candidate | flash | Δflash | RAM | ΔRAM | perf (rMx/rAv) | what it removes | risk |
|---|---|---|---|---|---|---|---|
| **a. charge-lite** | 28918 | **−218** | 1610 | 0 | — | L2 melee charge (`weaponCharge(def,1)`), `fireChargeShot` + `fireChargeShells` callsite, charged-ball spawn branch (`shot >= 3`), L2 bar state (bar now fills to `CHARGE_MIN`, shade 2) | MED — flail loses chargeslam2 (L1 chargeslam1 + all normal attacks stay); gun loses the charged ball entirely (its melee `charge` slots are already zero, so its ONLY charge was the ball → gun has no charge after this). Sword never charged. |
| **b1. tell→animation** | 28766 | **−370** | 1610 | 0 | 3348 / 3075 | `drawMonsterTell` LINE/ARC/RING/ZONE shapes + `tellOutline` + all `render_math` tell helpers; the per-attack windup frame byte (`combat.attack.tell`, already in the record) now selects the beast windup frame, 0 keeps the generic coil; legacy 2x2 shade-2 core + 4x4 shade-3 attack marker kept | MED — trades the procedural shape for an animation frame; needs authored per-attack windup art (FX cart = free MCU flash). **Contact-sheet review must check the windup pose matches the window class** (line-lunge reads as a thrust, ring-stomp as a windup curl, etc.). |
| **b2. RING-only tells** | 28910 | **−226** | 1610 | 0 | 3348 / 3075 | LINE/ARC/ZONE shapes + their `render_math` helpers; RING kept for the stomp shock class + core marker (tell types stay in the record) | MED-LOW — smaller reclaim than b1 but keeps the expanding shock ring; still breaks "telegraph == hit window" for the other classes. |
| **c. rare audio cues** | 29136 | **0** | 1610 | 0 | — | (already removed in prg.8: `CUE_BREAK`/`CUE_GATHER`/`CUE_EAT`/`CUE_WINDUP`, `mhCueTable` 14→9 rows) | n/a — nothing left to cut |
| **d1. B-branch buffer** (flag) | 28996 | **−140** | 1610 | 0 | — | `B_BRANCH_BUFFER_ENABLED` input path (tap-B through recovery/lock) | MED — input feel: loose A A B stops comboing through a gap |
| **d2. push-move flag** (flag) | 29014 | **−122** | 1610 | 0 | — | `PUSH_MOVE_ENABLED` per-tick player-move flag (`playerMoved`) | MED — reintroduces the body-push give-way bug |
| **d4. hub/quests shelf fold** | 28470 | **−666** | 1601 | **−9** | — | unreachable hub/quests graph from the sketch: `appScreenAccept` hub switch + `appScreenBack` hub level, the `.ino` screen `nav` block, `questApplyToGame` boot arm (its `questTarget/Need/Progress` now stay at the `newGame` defaults) | MED — demo-path-neutral (sketch already never activates hub/quests; gear/tier/items boot arm stays). Quest kill-tracking is inert while no quest is active. Tests referencing the shelf must be updated; `quest_state`/`quest_meta` stay for a future re-enable. `APP_NAV_HUB`+`MENU_UI` toggle left intact (menu-toggle idea) to avoid shifting `MenuState`/`appNavApply`. |

## Combined sets (measured, exact)

| Set | flags / edits | flash | Δflash | free | RAM |
|---|---|---|---|---|---|
| (a)+(b1) | charge-lite + tell→anim | 28560 | **−576** | 1136 | 1610 |
| (a)+(b1)+(d1)+(d2) | + B-branch buffer + push-move | 28302 | **−834** | 1394 | 1610 |
| (a)+(b1)+d4 | + hub/quests shelf fold | 28212 | **−924** | 1484 | 1601 |
| (a)+(b1)+(d1)+(d2)+d4 | full recommended set | 28212 | **−924** | 1484 | 1601 |

Perf for the full recommended set (`test_perf`):

```
B pUs=6348 pHz=157 lHz=52 lTk=480 rMx=3348 rAv=3075 ram=710
perf_test PASSED=5 FAILED=0
```

Render carve is a strict subtraction: `rMx` 3344→3348 (noise; tell shapes were
cheap), `rAv` 3074→3075, all gates pass (pHz 157≥135, lHz 52≥45, ram 710≥300).

## Recommendation

**Recommended set: (a)+(b1)+d4 = −924 B** (flash 28212, **1484 free**; RAM 1601,
−9). Clears the ≥800 B target with overlays / charge-existence / gather / drops
/ smith intact.

- **(a) charge-lite** (−218): keeps a single-level melee charge for the flail
  (chargeslam1) and drops only the L2 tier + the gun charged ball. If the owner
  wants the gun's charge back, use **b1+d4 (−706)** and fund the gun charge from
  the `MH_TRAIN`/cosmetic pool prg.8 already knows.
- **(b1) tell→animation** (−370): matches the owner constraint (tell readability
  in beast art, FX cart art free). Requires authored per-attack windup frames
  before shipping; **contact-sheet review must confirm each windup pose matches
  its window class** (dev-flow: telegraph == hit-test window). `b2` (−226) is
  the lower-risk fallback if the art is not ready.
- **d4 hub/quests fold** (−666, −9 RAM): removes unreachable demo-path weight;
  the only behavioural change is the inert quest kill-arm. Flips no `HAS_*` fact.

**If input-feel risk is acceptable**: add **d1+d2 (−262)** → full set −924 B but
drops B-branch buffering (combo feel) and reintroduces the push bug; not
recommended for a demo where combat feel is the point.

## Notes

- All prototypes reverted; `git status --short` empty at end.
- `make test` on the clean baseline: **Total Passed: 6060 / Total Failed: 0**.
- `make size` baseline unchanged: flash 29136/29696 (560 free), ram 1610/2560.
- Candidate (c) is already gone (prg.8); no further rare-cue reclaim exists.
- No generated artifacts touched; `output.md` is the only tree change.
