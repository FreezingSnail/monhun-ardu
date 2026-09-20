# monhun-ardu-fie.13 — trim: finish the Kiro byte-savings sweep

Baseline HEAD `f0ac4f2`, tree clean before. Final: **flash 26980/29696 (2716
free), RAM 1732/2560**. Net additional reclaim vs the 27188 baseline: **-208 B**.
`make test` 5391/0, full `make fxtest-headless` 17/17 (log
`build/fxtest13.log`), `make gen-check` PASS (82 generated artifacts
unchanged). No behavior change intended; host + device suites green. No
commit/push.

## Method / deviations

- One candidate at a time, whole-image `make size` delta, keep only wins,
  revert losers. Scratch drivers in gitignored `build/`:
  `gp2.awk` (portable hidden-register-base rewrite; Kiro's `gp.awk` used the
  BSD-awk-unsupported `[[:<:]]` class and silently failed), `try_gp.sh`,
  `try_ni.sh`; fresh disassembly `build/dis13.txt`, `build/disdl13.txt`.
- LTO makes per-symbol arithmetic unreliable; every number below is a
  whole-image `.text + .data` delta.
- Host `make test` run after the first kept batch and again at the end; the
  full device gate ran once at the end. `make size` re-run after `gen-check`.
- No `/tmp`, no Python harness, no generated-file hand edits.

## Kept candidates (final diff, 7 files)

| Candidate | File | Kind | Delta |
|---|---|---|---|
| `applyDrift` sub-pixel carry via `fpCarry` (drops `tdiv` + `%FP`) | `player.hpp` | narrowing/reuse | **-52** |
| `tapDefense` shove `mdx/mdy` int32 -> int16 (squares still int32) | `player.hpp` | narrowing | **-54** |
| `drawPlayer` reach products `(int32_t)p.fx*reach` -> int16 | `render.hpp` | narrowing | **-34** |
| `initMonster` hidden register base | `monster.hpp` | base ptr | **-44** |
| `updateCamera` hidden register base | `world.hpp` | base ptr | **-8** |
| `loadRoom` hidden register base | `world.hpp` | base ptr | **-2** |
| `beginAttack` hidden register base | `player.hpp` | base ptr | **-2** |
| `mhPgmReadU8` noinline | `progmem.hpp` | noinline | **-4** |
| `attackWindowLoad` noinline (cold cart-load path) | `combat.hpp` | noinline | **-4** |
| `shellPellets` noinline | `game.hpp` | noinline | **-4** |

Kept total -208 B (measured 27188 -> 26980, matches the sum of the
independently-measured deltas).

## Reverted candidates (measured, then reverted)

### Base-pointer sweep (hidden register base, `gp2.awk`)

| Function | Delta | Result |
|---|---|---|
| `newGame` | +10 (neutral after `initMonster` split) | revert |
| `creatureLoad` | +18 | revert |
| `initGame` | +22 | revert |
| `tryBranch` | +2 | revert |
| `patternStepsSingle` | +16 | revert |
| `syncMonsterTarget` | +18 | revert |
| `initPoleKind` | +6 | revert |
| `updateActiveTarget` | +4 | revert |
| `armPoleTarget` | +12 | revert |
| `startAttack` | +26 | revert |
| `initWorld` | +32 | revert |
| `updateEffects` | +18 | revert |
| `updatePole` | +26 | revert |
| `spawnShot` | +82 | revert |
| `combatZoneHitResolveAt` | +2 | revert |
| `monsterOnHit` | +10 | revert |
| `playerHurt` | +166 | revert |
| `addEffect` / `withWeapon` / `resetHunt` | 0 | revert |
| `drawMonster` / `drawPlayer` / `drawHud` / `drawPole` / `drawProps` / `drawEffects` / `renderScene` (const base) | +168/+126/+60/+58/+26/+32/+32 | revert |
| `drawProjectiles` / `drawDebug` / `drawRoom` (const base) | +2/0/0 | revert |
| `appNavApply` / `menuStart` / `screenReset` | no direct `g.` access; not applied | n/a |

Only cold functions with many direct `g.` writes win; warm/draw paths lose to
register pressure (matches Kiro's `drawPlayer` note). `creatureLoad` /
`initGame` / `newGame` lose because the win is eaten by base maintenance and
caller inlining.

### Noinline sweep

| Function | Delta | Result |
|---|---|---|
| `cos256` | -2 | revert (hot render math; not worth the call) |
| `partSheet` | +2 | revert |
| `mhFxReadI16` | +4 | revert |
| `combatReadBytes` | +6 | revert |
| `shellReload` / `shellStam` | +6 | revert |
| `bodyRect` | +64 | revert |
| `hudBlk` | +38 | revert |
| `combatMulPercent` | +16 | revert |
| `attackStartup` | +4 | revert |
| `attackActive` / `attackReach` / `combatReadU8` / `mhFxReadU8` | 0 | revert |
| `questTakeable` | +30 | revert |
| `questReady` | +54 | revert |
| `weaponHasChargeShells` | +8 | revert |
| `weaponCanCancel` | +4 | revert |
| `combatCreatureFirstAttack` | +10 | revert |
| `saveChecksum` | +48 | revert |
| `screenRowNext` | +6 | revert |
| `weaponId` / `shellCount` | +4 | revert |
| `mhFxReadBool` | +10 | revert |
| `combatFacingLockV` | +18 | revert |
| `mhPgmReadI16` | +4 | revert |
| `saveQuestBit` | +24 | revert |
| `questDone` | +60 | revert |

LTO already inlines the small helpers well; forcing them out-of-line grows the
image except for the three accessors kept above.

### Narrowing / other

| Candidate | Delta | Result |
|---|---|---|
| `drawPlayer` ANG `uint32_t` -> `uint16_t` (low byte identical) | 0 | revert |
| `updateMonster` delta `dx/dy` int32 -> int16 (isqrt product stays int32) | 0 | revert |
| `drawMonster` windup flash `% 2` -> `& 1` | 0 | revert (GCC already optimises power-of-two `%`) |
| `monster` hpPct `(uint32_t)hp*100/hpMax` narrowing | not applied | no provable 16-bit bound (hp*100 can exceed 65535) |

## Remaining 32-bit helper call sites (callers.awk, post-change)

- `main` `__divmodhi4` x7, `__udivmodhi4` x1, `__divmodsi4` x1: constant/runtime
  divisions inside inlined render (charge-bar `/20`, effect `/3`, bar-fill
  `/u16den`/`/u16rmax`). Not power-of-two and not provably narrowable; left.
- `updateMonster` `__mulhisi3` x2 (the `isqrt(dx*dx+dy*dy)` products, already
  the cheapest 16x16->32 form) and `__divmodhi4` x2 + `__udivmodhi4` x1
  (`% jitter`, `% 2`, `/ circleDen`); `% 2` is already optimised, the others are
  genuine runtime moduli.
- `updatePlayer` `__mulhisi3` x2: the same `isqrt` product.
- `combatZoneHitResolveAt` `__udivmodsi4` x2 / `__muluhisi3` / `__mulsi3`:
  uint32 percent scaling; `base` is clamped to 0xFFFF, so a 16-bit product is
  not provably safe.

## Perf (test_perf, whole-image)

`pUs 6371 -> 6372`, `pHz 156`, `lHz 52`, `lTk 452`, `rMx 4768 -> 4772`,
`rAv 4582 -> 4587`, `ram 595`. The noinline accessors cost ~5 us/render pass
well inside the gate.

## Conclusion

Net -208 B with all gates green. The base-pointer and noinline wells are dry
beyond the kept list (documented above with measured losses); the remaining
reclaimable bytes are the runtime constant divisions in render, which are not
provably narrowable without a behavior change.
