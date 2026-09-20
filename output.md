# monhun-ardu-fie.14 — Spike: global Game, drop `Game &` params (flash)

Baseline HEAD `e666bdb`, tree clean. Baseline shipping **26980/29696 (2716
free), RAM 1732/2560** (`.text` 26940, `.data` 40, `.bss` 1692; the global
`g` symbol is 602 B). Measurement only — no shipped change, no commit/push.

## Verdict

**No bytes on the table.** Dropping the `Game &g` parameter is flash-neutral to
slightly negative; adding the hidden-base barrier to global access is a clear
regression.

- **Variant A (param dropped, existing addressing kept): 26992 → +12 B.**
- **Variant B (A + hidden-base barrier on the global): 27282 → +302 B.**

Hypothesis confirmed: LTO already constant-propagates the single global ref to
the known absolute address, so the parameter is free; removing it only perturbs
(spill/frame) allocation. Global-only does **not** remove the 4-byte `lds`/`sts`
— it makes them more likely.

## Method / scratch

Scratch worktree `build/spike-global/monhun-ardu` (nested, detached at HEAD;
`Arduboy-Python-Utilities` symlinked because it is gitignored), shipping flags
via `make build` (`-mcall-prologues -mrelax -DMH_NO_USB`), sizes from `make
build` + `avr-size -A` + `avr-nm -S --size-sort`. Variant patches and ELFs were
kept under `build/spike-global/` during the run; worktree + scratch removed at
the end.

Converted representative set (the highest-access / highest-call-count sim
functions), one global named `g` declared `extern mh::Game g;` at global scope
in `src/core/game.hpp` (definition already exists in `monhun-ardu.ino`):

`updateCamera`, `loadRoom`, `initGame`, `initMonster`, `updatePlayer`,
`updateMonster`, `stepWorldBody` — every caller (`stepGame`, `newGame`,
`updateDoors`, `stepPlayer`, `stepMonster`, `stepHunt`, `stepWorld`,
`menuStart`) updated to drop the argument.

## Variant A — parameter removal alone (+12 B)

Kept the existing hidden-base pointers the code already had
(`updateCamera`/`loadRoom`/`initMonster`/`updateMonster`/`updatePlayer`'s `pp`);
changed only the signatures/argument lists.

| symbol | baseline | A | delta |
|---|---:|---:|---:|
| `.text` (whole image) | 26940 | 26952 | **+12** |
| `updatePlayer` | 3710 | 3744 | +34 |
| `updateMonster` | 2416 | 2402 | -14 |
| `initGame` | 290 | 290 | 0 |
| `loadRoom` | 268 | 268 | 0 |
| `updateCamera` | 122 | 122 | 0 |
| `main` | 9408 | 9408 | 0 |

`initMonster` and `stepWorldBody` do not get their own symbol (inlined). The
tracked functions sum to +20; the remaining -8 sits in renamed LTO clones.

**Why ~0:** the baseline `updatePlayer` prologue already emits
`lds r20, 0x056D <g+0x3>` for `g.weapon` although `g` arrives in `r24:r25` —
LTO has const-propagated the only caller's argument to the known global. The
function bodies are otherwise near byte-identical; the only change is the
prologue/`__prologue_saves__` frame (`ldi r30,0x2C/r31,0x0F` →
`ldi r30,0x3F/r31,0x15`) because freeing `r24:r25` reassigns spills. So the
parameter costs nothing to pass and nothing to drop.

## Variant B — + hidden-base on global access (+302 B)

Added `Game *gp = &g; __asm__("" : "+r"(gp));` (and `gp->` addressing) at the
top of the functions that still had direct global access (`initGame`,
`stepWorldBody`, plus the residual direct `g.` reads in `updatePlayer`);
functions that already carried a base pointer were unchanged.

| symbol | baseline | A | B | B-base |
|---|---:|---:|---:|---:|
| `.text` (whole image) | 26940 | 26952 | 27242 | **+302** |
| `initGame` | 290 | 290 | 312 | +22 |
| `updatePlayer` | 3710 | 3744 | 3782 | +72 |
| `updateMonster` | 2416 | 2402 | 2406 | -10 |
| `main` | 9408 | 9408 | 9572 | **+164** |

`main` +164 is the dominant cost: `stepWorldBody` is inlined into the loop, and
hoisting a base pointer + keeping it live across its many calls forces extra
spills. `initGame` +22 is pure base maintenance (4 B `ldi` + spills) for a
one-shot function. The existing per-site barriers only pay off where a function
is access-dense and self-contained (fie.13's measured -208/-34); applying the
barrier globally inverts that.

## Where the bytes come from, and full-conversion extrapolation

- **Call sequences:** passing `&g` was already free — LTO knows the single
  global address, so callers either had it in a register or materialise it once.
  Removing the argument saves nothing and can force the callee to re-derive it.
- **`lds`/`sts`:** global-only access does not eliminate 4-byte absolute
  accesses; it removes the ability to reuse a live base register. Re-adding the
  base costs `ldi`+`ldi` (4 B) plus register pressure/spills.
- **LTO clones:** the conversion renames clones (`.constprop.80` → a plain
  symbol) but does not multiply them; no clone explosion.
- **Extrapolation:** the converted set already contains the largest sim bodies
  (`updatePlayer` 3.7 KB, `updateMonster` 2.4 KB) and every function that
  inlines into `main`, i.e. the bulk of all call sites. The remaining ~74
  non-const `Game &` sites are low-access helpers (attack/projectile/combat
  helpers, callbacks) where parameter removal showed no measurable gain here.
  Expected full-image delta: **0 to +30 B (a small cost), never a saving.** Do
  not pursue global-only.

## Landing cost (why not anyway)

A global-only core means no function can be handed a different `Game`:

- **138** call sites in `tst/` + `tst/fxdatatest/` call the converted set;
  a full conversion touches all ~81 non-const `Game &` signatures and cascades
  to more call sites.
- **181** local `Game` declarations in tests, including **15 multi-instance**
  cases (`g2`/`g3`/`h`/`w`/`a`/`u`/`t`) across 8 tests — `player_test`
  independence checks, `shells_test` g/g2/g3, `monster_test` g2/g3,
  `zone_test` u/a, `fxdatatest/boot_test` `mh::Game w`. One global breaks
  simultaneous independent worlds; tests would need a reset/instance strategy
  (`g = Game{}` copy-in/out, or a test-only active-pointer redirect).
- Zero flash benefit to pay for that: the `Game &` parameter is free after LTO,
  so it is pure testability with no budget cost.

## Verification

- Baseline reproduced in the worktree: `make build` → 26980.
- A reproduced twice (reset + re-apply patch): 26992 both times.
- Scratch worktree removed (`git worktree list` no longer shows it);
  `build/spike-global/` deleted.
- Main tree clean after (`git status --short` empty, HEAD `e666bdb`).
- `make gen-check`: **PASS (82 generated artifacts unchanged)**.
- No `/tmp`, no float, no commit/push.
