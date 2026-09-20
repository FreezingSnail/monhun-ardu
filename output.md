# monhun-ardu-feel.13 — fix: test_combat device image headroom before kit data lands

Baseline: HEAD `4a39d9b`, clean tree. No commit/push (orchestrator commits).
Only `tst/fxdatatest/combat_test.hpp` changed. No `src/` production file, no
`src/generated/*` emission, no blob, no `mock/`, no `tools/` change. No
`tools/gen-combat.py` gate was needed, so the generated host artifacts stay
byte-identical (verified by `make gen-check`).

## Result

`test_combat` device image, stock arduino-cli flags (no `MH_NO_USB`):

```
before: Sketch uses 29690 bytes (99%) of program storage space. Maximum is 29696 bytes.   (6 B free)
after:  Sketch uses 23662 bytes (79%) of program storage space. Maximum is 29696 bytes.   (6034 B free)
```

**Reclaimed 6028 B** (acceptance: >=150 B). RAM unchanged (globals 1821 B
before/after; the new PROGMEM expectation tables live in flash).

## What changed (no coverage removed)

The per-record spot checks were **converted** from one `expectEq` per field to
one whole-struct compare per record, table-driven:

- new anonymous-namespace tables in `combat_test.hpp`: `kCreatures`/`kCreatureIds`,
  `kZones`/`kZoneIds`, `kAttacks`/`kAttackIds`, `kWindows`/`kWindowIds`,
  `kPatterns`, `kGuards`, `kSteps`, `kSkeleton` (all `PROGMEM`),
- one `progEq(ram, progmem, n)` byte-compare helper; the AVR value structs are
  padding-free and packed-ABI-sized (new `static_assert(sizeof(...) ==
  combat_expect::*_SIZE)` guards; `CombatWindow` is `WINDOW_SIZE - 1`, the
  packed record minus its reserved flags byte),
- 8 loops (3 creatures, 7 zones, 5 attacks, 11 windows) plus a 12-compare
  cross-reference walk that follows the same pointer graph the decision code
  follows (creature -> attack -> window, creature -> pattern -> guard / step),
- `COMBAT_FACING_LOCK_AWAY == 2` kept as a named pin.

Assert call sites: 281 -> 142. Unique `F()` labels: 289 -> 150
(4961 -> 2766 bytes incl NUL) — every shared label is now one per record kind.

### Field coverage is a strict superset

Every field the old per-field asserts pinned is still pinned by its record row;
the whole-struct compares additionally pin fields the old spot checks skipped
(e.g. creature `flags`/`sheet`/`brokenW`/`brokenH`/enrage quad, attack
`elem`/`onHitEffect`/`onHitPush`/`onHitStun`/`stagger`/`cue`/`recover`, window
`oy`/`h`/`dmgMul`, zone `brokenDmgMul`/`brokenFlags`).

| consolidated device group | old asserts | now | host coverage of the same bytes |
|---|---|---|---|
| heavy/lunge/sweep creature | 30 | 3 `CombatCreature` rows + 2 locals | `tst/combat_pack_test.hpp` "packed records decode to the generated host structs" (creature loop) + "blob spot values match combat_expect.hpp"; `tst/combat_test.hpp` "creature records match combat_data.hpp" |
| heavy/lunge/sweep/ravager zones | 35 | 7 `CombatZone` rows | `tst/combat_pack_test.hpp` zone decode loop + expect spot values; `tst/combat_test.hpp` "skeleton + zone records match combat_data.hpp", "ravager zone records", "bull zone records" |
| heavy bite/spin, lunge peck, bull stomp/gore attacks | 34 | 5 `CombatAttackValue` rows | `tst/combat_pack_test.hpp` attack decode loop + expect spot values + "hop dx/dy" test; `tst/combat_test.hpp` "attack + window records match combat_data.hpp", "attackLoad + attackWindowLoad cache lifecycle" |
| heavy bite/spin windows, lunge peck/leap, bull stomp/gore, ravager sweep windows | 31 | 11 `CombatWindow` rows | `tst/combat_pack_test.hpp` window decode loop; `tst/combat_test.hpp` window loop |
| lunge/bull pattern -> guard -> step walk (+ skeleton) | 15 | 12 struct compares following the real indices | `tst/combat_pack_test.hpp` pattern/guard/step decode loops + named-offset table; `tst/combat_test.hpp` "pattern + guard + step records match combat_data.hpp" |

No assertion was deleted outright: each removed per-field check is subsumed by a
whole-record compare that pins strictly more fields. The device suite still
exercises the AVR `offsetof` read path for every record type (the part the host
suites cannot reach) and keeps the cart read-budget / cache lifecycle /
guard-eval / damage-routing / fallback sections unchanged.

## Verification tails

```
# make fxtest-headless FXTEST_ONLY=test_combat
test_combat
Sketch uses 23662 bytes (79%) of program storage space. Maximum is 29696 bytes.
Global variables use 1821 bytes (71%) of dynamic memory, leaving 739 bytes for local variables. Maximum is 2560 bytes.
C reads spawn=15 attack=6 guard=2 hit=0 tick256=0 simAtk=7 simTk=0 winSw=1
combat_test PASSED=176 FAILED=0
P
test_combat: PASS

# make fxtest-headless (full, exit=0)
asset_test PASSED=270 FAILED=0
test_audio PASSED=17 FAILED=0
test_boot PASSED=4 FAILED=0
combat_test PASSED=176 FAILED=0
data_test PASSED=368 FAILED=0
test_hub PASSED=57 FAILED=0
test_hud PASSED=17 FAILED=0
test_menu_art PASSED=81 FAILED=0
menu_test PASSED=80 FAILED=0
test_monster_art PASSED=111 FAILED=0
parity_test PASSED=660 FAILED=0
perf_test PASSED=5 FAILED=0
test_player_art PASSED=111 FAILED=0
test_quests PASSED=50 FAILED=0
test_screens PASSED=78 FAILED=0
test_smith PASSED=66 FAILED=0
zones_test PASSED=69 FAILED=0

# make test
Total Passed: 5611
Total Failed: 0

# make test-tools
Ran 195 tests in 9.824s
OK

# make gen-check
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (82 generated artifacts unchanged)
git status: only tst/fxdatatest/combat_test.hpp modified

# make size (shipping image, unchanged vs HEAD)
Sketch uses 27474 bytes (92%) of program storage space. Maximum is 29696 bytes.
Global variables use 1740 bytes (67%) of dynamic memory, leaving 820 bytes for local variables. Maximum is 2560 bytes.
size: .text=27434 .data=40 .bss=1700
size: flash=27474/29696 (2222 free)  ram=1740/2560
size: data facts: HAS_ENRAGE:false HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false
  HAS_GUARD_FACING:false HAS_GUARD_HP:false HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true
  HAS_HIT_STAGGER:false HAS_MULTI_STEP:false HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false
  HAS_STAGGER:true HAS_STEP_AFTER:false HAS_STEP_CHANCE:false HAS_WAIT_STEPS:false HAS_ZONES:true
```

`HAS_*` facts and the 27474/2222-free shipping line are identical to HEAD
(`docs/dev-flow.md` feel.7 baseline), confirming no production-data flip.

## Headroom for the kit beads

`test_combat` now carries **6034 B** of free device flash (was 6 B). The
`feel.8/9/10` kit data (multi-step/after/chance facts, wallStun/enrage/tell
bytes) can land without touching this suite for budget.

## Per-file summary

- `tst/fxdatatest/combat_test.hpp` — per-record spot checks converted to
  table-driven whole-struct `progEq` compares + a pointer-graph cross-reference
  walk; 6028 B device flash reclaimed; coverage per field expanded.
