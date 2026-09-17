# monhun-ardu-ljj.4 — Migration B: skeleton hurt/collide boxes replace hardcoded w/h

Status: **DONE**. Tree left dirty for the orchestrator (no commit, no push).
Base HEAD: `011e2d7` (ljj.3). Design source: `docs/creature-framework.md`
§§4, 9, 11. Behavior byte-identical: the beast's body geometry, spawn stats and
part routing now come from the combat blob (skeleton → body part → box, creature
record → hp/spd/spawn), and the shipped data reproduces today's hardcoded
LUNGE 32x24 / SWEEP 28x22 / HEAVY 40x28 exactly. No data, mock, generated or
fixture bytes changed.

## Deliverables

1. **`src/core/combat.hpp`** — loader additions (both host + AVR paths):
   - Accessors `combatCreatureSkeletonIdx`, `combatCreatureFirstPart`,
     `combatCreaturePartCount`, `combatSkeletonFirstPart`,
     `combatSkeletonPartCount`, `combatSkeletonHeadRead` (packed
     `firstPart | partCount<<8`, one cart access) and `combatPartBoxRead`
     (`ox/oy` and `w/h` as two adjacent-byte u16 cart accesses; pinned by new
     `static_assert` adjacency checks).
   - `CombatSpawn` + `combatCreatureSpawnRead` (hp/x/y + spd in one burst).
   - `combatCreatureBodyBox(id, box, firstPart, partCount)` and a 2-arg
     convenience overload: the hurt/collide box is the creature's skeleton body
     part, with creature-0 fallback for bad ids.
   - `creatureCacheReset` clears the new caches; `creatureLoad` also caches the
     body box + list head (loader-only callers/tests).
   - `CombatBodyHit` + `combatResolveBodyHit(g, base)`: shipping hit-time
     resolver over the cached hurtbox list. Two FX reads per landed hit
     (`hurtOn`, `dmgMul`), no per-tick reads. All shipped multipliers are 100,
     so the routed damage is the base value unchanged; `combatResolveHit`
     (stage-aware reference path) is unchanged and still exercised by the
     host/device suites.
2. **`src/core/game.hpp`** — `CombatState` gains the spawn cache:
   `CombatBox body` (4 B), `bodyFirst`/`bodyCount` (2 B); AVR `static_assert`
   now pins 56 B.
3. **`src/core/monster.hpp`** —
   `initMonster`: `combatCreatureBodyBox` → `g.combat.body/bodyFirst/bodyCount`,
   `combatCreatureSpawnRead` → `m.x/m.y/m.hp/hpMax/m.spd` (all shipped values
   identical: 200/40, 200/150/320, 5/7/3), `m.w/m.h` from the box.
   `syncMonsterTarget`, `pushApart`, `monsterHitsPlayer` consume the box via
   `m.w/m.h` (collide box == body box today). `monsterOnHit` resolves the part
   (single body part on the shipped 3) and routes `hit.dmg`.
4. **`src/core/projectiles.hpp`** — player shell hits on the beast route through
   `monsterOnHit` (same part resolution path as melee; identical damage).
5. **`src/render.hpp`** — `DEBUG_HURTBOXES` wire draws the creature's part box
   from `g.combat.body` (hunt) and the window/telegraph centre from the same
   cache; body sprite anchor/shadow path untouched.
6. **Part stage state** stays allocated/disabled exactly as ljj.2 left it:
   `STAGES_COUNT == 0`, `CombatState::stages` reset to 0 at spawn (pinned by
   host + device tests).
7. **Tests** — `tst/combat_test.hpp` (+67): body-box/spawn accessors vs
   `combat_data.hpp`, pinned shipped sizes, bad-id fallback, hurtbox-list
   resolution vectors, `creatureLoad` cache. `tst/monster_test.hpp` (+35):
   box/stats provenance, cached list head, target rect from box, part routing.
   `tst/fxdatatest/combat_test.hpp` (+55): real blob box/spawn reads, per-call
   read-count gates (box ≤8, spawn burst ≤24, landed hit ≤16), cached-list and
   target-rect checks; final bare `P`.

## Budget / design notes (deliberate, recorded)

- **Shipping resolver is single-part** (cached list head). The shipped 3
  declare exactly one body part per skeleton, so selection over the list is
  trivially the head; the per-part rule (highest final multiplier, tie → lowest
  part id) and stage/phys/elem/bodyShare chain land with the first multi-part
  creature (`combatResolveHit` already models them and remains host/device
  tested). This was forced by flash: test_perf (the binding sketch) had only
  280 B of headroom total.
- Flash trims made along the way: box/spawn reads packed into adjacent-byte u16
  cart accesses, one skeleton-head read instead of two, no `CombatHitResult`
  (12 B) marshalling on the shipping path.
- **test_perf is now at 29686/29696 (10 B free)** — it builds and runs, but
  migration C should recover the 2474 B FSM before adding flash. This is the
  one tight spot of this bead.

## Evidence

### 1. Parity fixtures byte-identical (hard gate)

```
$ node tools/gen-parity-fixtures.js && git diff --stat tst/fxdatatest/parity_fixtures.hpp
wrote tst/fxdatatest/parity_fixtures.hpp
scenes=20 ticks=1269 snapshots=32 cpFields=20
(empty diff)
```

### 2. Host + device suites

```
$ make test
Total Passed: 2848
Total Failed: 0
```

```
$ make fxtest-headless
=== test_assets ===   asset_test PASSED=254 FAILED=0     -> PASS
=== test_audio ===    test_audio PASSED=14 FAILED=0      -> PASS
=== test_boot ===     test_boot PASSED=4 FAILED=0        -> PASS
=== test_combat ===   combat_test PASSED=195 FAILED=0    -> PASS
=== test_data ===     data_test PASSED=221 FAILED=0      -> PASS
=== test_hud ===      test_hud PASSED=17 FAILED=0        -> PASS
=== test_menu ===     menu_test PASSED=55 FAILED=0       -> PASS
=== test_parity ===   parity_test PASSED=660 FAILED=0    -> PASS
=== test_perf ===     perf_test PASSED=5 FAILED=0        -> PASS
```

test_combat tail (final bare `P`):

```
C reads spawn=21 attack=15 guard=10 hit=9 tick256=0 simAtk=16 simTk=0 winSw=7
combat_test PASSED=195 FAILED=0
P
test_combat: PASS
```

test_parity tail:

```
parity_test PASSED=660 FAILED=0
P
test_parity: PASS
```

### 3. Perf B line vs baseline

```
baseline: B pUs=6383 pHz=156 lHz=52 lTk=988 rMx=5040 rAv=4800 ram=422
current:  B pUs=6381 pHz=156 lHz=52 lTk=984 rMx=5040 rAv=4800 ram=411
gates:    rMx<=7407 PASS   pHz>=135 PASS   lHz>=45 PASS   ram>=300 PASS
```

(`pUs`/`lTk` delta is noise from the smaller spawn burst; all gates green.)

### 4. Flash / RAM delta

```
shipping   : 29332 / 29696  (baseline 29062)  -> +270 B, 364 B free
test_perf  : 29686 / 29696  (baseline 29416)  -> +270 B,  10 B free
global RAM : 2006 / 2560    (baseline 2000)   -> +6 B (CombatState 50 -> 56 B)
perf free  : ram=411 (baseline 422, gate >= 300)
```

### 5. Generation gate

```
$ make gen-check
fxdata_manifest: fxdata/manifest.json up to date (19 images, 11 inputs, 9 outputs)
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (34 generated artifacts unchanged)
```

## Files changed (uncommitted)

```
 src/core/combat.hpp            | 181 ++++++++++++++++++++++++++++++++++++++++-
 src/core/game.hpp              |  15 ++--
 src/core/monster.hpp           |  57 ++++++++-----
 src/core/projectiles.hpp       |   2 +-
 src/render.hpp                 |  18 ++--
 tst/combat_test.hpp            |  67 ++++++++++++++++
 tst/fxdatatest/combat_test.hpp |  55 +++++++++++++
 tst/monster_test.hpp           |  35 ++++++++-
 8 files changed, 395 insertions(+), 35 deletions(-)
```

## Blockers

None. Watch item for the next bead: test_perf flash headroom is 10 B — migration
C (FSM removal, 2474 B recoverable) should land before any further feature code.
