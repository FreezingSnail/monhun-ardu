# monhun-ardu-feel.15 — data: creature turn rates + wider flank bands

HEAD at start: `23097b1` (feel.14 turn-rate engine), clean tree. No commit/push
(orchestrator commits). Base flash `28720/29696 (976 free)`, RAM `1744/2560`.

## What changed

| File | Change |
|---|---|
| `data/creatures/lunge.json` | chicken `profile.faceHold` 5→6, `turnRate` 0→1; `p_flank` maxDist 26→32 |
| `data/creatures/sweep.json` | bull `turnRate` 0→1 (faceHold stays 10); `p_rear_kick` maxDist 24→30 |
| `data/creatures/heavy.json` | heavy `profile.faceHold` 8→10, `turnRate` 0→1; `p_tail_slam` band 20..60→16..64 |
| `data/creatures/ravager.json` | ravager `faceHold` default 0→8, `turnRate` default 0→2 |
| `docs/feel-design.md` | profile/pattern table cells + `turnRate` data-verb row + feel.14/15 budget entry |
| `src/generated/*`, `src/fxdata.h`, `fxdata/*`, `fxdata/manifest.json` | `make gen` regen (see below) |
| `tst/monster_test.hpp` | heavy faceHold 8→10 (turnRate isolated to 0 for that sub-test); shipped-heavy turnRate 0→1; heavy slam band 16..64 boundaries; new chicken 6/1 circling-behind reachability test |
| `tst/combat_test.hpp` | profile pins (heavy 10/1, chicken 6/1, bull 10/1, ravager 8/2); p_flank 32, rear_kick 30, tail_slam 16..64 boundary pins |
| `tst/combat_pack_test.hpp` | lunge turnRate literal 0→1, added ravager turnRate expect pin, retitled comment |
| `tst/fxdatatest/combat_test.hpp` | comments only (guard bands read from regenerated expect constants) |

Interface: no new symbols/types. `profile.turnRate` (u8, packed byte 11,
`PROFILE_SIZE` 24) and `profile.faceHold` already existed; this bead only
authors values. `HAS_TURN_RATE` flips `false -> true` in
`src/generated/combat_meta.hpp` (generated for the ledger only — the engine
comment at `src/core/monster.hpp` records that the stepping path stays compiled,
so the flip does not fold code).

## Authored rates — `python3 tools/gen-combat.py --dump` (changed sections)

```
creature heavy (skeleton longtail, stats w40 h28 hp320 spd5, ... )
  profile: engage36 keep12 attack42 circle8/10 retreat6/10 stagger0/0/0 faceHold10 turnRate1 cd55+40 spawn90/140 stun24
  pattern p_tail_slam: guard minDist16 maxDist64 hp[0,100] player0x00 cd0 chance100 zonesBroken - facing behind
creature lunge (skeleton chicken, stats w32 h24 hp200 spd6, ...)
  profile: engage36 keep16 attack42 circle8/10 retreat6/10 stagger30/2/30 faceHold6 turnRate1 cd48+60 spawn90/140 stun24
  pattern p_flank: guard minDist0 maxDist32 hp[0,100] player0x00 cd0 chance100 zonesBroken - facing behind
creature ravager (skeleton quad_32x24, stats w32 h24 hp260 spd6, ...)
  profile: engage36 keep24 attack42 circle8/10 retreat6/10 stagger60/1/24 faceHold8 turnRate2 cd50+30 spawn90/120 stun24
creature sweep (skeleton bull, stats w28 h22 hp150 spd7, ...)
  profile: engage36 keep18 attack42 circle8/10 retreat6/10 stagger40/1/24 faceHold10 turnRate1 cd55+40 spawn90/140 stun24
  pattern p_rear_kick: guard minDist0 maxDist30 hp[0,100] player0x00 cd0 chance100 zonesBroken - facing behind
```

Generated expect constants now: `PROFILE_LUNGE_FACE_HOLD=6`,
`PROFILE_LUNGE_TURN_RATE=1`, `PROFILE_SWEEP_TURN_RATE=1`,
`PROFILE_HEAVY_FACE_HOLD=10`, `PROFILE_HEAVY_TURN_RATE=1`,
`PROFILE_RAVAGER_FACE_HOLD=8`, `PROFILE_RAVAGER_TURN_RATE=2`,
`PATTERN_LUNGE_P_FLANK_MAX_DIST=32`, `PATTERN_SWEEP_P_REAR_KICK_MAX_DIST=30`,
`PATTERN_HEAVY_P_TAIL_SLAM_MIN_DIST=16`, `..._MAX_DIST=64`.

## Band exclusivity (no shadowing)

Pattern lists evaluated source-order, first match wins; the behind guards sit
ahead of the same-frontage bands and only claim behind-hemisphere points:

- chicken: `p_flank` behind 0..32 → `p_peck` any 0..28 → `p_leap`/`p_leap2`
  28..255 (hp 51..100 / 0..50). Non-behind at 28 still pecks; behind at >32
  falls through to peck/leap as before.
- bull: `p_rear_kick` behind 0..30 → `p_gore2` any hp 0..40 → `p_stomp` any
  0..24 → `p_gore` 24..255 hp 41..100. The two-zone overlap at exactly 24
  (stomp vs gore, hp 41..100) is resolved by source order and is unchanged from
  feel.9; the widened kick only steals behind points ≤30.
- heavy: `p_tail_slam` behind 16..64 → `p_bite_spin` any 0..20 → `p_spin` any
  21..30 → `p_bite` any 31..255 hp 0..100. The front bands are still an
  exclusive tile (0..20 / 21..30 / 31..255); behind 16..64 is disjoint by the
  facing clause. Behind <16 or >64 falls through to the frontage bands.

## Contact sheet review

`python3 tools/contact_sheet.py --creature lunge|sweep|heavy` rendered
`build/sheet_lunge.png`, `build/sheet_sweep.png`, `build/sheet_heavy.png`
(480x404 each). Attack windup/active/recover timelines and window boxes are
unchanged by this bead (only profile scalars and guard bands changed), so all
tell shapes / hit geometry read exactly as the feel.10 sheets; the guard bands
above are the dump-verified half of the review.

## Sanity-sim (host)

New `tst/monster_test.hpp` case
`turnRate: chicken faceHold 6 / turnRate 1 lets a circling hunter reach behind`:
frozen beast in PURSUE, hunter stepping one DIR8 position per tick around an
18 px radius (`~16-20 px`), `updateMonster` driven each tick; asserts shipped
chicken 6/1, then `facingDot < 0` on the third tick (`behindTick == 3`), well
inside the 24-tick bound. PASS (5/5 asserts).

## Verification tails

`make gen` (run twice; identical, `make gen-check` clean):

```
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: fxdata/manifest.json up to date (52 images, 62 inputs, 19 outputs)
```

`make gen-check`:

```
fxdata_manifest: PASS (82 generated artifacts unchanged)
```

`make test`:

```
Total Passed: 6513
Total Failed: 0
```

`make test-tools`:

```
Ran 202 tests in 10.986s

OK
```

`make fxtest-headless` (full default set, `test_parity` excluded by policy):

```
=== test_assets ===   test_assets PASSED=270 FAILED=0
=== test_audio ===    test_audio PASSED=17 FAILED=0
=== test_boot ===     test_boot PASSED=4 FAILED=0
=== test_combat ===   C reads spawn=15 attack=7 guard=2 hit=0 tick256=0 simAtk=8 simTk=0 winSw=1
                      combat_test PASSED=237 FAILED=0
=== test_data ===     data_test PASSED=368 FAILED=0
=== test_hub ===      test_hub PASSED=57 FAILED=0
=== test_hud ===      test_hud PASSED=17 FAILED=0
=== test_menu_art === test_menu_art PASSED=81 FAILED=0
=== test_menu ===     menu_test PASSED=80 FAILED=0
=== test_monster_art === test_monster_art PASSED=111 FAILED=0
=== test_perf ===     B pUs=6369 pHz=157 lHz=52 lTk=456 rMx=4772 rAv=4593 ram=581
                      perf_test PASSED=5 FAILED=0
=== test_player_art === test_player_art PASSED=111 FAILED=0
=== test_quests ===   test_quests PASSED=50 FAILED=0
=== test_screens ===  test_screens PASSED=78 FAILED=0
=== test_smith ===    test_smith PASSED=66 FAILED=0
=== test_tell ===     test_tell PASSED=17 FAILED=0
=== test_zones ===    zones_test PASSED=69 FAILED=0
```

`make size`:

```
size: .text=28680 .data=40 .bss=1704
size: flash=28720/29696 (976 free)  ram=1744/2560
size: data facts: HAS_ENRAGE:true ... HAS_TURN_RATE:true ...
```

## Budget

Delta over `23097b1`: **+0 B flash, +0 B RAM** — the changed fields already
exist in the packed profile record (`PROFILE_SIZE` 24) and pattern guards
(`GUARD_SIZE` 9), so only bytes already allocated change value. `HAS_TURN_RATE`
flips `false -> true`, generated for the ledger only and not folded into the
shipping path (no size effect). Flash remains `28720/29696 (976 free)`.

## Result

All gates green; no deviation. Data authored as specified, bands exclusive, new
behind-reachability assertion PASS.
