# monhun-ardu-ljj.8 — Parts: breakable-part budget + lean implementation

Status: **DONE.** Breakable parts landed end to end. Shipping + every Ardens
suite green; parity fixtures byte-identical. No commit/push (orchestrator
commits). One deviation from the dispatch, flagged below: the ravager-machinery
carve had to cover `test_parity` as well as `test_perf` (the shipped-3 parity
scene cannot fit the full machinery either), and the carve folds the
ravager-only multi-window/stagger/parts-guard facts too — behavior-identical
for the shipped 3.

## Shipped

Data (`data/creatures/ravager.json`):
- `parts`: `tail` `{ox:-14, oy:8, w:18, h:10}`, dmgMul 150, physMul SLASH 150 /
  BLUNT 75, elemMul FIRE 200, hp 60, bodyShare 40, breakTypes SLASH, hurtOn
  true; stages `at 30` (stagger 30, disableAttacks tail_sweep, speedMul 100,
  cue part_break) and `at 0` (dmgMulOverride 200, hurtOn false).
- `tail_sweep` split into 2 windows (`0..5` behind ox -22, `6..11` front ox 20).
- `p_enraged` parts-guard pattern (`parts.tail >= 1` -> bite), listed first.
- profile `staggerMax 60 / staggerDecay 1 / staggerRecoverT 24`.

Data facts (generated): `HAS_PARTS=true HAS_STAGGER=true HAS_MULTI_WINDOW=true
HAS_GUARD_PARTS=true HAS_SIMPLE_GUARDS=false` (HAS_HIT_STAGGER stays false: no
attack carries stagger; the meter is driven by the tail break-stage stagger).

Lean `combat.hpp` (kept, measured on shipping):
1. int16 per-part envelope extremes + override-only `combatAttackDisabled`
   (the generator rejects skeleton-part attack refs, so only the cached
   override list can disable): -46 B.
2. `combatPartHitResolve` rewritten (direct field reads, int16 rect, no
   `CombatPartNow` / `combatPartHitRect` / `combatPartRectRot` on the hot path):
   -420 B.
3. lazy override-only pool fill (skeleton parts are hp 0, never read): -52 B.
4. conservative hurt envelope `m=max(|ox|,|oy|,11(|ox|+|oy|)/16)` instead of the
   exact 8-facing union (exact per-part rects are still tested at hit time): -172 B.
5. `monsterOnHit` now gates the part-stage stagger channel on
   `PARTS_ENABLED && STAGGER_ENABLED && STAGES_COUNT > 0` (was
   `HAS_HIT_STAGGER`, which no attack sets, so the meter could never charge):
   the feature is now functional, +212 B.

## Per-image carve (flags in `src/core/game.hpp`)

`-DMH_COMBAT_PARTS=0` defines effective flags (`PARTS_ENABLED`,
`MULTI_WINDOW_ENABLED`, `STAGGER_ENABLED`, `GUARD_PARTS_ENABLED`,
`SIMPLE_GUARDS`) that fold the ravager machinery. The generated data facts stay
authoritative; `tools/tests/test_gen_combat.py::test_data_facts_match_fixture`
is unchanged and passes. Applied in `tst/fxdatatest/test_perf.ino` (as
dispatched) **and `tst/fxdatatest/test_parity.ino`** (deviation): parity with
the full machinery is 32212/29696; parts-only fold is 29770 (74 over, because
the ravager's multi-window/parts-guard/stagger facts are separate gate sites);
the broad carve returns parity to HEAD's exact 29452 and is behavior-identical
(parity 660/0). Shipping and `test_combat` compile the full machinery.

## Verification (all re-run at this tree)

```
make gen (x2)            deterministic
make gen-check           PASS (53 generated artifacts unchanged)
make test                3411 passed / 0 failed
make test-tools          Ran 81 tests, OK
make fxtest-headless     all PASS:
  assets 262/0  audio 14/0  boot 4/0  combat 211/0  data 221/0  hud 17/0
  menu 59/0  parity 660/0  perf 5/0  player_art 111/0
  combat reads: spawn=10 attack=5 guard=2 hit=9 tick256=0 simAtk=6 simTk=0 winSw=1
  perf: B pUs=6501 pHz=153 lHz=51 lTk=984 rMx=5388 rAv=5028 ram=418
        (vs rMx=5388 rAv=5028 pUs=6501 — unchanged; carve adds 10 B free RAM)
node tools/gen-parity-fixtures.js -> empty tst/fxdatatest/parity_fixtures.hpp diff
make build / make size   flash=29560/29696 (136 free)  ram=2032/2560
```

Shipping delta vs 26928 baseline: **+2632 B** (2768 -> 136 free).
`make size` data facts: `HAS_GUARD_PARTS:true HAS_MULTI_WINDOW:true
HAS_PARTS:true HAS_SIMPLE_GUARDS:false HAS_STAGGER:true` (rest false).

Per-image flash (fxtest = device, stock flags; shipping = size flags):

| image | flash | free |
|---|---|---|
| shipping (full machinery) | 29560 | 136 |
| test_combat (full) | 29508 | 188 |
| test_parity (carved) | 29452 | 244 |
| test_perf (carved) | 28202 | 1494 |
| test_data | 16486 | 13210 |
| test_boot | 15288 | 14408 |
| test_audio | 14986 | 14710 |
| test_assets | 8696 | 21000 |
| test_hud | 20132 | 9564 |
| test_menu | 15444 | 14252 |
| test_player_art | 16148 | 13548 |

## Un-gated / added tests

- `tst/combat_test.hpp`: tail record, pool seeding, stage thresholds + effect
  projections, `combatPartHitResolve` containment/multiplier/pool-drain/break,
  multi-window windows, enrage parts-guard flip, `combatPartArtFrame` <->
  `art_dims::tail_*` linkage, and the stagger meter tripping `MS_STAGGER`.
- `tst/combat_pack_test.hpp`: STAGE/ELEM/REF/PREDICATE decode loops plus
  MetaRecord coverage for every new ravager record.
- `tst/fxdatatest/combat_test.hpp`: real-cart tail record, spawn pool, stage
  stagger, break -> tail_sweep disable, enrage guard, mid-active window refresh.
- `tst/art_dims_test.hpp` + `tst/fxdatatest/asset_test.hpp`: `fxtail` blob
  header/pixels/bytes (new authored sheet).
- Art: `tools/gen-art.py` authors `images/blocks/fxtail_18x10.png` (4 frames:
  east-intact, east-broken, west-intact, west-broken, matching
  `combatPartArtFrame`); FX image now 33 sheets.

No commit/push/add performed.
