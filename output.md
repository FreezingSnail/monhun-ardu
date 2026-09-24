# monhun-ardu-bih.5 — art draw phase 3: zone part art in zone records

## What changed

Zone part-overlay art is now cart data: each breakable zone carries a 1-based
`partSheet` index into `data/art_sheets.json`, read once at spawn into its
`CombatZoneCache` slot. `drawMonster` drops the three per-kind overlay chains
(MON_HEAVY / MON_LUNGE / MON_SWEEP, 5 sheet constants) for one generic loop over
the two cached zone slots. The two skip rules are now generic: overlays are
suppressed whenever the active whole-body draw is an attack-art sheet
(`g.combat.attack.artSheet != 0`), mode 1 (spin) included.

## Data

- `data/art_sheets.json`: appended the 5 zone part sheets -> indices 10..14
  (fxtail_heavy, fxhead_chicken, fxlegs_chicken, fxhead_bull, fxhooves_bull;
  phase-1 1..5 and bih.4 6..9 indices unchanged).
- `data/creatures/*.json`: authored `zones.<name>.part` — heavy appendage,
  lunge head+appendage, sweep head+appendage. ravager/pole stay 0 (no invented
  art: the ravager's 18x10 fxtail is deliberately not overlaid, the pole's hp-0
  zone never breaks).
- `tools/gen-combat.py`: `normalize_zone` resolves the optional `part` name
  (strict: unknown name and a missing art_sheets.json both fail); ZONE record
  `13 -> 14 B` (partSheet byte appended); host mirror, data-header row, dump
  line, meta `ZONE_SIZE` and the blob sha all follow.
- Zone byte layout: `box(4) hp dmgMul bodyShare breakTypes staggerOnHit
  brokenDmgMul brokenFlags unlockMask(u16) partSheet`.

## Core

- `src/core/game.hpp`: `CombatZoneCache` +1 B (`partSheet`) -> 13 B, `zone[2]`
  22 -> 26 B, `CombatState` 108 -> 110 B.
- `src/core/combat.hpp`: value struct `CombatZone` + `PkZone` gain `partSheet`;
  host `combatZoneRead` and `combatZoneSeed` project it; `CombatZoneCache`
  static_assert 12 -> 13, `CombatState` 108 -> 110, zone-cache budget comment.
  ABI mirrors (`CombatZone == ZONE_SIZE`) stay padding-free on AVR.

## Render

- `src/render.hpp`: deleted the 3 chains + 5 `fxtail_heavy`/`fxhead_chicken`/
  `fxlegs_chicken`/`fxhead_bull`/`fxhooves_bull` references; one loop over
  `g.combat.zone[slot]` draws `artSheetAddr(partSheet - 1)` at the cached
  face-relative box (drawZonePart unchanged: shared 4-frame
  `combatPartArtFrame`, phase-0 west cell mirror). Attack-art skip is the single
  generic `attackArt` predicate; the spin is covered because every spin authors
  a non-zero attack artSheet (asserted by the existing spin pins).

## Oracle (before -> after, unchanged)

`tst/fxdatatest/monster_art_test.hpp`:
- per-zone `partSheet` pins: chicken head+legs, bull horns+hooves, heavy
  appendage -> the sheet index constants; heavy head / ravager head+appendage /
  pole head -> 0.
- generic attack-art skip: HEAVY's mode-0 bite ATTACK clears the resting-tail
  east band (x16..39) even though idle inks it; the mode-1 spin skip is already
  pinned (`spin attack skips resting tail cap`).
- every existing HEAVY-tail / chicken / bull pixel pin stays green
  (test_monster_art 170 -> 180 asserts, 0 failed).

`tst/combat_test.hpp` (host) + `tst/fxdatatest/combat_test.hpp` (device):
- zone partSheet projection / record spot pins (heavy tail 10, chicken head 11
  legs 12, bull head 13 hooves 14, ravager 0), cache seed pins, sheet-count
  9 -> 14 + 5 new address pins.
- `tools/tests/test_gen_combat.py`: new part emit/validate/requires-file cases;
  zone payload fixtures 13 -> 14 B; dump-line + host-struct needles.

## Gates

- `make gen`: PASS (fxdata-data 404597 B, 14 sheets); blob size shifted, so
  `make gen-check` needed **two passes** (first pass regenerated the cross-blob
  artifacts + manifest), second pass `PASS (160 generated artifacts unchanged)`.
- `make test`: `Total Passed: 6767  Total Failed: 0`
- `FXTEST_ONLY=test_monster_art make fxtest-headless`: `PASSED=180 FAILED=0`
- `FXTEST_ONLY=test_combat make fxtest-headless`: `PASSED=237 FAILED=0`
  (`C reads spawn=16 attack=8 guard=2 hit=0 tick256=0 simAtk=9 simTk=0 winSw=1`)
- `make test-tools`: `Ran 373 tests ... OK`
- `make size` / `size-line`: `size: .text=29300 .data=50 .bss=1764`
  **flash=29350/29696 (346 free) ram=1814/2560**

## Budget

Checkpoint 29476 (220 free) -> **29350 (346 free): net -126 B flash** (the
per-kind chains + 5 render constants cost less than the zone byte + generic
loop). RAM +2 B (`CombatZoneCache` 12 -> 13 x2). Well above the ~150 B reserve.

No commit/push.
