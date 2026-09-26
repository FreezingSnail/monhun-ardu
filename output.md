# monhun-ardu-tfo — Balance: beast HP -33%, stamina actions cost more (owner tune)

STATUS: DONE (fits; **flash delta 0 B** — 29666/29696, 30 free). No BLOCKED.

## What changed

### Data (beast body HP -33%)
- `data/creatures/lunge.json` stats.hp 1800→1200
- `data/creatures/sweep.json` stats.hp 1500→1000
- `data/creatures/heavy.json` stats.hp 2800→1900
- `data/creatures/ravager.json` stats.hp 2400→1600
- pole untouched (300).

### Data (breakable-zone HP ×2/3, truncating)
- every zone hp 160→106, 240→160 in lunge/sweep/heavy/ravager
  (head pools 106, appendage pools 160). pole head hp stays 0.

### src/core/game.hpp (WEAPON_DEFS, host source; make gen repacks the cart blob)
- every `Attack.stam` ×1.5 round-half-up `(v*3+1)/2`: sword combo 9/9/15→14/14/22,
  special 20→30, branches 10/16/18→15/24/27, roll/alt 10→15/12→18; flail combo
  13/12/17→20/18/26, special 22→33, branches 14/24→21/36, roll/alt 10→15/14→21,
  charge 14/22→21/33; gun combo 8/8/13→12/12/20, special 14→21, branches
  6/8/16→9/12/24, roll/alt 8→12/9→14.
- shell stam (ball 6, scatter 5) intentionally unchanged: design froze "Attack.stam
  field" only; shells were not listed. See DEVIATION below.

### src/core/player.hpp (hardcoded action stamina)
- dodge/roll 14→20 (all sites, incl. stowed roll): `startDodgeRoll` gate+cost.
- guard block 22→28 (`playerHurt` ST_GUARD chip).
- stance tick drain +6 per the owner's resolution of the design ambiguity
  (design said "stance tick drain 10->14 (whirl/parry/guard drain sites)"; no
  code site held 10 — `updateStance` drained parry 2 / whirl 8 / guard 1, and the
  only hardcoded 10s were the flail deflect / gun shove). Owner chose "+6 to all
  three stance drains": parry 2→8, whirl 8→14, guard 1→7. Deflect/shove 10 unchanged.
- stamina regen unchanged (8/16 per tick, +1 per 2 ticks).

### Generated (staged together after `make gen`)
- `fxdata/fxdata.bin`, `fxdata/fxdata-data.bin`, `fxdata/manifest.json`,
  `fxdata/tables/combat.bin`, `fxdata/tables/weapondefs.bin`,
  `src/generated/combat_data.hpp`, `src/generated/combat_expect.hpp`.
  `src/fxdata.h == fxdata/fxdata.h` verified by `cmp` (byte-stable regen).

### Tests (all pins updated to the frozen numbers)
- host: `tst/player_test.hpp` (WEAPON_DEFS s0 stam 14, guard 28, whirl drain
  100-start, roll 20 all/stowed, roll-attack stam 15/15/12, chargeslam1 21),
  `tst/monster_test.hpp` (legacy/lunge 1200, sweep 1000, heavy 1900, crit 1188,
  enrage band rescaled to hpMax 1000: 400/410/401), `tst/combat_test.hpp`
  (zone 106/160, drain 93/145, heavy tail 160, 11-hit tail drain),
  `tst/hitscan_test.hpp` (special stam 21), `tst/armor_effect_test.hpp`
  (guard 72 with direct stance, no tick).
- device: `tst/fxdatatest/data_test.hpp` — all 20 `attackStam` pins rescaled
  (+ roll-attack 15/15/12).
- `tst/fxdatatest/combat_test.hpp` already referenced `combat_expect::*`
  symbolic constants (regen carries the new values), so no edit needed.

### Docs
- `README.md` monster roster HP column (1200/1000/1900/1600).
- `docs/feel-design.md` chicken/bull/heavy stats + zone HP tables.
- `docs/creature-framework.md` quotes no HP/stamina numbers (no edit needed).

## DEVIATION (documented, design-literal)

1. Shell stam (ball/scatter) NOT scaled — design C enumerated only WEAPON_DEFS
   `Attack.stam`. If shells were meant to scale too, ball 6→9 / scatter 5→8 is a
   2-line follow-up.
2. "stance tick drain 10->14" had no matching code site; resolved by the owner as
   +6 to all three stance drains (see above).

## Verification

- `make gen` then `make gen-check`: PASS — `fxdata_manifest: PASS (217 generated
  artifacts unchanged)`; `cmp src/fxdata.h fxdata/fxdata.h` identical.
- `make test`: `Total Passed: 6995  Total Failed: 0`.
- `FXTEST_ONLY="test_combat test_data test_hub test_zones test_quests test_items
  test_monster_art" make fxtest-headless`: all PASS —
  combat 254/0, data 356/0, hub 86/0, items 35/0, monster_art 182/0,
  quests 110/0, zones 108/0. RAM audit OK every suite
  (max test_zones 2024 B, 536 B stack).
- `make size-line`: `size: flash=29666/29696 (30 free)  ram=1867/2560`.
  **Delta vs baseline 29666/29696 (30 free): 0 B flash, RAM 1867 (was 1867).**

## Wall time

- worker (implement + host/device gates + size + docs): ~one session; no separate
  spike needed (data-only values + a few int literals).
