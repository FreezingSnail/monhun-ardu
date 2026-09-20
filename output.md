# monhun-ardu-feel.6 — engine: creature enrage phase (hpPct, spdMul, faceHold, cue)

Baseline: HEAD `4934b30`, clean tree. No commit/push (orchestrator commits).
Verification: `make gen` x2, `make gen-check`, `make test`, `make test-tools`,
`make fxtest-headless` (full), `make size`.

## Schema + pack (tools/gen-combat.py)

- Creature `stats.enrage` optional object: `{ hpPct 0..100, spdMul 0..255,
  faceHold 0..255, cue optional CUES }`. Missing required keys, unknown keys,
  range violations and non-integer values all fail via `check_keys`/`read_int`;
  `cue` validates against the existing `CUES` table (`none`/`windup`/
  `part_break`). Absent = all-zero (disabled).
- Packed as the creature record's last four bytes (25..28), after `brokenH`:
  `enrageHpPct, enrageSpdMul, enrageFaceHold, enrageCue`. `SIZES["CREATURE"]`
  25 -> 29.
- `--dump` prints `..., enrage hpPct40 spdMul150 faceHold8 cue2`.
- `combat_expect.hpp` gains `CREATURE_<ID>_ENRAGE_{HP_PCT,SPD_MUL,FACE_HOLD,CUE}`
  **only for creatures that author `hpPct > 0`** (device-image budget: four dead
  constants per creature otherwise; see "Device test budget" below). No shipped
  creature declares enrage, so no pins ship today.
- New data fact `HAS_ENRAGE` (true iff any creature's `hpPct > 0`). False on the
  shipped set. The runtime branch is compiled unconditionally per the bead
  (`hpPct == 0` is the disabled path); the fact is emitted for visibility and
  future gating, exactly like the DESIGN's "New data fact HAS_ENRAGE".

## Loader (src/core/combat.hpp, src/core/game.hpp)

- `CombatCreature` / packed `PkCreature` / generated `combat_data::Creature` gain
  the four enrage bytes; `combatCreatureRead` decodes them.
- `CombatEnrage` (game.hpp): `hpPct, spdMul, faceHold, cue, fired` — 5 B AVR.
  Added to `CombatState` (offset 84).
- AVR: `combatCreatureEnrageRead(cid, e)` fetches the quad in two cart u16 reads
  (hpPct/spdMul, faceHold/cue) at spawn; host reads the generated struct.
- Static asserts: `sizeof(CombatState)` 84 -> 89 (nested packs to 92 on the host,
  asserted as 89 on AVR by construction); new `PkCreature` adjacency asserts
  (enrage quad follows `brokenH` and stays contiguous).
- `creatureCacheReset` clears `enrage` (latch to 0); `creatureLoad` seeds it after
  the collide/isStatic reads.

## Apply (src/core/monster.hpp, updateMonster)

- After the dead check and before the facing block / FSM switch, so the tick it
  fires already uses the new values:
  - fires once when `!fired && hpPct > 0 && m.hp*100 <= m.hpMax*hpPct`
    (int16 hp/hpMax promoted to `int32_t`, so no overflow);
  - `m.spd = max(1, (m.spd * spdMul) / 100)` (truncating, floor 1);
  - `pr.faceHold = enrage.faceHold` (the RAM profile cache, so the facing block
    this tick reads the new hold);
  - `fired = 1` — one-shot latch.
- `cue` is stored in the cache but no audio path is added: the audio edge on
  enrage is a follow-up (see below).
- Shipped data leaves `hpPct` 0, so shipped fights are byte-identical; existing
  host/device suites pin this (test_parity 660/660).

## Tests (permanent, native frameworks, co-located)

- `tst/monster_test.hpp` — 3 new cases: (a) crossing the threshold applies
  spdMul truncating (7*150/100 = 10) and faceHold (0 -> 8) exactly at the
  boundary, and nothing above it; (b) further damage below the threshold does
  not re-fire or grow (one-shot latch); (c) hpPct 0 inert and a tiny mul
  (5*3/100 = 0) floors spd at 1.
- `tst/combat_pack_test.hpp` — creature decode loop pins bytes 25..28; spot
  values pin the shipped quad at 0.
- `tst/combat_test.hpp` — creature mirror loop adds the four fields;
  `creatureLoad` cache lifecycle asserts enrage reset + latch clear.
- `tst/fxdatatest/combat_test.hpp` — one combined check that the spawn-cached
  enrage word is 0 (see budget note).
- `tools/tests/test_gen_combat.py` — default/emit/dump, missing key, unknown key,
  range, integer-only, cue-enum rejection, `HAS_ENRAGE` fact, expect pin
  presence/absence; packed-record bytes updated for the new record size.
- `tst/combat_test.hpp` counts unchanged; no `HAS_*` fact flips, so
  `test_data_facts_match_fixture` gains only the `HAS_ENRAGE` key.

## Two-pass gen

`make gen` run twice after the `CREATURE_SIZE` change (first pass moves the FX
image/zone/equip offsets, second converges). Generated sets staged together.
`make gen-check` clean.

## Device test budget (deviation, documented)

`test_combat` builds at 29474 B against the 29696 B ceiling at HEAD (99%); the
`CombatState` +5 B struct change plus the enrage branch pushed the same test to
29664 B (+190 B). That leaves 32 B. Two budget guards were applied so the suite
still fits:

1. `combat_expect.hpp` emits enrage pins only for creatures that author the
   phase. Emitting four dead constants per shipped creature added a device-test
   `.text` cost with zero AVR-side value; the host pack test still pins bytes
   25..28 directly.
2. `tst/fxdatatest/combat_test.hpp` uses one combined word check instead of four
   per-field expects.

The AVR enrage read path (`combatCreatureEnrageRead`) is exercised by
`creatureLoad` in `test_combat`; its read cost is covered by the existing
`spawn burst <= 40 reads` gate (2 extra u16 reads, actual ~36).

## Audio follow-up

`enrage.cue` is validated, packed, cached and available on `g.combat.enrage.cue`,
but this bead adds **no** audio consumer. An enrage cue needs a follow-up bead to
route the cue byte into `audio.hpp` (an edge-triggered play at the latch tick,
mirroring the attack-cue path) plus a test. `audio.hpp` was not touched.

## Verification tails

```
# make gen (2nd run)
gen-combat: 8 creatures, 8 attacks, 13 windows, 9 patterns, 9 steps, 5 skeletons, 11 zones, 1068 B, sha256 8f9a1f70efdd4f9dfb423a997c77bf6d0f9ee43a35b976125e9ff47359ae6bd6
gen-combat: fxdata/tables/combat.bin (unchanged)
gen-combat: src/generated/combat_data.hpp (unchanged)
gen-combat: src/generated/combat_meta.hpp (unchanged)
gen-combat: src/generated/combat_expect.hpp (unchanged)

# make gen-check
fxdata_manifest: PASS (82 generated artifacts unchanged)

# make test
Total Passed: 5558
Total Failed: 0

# make test-tools
Ran 189 tests in 10.828s
OK

# make fxtest-headless (full)
asset_test PASSED=270 FAILED=0
test_audio PASSED=17 FAILED=0
test_boot PASSED=4 FAILED=0
combat_test PASSED=295 FAILED=0
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

# make size
size: .text=27338 .data=40 .bss=1698
size: flash=27378/29696 (2318 free)  ram=1738/2560
size: data facts: HAS_ENRAGE:false HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false
  HAS_GUARD_FACING:false HAS_GUARD_HP:false HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true
  HAS_HIT_STAGGER:false HAS_MULTI_STEP:false HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false
  HAS_STAGGER:true HAS_STEP_AFTER:false HAS_STEP_CHANCE:false HAS_WAIT_STEPS:false HAS_ZONES:true
```

Baseline at HEAD `4934b30` (feel.4): `flash=27066/29696 (2630 free) ram=1733/2560`,
`.text=27026`, `.bss=1693`. **Delta: +312 B flash / +5 RAM**. Spike predicted
+170 B / +5 RAM; the measured value is +142 B over the spike. The extra is the
`CombatState` size assert/struct growth and, more, the inline enrage branch in
`updateMonster` (the spike's prototype folded on its fact; this bead compiles the
branch unconditionally per the bead wording). Perf unchanged (rMx 4772; ~2636 us
under the 7407 us floor). Cart blob: 1036 -> 1068 B (+32, four bytes per creature).
`test_combat` device image: 29474 -> 29664 B (+190), 32 B below the ceiling.

## Per-file summary

- `tools/gen-combat.py` — creature enrage schema, pack, dump, expect pins
  (conditional), `HAS_ENRAGE` fact.
- `tools/tests/test_gen_combat.py` — schema/pack/dump/fact tests; record bytes.
- `src/core/combat.hpp` — value/packed/generated mirrors, enrage read, assert.
- `src/core/game.hpp` — `CombatEnrage`; `CombatState.enrage`.
- `src/core/monster.hpp` — one-shot enrage apply in `updateMonster`.
- `tst/monster_test.hpp`, `tst/combat_test.hpp`, `tst/combat_pack_test.hpp`,
  `tst/fxdatatest/combat_test.hpp` — host + pack + device tests.
- `src/generated/*`, `fxdata/*`, `src/fxdata.h` — regenerated (two-pass).
