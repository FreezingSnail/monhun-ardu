# monhun-ardu-feel.4 — engine: attack wallStun (charge into a room bound staggers)

Baseline: HEAD `f74d10b`, clean tree. No commit/push (orchestrator commits).
Verification: `make gen` x2, `make gen-check`, `make test`, `make test-tools`,
`make fxtest-headless` (full), `make size`.

## Schema + pack (tools/gen-combat.py)

- Attack gains an optional key `wallStun` (u8 ticks, range 0..255, default 0).
  Unknown values of any kind already fail via `read_int`/`check_keys`; range and
  integer-only are exercised by the new tool tests.
- Packed as the attack record's 13th byte, immediately after `cue` (byte 12):
  `... stagger, cue, wallStun, firstWindow, windowCount, windup..dmg`.
  `SIZES["ATTACK"]` 22 -> 23. `firstWindow`/`windowCount` stay adjacent and the
  `windup..dmg` u16 quad stays contiguous (static_asserts updated).
- `--dump` prints `... windows N wallStun W`.
- `combat_expect.hpp` gains `ATTACK_<CREATURE>_<FIRST_ATTACK>_WALLSTUN` (spot
  value 0 for every shipped creature).
- No `HAS_*` fact: `wallStun` 0 in all shipped data means the runtime branch is
  inert, so the branch is compiled unconditionally per the bead (not gated).

## Loader (src/core/combat.hpp, src/core/game.hpp)

- `CombatAttackValue` / packed `PkAttack` / generated `combat_data::Attack` gain
  `wallStun` (after `cue`); `combatAttackRead` + new `combatAttackWallStun`
  accessor carry it on both host and AVR.
- `CombatAttackCache` gains `uint8_t wallStun` after `facing`. AVR `attackLoad`
  reads the one extra byte (attack-load burst 22 -> 23 reads, still under the
  device test's 24-read gate). Host `attackLoad` sets it from the accessor.
- Static asserts: `sizeof(CombatAttackCache)` 21 -> 22, `sizeof(CombatState)`
  83 -> 84; new `PkAttack` adjacency assert (wallStun == cue + 1).

## Detection (src/core/monster.hpp, updateMonster)

- The pre-clamp anchor is captured immediately before `clampMonster` (after the
  state switch has applied this tick's movement), then compared post-clamp. A
  delta means the tick's movement hit a room bound and was clipped. (Capturing
  before the switch does not work: a beast already pinned at the bound moves out
  and is clamped straight back, so pre == post. This is the one deviation from
  the bead's wording; the observable rule — "clamp displaced the beast" — is
  what is implemented and tested.)
- Trigger requires `m.state == MS_ATTACK`, `attack.wallStun > 0`,
  `attack.moveType != MOVE_NONE` (lunge/charge/hop), and a pre/post clamp delta:
  sets `MS_STAGGER`, `m.t = wallStun`, and clears the active pattern cursor
  exactly like `monsterStaggerAdd` (`patternIdx = COMBAT_NO_PATTERN`, `stepIdx =
  0`, `stepT = 0`). Release reuses the existing MS_STAGGER path (PURSUE +
  `cdBase`).
- One trigger per attack: the state change is the latch; the beast leaves
  MS_ATTACK, so sustained wall contact cannot re-arm or stack. Shipped data
  leaves `wallStun` 0, so shipped fights are byte-identical (existing host fight
  suites pin this).
- Audio/render: no new cue path. The existing MS_STAGGER whirl-dot render
  carries the tell. A wall-hit audio/visual cue edge is a reasonable follow-up
  (the DESIGN mentions it) but is out of this bead's scope.

## Tests (permanent, native frameworks, co-located)

- `tst/monster_test.hpp` — 4 new cases: (a) lunge into the west bound staggers
  for exactly `wallStun` ticks, clears the pattern cursor, then releases to
  PURSUE with cd 55; (b) same lunge mid-arena does not stagger; (c) continued
  bound contact through the stun drains the timer without re-arming/stacking;
  (d) `wallStun` 0 at the bound is inert (clamp only).
- `tst/combat_pack_test.hpp` — attack decode loop pins byte 12 == `wallStun`;
  shifted firstWindow/windowCount/quad byte positions; spot values extended with
  the expect wallStun pin.
- `tst/combat_test.hpp` — attack mirror loop adds `wallStun` + accessor;
  `attackLoad` cache lifecycle asserts `wallStun`.
- `tst/fxdatatest/combat_test.hpp` — device loader test asserts the decoded and
  cached `wallStun` against `combat_expect::ATTACK_LUNGE_PECK_WALLSTUN`.
- `tools/tests/test_gen_combat.py` — dump string, packed attack bytes (23 B),
  default/emit test (byte 12 + expect constant + dump), range rejection,
  integer-only rejection; expect-header spot assertion.
- No fact-map change, so `test_data_facts_match_fixture` is unchanged.

## Two-pass gen

`make gen` run twice after the `ATTACK_SIZE` change (first pass moves the FX
image offsets and the zone/equip images; second converges). Generated sets
(`combat.bin`, `combat_*`, `equip_meta.hpp`, `zone_meta.hpp`, `fxdata*`) staged
together. `make gen-check` clean.

## Verification tails

```
# make gen (2nd run)
gen-combat: 8 creatures, 8 attacks, 13 windows, 9 patterns, 9 steps, 5 skeletons, 11 zones, 1036 B, sha256 e38c229ab45f3d28a8cd1b518f0d5a10e46d240ae6988d0e047d711f3bb2038f
gen-combat: fxdata/tables/combat.bin (unchanged)
gen-combat: src/generated/combat_data.hpp (unchanged)
gen-combat: src/generated/combat_meta.hpp (unchanged)
gen-combat: src/generated/combat_expect.hpp (unchanged)

# make gen-check
fxdata_manifest: PASS (82 generated artifacts unchanged)

# make test
Total Passed: 5469
Total Failed: 0

# make test-tools
Ran 183 tests in 9.569s
OK

# make fxtest-headless (full)
combat_test PASSED=295 FAILED=0
parity_test PASSED=660 FAILED=0
perf_test PASSED=5 FAILED=0
B pUs=6371 pHz=156 lHz=52 lTk=456 rMx=4772 rAv=4587 ram=594
(asset=270, audio=17, boot=4, data=368, hub=57, hud=17, menu_art=81,
 menu=80, monster_art=111, player_art=111, quests=50, screens=78,
 smith=66, zones=69 — all PASS)

# make size
size: .text=27026 .data=40 .bss=1693
size: flash=27066/29696 (2630 free)  ram=1733/2560
size: data facts: HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false HAS_GUARD_FACING:false
  HAS_GUARD_HP:false HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true HAS_HIT_STAGGER:false
  HAS_MULTI_STEP:false HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false HAS_STAGGER:true
  HAS_STEP_AFTER:false HAS_STEP_CHANCE:false HAS_WAIT_STEPS:false HAS_ZONES:true
```

Baseline at HEAD `f74d10b` (feel.3): `flash=26962/29696 (2734 free) ram=1732/2560`,
`.text=26922`, `.bss=1692`. **Delta: +104 B flash / +1 RAM** (spike predicted
+134/+1; the extra byte on the attack field plus the gated detection are inside
that). Cart blob: 1028 -> 1036 B (+8, one byte per attack). Perf unchanged
(rMx 4772, ~2635 us under the 7407 us floor).

## Per-file summary

- `tools/gen-combat.py` — attack `wallStun` schema, pack, dump, expect spot.
- `tools/tests/test_gen_combat.py` — schema/pack/dump/expect tests.
- `src/core/combat.hpp` — value/packed/generated mirrors, accessor, attackLoad,
  asserts.
- `src/core/game.hpp` — `CombatAttackCache.wallStun`.
- `src/core/monster.hpp` — clamp-delta wall-stun detection in MS_ATTACK.
- `tst/monster_test.hpp`, `tst/combat_test.hpp`, `tst/combat_pack_test.hpp`,
  `tst/fxdatatest/combat_test.hpp` — host + pack + device tests.
- `src/generated/*`, `fxdata/*`, `src/fxdata.h` — regenerated (two-pass).
