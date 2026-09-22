# monhun-ardu-dx5.2 — perf: variable-divisor divisions -> reciprocal multiply

Status: **DONE (one site kept; reciprocal approach measured and dropped)**.
The only site with a clear `test_perf` CPU win is the two player HUD bars; the
32-bit divide there is replaced by the already-linked 16-bit divide. The
reciprocal-multiply helper was implemented and measured for every listed site
and lost on flash (and RAM) for no extra CPU win, so those sites are dropped per
the bead rule ("if a site adds flash without a clear CPU win in `test_perf`,
drop it and say so").

## Changed

- `src/render.hpp:1139-1160` — added `hudBar8(x,y,w,h,uint8_t num,uint8_t den,
  uint8_t shade)` next to `hudBar`. `Player::hpMax`/`stamMax` are `uint8`, so
  `(w-2)*num <= 42*255` fits 16 bits; the bar uses the 16-bit divide
  (`__divmodhi4`, already linked by `fp::tdiv`) instead of the 32/16
  (`__udivmodsi4`) the shared `hudBar` pays for the 2800-hp monster bar.
  Geometry/truncation identical (caller guarantees `num <= den <= 255`).
- `src/render.hpp:1214-1215` — player HP + stamina bars call `hudBar8`; the
  monster bar keeps `hudBar` (its `(44-2)*2800 = 117600` numerator needs 32-bit).

## Size

| | flash | free | ram | note |
|---|---:|---:|---:|---|
| before (HEAD 928c7bd) | 28446 | 1250 | 1638 | baseline |
| after | 28558 | 1138 | 1638 | +112 flash, ram flat |

`.text=28538 .data=20 .bss=1618` (was `.text=28426 .data=20 .bss=1618`).

## perf_test (Ardens, `FXTEST_ONLY=test_perf`)

| | pUs | pHz | lHz | lTk | rMx | rAv | ram |
|---|---:|---:|---:|---:|---:|---:|---:|
| before | 6344 | 157 | 52 | 184 | 3156 | 2761 | 680 |
| after  | 6343 | 157 | 52 | 184 | 3088 | 2691 | 680 |

Render avg −70 µs, render max −68 µs (both player-bar divides removed). `pUs`
is pinned by `waitForNextPlane` (plane-paced), so the win shows in the render
columns, not the plane period; the gate mask stays 0.

## Gate tails (in order)

1. `make test`
   ```
   Total Passed: 6285
   Total Failed: 0
   ```
2. `make size`
   ```
   size: .text=28538 .data=20 .bss=1618
   size: flash=28558/29696 (1138 free)  ram=1638/2560
   ```
3. `make fxtest-headless FXTEST_ONLY=test_combat`
   ```
   combat_test PASSED=237 FAILED=0
   test_combat: PASS
   ```
4. `make fxtest-headless FXTEST_ONLY=test_monster_art`
   ```
   test_monster_art PASSED=127 FAILED=0
   test_monster_art: PASS
   ```
5. `make fxtest-headless FXTEST_ONLY=test_hud`
   ```
   test_hud PASSED=29 FAILED=0
   test_hud: PASS
   ```
6. `make fxtest-headless FXTEST_ONLY=test_perf`
   ```
   B pUs=6343 pHz=157 lHz=52 lTk=184 rMx=3088 rAv=2691 ram=680
   perf_test PASSED=5 FAILED=0
   test_perf: PASS
   ```
7. `make fxtest-headless` (full)
   ```
   asset_test PASSED=270 FAILED=0        test_menu_art PASSED=53 FAILED=0
   combat_test PASSED=237 FAILED=0       test_monster_art PASSED=127 FAILED=0
   data_test PASSED=348 FAILED=0         test_player_art PASSED=120 FAILED=0
   menu_test PASSED=60 FAILED=0          test_quests PASSED=50 FAILED=0
   perf_test PASSED=5 FAILED=0           test_screens PASSED=85 FAILED=0
   test_audio PASSED=9 FAILED=0          test_smith PASSED=115 FAILED=0
   test_boot PASSED=4 FAILED=0           test_tell PASSED=18 FAILED=0
   test_hub PASSED=63 FAILED=0           test_zones PASSED=80 FAILED=0
   test_hud PASSED=29 FAILED=0
   test_items PASSED=35 FAILED=0
   ```
   (18 suites, 0 failures)

## Dropped sites + why (all measured, none faked)

A header-only `src/core/recip.hpp` (`Recip{den,r}`, lazy `recipBuild` =
`floor(65535/den)` via the already-linked 16-bit divide, `divRecip` =
multiply + bounded remainder correction, bit-identical truncation) was written
and compiled. Measurements (shipping `make size`, same test_perf):

- **Player bars, reciprocal (2 sites)**: +284 flash, +8 RAM, rAv 2691 → no
  better than the 16-bit split. The correction loop + lazy-cache checks cost
  more than the `__divmodhi4` call they replace. Dropped in favour of `hudBar8`.
- **All three bars, reciprocal**: +154 flash, +12 RAM, rAv 2694 (≈0 extra over
  `hudBar8`). The monster-bar reciprocal is not a win. Dropped.
- **`monster.hpp:328` hp*100/hpMax (guard hpPct)**: 32-bit divide, but the
  `test_perf` image is built with `MH_COMBAT_PARTS=0` → `SIMPLE_GUARDS=true`, so
  the full-guard path (and this divide) is compiled out of the bench. No
  `test_perf` exposure → dropped (it is a real shipping AI-path cost; needs a
  gameplay perf bench to justify flash).
- **`monster.hpp:671,674` `/retreatDen`, `/circleDen`**: numerator is
  `m.spd(uint8) * num(uint8) <= 1785`, divisor `uint8` → GCC already emits the
  16-bit divide (`__divmodhi4`), not a 32-bit one. No 32-bit divide to remove;
  a reciprocal cache only adds flash. Dropped.
- **`render_math.hpp:56` `(tick*8)/active`**: signed 32-bit divide, but the
  spin sheet is only drawn for the heavy tail-spin (the bench monster is
  MON_LUNGE), so it never runs in `test_perf`. Also `renderScene`/`drawMonster`
  take `const Game&`, so a cache would need a `mutable` field + a device-only
  `spinSheetFrameC`. No measured win → dropped.
- **`armor_state.hpp:184` `dmg*100/denom`**: signed 32-bit divide; the bench
  player is `PS_STUN` (never attacks, monster rarely lands), so at most a few
  calls and no measurable change. `denom = 100+def` can exceed 65535 for a
  synthetic def, so an exact 16-bit variant needs a fallback branch. Dropped.
- **`combat.hpp:1185` `combatMulPercent` `/100u`**: constant divisor; GCC
  already folds it to a multiply. Not touched (per bead).

No float, no new mutable globals, no new globals. Tree left dirty (no commit).

## Note

The bead title asks for "reciprocal multiply". The reciprocal helper was built
and is exact, but in this LTO image every reciprocal site costs more flash (and
RAM) than the cheaper exact alternative (16-bit divide for the uint8-maxe
player bars), for the same or worse CPU, matching the earlier checkpoint review
("adding inline code to remove shared helper calls loses"). The kept change is
the exact, measured CPU win; the reciprocal path is documented rather than
shipped.
