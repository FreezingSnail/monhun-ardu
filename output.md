# monhun-ardu-feel.10 — data: heavy kit — DONE

Status: **DONE**. Heavy kit retuned + `tail_slam` pounce + bite→spin combo.
The prior blocker (zone `unlockMask` u8 ABI, fixed at HEAD `a584b20`) is clear;
`make gen` now succeeds. No commit / push (orchestrator commits).

## What changed

`data/creatures/heavy.json` (only data file touched):

- `stats.spd` 3 → **5**; `profile.faceHold` 10 → **8**.
- `bite`: unchanged numbers, added `tell: "line"`.
- `tail_spin`: windup 42→**34**, active 20→**18**, recover 55→**62**, `tell: "arc"`;
  the 4 contiguous lock-away windows re-timed to active 18 and each widened
  +4 px on its long axis (24→28), centres kept:
  - w0 `t[0,5]  box(-20,0,28,16)`
  - w1 `t[6,10] box(0,-22,16,28)`
  - w2 `t[11,14] box(22,0,28,16)`
  - w3 `t[15,18] box(0,22,16,28)`
- `tail_slam` (new, heavy's 3rd attack): windup 26 / active 8 / recover 44 /
  dmg 12, `move {type: hop, dx: -56, dy: 0}` (face-relative; with the frozen
  away-facing of a flanking hunter this releases backward at 3.5 px/tick ×
  8 active ticks = **28 px toward the hunter**), `facing: lock-at-windup`,
  `tell: ring`, one window `t[0,8] box(-16,0,36,28)` (behind/around the 40×28
  body, covering the 20..60 decision band).
- Patterns (source order, first-match-wins):
  1. `p_tail_slam` `{facing: behind, minDist: 20, maxDist: 60}` → `tail_slam`
  2. `p_bite_spin` `{maxDist: 20}` → `bite after 12` → `WAIT 16` → `tail_spin`
  3. `p_spin` `{minDist: 21, maxDist: 30}` → `tail_spin`
  4. `p_bite` `{minDist: 31, maxDist: 255, hpBand: [0,100]}` → `bite`

  Bands are exclusive: `p_bite_spin` 0..20, `p_spin` 21..30, `p_bite` 31..255;
  `p_tail_slam` answers a behind flank at 20..60 only. `tail_spin` stays
  reachable ≤30 (via `p_spin` and the combo) and `bite` beyond — current fight
  identity preserved. The combo opens with `bite`, so a broken tail still fights
  (the `tail_spin` step is skipped by `combatAttackDisabled`, cursor clears).

Generated set regenerated (`make gen` ×2 + `make gen-check`):
`fxdata/fxdata*.bin`, `fxdata/fxdata.h`, `src/fxdata.h`,
`fxdata/manifest.json`, `src/generated/combat_data.hpp`,
`src/generated/combat_expect.hpp`, `src/generated/combat_meta.hpp`,
`src/generated/{equip,zone}_meta.hpp`.

Blob: `combat.bin` 1227 → 1301 B. Counts: attacks 10→11, windows 15→16,
patterns 13→15, guards 13→15, steps 15→19.

## REVIEW BEFORE DEVICE

### `python3 tools/gen-combat.py --dump` — heavy section

```
creature heavy (skeleton longtail, stats w40 h28 hp320 spd5, spawn 200,40, collide box(-8,3,48,22), enrage hpPct0 spdMul0 faceHold0 cue0) zones appendage D150 HP60 S40 ST30
  zone appendage: box(-24,0,24,16) dmgMul 150 hp 60 share 40 break 0x01 stagger 30 brokenOverride 200 hurtOff 1 disable tail_spin
  attack bite: windup30 active8 recover40 dmg10 move lunge(26) windows 1 wallStun 0 tell line
    window 0: t[0,8] box(14,0,18,14) dmgMul 100
  attack tail_spin: windup34 active18 recover62 dmg8 move none windows 4 wallStun 0 tell arc
    window 0: t[0,5] box(-20,0,28,16) dmgMul 100
    window 1: t[6,10] box(0,-22,16,28) dmgMul 100
    window 2: t[11,14] box(22,0,28,16) dmgMul 100
    window 3: t[15,18] box(0,22,16,28) dmgMul 100
  attack tail_slam: windup26 active8 recover44 dmg12 move hop(-56,0) windows 1 wallStun 0 tell ring
    window 0: t[0,8] box(-16,0,36,28) dmgMul 100
  pattern p_tail_slam: guard minDist20 maxDist60 hp[0,100] player0x00 cd0 chance100 zonesBroken - facing behind
    step 0: ATK heavy.tail_slam after0 chance100
  pattern p_bite_spin: guard minDist0 maxDist20 hp[0,100] player0x00 cd0 chance100 zonesBroken - facing any
    step 0: ATK heavy.bite after12 chance100
    step 1: WAIT 16 after0
    step 2: ATK heavy.tail_spin after0 chance100
  pattern p_spin: guard minDist21 maxDist30 hp[0,100] player0x00 cd0 chance100 zonesBroken - facing any
    step 0: ATK heavy.tail_spin after0 chance100
  pattern p_bite: guard minDist31 maxDist255 hp[0,100] player0x00 cd0 chance100 zonesBroken - facing any
    step 0: ATK heavy.bite after0 chance100
```

Dump validates (exit 0).

### `python3 tools/contact_sheet.py --creature heavy --dump`

`build/contact_sheet.png` (480x404). ASCII view (heavy band; `#`=active,
`*`=windup, `:`=recover, dark/light/mid window-span colours):

```
...##.####.##.##..#.#.#.##..#####..###..#####..#.#.##.####..##..###.....
...###..##.###....#.#..###..#####.####.#######.#.####.####.#.#.#.##.....
..#..###.#..#.#.#.##.###.###.#.####.....................................
..**.**.**.**.**.**.**.**.##.##.::.::.::.::.::.::.::.::.::.::.........
..**.**.**.**.**.**.**.**.##.##.::.::.::.::.::.::.::.::.::.::.........
..**.**.**.**.**.**.**.**.##.##.::.::.::.::.::.::.::.::.::.::.........
..#..............#..............#..............#........................
......##########........................................................
......#.................................................................
......#.................................................................
......#.................................................................
......#.................................................................
......#......####.......................................................
..#..###..#.#..#.#.#.#.#.#.###.####.####................................
..**.**.**.**.**.**.**.**.**.##.##.##.##.::.::.::.::.::.::.::.::.::.::.
..**.**.**.**.**.**.**.**.**.##.##.##.##.::.::.::.::.::.::.::.::.::.::.
..**.**.**.**.**.**.**.**.**###.##.#*.*#.#:.::.::.::.::.::.::.::.::.::.
..#..............#..........#...#..............#..............#........
............................#...........................................
............................#...........................................
............................#...........................................
......##########.........##########.........##########.........##########
......#..................#..#...............#..................#........
......#..................#..................#......*...........#........
......#..................#..................#......*...........#........
......#..................#..................#......*...........#........
......#..................#..................#......*...........#........
......#..................#..................#..................#..#.....
..................................................................#.....
..................................................................#.....
..................................................................#.....
..##..#...#.##..#.#####..#..#.##...##..####.......###..#.###.#....##.##..##..####
..................................................................#.....
..#..###..#.#..###.###.##.###.###.#.####..........................#.....
..**.**.**.**.**.**.**.##.##.::.::.::.::.::.::.::.::.::.::.::.........
..**.**.**.**.**.**.**.##.##.::.::.::.::.::.::.::.::.::.::.::.........
..**.**.**.**.**.**.**.##.##.::.::.::.::.::.::.::.::.::.::.::.........
..#..............#..............#..............#........................
...#############........................................................
......#.................................................................
......#.................................................................
......#.................................................................
......#.................................................................
......#.................................................................
......#.................................................................
..##..#.#.#..#..#..####.................................................
```

Per-attack review:

- **bite** — timeline windup 30 / active 8 / recover 40; one 18×14 window at
  `ox 14` on the tracked forward axis. `tell: line` draws 3 dashes along the
  cached window's face-relative offset (`drawMonsterTell` reads
  `g.combat.attack.win.box`), so telegraph == hit-test window. Window box is the
  real hit test (`monsterHitsPlayer` uses the same cached box). OK.
- **tail_spin** — timeline windup 34 / active 18 / recover 62; four contiguous
  windows (`[0,5][6,10][11,14][15,18]`, gaps exactly 1, last == active) read as a
  rotating arc behind → north → front → south. Each long axis widened 24→28
  (preview cells show the wider boxes; outer reach grows so out-ranging
  matters). `tell: arc` uses the cached window box, so the arc segment sweep and
  the hit test are the same geometry. Lock-away unchanged (negates the tracked
  vector once at windup, window 0's behind-the-back tail points at the hunter).
  OK.
- **tail_slam** — timeline windup 26 / active 8 / recover 44; one 36×28 window
  at `ox -16` (behind/around the 40×28 body). `tell: ring` outlines the cached
  window (expanding ring through windup, `tellRingHalf`), telegraph == hit test.
  Hop `dx -56` is face-relative; with `lock-at-windup` frozen on the away-facing
  of a behind-guarded hunter, `combatFacePoint` rotates it backward = toward the
  hunter: 8 active ticks × -56/16 = **-28 px** travel, landing the behind window
  across the 20..60 decision band. OK.

## Tests updated intentionally (no coverage weakened)

- `src/generated/combat_expect.hpp` — regen: heavy spd 5, faceHold 8, attacks 3,
  patterns 4, bite tell 1; first-pattern pins now `PATTERN_HEAVY_P_TAIL_SLAM_*`
  (20/60), `BLOB_SIZE` 1301, new sha256.
- `tst/combat_pack_test.hpp` — records table + all new heavy records; spot
  values for tail_spin retime / arc / slam hop / lock / ring; heavy guard bands
  pinned literally for non-first patterns; the hop dx/dy test now pins
  `tail_slam` as the first shipped hop (dx -56, dy 0) and keeps the other
  attacks' zeros.
- `tst/combat_test.hpp` — counts 11/16/15/15/19; heavy faceHold 8; exclusive
  guard bands + tail_slam behind clause; full heavy decode test rewritten
  (spin retime/widening, slam hop/lock/ring/window, source order, bite_spin
  `bite after 12 / WAIT 16 / tail_spin`).
- `tst/monster_test.hpp` — chooseAttack heavy bands rewritten (front combo/spin/
  bite + behind slam 20..60); faceHold 8 countdown; tail_spin selected at 21..30;
  new real-data test `tail_slam hop: backward vector pounces 28 px toward the
  flank` (release lvx -56, 8 ticks → -28 px, frozen through recovery).
- `tst/fxdatatest/combat_test.hpp` — creature heavy firstPattern → slam; attack/
  window rows for the new timings + slam; cart-load spot checks for the slam hop/
  lock/ring/window; guard-band and behind checks; read-budget cross-ref indices
  updated.

## Verification (tails)

- `make gen` (×2) && `make gen-check`:
  `fxdata_manifest: PASS (82 generated artifacts unchanged)`.
- `make test`: `Total Passed: 6454  Total Failed: 0`.
- `make test-tools`: `Ran 199 tests ... OK`.
- `make fxtest-headless` (full, 17 suites, test_parity excluded):
  all `: PASS`; `test_perf PASSED=5 FAILED=0` line:
  `B pUs=6369 pHz=157 lHz=52 lTk=456 rMx=4772 rAv=4593 ram=582`.
  `test_combat PASSED=236 FAILED=0` (`C reads spawn=15 attack=7 guard=2 hit=0
  tick256=0 simAtk=8 simTk=0 winSw=1`).
- `make size`:
  `size: flash=28610/29696 (1086 free)  ram=1743/2560`
  (`Sketch uses 28610 bytes (96%)`). Budget: HEAD was 28578 (1118 free) →
  **+32 B flash**, still fits. RAM unchanged.

`unlockMask` ABI untouched (still the u16 lo/hi pair from `a584b20`); the
appendage break still disables `tail_spin` (host + device assert the u16 mask
bit), which now also mutes the combo's tail step.
