# monhun-ardu-ynb — device: charge attacks (flail/gun) + charge meter

Baseline HEAD f8605ec (clean tree; ouq/8xx/7pw landed). No commit/push.

## What changed

- `src/core/game.hpp`
  - `PS_CHARGE` appended after `PS_STUN` (existing 0..6 values unmoved).
  - `WeaponDef` gains `Attack charge[2]; ShellDef chargeShells[2];` appended after
    `alt` (blob 253 -> 329 B). Host table filled: flail charge = chargeslam1
    (4/6/14 24d reach26 hw28 hh18 stam14) + chargeslam2 (5/8/20 36d reach28 hw34
    hh24 stam22 trip); gun chargeShells = {34,45,7,6,70,12,1} + {46,55,8,8,70,18,1};
    sword zero.
  - Accessors `weaponCharge`, `weaponChargeShell`, `weaponHasCharge`,
    `weaponHasChargeShells` (single level-1 dmg read = presence test, matching the
    mock's `def.charge` / `def.chargeShells` truthiness).
  - `CHARGE_MIN 14`, `CHARGE_L2 20`; `MH_CHARGE` carve default 1 ->
    `CHARGE_ENABLED` (same pattern as MH_SHEATHE/MH_STAGE3).
  - `Player` appends `bool pA; uint8_t aHold; uint8_t chargeT; bool chargeArmed;`
    (init in `Player::init`).
- `src/core/player.hpp`
  - A press/hold/release counters gated by `CHARGE_ENABLED` (aHold +1 clamp 255,
    cleared on release; release clears `chargeArmed`).
  - `startAttack` sets `chargeArmed = true`; `tryBranch` clears it.
  - `startChargeAttack` / `fireChargeShot` ported from the mock. Charge shot uses
    `g.lastShot` codes 3 (L1) / 4 (L2).
  - PS_CHARGE case: rooted, chargeT clamp 255, release fires flail/gun; stamina
    short -> idle. Entry: idle + chargeArmed + A held + aHold >= CHARGE_MIN +
    weapon has charge data.
- `src/core/projectiles.hpp` — `spawnShot` accepts codes 1..4; 3/4 read
  `weaponChargeShell(def, shot-3)` and spawn through the same centre + facing*13,
  speedF, life 90, heavy-ball math.
- `src/render.hpp` — drawPlayer charge meter: 16 px bar at cx-8, y-4, 2 px tall;
  fill fraction round(16*chargeT/CHARGE_L2) clamped [1,16]; shade 2, shade 3 at
  >= CHARGE_L2. Flail/gun/sword overlays unchanged.
- `tools/gen-fxtables.cpp` — putWeapon appends charge[2] + chargeShells[2];
  require 253 -> 329; WEAPON_DEFS_BYTES 759 -> 897... corrected to **987**
  (3 x 329); output weapondefs.bin 759 -> 987 B.
- `tst/fxdatatest/data_test.hpp` — sizeof/stride/field-offset map for the new
  fields + charge values (chargeslam1/2, charge ball L1/L2).
- `tst/fxdatatest/test_parity.ino` — `#define MH_CHARGE 0`.
- `tst/player_test.hpp` — 5 permanent charge tests (flail L1/L2 ids+data, gun
  L1/L2 charged projectile, sword never PS_CHARGE, tap does not charge).
- `tst/shells_test.hpp` — charged-ball spawn geometry test (codes 3/4 + code 5
  ignored).
- `tools/fxdump.cpp` left untouched (art artifacts not regenerated for weapon
  JSON).
- Generated: `fxdata/tables/weapondefs.bin` (759->987), `fxdata/fxdata.bin`,
  `fxdata/fxdata-data.bin`, `fxdata/fxdata.h`, `src/fxdata.h`,
  `fxdata/manifest.json`, and offset-shifted `fxdata/tables/equip.bin` +
  `src/generated/equip_meta.hpp` (all from `make gen`).

## Verification (exact tails)

### make test
```
Total Passed: 4981
Total Failed: 0
```
Charge tests:
```
charge: flail hold A past the swing -> level 1, longer hold -> level 2  Passed: 14 Failed: 0
charge: gun release fires the charged ball (level-1 dmg 34)             Passed: 9  Failed: 0
charge: gun level 2 release fires the 46 dmg ball                       Passed: 5  Failed: 0
charge: sword never enters PS_CHARGE (no charge data)                   Passed: 3  Failed: 0
charge: a tap does not charge (chargeArmed cleared on release)          Passed: 3  Failed: 0
charged ball spawn: shot code 3/4 reads weaponChargeShell ...           Passed: 18 Failed: 0
```

### make gen (twice) + make gen-check
```
gen-fxtables: fxdata/tables/weapondefs.bin (987 B), ...
fxdata_manifest: PASS (68 generated artifacts unchanged)
```
Second gen produced no further diff (`weapondefs.bin` 759 -> 987 B).

### parity fixtures regen
```
node tools/gen-parity-fixtures.js
wrote tst/fxdatatest/parity_fixtures.hpp
scenes=20 ticks=1269 snapshots=32 cpFields=20
git diff --stat tst/fxdatatest/parity_fixtures.hpp   -> EMPTY
```

### make fxtest-headless FXTEST_ONLY=test_parity
```
Sketch uses 29598 bytes (99%) of program storage space. Maximum is 29696 bytes.
Global variables use 1816 bytes (70%) of dynamic memory, leaving 744 bytes ...
parity_test PASSED=660 FAILED=0
test_parity: PASS
```

### make fxtest-headless FXTEST_ONLY=test_data
```
Sketch uses 24562 bytes (82%) ... Global variables use 1179 bytes (46%) ...
data_test PASSED=368 FAILED=0
test_data: PASS
```

### make fxtest-headless FXTEST_ONLY=test_player_art
```
Sketch uses 14190 bytes (47%) ... Global variables use 2226 bytes (86%) ...
test_player_art PASSED=111 FAILED=0
test_player_art: PASS
```

### node --test mock/game.test.js
```
tests 69 / pass 69 / fail 0
```

### Full device gate (all suites)
All suites PASS: audio, boot, combat 235, data 368, hub 57, hud 17, menu_art 81,
menu 78, monster_art 38, parity 660, perf 5, player_art 111, quests 50,
screens 78, smith 66.

### make size (shipping, MH_CHARGE=1)
```
Sketch uses 27264 bytes (91%) ... Global variables use 1759 bytes (68%) ...
size: .text=27224 .data=40 .bss=1719
size: flash=27264/29696 (2432 free)  ram=1759/2560
```
Baseline f8605ec: flash=26660/29696 (3036 free), ram=1755/2560.
Delta: flash **+604 B** (free 3036 -> 2432), ram **+4 B**. All data facts
unchanged (no HAS_* flip).

test_parity image: 29598/29696 (98 B free) with `MH_CHARGE 0` carve applied.

## Notes / deviations

- `make size` measured +604 B shipping, not the small number the bead's ~70 B
  test_parity remark implied; the carve keeps test_parity at 98 B free.
  No data fact flipped, so this is pure code + the charge accessor/FSM paths.
- `weapondefs.bin` is 987 B (3 x 329), not 897: 253 base + 2x23 charge + 2x15
  chargeShells = 329/weapon; 3 x 329 = 987. The bead's "759 -> 897" arithmetic
  omitted one weapon's worth (759 + 3*76 = 987).
