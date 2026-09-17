# monhun-ardu-ljj.5 — Migration C: profile + pattern interpreter replaces the monster FSM

Status: **DONE**. Tree left dirty for the orchestrator (no commit, no push).
Base HEAD: `b7da58a` (ljj.4) + the cancelled migration-C working tree, finished
here. Behavior byte-identical: parity fixtures regenerate with an empty diff.

## Deliverables

1. **`tools/gen-combat.py`** — emits data facts into
   `src/generated/combat_meta.hpp`, computed from the compiled model:
   `HAS_STAGGER`, `HAS_WAIT_STEPS`, `HAS_STEP_AFTER`, `HAS_STEP_CHANCE`,
   `HAS_MULTI_STEP`, `HAS_MULTI_WINDOW`, `HAS_SIMPLE_GUARDS` (shipped values:
   all false except `HAS_SIMPLE_GUARDS = true`). Each fact is true iff at least
   one shipped record uses the feature, so adding data re-compiles the generic
   path; the interpreter keeps the full feature set.
2. **`src/core/combat.hpp`** — ABI-pinned burst reads and a dist-only guard
   probe:
   - `combatPatternGuardRangeRead`: minDist/maxDist are the guard record's
     leading byte pair, so a simple guard is one u16 cart read.
   - `combatProfileLoad` fills the Game cache in place (no 22 B by-value copy);
     device `combatWindowRead` bulk-reads the 9 B cache mirror; device
     `attackLoad` bursts moveType/moveSpeedF, facing, firstWindow/windowCount
     and the contiguous windup..dmg quad; `combatStepRef` is a single-byte step
     read; `combatCreaturePatternHeadRead` packs firstPattern|patternCount.
   - New `static_assert` adjacency pins for every burst offset.
3. **`src/core/monster.hpp`** — gating of unreached machinery behind the
   constexpr facts (plain `if`, C++11-safe), with the generic paths intact:
   - `patternGuardOk` splits into `patternGuardFull` (loader evaluator: hp
     band, player flags, cooldown, predicates, tick chance) and a simple path
     (one cart read + inclusive min/max compare) selected by
     `HAS_SIMPLE_GUARDS`.
   - `patternSteps` splits into `patternStepsGeneric` (cursor/stepT/WAIT/after/
     chance) and `patternStepsSingle` (single-step, no-delay, always-hit fast
     path) selected by `HAS_MULTI_STEP`/`HAS_WAIT_STEPS`/`HAS_STEP_AFTER`/
     `HAS_STEP_CHANCE`.
   - `monsterWindowNext` call gated by `HAS_MULTI_WINDOW` (plus winRemain
     bookkeeping); `monsterStaggerAdd` call and the `MS_STAGGER` case gated by
     `HAS_STAGGER`. The generic functions remain compiled when flags are true;
     on the shipped data the linker drops them (`avr-nm` proof below).
4. **`tst/fxdatatest/boot_test.hpp`** — scoped the first `mh::Game` so both
   753 B games are not live at once. The migration-C inlining grew the boot
   test's AVR frame past the ~1.37 KB below the stack; the overflow silently
   corrupted globals (no serial, Ardens "no serial" FAIL). Scoping fixed it
   with no assertion changes (PASSED=4 FAILED=0).
5. **`tools/tests/test_gen_combat.py`** — new
   `test_data_facts_match_fixture` pins the emitted flag set for the
   fully-featured fixture (stagger/WAIT/after/multi-step/player guard true,
   multi-window false), so a wrong fact cannot silently change behavior.

## Flash/RAM budget

Shipping build (`make build`, arduboy-fx, --optimize-for-debug):

```
Sketch uses 29314 bytes (98%) of program storage space. Maximum is 29696 bytes.
Global variables use 2006 bytes (78%) of dynamic memory, leaving 554 bytes for local variables. Maximum is 2560 bytes.
```

- vs `b7da58a` baseline **29332 B**: **−18 B**; headroom **382 B** (gate ≥300).
- vs pre-framework **28378 B**: +936 B (the 499 B combat blob now lives in the
  FX image, not the sketch; measured against 28378+499 = 28877 the net
  interpreter/loader cost is +437 B — reported as measured, not spun).
- RAM unchanged at 2006 B (CombatState still 56 B per `static_assert`).

Per-stage measurements (each a full rebuild):

| stage | flash | saved |
|---|---|---|
| cancelled-run starting point | 30440 | — |
| data facts + guard/step/window/stagger gating | 29632 | −808 |
| single-step pattern fast path (`HAS_MULTI_STEP`) | 29572 | −60 |
| profile load-in-place + window bulk + attack burst | 29360 | −212 |
| winRemain else removal + guard bounds removal | 29354 | −6 |
| stepIdx=1 removal | 29348 | −6 |
| chooseAttack cursor-init gating | 29338 | −10 |
| `combatStepRef` single-byte step read | 29324 | −14 |
| `combatCreaturePatternHeadRead` u16 pair | 29314 | −10 |
| **total saved** | | **−1126** |

## Verification (exact tails)

1. `make build` — see block above (`29314` / RAM `2006`). Baseline rebuilt at
   `b7da58a` in a clean worktree for the comparison: `29332` / RAM `2006`.

2. Parity fixtures (hard gate):

```
$ node tools/gen-parity-fixtures.js && git diff --stat tst/fxdatatest/parity_fixtures.hpp
wrote tst/fxdatatest/parity_fixtures.hpp
scenes=20 ticks=1269 snapshots=32 cpFields=20
(empty diff)
```

3. Host suite:

```
$ make test
Total Passed: 2848
Total Failed: 0
```

4. `make fxtest-headless` (Ardens, all 10 sketches) — exit 0:

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
C reads spawn=6 attack=5 guard=2 hit=9 tick256=0 simAtk=5 simTk=0 winSw=1
combat_test PASSED=195 FAILED=0
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
B pUs=6383 pHz=156 lHz=52 lTk=984 rMx=5040 rAv=4800 ram=416
perf_test PASSED=5 FAILED=0
P
test_perf: PASS
```

   B-line gates: `rMx=5040 <= 7407`, `pHz=156 >= 135`, `lHz=52 >= 45`,
   `ram=416 >= 300` — all pass. Image sizes: parity 29478 B (218 free), perf
   29684 B (12 free) — perf **links** (it had 10 B free before this bead).
   Combat read-count gates unchanged: spawn=6, attack=5, guard=2, hit=9,
   256-tick steady state=0.

5. `make gen-check`:

```
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (34 generated artifacts unchanged)
```

6. Tooling suite including the new flag-pinning test:

```
$ make test-tools
Ran 44 tests in 2.451s
OK
```

7. avr-nm / objdump evidence (shipping ELF):

```
$ avr-nm -C --size-sort -S dist/monhun-ardu.ino.elf | grep -E " [tT] mh::"
0000188a 0000011c t mh::patternStepsSingle(mh::Game&) [clone .constprop.29]
0000166a 00000220 t mh::initMonster(mh::Game&, signed char) [clone .constprop.34]
00001c00 00000328 t mh::monsterOnHit(mh::Game&, int, int, int, int, int)
...

$ avr-nm -C dist/monhun-ardu.ino.elf | grep -cE \
  "combatGuardPasses|combatChanceRoll|combatChancePasses|monsterWindowNext|monsterStaggerAdd|combatPartStaggerNow|combatGuardRead|combatPredicateRead"
0        # every gated helper was dropped from the shipped image

$ grep -nE "dist > (36|24|42)|55 \+ \(g\.tick % 40\)|% 40|m\.t = 90|m\.cd = 140|m\.t = 24" src/core/monster.hpp
none     # old FSM literals are gone from the source
```

   `avr-objdump -dl` attributes shipped code to the interpreter, e.g.
   `patternStepsSingle` at 0x188a reads the cached cursor and the pattern/step
   cart records through the loader (`monster.hpp:323-329`,
   `combat.hpp:590/625`), and the updateMonster lines (230-540) emit the
   profile-driven FSM. `avr-size -A`: `.text 29260`, `.data 54`, `.bss 1952`.

## Notes / honest limits

- The generic paths are not deleted: setting any data fact true recompiles the
  corresponding full machinery (loader guard evaluator, WAIT/after/chance step
  loop, multi-window refresh, stagger meter). Host tests still exercise the
  generic helpers (`combatGuardPasses`, `combatChancePasses`, predicates,
  `monsterWindowNext` with a synthetic winRemain, etc.).
- `fxdata/manifest.json` changed as a generated-artifact consequence of the new
  `combat_meta.hpp` constants; `make gen-check` passes after regeneration.
- test_perf now links with 12 B free; the interpreter is within the shipping
  budget with 382 B headroom, so this is the tightest image, not the shipping
  sketch.
