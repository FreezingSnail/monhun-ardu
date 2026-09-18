# monhun-ardu-4t4 — menu name-only + HEAVY long tail

Worker report. Epic monhun-ardu-nch. No commit/push by worker.

## What changed

### SCOPE A — menu text-only
- `tools/gen-art.py`: `menu_defs()` no longer bakes the option mini-icons into
  `mh_menu_bg` / `mh_menu_wsel` / `mh_menu_msel`; names + bright frame + cursor
  are unchanged, v2 tile geometry (tile sizes, name lanes x15/x19, cursor at
  col 0, frame from x=4) is unchanged. Removed the now-dead icon helpers
  (`WEAPON_ICON_RECTS`, `mask_points`, `mini_points`, `icon_blocks`,
  `menu_wmasks`, `menu_mmasks`, `menu_source_crop`, `MENU_TARGET_SHEETS`).
  `check_menu_identity` still cross-checks every name cell against
  `fxfontw`/`fxfontg` + frame + cursor, and now asserts the old v2 icon slot is
  clear on every option.
- `tst/fxdatatest/menu_art_test.hpp`: icon hash/distinct assertions replaced with
  per-option name-ink checks (plane 0 dim, plane 2 sel bright), per-target
  sel-name/sel-frame checks, and icon-slot-clear checks on plane 0 + plane 2.
  Frame/cursor move checks unchanged. Dropped the now-unused `hashRegion`.
- `tst/fxdatatest/asset_test.hpp`: menu comment updated; header checks kept.

### SCOPE B — HEAVY long tail
- `data/creatures/heavy.json`: new `zones.appendage` — box `{ox:-24, oy:0,
  w:24, h:16}`, `dmgMul 150`, `hp 60`, `bodyShare 40`, `breakTypes ["SLASH"]`,
  `hurtOn true`, `staggerOnHit 30`, `broken {dmgMul 200, hurtOn false,
  cue "part_break", disableAttacks ["sweep"]}` (ravager tail scale).
- `tools/gen-art.py`: new `fxtail_heavy` sheet, 24x16 frames, 4 frames in
  `combatPartArtFrame` order (east intact / east broken / west intact / west
  broken), tip/underside/root in 4 shades, west = exact horizontal mirror.
- `src/render.hpp` `drawMonster()`: HEAVY overlay draw at the appendage-zone
  world anchor — `combatFaceOffset(m.fx, m.fy, zone.box)` on the RAM zone cache
  (same transform `combatZoneContains` uses, so art and hitbox cannot drift),
  frame via `combatPartArtFrame(m.fx < 0, broken)`. Drawn after the body sprite,
  skipped when dead. No new cart reads beyond the normal `sprDraw`.
- Tests:
  - `tst/art_dims_test.hpp`: new “heavy tail overlay is a 24x16 mirror sheet”
    suite (24 checks): sheet dims/frames, tip/root/underside pixels, broken
    stubs, exact west mirror (data+mask, all shades/pages).
  - `tst/combat_test.hpp` (host): new “heavy appendage zone (heavy.json) + tail
    art linkage” (17 checks): box/hp/mul/share/break/stagger/broken/unlockMask,
    `art_dims::tail_heavy_frame_{w,h} == zone box`, `creatureLoad` seeds the
    HEAVY appendage cache. Zone count 2 -> 3.
  - `tst/fxdatatest/asset_test.hpp`: `blobHeader(fxtail_heavy, 24, 16)`.
  - `tst/fxdatatest/combat_test.hpp`: HEAVY appendage zone spot checks (record +
    box + unlockMask), heavy `appendZone` now `ZONE_HEAVY_APPENDAGE`.
  - NEW `tst/fxdatatest/monster_art_test.hpp` + `test_monster_art.ino` (18
    checks): renders the real `drawMonster()` per plane and pins the east tail
    band left of the body (tip x=16 lit, x=15 clear), the plane-2 white cap/root
    highlight, the west mirror (tip right of body, east band empty), broken
    stubs, and LUNGE with no appendage -> no overlay.

## Deviations / notes
- Tail sheet is **24x16**, not the issue's example 24x10: `SpritesU`'s plus-mask
  frame stride is `(h >> 3)` pages, so a 10-tall 4-shade sheet mis-draws (only
  one of two pages). Height 16 is the issue's “height multiple of 8” constraint;
  the requested symbol name `fxtail_heavy` and the 24 px length are used.
- Zone `oy` is 0 on purpose: the zone origin rotates through `combatFacePoint`
  and a non-zero `oy` flips vertically at the 180° west facing. The sprite frame
  carries the vertical placement instead.
- The legacy ravager `fxtail` (18x10) is still not drawn: same page-stride issue,
  and the ravager is outside this bead's scope (RAVAGER keeps the legacy body
  sheet). Only `MON_HEAVY` overlays a tail.
- Target/body box untouched: `heavy.json` stats w40 h28 unchanged, so
  `syncMonsterTarget` rect and parity scenes are untouched. The zone addition
  intentionally lets tail hits route to the appendage in the HEAVY hunt; no
  parity scene uses HEAVY.
- README menu paragraph + heavy-zone mention updated (name-only, tail overlay).

## Verification (exact)

### 1. gen twice + gen-check
```
$ make gen   (first pass: fxdata/tables/equip.bin + src/generated/equip_meta.hpp
             changed — asset-address shift propagated into the equip part sheet
             offsets, the expected two-pass equip staleness flow)
$ make gen   (second pass: unchanged)
TWO GEN OK
$ make gen-check
fxdata_manifest: PASS (64 generated artifacts unchanged)
```
Two-pass equip flow returned to a fixed point; `fxdata/fxdata.h == src/fxdata.h`
asserted by gen-check.

### 2. host + tooling
```
$ make test
Total Passed: 3586
Total Failed: 0          (baseline 3521; +65 new checks)
  heavy tail overlay is a 24x16 mirror sheet   Passed: 24  Failed: 0
  heavy appendage zone (heavy.json) + tail art linkage  Passed: 17  Failed: 0
$ make test-tools
Ran 137 tests in 6.908s
OK
```

### 3. full device gate
```
$ make fxtest-headless
test_assets       PASSED=278 FAILED=0   PASS
test_audio        PASSED=14  FAILED=0   PASS
test_boot         PASSED=4   FAILED=0   PASS
test_combat       PASSED=193 FAILED=0   PASS
test_data         PASSED=221 FAILED=0   PASS
test_hub          PASSED=57  FAILED=0   PASS
test_hud          PASSED=17  FAILED=0   PASS
test_menu_art     PASSED=60  FAILED=0   PASS
test_menu         PASSED=59  FAILED=0   PASS
test_monster_art  PASSED=18  FAILED=0   PASS
test_parity       PASSED=660 FAILED=0   PASS
B pUs=6375 pHz=156 lHz=52 lTk=528 rMx=4968 rAv=4755 ram=601
test_perf         PASSED=5   FAILED=0   PASS
test_player_art   PASSED=111 FAILED=0   PASS
test_quests       PASSED=50  FAILED=0   PASS
test_screens      PASSED=78  FAILED=0   PASS
test_smith        PASSED=66  FAILED=0   PASS
```
Perf vs baseline `rMx=4968 rAv=4757 pUs=6375`: rMx/pUs identical, rAv 4757 ->
4755 (-2, the extra appendage branch; within bench noise, no regression).

### 4. build + size
```
$ make size
Sketch uses 26680 bytes (89%) of program storage space.
Global variables use 1863 bytes (72%) of dynamic memory.
size: .text=26614 .data=66 .bss=1797
size: flash=26680/29696 (3016 free)  ram=1863/2560
size: data facts: HAS_GUARD_CHANCE:false ... HAS_ZONES:true  (no fact flipped)
```
- flash 26434 -> 26680, **delta +246 B**; headroom 3262 -> 3016.
- cart `fxdata/fxdata.bin` 149760 -> **151040 B, delta +1280 B** (new 24x16x4
  tail sheet + menu shrink + equipment-address shift). No `HAS_*` fact flipped.

### 5. parity fixtures byte-identical
```
$ node tools/gen-parity-fixtures.js
wrote tst/fxdatatest/parity_fixtures.hpp
scenes=20 ticks=1269 snapshots=32 cpFields=20
$ git diff --stat tst/fxdatatest/parity_fixtures.hpp
(empty)
```

### 6. ASCII evidence (`make art-dump`, gen-art `--dump`)

menu_wsel tile 0 (SWD) — cursor col0, clear icon slot x6..13, white frame, name:
```
  ....WWWWWWWWWWWWWWWWWWWWWWWWWWWW
  ....W..........................W
  W...W...........WW.W.W.WW......W
  WW..W..........W...W.W.W.W.....W
  WWW.W...........W..WWW.W.W.....W
  WW..W............W.WWW.W.W.....W
  W...W..........WW..W.W.WW......W
  ....WWWWWWWWWWWWWWWWWWWWWWWWWWWW
```

menu_msel tile 0 (CHICKEN) — clear icon slot x6..17, name at x19:
```
  ....WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW
  ....W..........................................................W
  W...W...............WW.W.W.WWW..WW.W.W.WWW.W.W.................W
  WW..W..............W...W.W..W..W...W.W.W...WWW.................W
  WWW.W..............W...WWW..W..W...WW..WW..W.W.................W
  WW..W..............W...W.W..W..W...W.W.W...W.W.................W
  W...W...............WW.W.W.WWW..WW.W.W.WWW.W.W.................W
  ....WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW
```

fxtail_heavy 24x16, 4 frames laid left-to-right (columns 0-23 east intact,
24-47 east broken, 48-71 west intact, 72-95 west broken):
```
  ................................................................................................
  ................................................................................................
  ................................................................................................
  ................................................................................................
  ..............lllllllWWW....................llllWWWlllllll..............llll....................
  ......lllllllllllllllWWW.................gggllllWWWlllllllllllllll......llllggg.................
  ......lllllllllllllllWWW.................gggggggWWWlllllllllllllll......ggggggg.................
  llllllllllllllllllllllll.................gggggggllllllllllllllllllllllllggggggg.................
  WWWlllllllllllllllllllll.................ggggggglllllllllllllllllllllWWWggggggg.................
  WWWlllllllllllllllllllll.................ggggggglllllllllllllllllllllWWWggggggg.................
  WWWlllllllllllllllllllll.................ggggggglllllllllllllllllllllWWWggggggg.................
  llllllllllllllllllllllll.................gggggggllllllllllllllllllllllllggggggg.................
  ......lgggggggllllllllll.................gggggggllllllllllgggggggl......ggggggg.................
  ......lggggggglggggggggg.................gggggggggggggggglgggggggl......ggggggg.................
  ..............lggggggggg........................gggggggggl......................................
  ................................................................................................
```
(`l` light, `W` white, `g` dark, `.` clear.)

## Files
```
 M data/creatures/heavy.json
 M fxdata/blocks/Sprites.txt
 M fxdata/fxdata-data.bin
 M fxdata/fxdata.bin
 M fxdata/fxdata.h
 M fxdata/manifest.json
 M fxdata/menu/Sprites.txt
 M fxdata/tables/combat.bin
 M fxdata/tables/equip.bin
 M images/menu/mh_menu_bg_128x64.png
 M images/menu/mh_menu_msel_64x8.png
 M images/menu/mh_menu_wsel_32x8.png
 M src/fxdata.h
 M src/generated/art_dims.hpp
 M src/generated/combat_data.hpp
 M src/generated/combat_expect.hpp
 M src/generated/combat_meta.hpp
 M src/generated/equip_meta.hpp
 M src/render.hpp
 M tools/gen-art.py
 M tst/art_dims_test.hpp
 M tst/combat_test.hpp
 M tst/fxdatatest/asset_test.hpp
 M tst/fxdatatest/combat_test.hpp
 M tst/fxdatatest/menu_art_test.hpp
 M README.md
?? images/blocks/fxtail_heavy_24x16.png
?? tst/fxdatatest/monster_art_test.hpp
?? tst/fxdatatest/test_monster_art.ino
```
