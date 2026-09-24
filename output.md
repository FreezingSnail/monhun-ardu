# monhun-ardu-hbk.12 — GEAR equipment-box slot view (owned-only candidates)

Status: **DONE.** The shipping image links at **29512 / 29696 (184 free)**, RAM
1799/2560. Delta vs HEAD (28826, 870 free) is **+686 B** (the +712 B slot view
minus the 26 B denied-cue trim). No commit/push.

Design expected ~230 free; the implementation lands 184 free (a ~46 B gap over
the hand estimate). 184 free is above the ~150 floor. **LR cycle (+~70 B) would
NOT hold 150 free** (184 - 70 = 114). The hub header zenny block (86 B) was not
cut.

## What changed

1. **`data/screens/gear.json`** — GEAR is now `"slots": true` with 11 rows
   (2 baked pages): four slot rows (WEAPON / HEAD / BODY / CHARM, action
   `slot_pick`, param 0..3), the `-- SKILLS --` header, the five `skill` rows
   and LEAVE. The old `"weapons": "equip"` 24-row tree and the 5 armor rows are
   gone.

2. **`tools/gen-screens.py`**
   - `ACTION_NAMES` appends `slot_pick` (`ACTION_SLOT_PICK = 16`); `slot_pick`
     params are validated 0..3.
   - New `slots` key (optional bool) + `slot_candidates()`: slot 0 = the 9 forge
     nodes in data order (id = node id, label = node label); slots 1/2/3 = the
     armor pieces grouped by armor slot (id = piece index, label from
     `data/armor.json`). Loads both the forge and armor models when a screen
     asks.
   - `pack_blob()` appends each slots screen's candidate table **after** the
     fixed page table (so every existing offset is unchanged): `u8 slotStart[5]`
     (cumulative) then 4-byte `{u8 id, u24 labelOff}` entries then the label
     records (`u8 len + bytes`) the offsets point at. `emit_meta_header()`
     emits `SCREEN_GEAR_SLOT_TABLE` (869) and `SIZE` 1042.

3. **`src/screen_state.hpp`** — `SCREEN_SLOT_COUNT = 4`; `ScreenState::slotSel[4]`
   zeroed by `screenReset()`; header comments refreshed.

4. **`src/screens.hpp`**
   - `screenTextLabel()` helper (draws a buffered label, used by the selected-row
     white redraw and the slot names).
   - Slot table readers: `screenGearSlotFirst/Count/Id/Entry` + a single
     `screenGearSlotState(save, slot, id)` (0 none / 1 owned-crafted /
     2 equipped). The table walkers are `MH_NOINLINE` (measured win: three
     inlined copies cost ~90 B more).
   - `screenGearSlotDefaults()` — GEAR entry default per slot: equipped
     candidate, else first owned, else 0 (single pass; `sel == 0` doubles as
     "not found" because the fallback is 0). `screenEnter()` calls it for GEAR.
   - `screenGearSlotCycle()` — A on a slot row: advance `slotSel[slot]` to the
     next owned candidate (wrapping), then equip in place (weapons toggle
     `equippedNode` to `SAVE_NODE_NONE`, armor `armorEquipToggle`); false when
     nothing is owned.
   - `drawScreen()` — new `ACTION_SLOT_PICK` branch: resolve the shown candidate
     entry, draw its name with `screenTextLabel` at `SCREEN_LABEL_X`
     (`fxfontg`, or `fxfontw` when selected) and `screenMarker` at x=118
     (white equipped / gray owned). Nothing owned -> the baked slot label stays.
     Slot rows are excluded from the generic white label redraw (the candidate
     name replaces it).

5. **`monhun-ardu.ino`**
   - New `ACTION_SLOT_PICK` dispatch: `screenGearSlotCycle` -> `saveStore`, then
     falls through to the existing GEAR readout refresh (no card). Nothing owned
     -> plain return.
   - GEAR entry now calls `screenGearSlotDefaults` before `refreshGearReadout`.
   - **Denied-cue trim**: all three `mh::audioPlay(mh::CUE_HURT)` call sites
     (blocked card A, nothing-to-upgrade UPGRADE row, gated list row) are now
     plain returns.

6. **Tests**
   - `tst/fxdatatest/screens_test.hpp` — GEAR pins rewritten for the slot view:
     11 rows / action ids / baked slot + skill rows; the candidate table
     (counts 9/2/2/1, ids, label records); entry defaults (fresh save ->
     equipped sword root; crafted-not-equipped -> first owned; equipped ->
     equipped); A rotation walks the owned roots 0 -> 3 -> 6 -> 0 and equips
     each, nothing-owned is a no-op, armor A equips a crafted piece; slot
     pixels (baked HEAD label alone on a fresh save, "BONE CAP" + gray marker
     after crafting, white marker on plane 2 after equipping, SKILLS band);
     skill-row pixel rows moved to ATTACK UP row 5 (y=56) / DEFENSE UP row 6
     (y=11); GEAR page count 4 -> 2 and the page-3 address pin -> page 1.
   - `tst/fxdatatest/cards_test.hpp` — the armor card E2E reads
     `SCREEN_ARMOR_FORGE` row 0 (GEAR carries no armor rows now).
   - `tst/fxdatatest/forge_test.hpp` — the weapon equip/unequip block drives a
     synthetic `ACTION_EQUIP_WEAPON` row (GEAR has no weapon rows).
   - `tst/fxdatatest/hub_test.hpp` — the flail-equip E2E presses A on the GEAR
     WEAPON slot row (`screenGearSlotCycle`) instead of an equip row.
   - `tst/screens_test.hpp` — `screenReset` zeroes `slotSel`.
   - `tools/tests/test_gen_screens.py` — new `test_slots_candidate_table`
     (table layout + label records) and `test_slot_pick_param_out_of_range_rejected`.

## Command tails (final tree)

```
$ make gen
gen-screens: 7 screens, 47 rows, 10 pages, 1042 B blob (magic 0x5343 version 1)

$ make gen-check
fxdata_manifest: PASS (159 generated artifacts unchanged)

$ make test
Total Passed: 6359
Total Failed: 0

$ make test-tools
Ran 360 tests in 21.622s
OK

$ FXTEST_ONLY=test_screens make fxtest-headless
test_screens PASSED=212 FAILED=0
P
test_screens: PASS

$ FXTEST_ONLY=test_screens_smithy make fxtest-headless
test_screens_smithy PASSED=98 FAILED=0
P

$ FXTEST_ONLY=test_hub make fxtest-headless
test_hub PASSED=79 FAILED=0
P

$ FXTEST_ONLY=test_cards make fxtest-headless
test_cards PASSED=85 FAILED=0
P

$ FXTEST_ONLY=test_forge make fxtest-headless
test_forge PASSED=63 FAILED=0
P

$ make size-line
Sketch uses 29512 bytes (99%) of program storage space. Maximum is 29696 bytes.
Global variables use 1799 bytes (70%) of dynamic memory, leaving 761 bytes for local variables. Maximum is 2560 bytes.
size: flash=29512/29696 (184 free)  ram=1799/2560
```

## Generated set

`make gen` reached a fixpoint (page addresses resolve from the previous
`fxdata.h`, so it took two passes). The FX layout shrank (screens blob
1094 -> 1042, GEAR pages 4 -> 2, fxdata.bin 410624 -> 404480) so the absolute
offsets in `fxdata/tables/cards.bin`, `equip.bin`, `src/generated/equip_meta.hpp`
and `src/generated/zone_meta.hpp` shifted accordingly; all are regenerated
together and `gen-check` is green.

## Numbers

- size: **29512/29696 (184 free)**, delta **+686 B** vs HEAD 28826.
- LR cycle (+~70 B) -> 114 free: **would not** hold 150.
- RAM: 1799/2560 (slotSel adds 4 B).

## Wall time

- worker total: ~26 min (session 22:52 -> 23:18), including ~8 `make size-line`
  trim iterations and the 5 device-suite compiles+runs.
