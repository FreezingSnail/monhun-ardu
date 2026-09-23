# monhun-ardu-gis — per-weapon draw windup (feel.24)

## What changed

- `src/core/game.hpp`
  - `constexpr uint8_t DRAW_TICKS[3] = {6, 10, 16};` + `inline uint8_t
    weaponDrawTicks(int8_t id)`, indexed by `WeaponId` (sword fastest, gun
    slowest, flail between). No `WeaponDef` layout change; size asserts untouched.
  - `PS_DRAW` appended to `PState` after `PS_CARVE` (existing 0..9 values did
    not move).
- `src/core/player.hpp`
  - Sheathed A away from a gather node now enters `PS_DRAW` (rooted) instead of
    calling `startAttack` on the press tick; `sheathed=false`, `chain=0`,
    `chainWin=0`, `aBuffer=0`, `atk=nullptr`, `hitDone=false`. `aStowOk` latch
    unchanged, so the draw press still never re-stows.
  - New `case PS_DRAW:` in `updatePlayer`: `p.t++`; at `p.t >=
    weaponDrawTicks(g.weapon)` -> `PS_IDLE`, `t=0`, `startAttack(g, def)` (combo
    hit 1). No movement handling. If `startAttack` refuses on lock/stamina the
    hunter stands there with the weapon out. `playerHurt` already resets to
    `PS_IDLE` and leaves `sheathed=false`.
  - `PS_DRAW` added to the `tapDefenseReady()` exclusion list (B tap + d-pad
    double-tap roll cannot cancel a draw).
- `src/render.hpp`: NOT modified. `drawPlayer` only special-cases
  `PS_ATTACK`/`PS_SPECIAL` for the attack pose; with `sheathed=false` and any
  other state it draws the idle weapon pose, so the weapon is visible for the
  whole windup. Verified by reading lines 894/904/954/979.
- Docs: README input-table sheathed-A row and the S2 sheathe paragraph in
  `docs/feel-design.md` now state the rooted per-weapon draw windup
  (sword 6 / flail 10 / gun 16).

## Tests

`tst/player_test.hpp`: replaced the old "A draws hit 1" test with
"sheathe S2: A starts a rooted per-weapon draw, then combo hit 1" (42 asserts):
exact tick values 6/10/16 + ordering; `PS_DRAW` on the press with `atk==nullptr`;
position unchanged on the press and through the whole draw under a held
direction; `n+1 == weaponDrawTicks(w)` ticks to combo hit 1; attack data equals
`attacks[0]`; damage mid-draw -> `PS_IDLE` with `sheathed=false`; B tap mid-draw
does not dodge; draw-and-hold does not re-stow.

`tst/zone_test.hpp` `zswing()` (steps to `PS_IDLE`, 80-tick cap) and
`tst/gather_test.hpp` (depleted node: state != `PS_GATHER`, `sheathed==0`) pass
unchanged. No other suite assumed a same-tick draw.

## Gate (exact)

1. `make test`
   ```
   Total Passed: 6334
   Total Failed: 0
   ```
   New suite: `sheathe S2: A starts a rooted per-weapon draw, then combo hit 1`
   -> `Passed: 42  Failed: 0`.
2. `make gen-check`
   ```
   fxdata_manifest: PASS (91 generated artifacts unchanged)
   ```
   No generated artifact changed.
3. `make fxtest-headless` (all suites)
   ```
   EXIT=0; 18 "=== test_*: PASS ===" lines; no FAILED=[1-9]
   test_perf: B pUs=6342 pHz=157 lHz=52 lTk=184 rMx=3004 rAv=2550 ram=606
   test_tell: PASS, test_zones: PASS, test_player_art: PASS (120)
   ```
4. `make size` (delta vs pre-change baseline of the same tree minus this bead)
   - after:  `size: flash=28982/29696 (714 free)  ram=1708/2560`
     `.text=28958 .data=24 .bss=1684`
   - before: `size: flash=28920/29696 (776 free)  ram=1704/2560`
     `.text=28900 .data=20 .bss=1684`
   - delta: **flash +62 B** (.text +58, .data +4), ram +4 B. Free 776 -> 714.
     Baseline measured on the identical tree with only this bead's
     game.hpp/player.hpp hunks reversed; the other two in-tree tasks' changes
     were kept (render.hpp/fxdatatest never touched).
5. `make test-tools`: not run — no tooling changed.

No commit/push (orchestrator owns commits).
