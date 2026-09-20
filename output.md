# monhun-ardu-feel.7 — engine: move.type hop (dx, dy burst) for pounce / reposition

Baseline: HEAD `5a01f8a`, clean tree. No commit/push (orchestrator commits).
Verification: `make gen` (x2), `make gen-check`, `make test`, `make test-tools`,
`make fxtest-headless` (full), `make size`.

## What was already reserved vs. added

The generator schema and packed ABI already carried `move.type hop` and the
signed `dx`/`dy` bytes (`MOVE_TYPES["hop"] = 3`, `PkAttack.moveDx/moveDy` at
attack bytes 2/3, `ATTACK_SIZE` 23). `--dump` already printed `move hop(dx,dy)`.
This bead implements the **runtime** hop and adds the missing tests:

- `src/core/monster.hpp` — `startMonsterAttack` rotates the face-relative
  `moveDx/moveDy` into `lvx/lvy` at release via the existing `combatFacePoint`
  (`combatFaceOffset` helper family); the `MS_ATTACK` movement applies
  `fp::addVel` on `t <= active` for lunge **or** hop, so the hop stops exactly
  at the active boundary with no drift through recovery. `none`/`charge` still
  release with `lvx = lvy = 0`.
- `src/core/game.hpp` — `CombatAttackCache` gains `int8_t moveDx, moveDy`
  (22 -> 24 B AVR).
- `src/core/combat.hpp` — `attackLoad` AVR path now bulk-reads the 4-byte move
  prefix (`moveType/moveSpeedF/moveDx/moveDy`) in one cart transaction (same
  read budget, was a u16 pair); host path reads the generated fields; new
  `PkAttack` adjacency asserts and `CombatState` 89 -> 91 B assert.
- `tools/gen-combat.py` — no change needed (validation/pack/dump already
  correct; the new tool tests pin it).

The movement branch compiles unconditionally (same convention as feel.4
wallStun and feel.6 enrage), so the hop path is one shared host/device code path
and the host suites can exercise it directly. Shipped data authors no hop, so
shipped fights are byte-identical: `parity_test 660/660`, and all existing
lunge/none behavior tests unchanged.

## Size (whole-image, LTO; `make size`)

Baseline at HEAD `5a01f8a` (feel.6): `flash=27378/29696 (2318 free)`,
`.text=27338 .data=40 .bss=1698`, `ram=1738/2560`.
Now: `flash=27474/29696 (2222 free)`, `.text=27434 .data=40 .bss=1700`,
`ram=1740/2560`. **Delta: +96 B flash / +2 RAM.** The spike (`feel.1`) measured
+82 B / +2 RAM (ATTACK_SIZE unchanged because the dx/dy fields were already
packed); the +14 B is the inline hop branch plus the wider MS_ATTACK predicate.
Cart blob unchanged (no shipped data authors hop).

## Tests (permanent, native, co-located)

- `tst/monster_test.hpp` — 3 new cases (host total 5558 -> 5611):
  (a) release rotates the face-relative vector at E and S (`dx12,dy6` ->
  `(12,6)` and `(-6,12)`); (b) travel along the vector with sub-pixel carry for
  E and S, stop exactly at `active`, frozen remainder through recovery, and no
  lateral drift; (c) `none` release zeroes the velocity and lunge still
  projects `speedF` (`leap 42`).
- `tst/combat_pack_test.hpp` — one new case: the hop dx/dy are the signed
  bytes at attack offset 2/3, `ATTACK_SIZE` stays 23, and every shipped
  none/lunge record decodes `moveDx == moveDy == 0`.
- `tools/tests/test_gen_combat.py` — 6 new cases (189 -> 195): hop requires
  `dx`/`dy`, rejects `speedF` and unknown keys, range `-128..127`, integer-only
  (incl. bool), and packs signed `dx/dy` + `--dump` prints the vector.
- `tst/fxdatatest/combat_test.hpp` — cache mirror asserts `moveDx/moveDy` (one
  packed-word check; 293 PASSED).
- `tst/combat_test.hpp` (host loader mirror) already compares `moveDx/moveDy`
  against `combat_data.hpp` for every attack; unchanged.

## Device test budget (deviation, documented)

`test_combat` is the tight suite (feel.6 left 32 B below the 29696 ceil). The
new engine code + cache fields cost +98 B there, so two assert consolidations
were applied (same coverage, fewer flash-hungry calls), mirroring feel.6's
combined enrage check:

1. header magic lo/hi and version/flags: two adjacent-byte pairs now check one
   packed u16 each (4 calls -> 2).
2. lunge `moveType`/`moveSpeedF`: one packed u16 (2 calls -> 1).
3. the hop cache check is a single packed `moveDx|moveDy<<8` word.

Result: `test_combat` 29690/29696 (6 B headroom), `combat_test PASSED=293`.

## contact_sheet.py note (verified)

`tools/contact_sheet.py` does **not** read the move/cached velocity today: it
renders the phase timeline (one column per tick) and static per-window preview
rects from the raw JSON. It therefore does not draw per-tick hop positions; a
hop path preview would be a follow-up if the epic wants it. No change made here.

## Two-pass gen

No packed record size changed (`ATTACK_SIZE` stays 23), so no FX image offset
shift. `make gen` was still run twice; all generated artifacts were unchanged
(`gen.sh: ... regenerated`, then `fxdata_manifest: PASS (82 generated artifacts
unchanged)`), and `git status` shows only source/test files modified.

## Verification tails

```
# make gen (2nd run) / make gen-check
fxdata_manifest: PASS (82 generated artifacts unchanged)

# make test
Total Passed: 5611
Total Failed: 0

# make test-tools
Ran 195 tests in 11.690s
OK

# make fxtest-headless (full)
asset_test PASSED=270 FAILED=0
test_audio PASSED=17 FAILED=0
test_boot PASSED=4 FAILED=0
combat_test PASSED=293 FAILED=0
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
size: .text=27434 .data=40 .bss=1700
size: flash=27474/29696 (2222 free)  ram=1740/2560
size: data facts: HAS_ENRAGE:false HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false
  HAS_GUARD_FACING:false HAS_GUARD_HP:false HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true
  HAS_HIT_STAGGER:false HAS_MULTI_STEP:false HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false
  HAS_STAGGER:true HAS_STEP_AFTER:false HAS_STEP_CHANCE:false HAS_WAIT_STEPS:false HAS_ZONES:true
```

## Per-file summary

- `src/core/monster.hpp` — hop release rotation + `t <= active` addVel for hop.
- `src/core/game.hpp` — `CombatAttackCache.moveDx/moveDy`.
- `src/core/combat.hpp` — 4-byte move-prefix read, host reads, ABI asserts,
  `CombatState` 91 B.
- `tst/monster_test.hpp`, `tst/combat_pack_test.hpp`, `tst/fxdatatest/combat_test.hpp`
  — travel/stop/no-drift, pack round-trip, device cache check.
- `tools/tests/test_gen_combat.py` — hop schema/pack/dump tests.
