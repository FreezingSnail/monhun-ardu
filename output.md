# monhun-ardu-ikp — eqf.3 body/head split + 8 facing + two example heads

Worker report. No commit/push/`git add` performed.

## What changed

- **New authored art (tools/gen-equipment.py, the authored path → `images/equip/`)**
  - `shadow_base_16x16.png` — the existing dark bar (16x16, 1 frame).
  - `body_base_16x16.png` — 128x32, `order: facing*pose`, row 0 idle = torso+legs
    WHITE, row 1 dodge = same shapes LIGHT (matches the old fxplayer normal/dodge
    shades).
  - `head_base_16x16.png` — 128x16, 8 facings: plain helmet + eye slot.
  - `head_helm_16x16.png` — 128x16, 8 facings: wider LIGHT dome + DARK rim + eye slot.
  - `head_bandana_16x16.png` — 128x16, 8 facings: WHITE head + DARK band row + eye slot.
  - Eye slot reuses the `tools/gen-base-sheet.py` `SLIT_RECTS` table verbatim
    (kept as one module constant now): visible on the 5 toward-viewer facings
    (E/SE/S/SW/W), hidden on the 3 away facings (NW/N/NE).
  - New JSON: `data/equipment/{shadow_base,body_base,head_base,head_helm,head_bandana}.json`
    and `data/equipment/sets/default.json` (`shadow_base`/`body_base`/`head_base`).
- **Generated default draw set** — `tools/gen-equipment.py` now reads
  `data/equipment/sets/default.json`, validates slot/item, and emits
  `DEFAULT_SHADOW = PART_SHADOW_BASE`, `DEFAULT_BODY = PART_BODY_BASE`,
  `DEFAULT_HEAD = PART_HEAD_BASE` into `src/generated/equip_meta.hpp`.
  Changing the default head/body is a JSON edit + `make gen`; render never
  names a sheet.
- **Cart part view** — authored `shadow`/`body`/`head` items now emit part
  records too (alongside the gen-art overlays), so the slot loop draws them
  through the same cart path. `PART_SIZE` 17 → 19 (added `order` u8 +
  `frames` u8); `partFrame()` resolves the sheet frame from the record
  (`pose` = stored frame, `facing` = facing index, `facing*pose` =
  `row*FACINGS + facing`).
- **src/render.hpp `drawPlayer`** — computes the 8-way facing once
  (`fp::dirIndexFromDelta(p.fx, p.fy)`) then draws
  `DEFAULT_SHADOW → DEFAULT_BODY (POSE_DODGE when `p.state == PS_DODGE`) →
  DEFAULT_HEAD`, then the existing weapon overlays with unchanged shapes and
  offsets. The `player_body`/fxplayer record is no longer on the draw path (the
  record stays in gen-art for other consumers).
- Tests: new `GenEquipmentLayeredTests` (5 tests) in
  `tools/tests/test_gen_equipment.py` + `tools/tests/fixtures/gen_equipment/layered/`;
  `tst/fxdatatest/player_art_test.hpp` goldens regenerated.

### Deviations / notes

- `SHEET_OFF_*` widened `uint16_t` → `uint32_t`: authored equip sheets live
  after the tables in the FX image and exceed 64 KB (`mh_head_base = 0x0129EF`
  = 76271), so the old `uint16_t` would not compile. The part record already
  stores the sheet as u24 (`partSheet` reads it back as `uint24_t`); the
  static_assert guard was the only 16-bit holder.
- "emit into the packed/cart view … as constexpr ids": implemented as the
  default set's *part records* being in the packed `equip.bin` part view, with
  the ids emitted as `constexpr DEFAULT_*` in `equip_meta.hpp`. No extra blob
  bytes were added beyond the part records; render reads only the constexpr ids.
- `player_base` (slot `player`, combined template) is unchanged and still not a
  part; it stays in gen-art/docs for reference/tests.

## Verification (exact commands, tails, numbers)

### 1. `make gen` twice → `make gen-check`

First run (adds the 5 sheets; authored part offsets bake 0), second run
re-bakes against the new `fxdata/fxdata.h`:

```
gen-equipment: images/equip/mh_body_base_16x16.png (128x32, wrote)
gen-equipment: images/equip/mh_head_bandana_16x16.png (128x16, wrote)
gen-equipment: images/equip/mh_head_base_16x16.png (128x16, wrote)
gen-equipment: images/equip/mh_head_helm_16x16.png (128x16, wrote)
gen-equipment: images/equip/mh_shadow_base_16x16.png (16x16, wrote)
```

```
make gen-check
fxdata_manifest: fxdata/manifest.json up to date (30 images, 40 inputs, 11 outputs)
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (50 generated artifacts unchanged)
```

### 2. Host + tooling

```
make test
Total Passed: 3119
Total Failed: 0
```

```
make test-tools
Ran 79 tests in 4.594s
OK
```

(Goes 334 → 379 individual assertions across the 5 new layered tests + updates;
suite count 74 → 79.)

### 3. `make fxtest-headless` (full)

```
=== test_assets ===      asset_test PASSED=254 FAILED=0      test_assets: PASS
=== test_audio ===       test_audio PASSED=14 FAILED=0       test_audio: PASS
=== test_boot ===        test_boot PASSED=4 FAILED=0         test_boot: PASS
=== test_combat ===      combat_test PASSED=195 FAILED=0     test_combat: PASS
=== test_data ===        data_test PASSED=221 FAILED=0       test_data: PASS
=== test_hud ===         test_hud PASSED=17 FAILED=0         test_hud: PASS
=== test_menu ===        menu_test PASSED=59 FAILED=0        test_menu: PASS
=== test_parity ===      parity_test PASSED=660 FAILED=0     test_parity: PASS
=== test_perf ===        B pUs=6925 pHz=144 lHz=48 lTk=984 rMx=5968 rAv=5451 ram=408
                         perf_test PASSED=5 FAILED=0         test_perf: PASS
=== test_player_art ===  test_player_art PASSED=111 FAILED=0 test_player_art: PASS
```

`rMx=5968` (budget floor 7407). player_art = 111 checks = 37 cases × 3 planes.

Parity fixtures regen (`node tools/gen-parity-fixtures.js`) → empty diff on
`tst/fxdatatest/parity_fixtures.hpp`.

### 4. `make build` / `make size`

```
Sketch uses 27076 bytes (91%) of program storage space. Maximum is 29696 bytes.
Global variables use 2018 bytes (78%) of dynamic memory, leaving 542 bytes.

size: .text=27018 .data=58 .bss=1960
size: flash=27076/29696 (2620 free)  ram=2018/2560
size: data facts: HAS_GUARD_CHANCE:false ... HAS_SIMPLE_GUARDS:true ... (unchanged)
```

Flash delta vs baseline **26996 → 27076 = +80 B** (2620 B free). Cart:
`fxdata/fxdata.bin` = 96256 B; `fxdata/fxdata-data.bin` = 96243 B.

### 5. Art review (docs/dev-flow.md checklist)

- **Shades** (1:1 L4 triplane, 0/1/2/3 = black/dark/light/white): body idle =
  shade 3 (white), body dodge = shade 2 (light) — identical to the old fxplayer
  normal/dodge shading. Shadow = shade 1 (dark). Head skin/helmet = shade 3,
  helm dome = shade 2 + rim shade 1, bandana band = shade 1. Eye slot = shade
  0 (black body pixel; plus-mask sets the mask bit so it blackens all 3 planes —
  it is the facing read, not an erase frame).
- **Eye slot rule**: drawn on exactly the 5 toward-viewer facings (0=E, 1=SE,
  2=S, 3=SW, 4=W) with the gen-base-sheet rects; none on 5=NW, 6=N, 7=NE. Tool
  test asserts per-sheet: cells 0..4 contain black and are pairwise distinct,
  cells 5..7 contain no black.
- **Anchors**: body/head/shadow cells are 16x16 with anchor (8,8) = player
  centre; draw x/y = `cx-8, cy-8` = the old fxplayer top-left. No offset drift.
- **Hitboxes / telegraph**: untouched — weapon overlay math and the
  telegraph==hit-test window are unchanged.
- Weapon overlays are byte-identical where they cover the head/body (goldens
  2, 4, 10, 27–33, 35, 36 unchanged), confirming overlay shapes did not move.

## Golden regen (player_art)

Captured with `PRINT_GOLDENS=true` + `make fxtest-headless
FXTEST_ONLY=test_player_art`, then `PRINT_GOLDENS=false`.

Changed case indices (intentional body/head split + eye slot):
`0, 1, 3, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
24, 25, 26, 34`.

Unchanged (weapon overlay covers the changed head/body pixels; overlay shapes
byte-identical): `2, 4, 10` (sword 0.6-arc/step/spin slash) and `27..33`
(gun plate/guard/shove/reload), `35` (gun stun), `36` (gun i-frames).

## New interfaces

- `equip::DEFAULT_SHADOW`, `equip::DEFAULT_BODY`, `equip::DEFAULT_HEAD`
  (part ids from `data/equipment/sets/default.json`).
- `equip::PART_ORDER_OFF`, `equip::PART_FRAMES_OFF`; `PART_SIZE = 19`.
- `equip::SHEET_OFF_*` type is now `uint32_t`.
- Render: `partFrame(rec, pose, facing)`; `partDraw(part, pose, facing, rx, ry)`.
