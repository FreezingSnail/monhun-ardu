# monhun-ardu-bih.3 — art draw phase 1: migrate the 4 beasts' base sheet/layout to descriptors

Status: **DONE (gates green).** Shipping image links at **29542 / 29696 (154
free)**, RAM 1809/2560. No commit/push.

## Net whole-image delta (the payback)

| build | flash | free |
|---|---|---|
| baseline HEAD 33d02fd (given) | 29374 | 322 |
| spike monhun-ardu-bih.1 (in tree) | 29670 | 26 |
| this bead (phase 1) | **29542** | **154** |

- **Net vs the spike: −128 B** (the deleted legacy per-kind base branch).
- **Net vs baseline: +168 B** (the descriptor machinery the spike added is not
  yet fully paid back).
- `make size`: `.text=29492 .data=50 .bss=1759`;
  `flash=29542/29696 (154 free) ram=1809/2560`.

Headroom: **154 free ≥ the ~150 B wave floor — PASS, but only 4 B of margin.**
Not blocked; flagged for the wave. Remaining deletable payback (deliberately left
for later phases per the epic): phase 2 = `beastAtk` + `spinSheet` gates and
their sheet ordinals → attack records; phase 3 = the three zone part-overlay
chains → zone records; phase 4 = `art_dims` beast_* constants, docs/pins.

## What changed

### Data
- **`data/art_sheets.json`**: grew 1 → 5 names, **sorted**:
  `fxmonster, fxmonster_heavy, fxmonster_lunge, fxmonster_sweep, fxpole`
  (so `fxpole` moves from index 1 to 5; every index is derived by
  `gen-art-sheets.py`, so the table and the records stay consistent).
- **`data/creatures/{lunge,sweep,heavy,ravager}.json`**: added the frozen `art`
  blocks matching the legacy code exactly:
  - lunge/sweep/heavy: `sheet fxmonster_{lunge,sweep,heavy}`, anchorY 0,
    stride 7, idle0 0, idleCount 2, windup 2, attack 3, recover 4, flash 5,
    dead 6.
  - ravager: `sheet fxmonster` (legacy sheet), anchorY 0, stride 4, idle0 0,
    idleCount 0, windup 0, attack 0, recover 1, flash 2, dead 3 (no idle bob;
    windup/attack hold the idle frame).
- All 5 creatures now pack a non-zero ART record. The cart blob **size is
  unchanged** (1221 B — the 5×10 B ART section already existed from the spike),
  so no stale-address repack was needed; `make gen` converged in one pass and
  `make gen-check` then reported 160 generated artifacts unchanged.

### Render (`src/render.hpp`)
- **Deleted the legacy base draw**: the `monsterSheet()` per-kind sheet chain,
  the `MON_RAVAGER` legacy frame branch, the `BEAST_POSES` frame math, the
  `spr::MON_IDLE/RECOVER/FLASH/DEAD/WEST` constants, and the now-dead `flashing`
  local and `generic` routing flag.
- `drawMonster` now checks the two whole-body replacements **first** (they
  supersede the body), then falls through to the descriptor draw:
  `if (spinSheet) fxtailspin … else if (beastAtk) bespoke attack sheet …
  else drawMonsterBodyGeneric(g, x, y)`. The spin/`beastAtk` selection math is
  unchanged.
- Zone part-overlay chains stay (phase 3) but are now gated on the **loaded
  creature record** (`g.combat.creature == CREATURE_HEAVY/LUNGE/SWEEP`) instead
  of `g.monsterKind` and the removed `!generic` flag. This is required: the pole
  draft test parks a `fxpole` creature under `MON_LUNGE`, and the old kind gate
  would have painted chicken head/leg overlays onto the pole once `!generic`
  was dropped. Real beasts map 1:1 kind→creature, so their pixels are unchanged.

### Tests (permanent, native framework)
- **Device `tst/fxdatatest/monster_art_test.hpp`**: added the phase-1 oracle
  **before** switching: per-beast base-body pins (idle = DARK body on plane 0 /
  not plane 2; flash = `hitFlash` → WHITE flash frame on plane 2; west = the
  mirrored frame flips the east mark to the west cell / swaps body↔head shade),
  for all four beasts, at coordinates outside each beast's zone-overlay boxes.
  Validated green against the legacy path first, then again after migration.
  The pole `art.sheet` pin now uses `art_sheets::ART_SHEET_FXPOLE`.
- **Host `tst/combat_test.hpp`**: art block now asserts the 4 beast descriptors
  (sheets, strides, idle/flash/dead frames, ravager recover/windup/attack) plus
  the sorted sheet table (5 sheets, `FXMONSTER` first, `FXPOLE` last) and the
  host half of every `ART_SHEET_ADDR_<NAME> == src/fxdata.h` check.

## Verification (tails)

```
$ make gen
gen-art-sheets: 5 sheets
gen-art-sheets: src/generated/art_sheets.hpp
fxdata_manifest: wrote fxdata/manifest.json (115 images, 76 inputs, 32 outputs)

$ make gen-check
fxdata_manifest: PASS (160 generated artifacts unchanged)

$ make test
Total Passed: 6667
Total Failed: 0

$ FXTEST_ONLY=test_monster_art make fxtest-headless
Sketch uses 26170 bytes (88%)
test_monster_art PASSED=153 FAILED=0
P
test_monster_art: PASS

$ FXTEST_ONLY=test_combat make fxtest-headless
Sketch uses 27920 bytes (94%)
C reads spawn=16 attack=7 guard=2 hit=0 tick256=0 simAtk=8 simTk=0 winSw=1
combat_test PASSED=237 FAILED=0
P
test_combat: PASS

$ make size
size: .text=29492 .data=50 .bss=1759
size: flash=29542/29696 (154 free)  ram=1809/2560
size: data facts: HAS_CARVE:true HAS_ENRAGE:true HAS_GUARD_CHANCE:false ...
```

Pole parity: every pre-existing HEAVY/LUNGE/SWEEP/RAVAGER pixel pin stays green
through the descriptor path (153/153). `make test-tools` also OK (366 tests).

## Phase 1 pixel oracle (real `drawMonster`, device triplane)

Rendered through the shipping `drawMonster` (`startGray()` before `waitPlane()`,
each shade checked on a plane where it is ink). Cell origin `(BX,BY)=(40,28)`.

- **LUNGE** tail `(BX+3,BY+8)`: plane 0 = 1 / plane 2 = 0 idle; plane 2 = 1
  flash; west `(BX+3,BY+8)` = 0 and `(BX+29,BY+8)` = 1.
- **SWEEP** tail `(BX+2,BY+12)`: plane 0 = 1 / plane 2 = 0 idle; plane 2 = 1
  flash; west plane 2 = 1 (head reaches the cell).
- **HEAVY** tail `(BX+1,BY+14)`: plane 0 = 1 / plane 2 = 0 idle; plane 2 = 1
  flash; west `(BX+1,BY+14)` = 0 and `(BX+29,BY+14)` = 1.
- **RAVAGER** body `(BX+10,BY+10)`: plane 0 = 1 / plane 2 = 0 idle; plane 2 = 1
  flash; east head `(BX+24,BY+8)` plane 2 = 1, west plane 2 = 0.

## Interfaces

- Unchanged from the spike: `combat::ART_*`, `combat_data::Art`/`ART[]`,
  `combat_expect::CREATURE_<ID>_ART_*`, `mh::CombatArt`,
  `mh::combatCreatureArtRead`, `Game::combat.art`, `art_sheets::ART_SHEET_<NAME>`
  / `ART_SHEETS_COUNT` / `ART_SHEET_ADDR_<NAME>` / `ART_SHEETS[]`.
- Removed: `mh::render`-local `monsterSheet(int8_t)` and `spr::MON_*`.
- `drawMonster` body routing is now unconditional descriptor draw (spin/attack
  sheet branches still win first); zone overlays gate on `Game::combat.creature`.

## Notes / deviations

- **Sheet order**: the design's parenthetical ("keep fxpole last or sorted") is
  satisfied by the **sorted** list; fxpole is index 5. The existing pole
  `art.sheet` numeric pins were updated to the derived values
  (`ART_SHEET_FXPOLE`, count 5) — pixel pins untouched.
- Zone overlays were re-keyed off `Game::combat.creature` (see above); behavior
  for the four shipped beasts is identical, and it keeps the pole free of beast
  part overlays without depending on bead .2's `MON_POLE`.
- No `float`/`double`; no new cart reads in the paint path (art is seeded once in
  `creatureLoad`).
