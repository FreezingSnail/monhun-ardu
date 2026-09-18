# monhun-ardu-46g — trim: narrow int32 hot-path args (blkClamp, zone helpers)

Status: **DONE** (all gates green; no commit per worker protocol).
**Net flash recovery 822 B** vs 27690: **27690 -> 26868** (2006 -> 2828 free).
Target 200-400 B: exceeded. Parity fixtures / art / hud goldens unchanged (no regen).

## What changed

Five files, int32 -> int16 narrowing only where the full domain provably fits.

- `src/render.hpp` — `blkClamp` params + clamp locals `int32_t` -> `int16_t`.
  Callers (`blk`, `hudBlk`) already take int16; world coords <= 256, sizes
  <= 128, `minY` 0/HUD_H, so `x+w`, `y+h` <= ~384. Removed the int32 casts at
  the wrappers implicitly.
- `src/core/combat.hpp` — `combatZoneContains` locals `x`/`y` int32 -> int16.
  Monster coords <= 256 and the int8 box rotation with |fx|,|fy| <= 16 gives
  |dx|,|dy| <= 254, so `x+w`, `y+h` <= 511.
- `src/core/monster.hpp` — `knockMonsterAway` `cx`/`cy` -> int16 (+dx/dy);
  `damageMonster` `cx`/`cy`/`proj` -> int16 (products bounded by 512*16*2 >> 4).
- `src/core/player.hpp` — `applyDrift` `avx`/`avy` -> int16; `meleeHitbox`
  `cx`/`cy` -> int16; whirl `cx`/`cy` -> int16 (dropped redundant casts);
  `playerHurt` guard `chip` -> int16; attack `total`/`mult`/`hx`/`hy` -> int16.
- `src/core/fp.hpp` — `tdiv(int16_t, int16_t)` (all callers pass int16-scale
  values: `dx*spd` products already evaluate in 16-bit on AVR, subX/subY);
  `dirIndexFromDelta(int16_t, int16_t)` (all callers <= ~512).
- Audio: no remaining wide helpers to narrow. `AudioState::trainTotal` is int32
  by design (`train.total` unbounded); `mhPlay`/table already uint16. **No change.**

## Per-group deltas (measured sequentially, whole-image `make build`)

| group | change | flash | delta |
|---|---|---|---|
| baseline (HEAD 1590750) | — | 27690 | — |
| A | `blkClamp` int16 | 27296 | **-394** |
| B | `combatZoneContains` int16 locals | 27222 | **-74** |
| C1 | `knockMonsterAway`/`damageMonster` | 27132 | **-90** |
| C2a | `applyDrift` | 27126 | **-6** |
| C2b | `meleeHitbox`/whirl/`playerHurt`/attack | 27116 | **-10** |
| C3a | `tdiv` int16 (isolated) | 27048 | **-68** |
| C3b | `dirIndexFromDelta` int16 (isolated) | 26868 | **-180** |
| D | audio | 26868 | **0** |
| — | `combatZoneHitResolve`/`combatResolveBodyHit` base int16 | 26868 | **0 (reverted)** |
| **total** | | **26868** | **-822** |

C3 isolation: with `dirIndexFromDelta` restored to int32 the image was 27048
(tdiv alone -68); the 26868 result is both. The combat `base` param narrowing
was behavior-equivalent (base is 16-bit at the only call site) but paid 0 B, so
it was reverted to keep the diff to wins only.

## Verification (exact tails / numbers)

1. `make gen` (x2) -> `make gen-check`:
   ```
   fxdata_manifest: PASS (53 generated artifacts unchanged)
   git status --short  (only the 5 edited sources, no generated files)
   ```
2. `make test` -> `Total Passed: 3144  Total Failed: 0`.
   `make test-tools` -> `Ran 82 tests in 4.542s ... OK`.
3. `make fxtest-headless` (full): all suites PASS —
   `test_assets 262/0, test_audio 14/0, test_boot 4/0, test_combat 184/0,
   test_data 221/0, test_hud 17/0, test_menu 59/0, test_parity 660/0,
   test_perf 5/0, test_player_art 111/0`.
   perf tail: `B pUs=6377 pHz=156 lHz=52 lTk=524 rMx=5256 rAv=4910 ram=410`
   vs reference `rMx=5392 rAv=5028 rUs=6502` — **improved, no regression**
   (rMx -136, rAv -118, pUs -125).
4. `make build` + `make size`:
   ```
   Sketch uses 26868 bytes (90%) ... Global variables use 2005 bytes ...
   size: .text=26810 .data=58 .bss=1947
   size: flash=26868/29696 (2828 free)  ram=2005/2560
   ```
   Baseline 27690 -> new 26868 = **-822 B**.
5. Parity + goldens: `test_parity 660/0` unchanged, `test_player_art 111/0`
   unchanged, `test_hud 17/0` unchanged. No fixture regen, no test edits.
6. Data facts unchanged (no `HAS_*` flip): `HAS_MULTI_WINDOW:true HAS_STAGGER:true
   HAS_ZONES:true HAS_GUARD_ZONES:true`, rest false.

## Domain notes (why narrowing is exact)

- `tdiv`: on AVR `int` is 16-bit, so `dx * spd` already wraps to int16 before
  the old int32 param; narrowing changes nothing for the existing callers.
- `dirIndexFromDelta`: compares `adx > ady*2`; all call sites pass |delta|
  <= ~512, so the promoted product stays inside int (16-bit) either way.
- `damageMonster` proj: `(hx-cx)*fx + (hy-cy)*fy` <= 512*16*2 = 16384, >>4.
- `combatZoneContains` / `blkClamp`: max coordinate + size <= 511, well inside
  int16.
