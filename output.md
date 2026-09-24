# monhun-ardu-bih.6 — art draw phase 4: delete legacy kind art code + docs/pins

## What changed

Sweep + closure for the art-draw epic. Phases 1-3 had already deleted the
per-kind render machinery; this bead removes the last dead data path (the
creature-record `sheet` u8) and re-pins everything that moved.

Code / data:

- `src/core/combat.hpp`: dropped the dead `CombatCreature::sheet` /
  `detail::PkCreature::sheet` byte, the host + AVR
  `combatCreatureSheet()` accessors, and both `combatCreatureRead` mirror
  assignments. Creature core 29 B -> 28 B (`CREATURE_CARVE_OFF` 29 -> 28).
- `tools/gen-combat.py`: creature schema/ABI doc 29 B -> 28 B; `SIZES`
  `CREATURE = 28 + CARVE*SLOTS`; removed the `sheet` optional JSON key,
  `read_int`, packed byte, host-mirror field + array-init arg, and the
  `CREATURE_<id>_SHEET` expect pins; fixed the array-init format arity.
- `data/creatures/pole.json`: dropped the now-unknown `"sheet": 1` key (the
  only top-level sheet key in the tree; the `art.sheet` descriptor key stays).
- `tools/gen-art.py`: `art_dims::beast_*` kept (authoring contract + host pin)
  with a comment noting the firmware reads each creature's art descriptor.
- Pins: `tst/combat_pack_test.hpp` (creature decode offsets 22..27, carve 28,
  enrage quad 24..27), `tst/combat_test.hpp` (dropped the sheet-accessor
  block), `tst/fxdatatest/combat_test.hpp` (dropped the 3 `*_SHEET` struct
  fields), `tools/tests/test_gen_combat.py` (size 40, core 28, offsets, byte
  vectors, static-probe doc).

## No per-kind art branch evidence (src/render.hpp)

```
$ grep -n "MON_\|monsterSheet\|beastAtk\|spinSheet\b" src/render.hpp
684:    // bih.4): artSheet/artFrame/artMode replace the per-kind beastAtk compare
685:    // chain and the MON_HEAVY spinSheet gate. During windup+attack the whole
```

Only the historical comment mentions the deleted chain — zero per-kind art
branches remain in `src/render.hpp`. `MON_*` / `monsterKind` remain in
`game.hpp` / `monster.hpp` / `app_setup.hpp` for sim behavior (allowed).
`spr::` constants (SPARK_*, WHIRL_DOT, SPIN_*) are all still odr-used.

## Generated two-pass note

The creature record shrinks 5 B (5 creatures), so `mhCombat` and every
following sheet offset shift by -5. The absolute-offset bakers
(`gen-equipment` SHEET_OFF_*, `gen-zones` ROOM/PROP off) read the fxdata.h
header produced by the previous pass, so a single `make gen` bakes stale
addresses. Needed TWO `make gen` passes to converge (offset deltas -5 applied
on pass 2), then `make gen-check` PASSES.

## Net delta vs checkpoint f08b286 (29348/29696, 348 free)

- flash: **29352/29696 (344 free) = +4 B** vs the checkpoint.
- ram: 1814/2560 (unchanged).
- cart: combat.bin 1262 -> 1257 B. Wave rule (~150 B free floor) satisfied.

The +4 is cart-address codegen entropy (the -5 sheet/table offsets change
instruction immediates); the deleted accessor + byte are LTO-dead already. No
shipping code path changed.

## Verification tails

```
make gen            -> converged: all "(unchanged)" (pass 3)
make gen-check      -> fxdata_manifest: PASS (162 generated artifacts unchanged)
make test           -> Total Passed: 6791  Total Failed: 0
FXTEST_ONLY=test_monster_art make fxtest-headless -> PASSED=180 FAILED=0
FXTEST_ONLY=test_combat   make fxtest-headless -> PASSED=251 FAILED=0
make size           -> size: flash=29352/29696 (344 free)  ram=1814/2560
make size-line      -> size: flash=29352/29696 (344 free)  ram=1814/2560
make test-tools     -> Ran 373 tests ... OK   (tools test edited)
```

Out of scope (orchestrator): `docs/` updates. No commit/push.
