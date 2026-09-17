# monhun-ardu-ljj.3 — Migration A: attacks through the combat loader

Status: **DONE**. Tree left dirty for the orchestrator (no commit, no push).
Base HEAD: `1a8ecdf` (ljj.2). Design source: `docs/creature-framework.md`
§§4–9 + §11 (reference values). Behavior byte-identical: the lunge/sweep stop
reading the hardcoded `MONSTER_ATTACKS` table and consume the combat blob's
attack records + windows through the ljj.2 loader caches. No data, mock,
generated or fixture bytes changed.

## Deliverables

1. **`src/core/monster.hpp`** — attack FSM on the RAM cache:
   - `initMonster` resets the combat caches to the variant's creature identity
     (`creatureCacheReset`), then `m.atkIdx = COMBAT_NO_ATTACK`.
   - `chooseAttack` keeps the roster `atkDist` split, picks the creature's
     authored attack slot (0 lunge / 1 sweep), and `monsterAttackSet` loads the
     attack scalars + first window via `attackLoad` (~16 reads) and records the
     stable identity `Monster::atkIdx` + `winRemain`.
   - `startMonsterAttack(g)` releases from the cache (`moveType == MOVE_LUNGE`,
     `moveSpeedF`); `monsterHitsPlayer(g)` builds the window box from the
     cached window, inclusive `[t0, t1]` with `t` 1-based (spike 1c contract).
   - `monsterWindowNext(g)` refreshes the next contiguous window only when
     `winRemain > 0`; shipped attacks declare one window, so the per-tick path
     performs **0 cart reads** during windup/active.
   - Interrupt order (`hitFlash-- → dead → face/dist → stun → FSM`),
     `pushApart`/clamp and all other native paths are untouched.
2. **`src/render.hpp`** — telegraph and debug wire read the **same** cached
   window scalars (`God::combat.attack.win.box` via `combatFaceOffset`) and the
   move-type identity; no per-plane cart reads. The `drawDebug` wire geometry is
   unchanged for shipped windows (box centre = centre + facing projection).
3. **Attack data** — `data/creatures/*.json` already matched the doc §11
   reference exactly (lunge `{40,10,55,speedF34,dmg12, window ox12 oy0 w24 h22}`,
   sweep `{48,12,60,none,dmg9, window ox17 oy0 w32 h24}`); no data change needed,
   blob sha256 baseline untouched.
4. **One variant at a time** — lunge was migrated first and gated
   (host + parity + device) before sweep; both share the same record path and
   the migration was validated once more end-to-end afterwards.
5. **Dead direct table uses in core/render removed** — no
   `monsterAttack*`/`MONSTER_ATTACKS` reference remains in `src/`; the host
   array + accessors and the parity fixtures are kept untouched as the
   reference (`data_test`, `art_dims_test`, `monster_test` pins still read the
   table).

Supporting changes: `src/core/game.hpp` (`Monster::atkIdx`/`winRemain` replace
the `const MonsterAttack *atk` pointer — net 0 RAM; `combatFaceOffset` helper),
`src/core/combat.hpp` (`MoveType`, `COMBAT_NO_ATTACK`,
`combatCreatureFirstAttack`, `creatureCacheReset`).

## Flash / RAM

Measured against a clean `git archive HEAD` build of the same tree (baseline
verified at 28378/29696, RAM 2000):

| Metric | Baseline | Now | Delta |
|---|---|---|---|
| Shipping flash | 28378 / 29696 B | **29062 / 29696 B (97%)** | **+684 B** |
| Shipping RAM | 2000 / 2560 B | **2000 / 2560 B** | **0** |
| Perf free RAM (deepest stack) | 417 B | **422 B** | +5 B |

The +684 B is the migration's real cost: the loader read layer
(`attackLoad`/window/`combatCreatureFirstAttack`/face offset + FSM wiring) is
now linked into shipping instead of being LTO-dropped. Post-migration headroom
is 634 B; migration C's FSM replacement (design: 2474 B recoverable) is the
budgeted source for the interpreter. Render got slightly cheaper because paint
no longer issues cart reads for the telegraph scalars.

## Verification (exact tails)

### 1. Parity fixtures byte-identical (hard gate)

```
$ node tools/gen-parity-fixtures.js
wrote tst/fxdatatest/parity_fixtures.hpp
scenes=20 ticks=1269 snapshots=32 cpFields=20
$ git diff --stat tst/fxdatatest/parity_fixtures.hpp
(empty — 0 bytes of diff)
```

### 2. `make test` — 2756 passed / 0 failed (was 2716; +40 migration asserts)

```
Total Passed: 2756
Total Failed: 0
```

### 3. `make build` — flash/RAM

```
Sketch uses 29062 bytes (97%) of program storage space. Maximum is 29696 bytes.
Global variables use 2000 bytes (78%) of dynamic memory, leaving 560 bytes for local variables. Maximum is 2560 bytes.
```

### 4. `make fxtest-headless` — all 9 suites PASS, parity 660/0

```
=== test_assets ===
asset_test PASSED=254 FAILED=0
test_assets: PASS
=== test_audio ===
test_audio PASSED=14 FAILED=0
test_audio: PASS
=== test_boot ===
test_boot PASSED=4 FAILED=0
test_boot: PASS
=== test_combat ===
C reads spawn=17 attack=15 guard=10 hit=9 tick256=0 simAtk=16 simTk=0 winSw=7
combat_test PASSED=165 FAILED=0
test_combat: PASS
=== test_data ===
data_test PASSED=221 FAILED=0
test_data: PASS
=== test_hud ===
test_hud PASSED=17 FAILED=0
test_hud: PASS
=== test_menu ===
menu_test PASSED=55 FAILED=0
test_menu: PASS
=== test_parity ===
parity_test PASSED=660 FAILED=0
test_parity: PASS
=== test_perf ===
B pUs=6383 pHz=156 lHz=52 lTk=988 rMx=5040 rAv=4800 ram=422
perf_test PASSED=5 FAILED=0
test_perf: PASS
```

Perf B line vs baseline and gates:

| Metric | Baseline | Now | Gate | Result |
|---|---|---|---|---|
| pUs | 6389 | 6383 | — | slightly faster |
| pHz | 156 | 156 | >= 135 | PASS |
| lHz | 52 | 52 | >= 45 | PASS |
| lTk | 988 | 988 | — | flat |
| rMx | 5056 | 5040 | <= 7407 | PASS |
| rAv | 4809 | 4800 | — | slightly faster |
| ram | 417 | 422 | >= 300 | PASS |

### 5. `make gen-check` — PASS

```
fxdata_manifest: fxdata/manifest.json up to date (19 images, 11 inputs, 9 outputs)
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (34 generated artifacts unchanged)
```

### 6. Cache-model read counts (device `test_combat`)

| Phase | Reads | Gate |
|---|---|---|
| Spawn (`creatureLoad`) | 17 | <= 40 |
| Attack load (`attackLoad` + window) | 15 | <= 24 |
| Guard decision | 10 | <= 12 |
| Landed hit | 9 | <= 12 |
| Steady state (`combatTick` x 256) | **0** | 0/tick |
| Sim attack start (`monsterAttackSet`) | 16 | <= 24 |
| Sim windup + active ticks | **0** | 0/tick |
| Window switch (`attackWindowLoad`) | 7 | <= 8 |

`chooseAttack` adds one `combatCreatureFirstAttack` read on top of
`monsterAttackSet` (17 total at attack start). The windup/active gate ticks the
real `updateMonster` over a forced lunge: attack release + 5 active ticks with
the read counter frozen.

### 7. Debug wire (`-DDEBUG_HURTBOXES=1`)

Compiled and linked cleanly with the overlay enabled (patched tree). The debug
image still exceeds program space at the link size gate — verified identical on
a clean HEAD build, so this opt-in configuration is unchanged.

## Honest notes / deviations

- **Window box semantics**: the doc's box is face-relative, so the world centre
  is the DIR8 rotation of `(ox, oy)` (`combatFaceOffset`). All shipped windows
  have `oy == 0`, where the rotation term vanishes and the result is exactly
  the legacy scalar projection (`(fx*reach)>>4`, `(fy*reach)>>4`) — the
  660/0 parity suite is the proof. `oy` is carried and exercised for future
  boxes.
- **Multi-window path**: shipped attacks have one window, so
  `monsterWindowNext` stays cold in parity/perf. It is covered by a host unit
  test (synthetic `winRemain`, real WINDOW records) and by the device
  window-switch read gate; no data was invented to force it hot.
- **Spawn profile read deferred**: `initMonster` uses `creatureCacheReset`
  (identity + cache reset, no record reads) instead of `creatureLoad`, so the
  profile read burst does not enter shipping flash until migration C consumes
  it. `creatureLoad` itself is unchanged and still device-tested.
- **Attack identity**: render/debug use `Monster::atkIdx` + the cache
  (`moveType`, `win.box`); no table pointer exists anywhere in the sim/render.
  The telegraph frame choice is `moveType == MOVE_LUNGE`, which reproduces the
  old `kind == MK_LUNGE` selection for all shipped attacks.
- **Host tests adapted**: direct `m.atk = &MONSTER_ATTACKS[i]` setups became
  `monsterAttackSet(g, combat::ATTACK_<creature>_<attack>)`; the MONSTER_ATTACKS
  table pins stay as the reference values. New tests: chooseAttack cache load,
  cached-window hit flip on window switch, multi-window refresh.
- **Parity harness** reconstructs the fixture's forced attack (`kind` 0/1) from
  the LUNGE creature's records and hashes the cached scalars in the same order
  as `gen-parity-fixtures.js`; fixtures are byte-identical.
- No README update in this bead (status numbers there are stale but outside the
  issue's file list); noted for the gate bead if desired.

## Files changed

Modified: `src/core/game.hpp`, `src/core/combat.hpp`, `src/core/monster.hpp`,
`src/render.hpp`, `tst/monster_test.hpp`, `tst/combat_test.hpp`,
`tst/fxdatatest/combat_test.hpp`, `tst/fxdatatest/parity_test.hpp`,
`tst/fxdatatest/perf_test.hpp`.
No data, generated, mock, fxdata or fixture changes (`make gen-check` PASS,
parity fixtures empty diff).

## Blockers

None.
