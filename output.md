# monhun-ardu-9lw — eqf.2.1 move PART tables to cart blob

Worker report. No commit/push/`git add` performed.

## What changed

- `tools/gen-equipment.py`: the gen-art player part view moved out of flash into
  the `mhEquip` blob. New part section after the 19 B item records:
  `PART_COUNT` x 18 B records (sheet u24, anchorX i8, anchorY i8, flat u8,
  frame[POSE_COUNT] u8), then `PART_COUNT+1` u16 variant-index entries, then the
  variant frame bytes. `load_fxdata_symbols()` now parses the uint24_t values
  from `fxdata/fxdata.h` (not just names) and the records bake the sheet offset
  in. New `part_section()` is the single layout authority shared by the blob
  packer and the header emitter, so offsets cannot drift.
- `src/generated/equip_meta.hpp`: no more PROGMEM part arrays / `partSheet()`.
  Emits only constants: `PART_*` ids, `PARTS_OFF`, `PART_SIZE`, the per-record
  field offsets, and the variant table offsets/count. Authoring-catalog
  constants (`ITEM_*`, `POSE_ROW_*`, `FRAME_*`, `SHEET_*`) stay as-is (compile
  time; not emitted in the device image).
- `src/render.hpp`: `PartRec` (static_assert == `equip::PART_SIZE`) + `partCart`,
  `partRead`, `partSheet`. `partDraw`/`partVariantDraw` now fetch one record with
  a single `mhFxReadBytes` burst (plus one u16 + one u8 read for the variant
  form). All reads are in `drawPlayer`, in the render pass between plane blits.
- `tools/tests/test_gen_equipment.py`: part-view/`flat` tests updated to the blob
  layout (offsets 103/121/125, record bytes, variant table/data); blob length now
  131 B for the 4 authored + 1 gen-art fixture.

## Verification (exact tails)

1. `make gen` twice -> `make gen-check`
   `fxdata_manifest: PASS (45 generated artifacts unchanged)`
2. `make test` -> `Total Passed: 3119` / `Total Failed: 0`
   `make test-tools` -> `Ran 72 tests in 3.988s` / `OK`
3. `make fxtest-headless` (all 10 suites)
   - assets 254/0, audio PASS, boot PASS, combat 195/0, data 221/0, hud PASS,
     menu 59/0, `parity_test PASSED=660 FAILED=0`
   - `test_player_art PASSED=111 FAILED=0` (goldens byte-identical, not edited)
   - perf `B pUs=6562 pHz=152 lHz=50 lTk=984 rMx=5492 rAv=4984 ram=411` -> 5/5 P
4. `make build` + `make size`
   `size: .text=26940 .data=58 .bss=1960`
   `size: flash=26998/29696 (2698 free)  ram=2018/2560`
   flash delta vs **27282 baseline: -284 B** (26998). Target <=27000 met;
   the ~26800 stretch target is not (2 B margin).

## Deviations / notes

- **Acceptance met at the wire: 26998 vs the 27000 cap (2 B).** The 328 B of
  removed tables costs ~44 B of new cart-read code/offsets, so the net is 284 B.
- **Sheet offsets are baked into the blob; this is only safe while every gen-art
  sheet lives before `mhEquip` in the FX image.** All 10 referenced sheets are
  `fx*` and precede `mhEquip` (checked in `src/fxdata.h`: max referenced is
  `fxslash` 0x291E < `mhEquip` 0x77CD), so growing the blob does not shift them.
  The authored `mh_*` equip sheets (after `mhEquip`) would shift and must not be
  referenced by a gen-art record without a reorder; no code guard enforces this
  yet.
- Blob header/version unchanged (`MAGIC 0x4551`, `VERSION 1`): the part section
  is appended, existing header + item records are byte-identical.
- `rMx` 5492 us vs the 5120 us recorded at the abr bead (+372 us, +1 extra cart
  burst per part draw); floor 7407 us.
- `fxdata.bin`/`fxdata-data.bin`/`fxdata.h`/`manifest.json` are the regenerated
  set (blob 388 -> 716 B, FX image 87897 -> 88225 B); staged together with the
  generated header as usual.

## Follow-up: stale-blob compile-time guard

- `tools/gen-equipment.py` now also emits, per referenced gen-art sheet symbol,
  the baked absolute offset as `constexpr uint16_t SHEET_OFF_<SYMBOL>`, and on
  AVR a matching `static_assert(SHEET_OFF_<SYMBOL> == static_cast<uint16_t>(<symbol>),
  "equip blob stale: re-run make gen")`. `<symbol>` comes from `../fxdata.h`
  (included only under `__AVR__`; host builds skip it). Zero flash cost
  (constexpr + static_assert only) -- `make size` still 26998.
- Generator/docstring explains the two-pass behaviour: addresses come from the
  previous `fxdata/fxdata.h`, so adding/renaming a gen-art sheet needs a second
  `make gen` to re-bake; the assert catches a skipped pass.
- `tools/tests/test_gen_equipment.py`: new `test_gen_art_sheet_offset_rebakes_from_fxdata`
  (baked constant + assert text, then a shifted `fxdata.h` re-bakes both blob and
  constant) -> tools suite now `Ran 73 tests`.

Verification:

1. `make gen` twice -> `make gen-check`: `fxdata_manifest: PASS (45 generated artifacts unchanged)`
2. `make build`: clean (after temporarily breaking `SHEET_OFF_FXCHIP` to 9999 the
   build fails with `error: static assertion failed: equip blob stale: re-run
   make gen`; `make gen` restores and it compiles clean). `make size` unchanged
   `flash=26998/29696 (2698 free)`.
3. `make test` -> `Total Passed: 3119` / `Total Failed: 0`
4. `make test-tools` -> `Ran 73 tests in 4.428s` / `OK`
5. `make fxtest-headless FXTEST_ONLY=test_player_art` -> `PASSED=111 FAILED=0`

