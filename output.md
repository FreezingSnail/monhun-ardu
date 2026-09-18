# monhun-ardu-cgk — zones: replace N-part machinery with fixed head/body/appendage

Status: **DONE** (all gates green; no commit per worker protocol).

## What changed

- `tools/gen-combat.py`: schema `"parts"` (array of stage/elem records) replaced
  by `"zones"` (object with optional `head` / `appendage`). New fixed `ZONE`
  12 B record (`box, hp, dmgMul, bodyShare, breakTypes, staggerOnHit,
  brokenDmgMul, brokenFlags, unlockMask`); `SKELETON` shrunk to 2 B
  (`firstAnchor, anchorCount`); creature record's `firstPart/partCount` became
  `headZone/appendZone`; profile's `partCount` became `zoneFlags`; guard's
  `firstPartPred/partPredCount` became a `zonesBroken` bitmask. Deleted
  `STAGES`, `ELEMS`, `REFS`, `PREDICATES` sections; header is 10 counts + 4
  reserved. New facts `HAS_ZONES`, `HAS_GUARD_ZONES`.
- `src/core/game.hpp`: `CombatZoneCache` (10 B) + `CombatState` (75 B) with
  `zone[2]`, `headZone/appendZone`, `zoneBroken`; removed `stages`, `partHp[]`,
  `partsHurt`, `bodyFirst/bodyCount/overFirst/overCount`, `COMBAT_PART_SLOTS`,
  `COMBAT_MAX_PARTS`; `PARTS_ENABLED/GUARD_PARTS_ENABLED` -> `ZONES_ENABLED/
  GUARD_ZONES_ENABLED`.
- `src/core/combat.hpp`: unrolled 3-zone resolve (`combatZoneHitResolve`: body
  implicit + wins ties, head tested before appendage on strict `>`; pool drain +
  single broken bit per zone), `combatZoneHitResolve`/`combatZoneStagger`/
  `combatAttackDisabled` (broken-zone `unlockMask`); guard `zonesBroken` mask
  compare. Deleted predicate interpreter, ordinals/`combatPartAt/Count`,
  `CombatPartNow` stage walk, `combatStage*`, pool indexing, hurt-envelope
  union, part/stage/elem accessors, `combatResolveHit`. Kept 2-window attacks +
  window cache, stagger meter/STATE, `combatPartArtFrame` (param is now the
  broken bit).
- `src/core/monster.hpp`: target rect is body-only; `monsterOnHit` resolves the
  zone path and feeds `combatZoneStagger` to `monsterStaggerAdd`; attack-disable
  gating switched to the zone broken mask. Pattern interpreter unchanged.
- Data: `data/skeletons.json` drops `parts`; `ravager.json` `"parts"` ->
  `"zones"` (head + appendage) and `p_enraged` guard -> `zonesBroken:
  ["appendage"]`.
- Tests ported (not weakened): `tst/combat_test.hpp`, `tst/combat_pack_test.hpp`,
  `tst/monster_test.hpp`, `tst/fxdatatest/combat_test.hpp`,
  `tools/tests/test_gen_combat.py` + fixture, `tools/contact_sheet.py` +
  its test. Regenerated `combat.bin` + headers + `fxdata`.
- README status/comments refreshed.

## Ravager zones declared

- **head**: box (20,4,12,12), dmgMul 130, hp 40, bodyShare 100, breakTypes
  SLASH, staggerOnHit 12, broken { dmgMul 130, hurtOn false }.
- **appendage (tail)**: box (-14,8,18,10), dmgMul 150, hp 60, bodyShare 40,
  breakTypes SLASH, staggerOnHit 30, broken { dmgMul 200, hurtOff, cue
  part_break, disableAttacks ["tail_sweep"] }.

## Verification (exact tails / numbers)

1. `make gen` (x2) -> `make gen-check`:
   ```
   gen-combat: 4 creatures, 8 attacks, 9 windows, 8 patterns, 8 steps, 3 skeletons, 2 zones, 616 B, sha256 d72d0a10...
   fxdata_manifest: PASS (53 generated artifacts unchanged)
   ```
2. `make test`: `Total Passed: 3144  Total Failed: 0` (baseline for this task: 3411;
   the drop is the generic N-part reference suite collapsed into the fixed-zone
   suite — same behavioral surface, fewer synthetic stage/ordinal vectors).
   `make test-tools`: `Ran 82 tests ... OK`.
3. `make fxtest-headless` (full): all suites PASS —
   `test_assets 262/0, test_audio 14/0, test_boot 4/0, test_combat 184/0,
   test_data 221/0, test_hud 17/0, test_menu 59/0, test_parity 660/0,
   test_perf 5/0, test_player_art 111/0`.
   `test_combat` read budget tail:
   `C reads spawn=5 attack=5 guard=2 hit=0 tick256=0 simAtk=6 simTk=0 winSw=1`.
   perf tail: `B pUs=6502 pHz=153 lHz=51 lTk=988 rMx=5392 rAv=5028 ram=409`
   vs reference `pUs=6501 rMx=5388 rAv=5028` (+1 µs plane, +4 µs render max;
   gates still PASS).
4. `make build` + `make size`:
   ```
   Sketch uses 28242 bytes (95%) ... Global variables use 2029 bytes ...
   size: .text=28184 .data=58 .bss=1971
   size: flash=28242/29696 (1454 free)  ram=2029/2560
   ```
   **Recovery vs 29560 = 1318 B (> 1 KB spike gate).**
5. `node tools/gen-parity-fixtures.js` -> `git status tst/fxdatatest/parity_fixtures.hpp` clean (empty diff).
6. Data facts: `HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false HAS_GUARD_HP:false
   HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true HAS_HIT_STAGGER:false
   HAS_MULTI_STEP:false HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false
   HAS_STAGGER:true HAS_STEP_AFTER:false HAS_STEP_CHANCE:false
   HAS_WAIT_STEPS:false HAS_ZONES:true`.

## Per-symbol delta

Not reported: LTO makes per-symbol size math meaningless in this repo
(AGENTS.md "Budget first": measure whole-image deltas). Whole-image delta is
-1318 B. The bytes are in the deleted machinery (predicate interpreter +
ordinals + `CombatPartNow` stage walk + `combatStage*` + hurt-envelope union +
pool-index/stage caches, previously ~2.6 KB) minus the ~1.3 KB of new
zone-resolve/seed code and the retained attack/window/guard interpreter.

## Deviations from the old behavior (shipped 3 unchanged; parity fixtures byte-identical)

- Zone `staggerOnHit` is a flat per-zone stat applied on every zone hit; the old
  tail stagger came from a *crossed stage* (hp <= 30%). Ravager stagger timing
  therefore differs (tail hits add 30 immediately; head adds 12). The shipped 3
  have no zones and `staggerMax 0`, so device parity is unaffected.
- Single broken record: tail break (and tail_sweep disable) now happens only at
  pool 0; the old first stage fired at 30% hp.
- `physMul`/`elemMul` are dropped per the design (elem accessors deleted), so
  tail damage uses only `dmgMul` (150) + bodyShare; the old SLASH 150 / FIRE
  200 multipliers are gone.
- Broken zones leave the candidate set; `brokenDmgMul`/`brokenFlags` are
  data-only (matches the old effective behavior: stage 2 was hurtOff, so the
  200 override never applied to a landed hit).
- `-DMH_COMBAT_PARTS=0` macro name kept (only the effective constexpr names
  changed to `ZONES_ENABLED`/`GUARD_ZONES_ENABLED`) to keep the perf/parity
  carves and the Makefile untouched.
- `docs/creature-framework.md` still describes the old part/stage schema; the
  binding design is `build/zones-design.md`. Left for the docs bead (not in
  scope / not gated).
