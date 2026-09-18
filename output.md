# monhun-ardu-76y — chicken: tall legs, head/legs zones, legs-only collide

Worker report. Epic monhun-ardu-nch. No commit/push by worker. Finishing the
cancelled partial change on disk.

## What changed

### 1. Data / schema (generator end-to-end)
- `data/creatures/lunge.json`: added `collide {ox:9, oy:11, w:12, h:13}` (legs),
  `zones.head` (box 18,0,11,7; hp 40; dmgMul 130; share 100; SLASH; stagger 12)
  and `zones.appendage` = legs (box 9,0,9,24; hp 60; dmgMul 150; share 40;
  SLASH; stagger 30; broken dmgMul 200 + `disableAttacks:["sweep"]`).
- `tools/gen-combat.py`: `normalize_collide`, optional `collide` key defaulting
  to the body box (stats w/h at origin), 21 B `CREATURE` packed record
  (collide 4 B before hp/spawnX/spawnY) and 21 B emitted `combat_data::Creature`
  with a `Box collide` member; dump line now prints `, collide body|box(...)`.
- `src/core/combat.hpp`: `PkCreature` gains `collideOx/Oy/W/H`; `CombatCreature`
  and `CombatSpawn` gain `collide`; reader bulk-reads the 4 contiguous collide
  bytes; new `combatCreatureCollideBox(creatureId)`; `creatureCacheReset`
  zeroes `g.combat.collide`; `creatureLoad` caches it. `CombatState` is now
  79 B (`static_assert` updated).
- `src/core/game.hpp`: `CombatState.collide` member + comment.
- `src/core/monster.hpp`: `monsterCollideRect()` (authored collide box, else the
  body box); `syncMonsterTarget` targets it and `pushApart` uses it. Falls back
  to `m.w/m.h` when `collide.w/h == 0`, so every creature without a box is
  byte-identical.
- Regenerated `src/generated/combat_{data,expect,meta}.hpp`,
  `src/fxdata.h`, `fxdata/*`, `equip_meta.hpp` (sheet offsets shifted +40 B).

### 2. Art
- `tools/gen-art.py:_chicken_east`: body raised into rows 4..13, two long legs
  with knee/shank/foot reaching row 21; still 32x24, 14-frame layout.
  `images/blocks/fxmonster_lunge_32x24.png` + generated FX data updated.

### 3. Sim (host C++)
- Player melee/whirl/projectile hit tests already route through `g.target.rect`
  (now the collide rect); zone resolve still happens at the landed hit point.

### 4. Mock (source of truth)
- `mock/game.js`: `MONSTER_DEFS.lunge.collide`; `MONSTER_ZONES.lunge` head+legs;
  live pools on `m.zones`; `m.kind`; `facePoint`/`zoneContains`/`zoneHitResolve`
  mirroring `combatFacePoint`/`combatZoneContains`/`combatZoneHitResolve`;
  `monsterOnHit` wrapper; melee/whirl/projectile overlap now uses `targetRect`
  (legs-only) and routes damage through the zone resolver. `pushApart` uses the
  collide rect (as before).

### 5. Tests
- Host: `combat_pack_test.hpp` creature decode offsets now +11 collide, hp +15,
  spawn +17/+19; `combat_test.hpp` ZONES_COUNT 3→5, collide-box accessor checks,
  lunge zone seeding in `creatureLoad`; `monster_test.hpp` push test split into
  "body overlap passes under" + "legs shove west by 3", sword test moved to
  `dx=14` (front crit body path still 12); `world_test.hpp` target rect = 12x13;
  `art_dims_test.hpp` eye at (23,2).
- Device: `fxdatatest/combat_test.hpp` lunge head/legs zone + collide spot
  checks; `fxdatatest/monster_art_test.hpp` asserts lunge HAS a legs appendage
  zone but still draws no overlay (east band clear).
- `tools/tests/test_gen_combat.py`: dump string `collide body`; 21 B creature
  payload with default body collide.
- `mock/game.test.js`: sword tap `dx=14`, whirl throw `dx=28` (both now reach the
  legs-only target rect). Also fixed the stale gunshield ammo assert from the
  already-landed unlimited-ammo change (deviation, see below).

## Verification

### 1. Generation
`make gen` (x2) + `make gen-check` → exit 0
```
fxdata_manifest: fxdata/manifest.json up to date (38 images, 55 inputs, 17 outputs)
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (64 generated artifacts unchanged)
```

### 2. Host + tools
`make test` → exit 0
```
Total Passed: 3686
Total Failed: 0
```
`make test-tools` → exit 0
```
Ran 137 tests in 7.122s

OK
```
`node --test mock/game.test.js` (native JS suite) → 24/24 pass.

### 3. Device (full `make fxtest-headless`)
All suites PASS = 0 FAILED:
```
asset_test PASSED=278  audio PASSED=14  boot PASSED=4
combat PASSED=210  data PASSED=221  hub PASSED=57  hud PASSED=17
menu_art PASSED=60  menu PASSED=59  monster_art PASSED=18
parity PASSED=660  perf PASSED=5  player_art PASSED=111
quests PASSED=50  screens PASSED=78  smith PASSED=66
```
Perf vs baseline `rMx=4968 rAv=4755 pUs=6375`:
```
B pUs=6372 pHz=156 lHz=52 lTk=536 rMx=4968 rAv=4755 ram=601
```
pUs 6372 (−3), rMx/rAv identical, ram 601.

### 4. Build + size (baseline flash 26680 / free 3016)
`make build` / `make size`:
```
Sketch uses 26900 bytes (90%) of program storage space.
Global variables use 1867 bytes (72%) of dynamic memory, leaving 693 bytes.
size: .text=26834 .data=66 .bss=1801
size: flash=26900/29696 (2796 free)  ram=1867/2560
```
Flash delta **+220 B** (26680 → 26900), free 3016 → 2796.
Cart: `fxdata/fxdata.bin` **151040 → 151040 (0 B)**; `fxdata/fxdata-data.bin`
150883 → 150923 (**+40 B**, all `combat.bin` 634 → 674).

### 5. Parity regen scope
`node tools/gen-parity-fixtures.js` (idempotent; same md5 on repeat). Diff vs
HEAD touches 2 of 20 scenes — both lunge (parity only ever spawns lunge):
```
flail_whirl_throw_hit: changed hashes=12/28, changed snap fields=2/20 (1 cp)
beast_no_shove_idle:   changed hashes=40/40, changed snap fields=2/20 (1 cp)
changed scenes=2 of 20
```
The other 18 scenes (including `monster_sweep_hit`, `monster_windup_cycle`,
train/pole scenes) are byte-identical. SWEEP/HEAVY/RAVAGER have no parity
scenes; their sim is unchanged because the generator defaults their collide box
to the body box, and `monsterCollideRect` returns exactly `m.x/m.y/m.w/m.h` for
them (device `combat/data` suites green). The heavy tail zone was the earlier
landed change and stays parity-clean.

### 6. Chicken idle frame (frame 0, east) ASCII
```
  ..................WWWWWWWWWWW...
  .................gWWWWWWWWWWW...
  .................gWWWWWKKWWWW...
  lllggg...........gWWWWWKKWWWKKKK
  ggggggggggggggggggWWWWWWWWWWKKKK
  ggggggggggggggglllWWWWWWWWWWKKKK
  lllgggglllllllllglllg.......KK..
  ggggggggggggggggglllg.......KK..
  ggggggggggggggggglllg...........
  ggggggggKKKKKKKKKlllg...........
  .lllggggggggggggglllg...........
  .gggggggKKKKKKKKglllg...........
  .ggggggKKKKKKKKKKKKgg...........
  ......gKKKKKKKKKKKKgg...........
  ...........KK...KK..............
  ...........KK...KK..............
  ..........KKKK.KKKK.............
  ..........KKKK.KKKK.............
  ...........KK...KK..............
  ...........KK...KK..............
  .........K.KK...KK.K............
  .........KKKKK.KKKKK............
  ................................
  ..KKKKKKKKKKKKKKKKKKKKKKKKKKKK..
```

## Deviations / notes
- Pre-existing failure fixed: `mock/game.test.js` "gunshield hold-B …" still
  asserted magazine decrement, stale since `396bcdc` (unlimited demo ammo).
  Corrected the assertion to `shells0` (no behavior change; the JS suite is not
  part of `make` gates but is a permanent co-located test).
- No behavior changes for SWEEP/HEAVY/RAVAGER; only the chicken moved.
- `fxdata.bin`/`equip.bin` contents changed (sheet offsets) but sizes are
  unchanged; staged together by the orchestrator via `make gen` outputs.
- No BLOCKED items.
