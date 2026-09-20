# monhun-ardu-feel.3 — engine: guard behind (player delta vs facing dot)

Baseline: HEAD `8732379`, clean tree. No commit/push (orchestrator commits).
Verification: `make gen` x2, `make gen-check`, `make test`, `make test-tools`,
`make fxtest-headless` (full), `make size`.

## Schema (tools/gen-combat.py)

- Guard gains an optional key `facing` with string values `behind` / `front`,
  mapped through `GUARD_FACINGS = {"behind": 1, "front": 2}`. Unknown values
  fail validation via `read_enum` (message: `guard: facing: unknown value 'x'
  (want one of behind, front)`); default 0 = any.
- Packed as a u8 in the guard record's 9th byte (after `zonesBroken`):
  `0 = any, 1 = behind, 2 = front`. `SIZES["GUARD"]` 8 -> 9.
- `--dump` prints `... zonesBroken <list> facing any|behind|front`.
- Fact `HAS_GUARD_FACING` true iff any shipped guard declares a facing; a
  facing clause also clears `HAS_SIMPLE_GUARDS` so the dist-only fast path is
  not used.

## Evaluator (src/core/combat.hpp, src/core/monster.hpp)

- `enum GuardFacing { ANY=0, BEHIND=1, FRONT=2 }`.
- Pure helpers: `combatFacingDot(px,py,mx,my,fx,fy)` =
  `((px-mx)*fx + (py-my)*fy) >> 4` (int32 intermediate, body centres) and
  `combatGuardFacingOk(required, dot)` (any always; behind `dot < 0`; front
  `dot > 0`; dot 0 matches neither).
- `CombatGuardInput` gains `int16_t facingDot` (last field, value-initialised 0
  by existing aggregate initialisers).
- `combatGuardPasses` adds
  `if (combat::HAS_GUARD_FACING && !combatGuardFacingOk(gu.facing, in.facingDot)) return false;`
  — dead-code-eliminated while the fact is false.
- `patternGuardFull` (monster.hpp) computes `facingDot` from player/beast body
  centres mirroring the existing dist math, gated on `HAS_GUARD_FACING`
  (decision-time only; no per-tick cost, no new cart reads beyond the guard
  record already loaded).
- `CombatGuard` / `PkGuard` / generated `combat_data::Guard` all gain `facing`;
  static asserts updated (`sizeof(CombatGuard) == GUARD_SIZE` 9 B, guard
  mirror-drift assert for `facing`).

## Data

No shipped guard declares a facing clause, so `HAS_GUARD_FACING` is **false**
and the evaluator ships folded out (zero behaviour change). Device expectation
(`src/generated/combat_expect.hpp`) was regenerated for `GUARD_SIZE` 9 and the
shifted offsets; no facing spot value is asserted because no data uses it yet,
so `tst/fxdatatest/combat_test.hpp` stays size/offset-based (unchanged).

## Tests (permanent, native frameworks, co-located)

- `tst/combat_test.hpp`: guard record mirror now checks `facing`; new pure-helper
  block covers behind true/false, front true/false, dot==0 edge (neither), and
  the dot sign for a west-facing beast (east player behind, west player front,
  same column abeam, vertical offset ignored).
- `tst/combat_pack_test.hpp`: guard loop pins byte `o + 8` == `h.facing`
  (position + round-trip).
- `tools/tests/test_gen_combat.py`: dump string updated; default fact map adds
  `HAS_GUARD_FACING: false`; guard payload bytes extended with the facing byte;
  new `test_guard_facing_unknown_value_rejected` and
  `test_guard_facing_encodes_behind_front_and_fact` (behind=1, front=2, fact
  flips true, dump prints `facing behind`).

## Two-pass gen

`make gen` run twice after the `GUARD_SIZE` change (first pass moves the FX image
offsets and trips the stale-blob asserts, second converges); generated sets
(`combat.bin`, `combat_*`, `equip_meta.hpp`, `zone_meta.hpp`, `fxdata*`) staged
together. `make gen-check` clean.

## Verification tails

```
# make gen (2nd run)
gen-combat: 8 creatures, 8 attacks, 13 windows, 9 patterns, 9 steps, 5 skeletons, 11 zones, 1028 B, sha256 2938ed6e62aa98b995af6eca283462bd7369e3b3c9fac565dcd36fff1d04baa5
gen-combat: fxdata/tables/combat.bin (unchanged)
gen-combat: src/generated/combat_data.hpp (unchanged)
gen-combat: src/generated/combat_meta.hpp (unchanged)
gen-combat: src/generated/combat_expect.hpp (unchanged)
gen.sh: FX data + src/fxdata.h regenerated

# make gen-check
fxdata_manifest: PASS (82 generated artifacts unchanged)

# make test
Total Passed: 5423
Total Failed: 0

# make test-tools
Ran 180 tests in 8.956s
OK

# make fxtest-headless (full)
test_combat PASSED=293 FAILED=0
parity_test PASSED=660 FAILED=0
perf_test PASSED=5 FAILED=0
B pUs=6372 pHz=156 lHz=52 lTk=452 rMx=4772 rAv=4587 ram=595
(all other suites PASS)

# make size
size: .text=26922 .data=40 .bss=1692
size: flash=26962/29696 (2734 free)  ram=1732/2560
size: data facts: HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false HAS_GUARD_FACING:false
  HAS_GUARD_HP:false HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true HAS_HIT_STAGGER:false
  HAS_MULTI_STEP:false HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false HAS_STAGGER:true
  HAS_STEP_AFTER:false HAS_STEP_CHANCE:false HAS_WAIT_STEPS:false HAS_ZONES:true
```

Baseline at HEAD (`make size`, stashed): `flash=26980/29696 (2716 free)
ram=1732/2560`, `.text=26940`. **Delta: -18 B flash / +0 RAM** (the +9 B cart
blob growth shifts generated offsets; the gated evaluator adds no shipping
code, and the net image is 18 B smaller). Cart blob: 1019 -> 1028 B.

## Per-file summary

- `tools/gen-combat.py` — guard `facing` schema, packing, fact, dump.
- `tools/tests/test_gen_combat.py` — schema/pack/dump/fact tests.
- `src/core/combat.hpp` — GuardFacing, structs, helpers, evaluator clause, asserts.
- `src/core/monster.hpp` — `patternGuardFull` facingDot.
- `tst/combat_test.hpp`, `tst/combat_pack_test.hpp` — host + pack tests.
- `src/generated/*`, `fxdata/*`, `src/fxdata.h` — regenerated (two-pass).
- `tst/fxdatatest/combat_test.hpp` — unchanged (no facing data; size/offset only).
