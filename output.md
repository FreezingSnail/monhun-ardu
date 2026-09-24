# monhun-ardu-bih.4 — art draw phase 2: attack art in attack records

## What changed

Attack art moved into the attack records; the per-kind `beastAtk` compare chain
and the `MON_HEAVY` `spinSheet` gate are deleted from `drawMonster`.

- **data (`+3 B/attack` cart, 11 attacks = +33 B)**: each attack may author
  `art: { sheet, frame, mode }`. `sheet` resolves against `data/art_sheets.json`
  (1-based index, 0 = no overlay); `frame` is the pre-doubled 2-facing pose base;
  `mode` is `normal` (0) or `spin` (1). Packed as attack bytes 24/25/26;
  `ATTACK_SIZE` 24 -> 27.
- **authored**: lunge peck/leap/wing_beat -> `fxchickenatk` frames 0/2/4 mode 0;
  sweep stomp/gore/rear_kick -> `fxbullatk` 0/2/4 mode 0; heavy bite 0 / tail_slam
  4 `fxheavyatk`, tail_spin -> `fxtailspin` mode 1. Ravager/pole attacks stay
  sheet 0 (generic body), as before.
- **data/art_sheets.json**: appended `fxchickenatk`(6) `fxbullatk`(7)
  `fxheavyatk`(8) `fxtailspin`(9). Phase-1 indices 1..5 unchanged.
- **cache**: `CombatAttackCache` gains `artSheet/artFrame/artMode` (+3 B RAM),
  read once at `attackLoad` (one extra 3-byte burst); `CombatState` 105 -> 108.
- **render**: `attackPose` (WINDUP/ATTACK + atkIdx) selects `artMode == 1`
  (spin draw via the art table) / `artMode == 0 && artSheet` (2-facing sheet,
  windup tell 1..3 -> `(tell << 1) | west`, unauthored tell -> `artFrame | west`)
  / else the phase-1 generic body. Zone-overlay skip keyed on the new
  `attackSheet` flag instead of `beastAtk`.
- **oracle**: `tst/fxdatatest/monster_art_test.hpp` setups now `attackLoad()` the
  real records; added a per-attack art-sheet/frame/mode pin block (all 9 beast
  attacks, east+west pose pixels already pinned). All existing pins kept green.

## Verification (exact tails)

- `make gen` (x2, then `make gen-check` single-pass stable): the +33 B cart
  shift moved the equip/screens/cards/zone page offsets, so a second gen pass was
  required before `gen-check`:
  `fxdata_manifest: PASS (160 generated artifacts unchanged)`
- `make test`: `Total Passed: 6744  Total Failed: 0`
- `make test-tools`: `Ran 370 tests ... OK`
- `FXTEST_ONLY=test_monster_art make fxtest-headless`:
  `test_monster_art PASSED=170 FAILED=0` / `test_monster_art: PASS`
- `FXTEST_ONLY=test_combat make fxtest-headless`:
  `combat_test PASSED=237 FAILED=0` / `test_combat: PASS`
- `FXTEST_ONLY=test_tell make fxtest-headless`: `test_tell PASSED=18 FAILED=0`
- `make size` / `make size-line`:
  `size: flash=29476/29696 (220 free)  ram=1812/2560`

## Net delta vs phase-1 checkpoint (29542 / 154 free)

- **flash 29476/29696 = 220 free -> -66 B whole-image (154 -> 220 free)**, despite
  +33 B cart attack art + 12 B art-sheet table. The deleted `beastAtk` sheet/ordinal
  compare chain and `spinSheet` kind gate paid for the data. Above the ~150 B floor.
- **RAM 1809 -> 1812 (+3 B)** = the three cached attack-art bytes.
- Host test_image / device test images: test_monster_art 27260 B, test_combat 28006 B.

## Notes / interfaces

- `CombatAttackCache.artSheet/artFrame/artMode` (new), `combat::ATTACK_SIZE = 27`,
  generated `ATTACK_<CID>_<AID>_ART_{SHEET,FRAME,MODE}` expect pins.
- `art_sheets::ART_SHEET_FXCHICKENATK/FXBULLATK/FXHEAVYATK/FXTAILSPIN` +
  `ART_SHEETS_COUNT = 9`.
- Phase 3 (bih.5) can now move the chicken/bull/heavy zone part sheets into zone
  records; `attackSheet` is the hook the zone-overlay skip uses.

No git commit/push (orchestrator owns it).
