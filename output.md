# monhun-ardu-prg.3 — drops: carve monster parts after the kill

HEAD at start: `ca437db` (prg.2 item table). No commit/push (orchestrator
commits). End state: clean gate — gen (x2), gen-check, host, tooling, full
`fxtest-headless` (18 maintained suites), size all green.

## What landed

- `data/creatures/*.json`: per-creature `carve` table (2–3 entries each,
  `{item, count, chance}`). lunge scale 100 / shell 45; sweep shell 100 /
  fang 40; heavy tail 100 / scale 55 / shell 25; ravager fang 100 / tail 45 /
  scale x2 30. A full 3-carve haul is meaningful (each table has a 100% anchor).
- `tools/gen-combat.py`: validates `carve` (array, ≤4 slots, item id resolved
  against `data/items.json` via the same ordered-id loader gen-zones uses,
  count 1..3, chance 0..100, duplicate/unknown-key rejection; item file required
  only when a carve block is present). Packs a fixed 4×3 tail
  (item u8, count u8, chance u8; empty slot = count 0 / chance 0) onto the
  creature record, emits host `struct Carve` + `carve[4]`, `combat_meta`
  `CARVE_SIZE`/`CARVE_SLOTS`/`CARVE_*_OFF`/`CREATURE_CARVE_OFF`/
  `CREATURE_CORE_SIZE`, `combat_expect` per-creature `CREATURE_<ID>_CARVES` +
  `CARVE<n>_ITEM/COUNT/CHANCE` pins, `HAS_CARVE`, and `--dump` carve lines.
  `CREATURE_SIZE` 29 -> 41 (+48 B blob for the 4 creatures).
- `src/core/combat.hpp`: `PkCarve` + carve tail in `PkCreature` (ABI asserts),
  `CombatCarve`, `combatCarveRead(creatureId, slot)` on both host and AVR
  (3-byte bulk cart read, bad ids inert), `CARVE_SLOTS` runtime/generated pin.
- `src/core/carve.hpp` (new): `stepCarve` / `applyCarve`. After `MS_DEAD` the
  carcass is the beast's last body box; on `over == WIN` a fresh A inside it
  enters `PS_CARVE` (~40 rooted ticks). Move cancels before the yield; damage
  cancels through `playerHurt`. Completion rolls every authored slot through
  `combatChancePasses(tick, creature, slot, carveOrdinal, chance)` (no RNG),
  adds with `itemAdd` and sparks. Three carves per hunt, then inert. Records
  `Game::carveHold` for the app gate.
- `src/core/game.hpp`: `PS_CARVE` appended; `CARVE_TICKS`/`CARVE_MAX`/
  `CARVE_SLOTS`; `MH_CARVE` carve-out (`CARVE_ENABLED = MH_CARVE &&
  SHEATHE_ENABLED`, so the at-budget parity image folds it out with MH_SHEATHE
  0); `Game::carvesDone` + `Game::carveHold` appended last. `initGame` resets.
- `src/core/world.hpp`: the `over != OVER_NONE` branch calls `stepCarve` before
  `updateEffects`.
- `src/app_state.hpp`: `appHuntReturnAllowed(g)` (`!g.carveHold`).
- `monhun-ardu.ino`: over-screen A consumes the menu edge but applies the return
  nav only when allowed, so a carve A never also exits to the menu.
- `src/core/player.hpp`: `initGame` resets the carve counters.

## Interfaces (new)

- `combat::CARVE_SLOTS` (4), `combat::CARVE_SIZE` (3), `combat::CARVE_ITEM_OFF`
  (0), `combat::CARVE_COUNT_OFF` (1), `combat::CARVE_CHANCE_OFF` (2),
  `combat::CREATURE_CARVE_OFF` (29), `combat::CREATURE_CORE_SIZE` (29),
  `combat::CREATURE_SIZE` (41), `combat::HAS_CARVE`.
- `mh::CombatCarve {item, count, chance}`, `mh::combatCarveRead(id, slot)`.
- `mh::CARVE_TICKS` (40), `mh::CARVE_MAX` (3), `mh::CARVE_SLOTS` (4),
  `mh::CARVE_ENABLED`, `mh::stepCarve`, `mh::applyCarve`.
- `mh::PS_CARVE`, `mh::Game::carvesDone`, `mh::Game::carveHold`,
  `mh::appHuntReturnAllowed`.

## Gates (tails)

`make gen` (x2, then stable) + `make gen-check`:
```
gen-combat: 4 creatures, 11 attacks, 16 windows, 15 patterns, 19 steps, 5 skeletons, 7 zones, 1093 B, sha256 28d58d10...
gen-combat: src/generated/combat_meta.hpp (unchanged)
fxdata_manifest: PASS (85 generated artifacts unchanged)
```

`make test`:
```
Total Passed: 5984
Total Failed: 0
```

`make test-tools`:
```
Ran 239 tests in 13.679s
OK
```

`make fxtest-headless` (full; test_parity excluded by design), 18/18 PASS:
```
test_assets PASSED=270  test_audio PASSED=10  test_boot PASSED=4
test_combat PASSED=237  test_data PASSED=368  test_hub PASSED=57
test_hud PASSED=25  test_items PASSED=35  test_menu_art PASSED=53
test_menu PASSED=60  test_monster_art PASSED=111  test_perf PASSED=5
test_player_art PASSED=111  test_quests PASSED=50  test_screens PASSED=78
test_smith PASSED=66  test_tell PASSED=17  test_zones PASSED=72
```
`test_perf` line:
```
B pUs=6347 pHz=157 lHz=52 lTk=480 rMx=3348 rAv=3074 ram=717
```
`rMx`/`rAv` unchanged from prg.8 (3348/3075, budget 7407).

`make size`:
```
size: .text=27750 .data=20 .bss=1573
size: flash=27770/29696 (1926 free)  ram=1593/2560
size: data facts: HAS_CARVE:true HAS_ENRAGE:true ... (all others unchanged)
```

## Coverage

- Host `tst/carve_test.hpp` (new): 4-slot table decode + inert padding/bad ids;
  3 carves then inert with inventory/scale floor/spark/carcass persistence;
  off-carcass no-carve; move cancel; damage cancel; deterministic yield replay
  against the completion tick; win-only (loss + live hunt don't carve); the
  `carveHold`/`appHuntReturnAllowed` return gate. Wired into `tst/main.cpp`.
- Host `tst/combat_pack_test.hpp`: carve tail decoded from the packed blob and
  compared to the host `Creature.carve[]`, plus `combat_expect` spot pins.
- Device `tst/fxdatatest/combat_test.hpp`: static asserts moved to
  `CREATURE_CORE_SIZE` + `CARVE_SIZE` (CombatCreature mirrors the core; the
  carve tail is read through `combatCarveRead`).
- Device `tst/fxdatatest/items_test.hpp`: carve table read off the real cart,
  pinned against `combat_expect`, plus one end-to-end carve (start -> complete
  -> scale yielded).
- `tools/tests/test_gen_combat.py`: fixture gains `data/items.json`; new tests
  for default-empty tail + fact, emit/dump/validate, unknown item, count/chance
  range, slot cap, missing/unknown keys, duplicate item, and missing item file.

## Budget

Baseline `27180/29696 flash (2516 free)`, `1591/2560 RAM`. After: **+590 B
flash / +2 B RAM** (target <=600 B). Measured carve-off intermediate `27200`
(+20 from the app gate/new fields); the carve machinery is ~570 B. The data
rides the cart blob (`combat.bin` +48 B) and does not touch the ELF. The only
fact flip is `HAS_CARVE false -> true`. `equip_meta.hpp`/`zone_meta.hpp`/
`equip.bin`/`manifest.json` offsets shift by the +48 B combat blob growth
(baked FX offsets), not by an unrelated change.

## Deviation

- **Victory walk trimmed (budget).** A walk-to-the-carcass block (d-pad move at
  stowed speed on the over screen) measured +194 B and pushed the bead to
  +784 B, over the 600 B target. It is removed; carve requires the hunter to be
  in the carcass rect at the kill (usual for melee). Move input still cancels
  a running carve, so the cancel contract holds. Split recorded in
  `docs/feel-design.md` (prg.3 ledger).
- **No sheathed gate.** The kill leaves the weapon drawn and the sim is frozen
  on the over screen, so requiring `p.sheathed` would make carve unreachable;
  the A press enters the carve (start sets `p.sheathed = true` for the stowed
  verb). "Cancel on move/damage" is covered directly in the host suite.
- **Parity untouched.** `test_parity` is excluded from the gate; carve folds
  out of that image via the existing `MH_SHEATHE 0` (no parity/mock edits).
