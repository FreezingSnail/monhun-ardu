# monhun-ardu-ljj.2 — Loader: src/core/combat.hpp + host/pack/Ardens tests

Status: **DONE**. Tree left dirty for the orchestrator (no commit, no push).
Base HEAD: `7dc928a` (ljj.1). Design source: `docs/creature-framework.md`
§§3–9 + §11, generated ABI: `src/generated/combat_{data,meta,expect}.hpp`.
No behavior wiring: game render/sim untouched; shipping flash byte-flat.

## Deliverables

1. **`src/core/combat.hpp`** — header-only production loader, one code path over
   two backends:
   - host reads the generated `combat_data.hpp` structs (identity);
   - AVR reads the packed `mhCombat` FX blob via `mhFxRead*` at
     `mhCombat + <combat_meta.hpp offset>`, one cart access per scalar.
   Typed value reads for every section (creature/profile/skeleton/part/stage/
   anchor/elem/ref/attack/window/pattern/guard/predicate/step) plus field-targeted
   hot-path accessors (`combatPartDmgMul`/`PhysMul`/`ElemMul`/…,
   `combatPatternGuardIdx`, `combatAttackFirstWindow`). Packed ABI mirrors are
   `static_assert`ed against the generated record sizes, so a blob layout drift
   fails the compile.
   - `creatureLoad(Game&, id)` reads creature profileIdx + full profile into the
     cache, resets stages/cursor/stagger, bad id → creature 0.
   - `attackLoad` caches attack scalars + first window (~15 reads); 
     `attackWindowLoad` refreshes on window switch; `combatTick` is the
     zero-read per-tick path.
   - Guard eval: inclusive integer ranges, hp band, required player flags,
     cooldown-vs-sinceUse, part-stage predicates (`>=`/`<=`/`==`), deterministic
     tick-derived chance `hash(tick, creature, pattern, step) % 100 < chance`
     (no RNG state), first-match selection is caller order per doc §6.
   - Damage/stagger routing per doc §3: integer percent, truncating division at
     each step, 32-bit intermediates; candidate overlap set is caller-side
     (native, migration B); winner = highest final multiplier, tie → lowest
     part id; `bodyShare` routes to the body pool; stagger = attack stagger ×
     winning multiplier.
   - Part stages: 2 bits × 8 parts saturating at 3, `combatStageCross` threshold
     fold, `combatPartStageForHp`, stage-effect projections
     (`dmgMulOverride`/`speedMul`/`hurtOff`/`stagger`/`cue`) and
     `combatAttackDisabled` via REFS disable/enable lists.
2. **`src/core/game.hpp`** — `CombatState` + cache structs live next to Game
   (not in combat.hpp) because Game stores them by value and combat.hpp includes
   game.hpp; circularity otherwise. `Game::combat` appended last: **50 B AVR**
   (profile 22 + attack/window 21 + runtime 7), `static_assert`ed in combat.hpp.
3. **`src/core/fxmem.hpp`** — optional `MH_FX_READ_COUNT` access counter (one
   increment per `mhFxReadU8`/`mhFxReadU16`; `pure` is dropped under the macro
   so GCC cannot reuse stale counter loads). Undefined in shipping builds: zero
   cost.
4. **Host tests `tst/combat_test.hpp`** (registered in `tst/main.cpp`):
   full field-count parity vs `combat_data.hpp` for every record, cache
   lifecycle (load/window switch/reload/tick floor), guard boundaries at 24/25,
   32/33, 0/255 + pure clause/predicate vectors, pinned chance rolls, damage
   chain reference vectors (truncation, tie-break, stagger, 32-bit), stage
   bitfield/saturation/independence + break-effect projections, bad-id fallback.
5. **Host pack parity `tst/combat_pack_test.hpp`**: opens
   `fxdata/tables/combat.bin` from the repo root, embedded SHA-256
   (self-checked against the `abc` FIPS vector) vs `combat_expect::BLOB_SHA256`,
   header magic/version/flags/14 counts, section tiling, all 45 named record
   offsets land record-aligned inside the right section, and every section
   record decodes byte-for-byte to the generated host structs.
6. **Device `tst/fxdatatest/test_combat.ino` + `combat_test.hpp`**: real FX
   reads after the enableOLED/waitForNextPlane/disableOLED bracket; header
   (magic/version/flags/all 14 counts); per-record spot values vs
   `combat_expect.hpp`; cross-ref walk (creature→skeleton→part, attack→window,
   pattern→guard→step→attack); guard eval; damage routing; break-stage
   transition; bad-id fallback; read-count gate. Final bare `P`.
7. **README** touch-ups: status table (2716 host asserts, combat suite, RAM
   2000/560 free, perf RAM 417), `combat.hpp` core row, pipeline text, device
   test list, challenge numbers.

## RAM / flash

| Metric | Baseline | Now | Delta |
|---|---|---|---|
| Shipping flash | 28378 / 29696 B | **28378 / 29696 B** | **0** |
| Shipping RAM | 1950 / 2560 B | **2000 / 2560 B** | **+50 B** |
| Perf free RAM | 467 B | **417 B** | −50 B |

The loader is compiler-invisible to the shipping build (nothing in the sketch
includes `combat.hpp`), so LTO retains zero loader code — flash is identical.
The +50 B is exactly the `Game::combat` cache the design reserves (≤50 B gate).
Device test build (test-only): 17826 flash / 1966 RAM.

## Verification (exact tails)

### 1. `make test` — 2716 passed / 0 failed

New suites included (`CombatSuite` + `CombatPackSuite` = 1226 new asserts):

```
Total Passed: 2716
Total Failed: 0
```

### 2. `make build` — flash flat, RAM +50 as designed

```
Sketch uses 28378 bytes (95%) of program storage space. Maximum is 29696 bytes.
Global variables use 2000 bytes (78%) of dynamic memory, leaving 560 bytes for local variables. Maximum is 2560 bytes.
```

### 3. `make fxtest-headless` — all suites PASS incl. test_combat

```
=== test_assets ===
asset_test PASSED=254 FAILED=0
P
test_assets: PASS
=== test_audio ===
test_audio PASSED=14 FAILED=0
P
test_audio: PASS
=== test_boot ===
test_boot PASSED=4 FAILED=0
P
test_boot: PASS
=== test_combat ===
C reads spawn=17 attack=15 guard=10 hit=9 tick256=0
combat_test PASSED=157 FAILED=0
P
test_combat: PASS
=== test_data ===
data_test PASSED=221 FAILED=0
P
test_data: PASS
=== test_hud ===
test_hud PASSED=17 FAILED=0
P
test_hud: PASS
=== test_menu ===
menu_test PASSED=55 FAILED=0
P
test_menu: PASS
=== test_parity ===
parity_test PASSED=660 FAILED=0
P
test_parity: PASS
=== test_perf ===
B pUs=6389 pHz=156 lHz=52 lTk=988 rMx=5056 rAv=4809 ram=417
perf_test PASSED=5 FAILED=0
P
test_perf: PASS
```

Perf gates vs baseline: `rMx=5056 <= 7407`, `pHz=156 >= 135`,
`lHz=52 >= 45`, `ram=417 >= 300` (free RAM drop is exactly the 50 B cache).
Parity stays **660/0**.

### 4. `make gen-check` — PASS

```
fxdata_manifest: PASS (34 generated artifacts unchanged)
```

### 5. Pack parity sha256 + device read counts

```
$ shasum -a 256 fxdata/tables/combat.bin
fe41b01e71a22c3e2c83dc9c6770929ffd7a2531f245c11d735e0f60bbc39994  fxdata/tables/combat.bin
$ wc -c < fxdata/tables/combat.bin
     499
$ grep -o "fe41b01e[0-9a-f]*" src/generated/combat_expect.hpp
fe41b01e71a22c3e2c83dc9c6770929ffd7a2531f245c11d735e0f60bbc39994
```

(`CombatPackSuite` recomputes the same digest in C++ with its own SHA-256 and
asserts equality with `combat_expect::BLOB_SHA256`; 40 header/size asserts +
135 offset asserts + 401 record-decode asserts + 21 spot values all pass.)

Device read-count summary from `test_combat`:

| Phase | Reads | Gate |
|---|---|---|
| Spawn (`creatureLoad`) | 17 | ≤ 40 |
| Attack start (`attackLoad` + window) | 15 | ≤ 24 |
| Guard decision (`combatGuardPasses`) | 10 | ≤ 12 |
| Landed hit (`combatResolveHit`) | 9 | ≤ 12 |
| Steady state (`combatTick` × 256) | **0** | 0/tick |

## Honest notes / deviations

- Cache structs are declared in `game.hpp` (before `struct Game`), not inside
  `combat.hpp`: `Game` holds them by value and includes `combat.hpp`, so
  declaring them in combat.hpp would be circular. The loader, its size
  static_asserts and all logic are in combat.hpp.
- Shipped blob has `STAGES_COUNT=0`, `ELEMS_COUNT=0`, `PREDICATES_COUNT=0`
  (three body parts, all 100/100/100, no break stages). The real-record paths
  (read layer, cross-refs, tie-break, fallback) are device-tested; the
  stage-transition, predicate-op and stage-effect-flag paths are pinned with
  synthetic `CombatStage`/`CombatPredicate` values in the host suite (same pure
  helpers the loader calls). No data was added — the ljj.1 sha256 baseline is
  untouched, and the first real multi-part creature is bead ljj.6.
- Stage `flags` bit 0x01 carries hurtOn *presence*, not value, in the packed ABI
  (ljj.1 generator). v1 convention documented in combat.hpp: the bit means
  "no longer hurtable while this stage is active" (matches the doc's only
  example, `hurtOn:false`). Content bead can revisit with a schema bump.
- Window `flags` is reserved/always 0 and deliberately not cached (cache budget
  exact at 50 B); the pack test pins it to 0.
- Parts ≥ 8 are not stage-tracked (2 bits × 8 in the 50 B budget); v1 data has
  1 part per creature. Documented at the bitfield.
- `sinceUse` guard cooldown semantics (`sinceUse >= guard.cooldown`, 0xFFFF =
  never) chosen because the doc leaves guard cooldown v1-inert (all 0); the
  profile-level cdBase/cdJitter stay native in migration C.

## Files changed / added

Modified: `src/core/game.hpp` (CombatState caches + Game field),
`src/core/fxmem.hpp` (optional read counter), `tst/main.cpp` (suite
registration), `README.md` (status/pipeline touch-ups only).
Added: `src/core/combat.hpp`, `tst/combat_test.hpp`,
`tst/combat_pack_test.hpp`, `tst/fxdatatest/combat_test.hpp`,
`tst/fxdatatest/test_combat.ino`.
No data/generated/tool changes (`make gen-check` PASS).

## Blockers

None.
