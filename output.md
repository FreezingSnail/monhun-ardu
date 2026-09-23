# monhun-ardu-5co.8 — ui.5.1 trim: reclaim >= ~600 B for the hub chrome

Status: **DONE.** Reclaimed **632 B** flash (vs the ~600 B target) with the full
gate green and no behavior change. Free headroom is now **746 B**, enough for the
deferred ui.5b chrome (658 B measured) with ~88 B margin.

```
baseline (HEAD 5f4b7f9):
size: .text=29550 .data=32 .bss=1769
size: flash=29582/29696 (114 free)  ram=1801/2560

after:
size: .text=28918 .data=32 .bss=1766
size: flash=28950/29696 (746 free)  ram=1798/2560
```

| | flash |
|---|---|
| HEAD 5f4b7f9 (baseline) | 29582 (114 free) |
| this wave | **28950 (746 free)** |
| reclaimed | **632 B** |

Target: free >= ~700 B (114 + reclaim >= ~600). **746 >= 700.** RAM also drops
3 B (the removed `Game` smithy-cache fields).

## Adopted reclaim set (whole-image `make size`, isolated from HEAD)

Each candidate was built alone against HEAD 29582 so the delta is its own cost.

| # | Candidate | files | isolated flash | free after | notes |
|---|---|---|---|---|---|
| 1 | Save wire image copied, not field-by-field | `src/core/save.hpp` | **−216** | 330 | encode/decode = two `__builtin_memcpy` around the 3 reserved bytes; the old per-field stores/loads were ~33 unrolled each |
| 2 | Armor device aggregation rewrite | `src/armor.hpp` | **−222** | 336 | one bulk read per equipped piece; skip the never-read resist/zenny/mat/sheet bytes; finalize+tier+effects share one loop over one bulk skill-table read |
| 3 | `QuestDef` bulk read | `src/quest.hpp` | **−92** | 206 | 9 B record is byte-identical to `QuestDef`; one `mhFxReadBytes` replaces 7 seek-per-field reads |
| 4 | smithy runtime cache removal | `zones/world/game/player.hpp` | **−48** | 162 | dead since the SMITH screen was deleted (ui.3.1); FORGE lives on the hub, so the camp-smithy range was never read. Cart data/generator untouched |
| 5 | `screenReadRow` fixed-part bulk read | `src/screens.hpp` | **−48** | 162 | cost/action/flags/cond/param are contiguous and match the front of `ScreenRow`; one 6 B read replaces 6 reads |
| | **combined** | | **−632** | **746** | interactions net the isolated sum (−626) to −632 |

## What changed

### 1. Save encode/decode (`src/core/save.hpp`) — 216 B

The wire record is the in-RAM `SaveBlock` byte image with a 3-byte header
(magic u16 + version) in front and the 3 reserved bytes between `progress` and
`equip`, plus the checksum. `saveEncode`/`saveDecode` now copy the two
sub-images (`__builtin_memcpy`) instead of ~33 unrolled field stores/loads.
Endianness is LE on AVR and every host target; the static layout asserts pin the
coupling. Also fixed the stale 44-byte comments (record is 33 B since ui.4.1).

### 2. Armor device path (`src/armor.hpp`) — 222 B

- `armorApplyToGame`: aggregates straight into `g.armor`, walks only the three
  equipped slots, one `mhFxReadBytes` per packed 18 B piece record, and reads
  only `slot`/`defense`/`skills[]`/`points[]`. The resist/zenny/mat/sheet bytes
  were already never read by the runtime (the GEAR readout reads points/tier,
  combat reads `armorFx`); they stay in the cart record.
- New `armorResolve(ArmorAgg &, ArmorEffects &)`: fuses the old
  `armorFinalize` + `armorEffects` into one loop, bulk-reads the 5 skill records
  once, and resolves kind -> magnitude. Semantics match the host
  `armorFinalize`/`armorEffects` (clamp points to `THRESHOLD_M`, tier, then
  `min(points,maxPoints)*perPoint`).
- Removed the now-unused `armorReadPiece`/`armorReadSkill` decode helpers (only
  `armorApplyToGame` used them; the host suite uses plain structs).

### 3. Quest def reader (`src/quest.hpp`) — 92 B

`QuestDef` has the packed record's byte layout, so `questReadDef` is one bulk
read of `RECORD_SIZE`; `offsetof` asserts pin `rewardZenny`/`unlockFlag`.

### 4. Dead smithy cache (`zones/world/game/player.hpp`) — 48 B

`Game::roomFirstSmithy`/`roomSmithyCount` and `ZoneRoom::firstSmithy`/
`smithyCount` were only written (loadRoom/initGame) and never read after the
SMITH screen removal; dropped the fields, the two cart reads, the two stores and
the init. `zoneSmithyRead`/`ZoneSmithy` were already unused (LTO-dropped, 0 B)
and left in place; the generator + cart smithy data are untouched.

### 5. Screen row fixed-part bulk (`src/screens.hpp`) — 48 B

`screenReadRow` copies the 6 contiguous bytes after the variable label straight
into `&row.cost`; static asserts pin the `ScreenRow` field order.

## Rejected / measured candidates

| Candidate | Δ flash | why not |
|---|---|---|
| `armorEffects` `MH_NOINLINE` | +22 | outlining one-caller helper regresses |
| `itemRead` bulk read | +30 | inlined into `main`; call+stack beats 4 shared reads |
| `drawScreen` per-row 6 B bulk | −4 | not worth the added branch |
| `EFF_OFF` offset table for the effect switch | −14 | +4 B `.data` (const-in-RAM trap) — net not worth it |
| Bake `perPoint` as flash constants | −20 | only −20 over the bulk skill-table read; violates the cart-data principle |
| `-fno-tree-sink` / `-fno-move-loop-invariants` | −84 / −24 | global codegen flags; need perf re-validation and were not needed once the code trims hit target |
| `-maccumulate-args` / `-fno-jump-tables` / `-fira-region=all` | +50 / +4 / +12 | regress |
| `smith.hpp` / `upgradeFind` / `upgradeResolve` | 0 | never included by a shipping TU — LTO already drops them; removes FX-cart bytes only |
| generated dead consts (`ACTION_BUY_UPGRADE`, `COND_ZENNY/FLAG/TIER/UPGRADE`, `ROW_F_HIDE_LOCKED`, `ROW_F_FORGE`) | 0 | `constexpr`, no emitted code |

## Tests

Permanent coverage is the existing native suites; the new layout couplings are
pinned with compile-time `static_assert`s (host + device) instead of duplicated
runtime tests:

- `src/quest.hpp`: `offsetof(QuestDef, rewardZenny/unlockFlag)` == the packed
  `quests::DEF_*_OFF`.
- `src/screens.hpp`: `offsetof(ScreenRow, action/flags/cond/param)`.
- `src/core/save.hpp`: `sizeof(SaveBlock)` and `SAVE_EQUIP_OFF` vs the wire
  header/reserved gap.
- Save wire bytes are already asserted byte-for-byte by
  `tst/screens_test.hpp` and `tst/quests_test.hpp`; armor aggregation/quest/
  screen reads are exercised by the device suites (`test_smith`, `test_screens`,
  `test_hub`, `test_quests`, `test_data`, `test_boot`).

No tests were removed or weakened.

## Gate tails

`make gen-check`:
```
fxdata_manifest: PASS (148 generated artifacts unchanged)
```

`make test`:
```
Total Passed: 6306
Total Failed: 0
```

`make test-tools`:
```
Ran 344 tests in 19.414s
OK
```

`make fxtest-headless` (full, 18/18; log `build/5co8-fxtest.log`):
```
asset_test PASSED=264  test_audio PASSED=9    test_boot PASSED=4
test_cards PASSED=85   combat_test PASSED=237 data_test PASSED=348
test_forge PASSED=58   test_hub PASSED=81     test_hud PASSED=29
test_items PASSED=35   test_monster_art PASSED=127  test_perf PASSED=5
test_player_art PASSED=120  test_quests PASSED=87  test_screens PASSED=131
test_smith PASSED=51   test_tell PASSED=18     zones_test PASSED=82
```

`make size`:
```
size: .text=28918 .data=32 .bss=1766
size: flash=28950/29696 (746 free)  ram=1798/2560
```

`make mini` also builds clean at 28950 / ram 1798.

## Files

```
 M src/armor.hpp        device aggregation rewrite + fused armorResolve
 M src/quest.hpp        QuestDef bulk read + offsetof asserts
 M src/screens.hpp      screenReadRow 6 B bulk + field-order asserts
 M src/core/save.hpp    wire byte-image encode/decode + layout asserts/comment fix
 M src/core/zones.hpp   drop the dead smithy range fields/reads
 M src/core/world.hpp   drop the smithy cache stores
 M src/core/game.hpp    drop Game::roomFirstSmithy/roomSmithyCount
 M src/core/player.hpp  drop the smithy init
```

No commit/push (orchestrator commits between bead waves).

## Blockers

None. Free 746 B > the ~700 B target; ui.5b's deferred chrome (HUNT column +238,
bottom strip +276, page indicator +144 = 658) now fits with ~88 B to spare.
