# monhun-ardu-prg.5 — save v2: EEPROM block for inventory + equipment + zenny

HEAD at start: `1499a96` (prg.4 gather). No commit/push (orchestrator commits).
End state: full gate green — gen-check, host, tooling, full `fxtest-headless`,
size. Flash delta **+422 B** (27914 -> 28336), RAM **+12 B** (1593 -> 1605).
Both inside the bead budget (<=900 B flash, small RAM).

## Layout (SaveBlock v3, 27 B)

Extends the bead-me6 v2 record with a progression tail; the 14-byte v2 payload
is unchanged and the old checksum byte (14) becomes the first equip slot:

```
 0..1  magic   u16 0x484D
 2     version u8 3
 3..4  zenny   u16
 5..8  quest   u8[4]   (16 x 2 bits: taken, done)
 9     activeQuest u8  (0xFF = none)
 10    progress    u8
 11..13 tier  u8[3]    (N_WEAPONS)
 14..16 equip u8[3]    (head/body/charm; 0 = none; arm.2 slots)
 17    flags  u8       (SAVE_FLAG_SMITHY_SEEN reserved for prg.7)
 18..25 items u8[8]    (inventory counts, cap 255; item::ITEM_COUNT)
 26    checksum u8     (sum of bytes 0..25)
```

`SAVE_ITEMS_OFF`/`SAVE_ITEMS` size come from the generated `item::ITEM_COUNT`,
so an item-table regen re-sizes the record automatically (C++ constexpr, not a
baked literal). `SAVE_VERSION` 2 -> 3.

## What landed

- `src/core/save.hpp`: v3 struct (equip/flags/items), `saveEncode`/`saveDecode`
  over the full record, `saveDecodePrefix` (shared 14-byte v1/v2 payload),
  `saveDefaults` clears the tail, `saveItemCount/Add/Consume` (id-checked,
  saturating at 255), `saveFoldItems` (per-slot max of live vs saved).
  - Migration in `saveLoad`: v3 decodes; a **v2** record keeps its prefix and
    defaults the prg.5 tail; a **v1** record keeps its zenny/quests/tiers and
    clears the active quest/progress it never had; anything else ->
    `saveDefaults`. Never crashes, never returns garbage. Returns true for a
    well-formed (possibly migrated) record, false only for blank/junk.
  - `saveStore` unchanged (write-if-different + verify read) and now covers the
    tail.
- `src/app_state.hpp`: `appHuntCommit` now takes `Game &g` and folds *both* the
  quest progress (when a quest is active) and the hunt inventory gains
  (`max(live, saved)` per slot) in one latency-once-per-hunt call; returns true
  only when something changed. One `saveStore` at the sketch's single hunt-end
  commit persists both.
- `src/app_setup.hpp`: new `itemsApplyToGame()` re-seeds `Game::items[]` from
  the save (newGame clears RAM items each hunt).
- `monhun-ardu.ino`: apply items on boot and at every hunt start (menu + shelf
  branches); hunt-end commit call passes `g`.
- Gather/carve already update `Game::items[]` in RAM only (no per-item EEPROM
  write); the hunt-end commit is the single coalescing point.

## Tests

- `tst/screens_test.hpp` (host, +5): v2 layout offsets + encode/decode of
  equip/flags/items with exact bytes and checksum coverage; `saveItemAdd/
  Consume` saturation + out-of-range inert + `saveFoldItems` max semantics;
  v2-record migration (prefix preserved, tail defaults); v1-record migration
  (zenny/tier/done preserved, activeQuest cleared). Existing pins updated
  intentionally (checksum offset, version, byte count).
- `tst/quests_test.hpp` (host): save-layout pins now assert the checksum
  terminates the record (offset == `SAVE_ITEMS_OFF + ITEM_COUNT`) and version 3.
- `tst/app_state_test.hpp` (host, +1): hunt-end commit folds quest progress and
  item gains (`max`) once per hunt, latch semantics unchanged.
- `tst/fxdatatest/hub_test.hpp` (device): a 4-herb pantry is stored, restored
  into the hunt by `itemsApplyToGame`, a carved scale + gathered ore are folded
  by the hunt-end commit, and all reload from real EEPROM.
- `tst/fxdatatest/screens_test.hpp` (device): EEPROM round-trip now covers the
  prg.5 tail (equip/flags/inventory) plus a write-on-change pin that one changed
  inventory byte rewrites exactly that byte + the checksum.

## Verify (exact tails)

- `make gen-check`: `fxdata_manifest: PASS (85 generated artifacts unchanged)`.
- `make test`: `Total Passed: 6053` / `Total Failed: 0`.
- `make test-tools`: `Ran 240 tests in 12.400s` / `OK`.
- `make fxtest-headless` (full): all maintained suites PASS — assets 270, audio
  10, boot 4, combat 237, data 368, hub 61, hud 25, items 35, menu_art 53, menu
  60, monster_art 111, player_art 111, quests 50, screens 83, smith 66, tell 17,
  zones 80. `test_perf`:
  `B pUs=6348 pHz=157 lHz=52 lTk=480 rMx=3344 rAv=3074 ram=713`
  `perf_test PASSED=5 FAILED=0` -> PASS.
- `make size`: `size: flash=28336/29696 (1360 free)  ram=1605/2560`
  (baseline 27914/1782 free -> **+422 B** flash, RAM +12). Data facts unchanged
  from baseline.

## Deviations / notes

- `SaveBlock` grew from the v2 field set; the struct in RAM mirrors the new
  payload. The `sizeof(SaveBlock) >= SAVE_BYTES - 3` policy pin is unchanged and
  still holds (struct is fields only).
- `saveLoad` now reports `true` for a migrated older record (fields were
  loaded); only blank/junk is `false` with defaults. This is the documented
  contract in the header.
- Equipment ids are reserved placeholders (0 = none); `arm.2` fills the slots.
  `SAVE_FLAG_SMITHY_SEEN` is reserved for prg.7's camp smithy route.
- Parity image / `mock/` untouched (`make fxtest-headless` still excludes
  `test_parity`).
