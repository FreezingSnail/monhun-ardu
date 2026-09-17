# monhun-ardu-ljj.1 — Tools: gen-combat.py + single-image FX pipeline

Status: **DONE**. Tree left dirty for the orchestrator (no commit, no push).
Base HEAD: `f5cdb8b`. Design source: `docs/creature-framework.md` (blob §8,
schema §§3-7, reference values §11). No game/flash behavior touched: the blob
is packed but unused.

## Deliverables

1. **`data/skeletons.json` + `data/creatures/{lunge,sweep,heavy}.json`** —
   shipped 3 beasts per doc §11 exactly. `quad_32x24`/`quad_28x22`/`quad_40x28`
   each carry their body part so migration B can read w/h from the skeleton
   byte-identically; stats w/h/hp/spd come from `MONSTER_DEFS` (200/5, 150/7,
   320/3), spawn (200,40) all. Attacks lunge `{windup40, active10, recover55,
   dmg12, move lunge speedF34, window ox12 oy0 w24 h22}` and sweep `{48,12,60,
   move none, dmg9, window ox17 oy0 w32 h24}`, windows `t0=0..t1=active`.
   Profile all three: engage36 keep24 attack42 circle8/10 retreat6/10 cdBase55
   cdJitter40 spawnT90 spawnCd140 stunRecover24. Patterns: LUNGE `p_lunge`
   minDist33 → lunge, `p_sweep` maxDist32 → sweep; HEAVY minDist25 / maxDist24;
   SWEEP has **no** lunge pattern (`p_sweep` match-all).
2. **`tools/gen-combat.py`** (Python 3, stdlib only):
   - strict schema validation: unknown/missing keys, integer-only quantized
     fields (floats **and bools** rejected), ranges, phys/elem enum membership,
     stage thresholds strictly descending, window `0 <= t0 <= t1 <= active`,
     guard ordering (`minDist<=maxDist`, `hpBand` lo<=hi), pattern step refs
     resolve to creature-local attack ids, part predicates resolve, stage
     disable/enable attack refs resolve, duplicate local ids, local-namespace
     rules (skeleton/part/anchor/creature global or owner-scoped), u8/u16 size
     limits (255 records/section, u16 offsets).
   - deterministic pack: creatures sorted by id, per-creature attack/window/
     pattern/step source order preserved, little-endian explicit u8/u16, no
     padding, fixed section order.
   - outputs + `--dump` listing mode; `--root DIR` for fixture tests.
3. **`fxdata/fxdata.txt`**: `raw_t mhCombat = "tables/combat.bin"`; `tools/gen.sh`
   runs `gen-combat.py` before `fxdata-build.py`; `make gen` deterministic.
4. **Manifest**: `tools/fxdata_manifest.py` now tracks `data/**/*.json` inputs
   and `fxdata/tables/combat.bin` as an output (the three `src/generated`
   headers are already covered by `OUTPUT_GLOBS`); canonical JSON unchanged.
5. **`tools/tests/test_gen_combat.py`** (unittest) + co-located
   `tools/tests/fixtures/gen_combat/clean/` (skeleton + one creature with a
   part override, stages, phys/elem multipliers, guard predicate, ATK+WAIT
   steps): 27 cases — schema errors, id/ref errors, integer-only, size limits,
   determinism, dump smoke, and byte-level blob ABI checks against
   `combat_meta.hpp`.
6. **README** pipeline section + docs cross-ref; repo layout updated;
   `Makefile FORMAT_SKIP` and `.githooks/pre-commit` now skip `src/generated/`
   (generated headers stay out of clang-format churn).

## Blob ABI (499 B, sha256 `fe41b01e71a22c3e2c83dc9c6770929ffd7a2531f245c11d735e0f60bbc39994`)

Header 32 B: `magic u16 0x4D43` (bytes `43 4D`), `version u8 1`, `flags u8 0`,
then 14 little-endian `u16` counts. Section order:
`creatures, profiles, skeletons, parts, stages, anchors, elems, refs, attacks,
windows, patterns, guards, predicates, steps`.

Actual section offsets/counts for the shipped data:

| Section | Off | Count | Record | Size |
|---|---|---|---|---|
| header | 0 | – | – | 32 |
| creatures | 32 | 3 | creature | 17 |
| profiles | 83 | 3 | profile | 22 |
| skeletons | 149 | 3 | skeleton | 4 |
| parts | 161 | 3 | part | 18 |
| stages | 215 | 0 | stage | 10 |
| anchors | 215 | 6 | anchor | 2 |
| elems | 227 | 0 | elem | 2 |
| refs | 227 | 0 | ref | 1 |
| attacks | 227 | 6 | attack | 22 |
| windows | 359 | 6 | window | 10 |
| patterns | 419 | 5 | pattern | 3 |
| guards | 434 | 5 | guard | 9 |
| predicates | 479 | 0 | predicate | 3 |
| steps | 479 | 5 | step | 4 |

Record field layouts are documented byte-for-byte in the `gen-combat.py`
docstring and generated header comments. Global indices: creatures sorted by
id (`heavy=0, lunge=1, sweep=2`), attacks/windows/patterns/guards/steps in
source order per creature; `GUARD_x = PATTERN_x` (1:1); part predicates are
`u8 partIdx` into the global part list (skeleton parts first, then per-creature
overrides). `combat_data.hpp` carries the host mirror (structs + `std::array`
sections + index constants); `combat_expect.hpp` pins record sizes, per-creature
spot values and the blob sha256; `combat_meta.hpp` carries `MAGIC/VERSION/FLAGS/
SIZE/HEADER_SIZE`, per-section offsets/counts and per-record offsets
(`*_OFF`) + indices. Generated headers compile clean host-side with
`g++ -std=c++17 -Wall -Wextra`.

Single FX image: `mhCombat = 0x005283`, `FX_DATA_BYTES = 21622` (was 21123);
still one `fxdata/fxdata.bin`, `combat.bin` never flashed separately.

## Verification (exact tails)

### 1. `make gen` twice — second run leaves everything unchanged

```
$ git status --porcelain | sort > build/final-run1.status
$ make gen > build/final-gen.log 2>&1
$ git status --porcelain | sort > build/final-run2.status
$ diff build/final-run1.status build/final-run2.status
PASS: second make gen leaves all tracked/untracked state unchanged
```

Second-run gen-combat tail (all outputs byte-identical, written only on change):

```
gen-combat: 3 creatures, 6 attacks, 6 windows, 5 patterns, 5 steps, 3 skeletons, 3 parts, 499 B, sha256 fe41b01e71a22c3e2c83dc9c6770929ffd7a2531f245c11d735e0f60bbc39994
gen-combat: fxdata/tables/combat.bin (unchanged)
gen-combat: src/generated/combat_data.hpp (unchanged)
gen-combat: src/generated/combat_meta.hpp (unchanged)
gen-combat: src/generated/combat_expect.hpp (unchanged)
fxdata_manifest: fxdata/manifest.json up to date (19 images, 11 inputs, 9 outputs)
```

Blob size **499 B**; record sizes **creature 17, profile 22, skeleton 4, part
18, stage 10, anchor 2, elem 2, ref 1, attack 22, window 10, pattern 3, guard 9,
predicate 3, step 4** (header 32).

### 2. `make gen-check` PASS + `make test-tools` PASS

```
fxdata_manifest: PASS (34 generated artifacts unchanged)
```

```
Ran 43 tests in 2.650s

OK
```

(16 manifest tests + 27 gen-combat tests.)

### 3. `make build` — shipping flash/RAM unchanged

```
Sketch uses 28378 bytes (95%) of program storage space. Maximum is 29696 bytes.
Global variables use 1950 bytes (76%) of dynamic memory, leaving 610 bytes for local variables. Maximum is 2560 bytes.
```

```
.text                       28324         0
.data                          54   8388864
.bss                         1896   8388918
```

flash = 28378, RAM = 1950 — identical to the pinned baseline (data is packed
but unused).

### 4. `python3 tools/gen-combat.py --dump` — sample (lunge)

```
creature lunge (skeleton quad_32x24, stats w32 h24 hp200 spd5, spawn 200,40)
  attack lunge: windup40 active10 recover55 dmg12 move lunge(34) windows 1
    window 0: t[0,10] box(12,0,24,22) dmgMul 100
  attack sweep: windup48 active12 recover60 dmg9 move none windows 1
    window 0: t[0,12] box(17,0,32,24) dmgMul 100
  pattern p_lunge: guard minDist33 maxDist255 hp[0,100] player0x00 cd0 chance100
    step 0: ATK lunge.lunge after0 chance100
  pattern p_sweep: guard minDist0 maxDist32 hp[0,100] player0x00 cd0 chance100
    step 0: ATK lunge.sweep after0 chance100
```

### 5. Negative tests — schema errors rejected

```
gen-combat: error: data/creatures/beast.json.attacks[0].windows[0]: window ends outside the active phase: t1 9 > active 6
gen-combat: error: data/creatures/beast.json.attacks[0]: windup: expected an integer, got 20.0
gen-combat: error: data/creatures/beast.json.attacks[0]: phys: unknown value 'FIRE' (want one of BLUNT, SHOT, SLASH)
gen-combat: FAIL (3 errors)
```

Other covered examples (each asserted by a unit test): bools rejected, range
overflow, stage thresholds ascending, inverted windows, inverted guard,
unknown step/skeleton/part refs, duplicate local/creature/skeleton ids, id
length, part override colliding with a skeleton part, 256-window size limit.

### 6. `make test` — host suite unaffected

```
Total Passed: 1490
Total Failed: 0
```

## Files changed / added

Modified: `.githooks/pre-commit`, `Makefile`, `README.md`, `fxdata/fxdata.txt`,
`fxdata/fxdata.bin`, `fxdata/fxdata-data.bin`, `fxdata/fxdata.h`,
`src/fxdata.h`, `fxdata/manifest.json`, `tools/gen.sh`,
`tools/fxdata_manifest.py`, `tools/tests/test_fxdata_manifest.py`,
`tools/tests/fixtures/fxdata_manifest/clean/fxdata/manifest.json`.
Added: `data/skeletons.json`, `data/creatures/{lunge,sweep,heavy}.json`,
`tools/gen-combat.py`, `tools/tests/test_gen_combat.py`,
`tools/tests/fixtures/gen_combat/**`, `fxdata/tables/combat.bin`,
`src/generated/combat_{data,meta,expect}.hpp`,
`tools/tests/fixtures/fxdata_manifest/clean/fxdata/tables/combat.bin`.

## Notes for beads 2-5

- Offsets in `combat_meta.hpp` are absolute within the `mhCombat` raw_t
  section; on AVR `mhCombat + offset` is the record address. u16 offsets are
  valid while the blob stays < 64 KB (generator hard-fails otherwise).
- Windows are inclusive with `t` 1-based, and shipped windows are `t0=0,
  t1=active` so `t=1..active` all match (spike parity contract).
- `lungeSplit` is expressed only as pattern guards (`minDist`/`maxDist`), never
  a profile sentinel; pattern order is semantic first-match.
- `profile.partCount` is the effective part count (skeleton + overrides); the
  creature's own overrides start at `creature.firstPart/partCount`.
- Host loader should read `combat_data.hpp` structs; device loader should read
  the blob via offsets — both are final ABI here.

## Blockers

None.
