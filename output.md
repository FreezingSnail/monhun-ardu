# monhun-ardu-2u8 — menu v2: icon + name options, selection frame, clearer layout

Epic: monhun-ardu-nch. No commit/push/git-add per worker protocol.

## What changed

- `tools/gen-art.py`
  - Menu bake rewritten: `mh_menu_bg` (128x64: title, `WEAPON`/`MONSTER`
    labels, dim option icons + names, `A HUNT` footer), `mh_menu_wsel`
    (3 x 32x8 weapon tiles), `mh_menu_msel` (5 x 64x8 target tiles). The old
    `mh_menu_sel` (8 x 28x16 underlined text) is gone.
  - Each sel tile = white icon + name + bright 1 px open frame + 3x5 cursor
    arrow (local x0..2), so the selected option composites bright in one blit
    per plane over its dim bg copy.
  - Option icons: 8x6 sword/flail/gun rect icons (`WEAPON_ICON_RECTS`) and
    12x6 monster/pole icons produced by `mini_points()`, a deterministic
    reduction of the shipped sheet's east idle frame (frame 0). The reduction
    drops BLACK (shadow row, eyes, hooves) and the final source row, keeping
    the comb/horns/tail readable.
  - `check_menu_identity()` extended: every text cell still cross-checked
    against `fxfontw`/`fxfontg`; every icon re-derived from the shipped
    beast/pole sheets and checked LIGHT in the bg / WHITE in the sel tiles;
    frame outline + cursor asserted white. `sheet_filename`/`sheet_kind`
    updated; menu dumps now print whole (320 px) for review.
- `src/menu.hpp` — `drawMenu()` draws the three sheets: bg, weapon tile at
  x=24+34i/y=11, target tile at x=(t&1)*64 / y=29+9*(t>>1). Plain arithmetic
  (no non-PROGMEM lookup arrays) so RAM stays flat.
- `tst/fxdatatest/asset_test.hpp` — header checks for the three new sheets
  (+6 asserts).
- `tst/fxdatatest/menu_art_test.hpp` + `test_menu_art.ino` — new device pixel
  oracle (hud_test style): plane 0 pins the frame top row / cursor column on
  the picked tiles and clear on the others (selection A SWD+CHICKEN,
  selection B GUN+POLE); plane 2 proves white sel sets while light bg clears
  (selected names bright, all unselected dim names erased); the 12x6 icon of
  every target hashes pairwise-distinct. 42 asserts.
- `README.md`, `docs/equipment-framework.md` — menu v2 sheets/layout; stale
  `mh_menu_sel` doc reference re-pointed.

## Option counts / semantics (unchanged)

`MenuState` still 3 weapons x 5 targets, `MENU_POLE_TARGET = 4`, nav/debounce
and `menuMonsterKind`/`menuMode` untouched. Host `menu_test` (state) is byte-
for-byte unchanged at 59/0. Only the displayed target labels changed
(LUNGE/SWEEP/HEAVY -> the beast art names CHICKEN/BULL/LONGTAIL).

## Verification (exact commands + tails)

1. `make gen` x2 then `make gen-check` — PASS
   - Two-pass equip staleness observed: the menu section shrank 256 B
     (`fxdata.bin` 150016 -> 149760), shifting the equip sheet offsets, so the
     first `make gen` re-baked `fxdata/tables/equip.bin` +
     `src/generated/equip_meta.hpp` from the pre-shift header and the second
     pass converged. `src/fxdata.h` symbols:
     `mh_menu_wsel = 0x012A3D`, `mh_menu_msel = 0x012C7F`, `mh_menu_bg = 0x013401`.
   - `fxdata_manifest: PASS (63 generated artifacts unchanged)`
2. `make test` — `Total Passed: 3521  Total Failed: 0` (baseline 3521/0)
   `make test-tools` — `Ran 137 tests in 7.348s  OK`
3. `make fxtest-headless` — all suites PASS
   ```
   test_assets PASSED=276 FAILED=0      test_audio PASSED=14 FAILED=0
   test_boot PASSED=4 FAILED=0          test_combat PASSED=184 FAILED=0
   test_data PASSED=221 FAILED=0        test_hub PASSED=57 FAILED=0
   test_hud PASSED=17 FAILED=0          test_menu_art PASSED=42 FAILED=0
   test_menu PASSED=59 FAILED=0         test_parity PASSED=660 FAILED=0
   test_perf PASSED=5 FAILED=0          test_player_art PASSED=111 FAILED=0
   test_quests PASSED=50 FAILED=0       test_screens PASSED=78 FAILED=0
   test_smith PASSED=66 FAILED=0
   ```
   perf tail: `B pUs=6375 pHz=156 lHz=52 lTk=528 rMx=4968 rAv=4757 ram=602`
   (baseline `rMx=4968 rAv=4757 pUs=6375` — identical; menu is not in the
   renderScene bench).
4. `make build` + `make size`
   - `Sketch uses 26434 bytes (89%)` / `Global variables use 1863 bytes`
   - `size: flash=26434/29696 (3262 free)  ram=1863/2560`
   - vs baseline 26436/3260: flash delta **-2 B** (budget was +200 B), RAM
     delta 0.
   - cart: `fxdata/fxdata.bin` 150016 -> 149760 B, **-256 B**.
5. Parity — `node tools/gen-parity-fixtures.js` -> `scenes=20 ticks=1269
   snapshots=32 cpFields=20`; `git status --short tst/fxdatatest/
   parity_fixtures.hpp` empty -> byte-identical.

## ASCII dump (new menu sheets; `make art-dump`)

`menu_wsel` — 3 frames SWD / FLS / GUN (32x8). Cursor = `W` col 0, frame from
col 4, icon cols 6..13 (`WW`/sword, ball, pistol), name at col 15:
```
menu_wsel  frame 32x8  frames 3  size 96x8
  ....WWWWWWWWWWWWWWWWWWWWWWWWWWWW....WWWWWWWWWWWWWWWWWWWWWWWWWWWW....WWWWWWWWWWWWWWWWWWWWWWWWWWWW
  ....W....WW....................W....W......WWW.................W....W.......WW.................W
  W...W....WW.....WW.W.W.WW......WW...W......WWW.WWW.W....WW.....WW...W.WWWWWW....WW.W.W.W.W.....W
  WW..W....WW....W...W.W.W.W.....WWW..W.....WWWW.W...W...W.......WWW..W.WWWW.....W...W.W.WWW.....W
  WWW.W..WWWWWW...W..WWW.W.W.....WWWW.W....W.....WW..W....W......WWWW.W.WWWW.....W.W.W.W.W.W.....W
  WW..W....WW......W.WWW.W.W.....WWW..W..WW......W...W.....W.....WWW..W..WW......W.W.W.W.W.W.....W
  W...W....WW....WW..W.W.WW......WW...W..WW......W...WWW.WW......WW...W..WW.......WW..WW.W.W.....W
  ....WWWWWWWWWWWWWWWWWWWWWWWWWWWW....WWWWWWWWWWWWWWWWWWWWWWWWWWWW....WWWWWWWWWWWWWWWWWWWWWWWWWWWW
```
`menu_msel` — 5 frames CHICKEN / BULL / LONGTAIL / RAVAGER / POLE (64x8, one
64 px cell each). The 12x6 icon (cols 6..17) is the reduced beast sheet: chicken
top-right comb rows 2..3, bull's twin horns rows 2..3, longtail ridge row 2 and
tail-left bulk, ravager blob, pole verticals:
```
menu_msel  frame 64x8  frames 5  size 320x8
  ....WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW....WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW....WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW....WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW....WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW
  ....W........WWWW..............................................W....W..........................................................W....W..........................................................W....W...WWWWWWWW...............................................W....W.WWWWWWWWWWWW.............................................W
  W...W.WWW....WWWW...WW.W.W.WWW..WW.W.W.WWW.W.W.................WW...W...WWWWW.WWWW.WW..W.W.W...W...............................WW...W.WWWWWWWWWWWW.W....W..W.W..WW.WWW..W..WWW.W...............WW...W..WWWWWWWWWWW.WW...W..W.W..W...WW.WWW.WW..................WW...W.WWWWWWWWWWWW.WW...W..W...WWW.............................W
  WW..W.WWWWWWWWWWW..W...W.W..W..W...W.W.W...WWW.................WWW..W.WWWWWWWWWWWW.W.W.W.W.W...W...............................WWW..W.WWWWWWWWWWWW.W...W.W.WWW.W....W..W.W..W..W...............WWW..W..WWWWWWWWWWW.W.W.W.W.W.W.W.W.W...W...W.W.................WWW..W.WWWWWWWWWWWW.W.W.W.W.W...W...............................W
  WWW.W.WWWWWWWWW....W...W.W..W..W...WW..WW..W.W.................WWWW.W.WWWWWWWWWWWW.WW..W.W.W...W...............................WWWW.W.WWWWWWWWWWWW.W...W.W.W.W.W.W..W..WWW..W..W...............WWWW.W..WWWWWWWWWWW.WW..WWW.W.W.WWW.W.W.WW..WW..................WWWW.W..WWWWWWWWWW..WW..W.W.W...WW..............................W
  WW..W.WWWWWWWWW....W...W.W..W..W...W.W.W...W.W.................WWW..W.WWWWWWWWWWWW.W.W.W.W.W...W...............................WWW..W.WWWWWWWWW....W...W.W.W.W.W.W..W..W.W..W..W...............WWW..W..WWWWWWWWWWW.W.W.W.W.W.W.W.W.W.W.W...W.W.................WWW..W..WWWWWWWWWW..W...W.W.W...W...............................W
  W...W...W....WW.....WW.W.W.WWW..WW.W.W.WWW.W.W.................WW...W..WW.W..WWW...WW...WW.WWW.WWW.............................WW...W....WWWWWWWW..WWW..W..W.W..WW..W..W.W.WWW.WWW.............WW...W..............W.W.W.W..W..W.W..WW.WWW.W.W.................WW...W..WWWWWWWWWW..W....W..WWW.WWW.............................W
  ....WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW....WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW....WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW....WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW....WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW
```
`menu_bg` option region (dim light-gray options; `l` = light, `W` = white).
Weapon row y13 (`l` label + 3 icon/name cells) and the 2-column monster grid
y29/38/47, footer y56:
```
y11 ................................................................................................................................
y12 .................................ll..................................lll................................ll......................
y13 l.l.lll..l..ll...l..l.l..........ll.....ll.l.l.ll....................lll.lll.l....ll..............llllll....ll.l.l.l.l..........
y14 l.l.l...l.l.l.l.l.l.lll..........ll....l...l.l.l.l..................llll.l...l...l................llll.....l...l.l.lll..........
y15 lll.ll..lll.ll..l.l.l.l........llllll...l..lll.l.l.................l.....ll..l....l...............llll.....l.l.l.l.l.l..........
y16 lll.l...l.l.l...l.l.l.l..........ll......l.lll.l.l...............ll......l...l.....l...............ll......l.l.l.l.l.l..........
y17 l.l.lll.l.l.l....l..l.l..........ll....ll..l.l.ll................ll......l...lll.ll................ll.......ll..ll.l.l..........
y22 l.l..l..l.l..ll.lll.lll.ll......................................................................................................
y23 lll.l.l.lll.l....l..l...l.l.....................................................................................................
y24 lll.l.l.l.l..l...l..ll..ll......................................................................................................
y25 l.l.l.l.l.l...l..l..l...l.l.....................................................................................................
y26 l.l..l..l.l.ll...l..lll.l.l.....................................................................................................
y29 .............llll...............................................................................................................
y30 ......lll....llll...ll.l.l.lll..ll.l.l.lll.l.l..........................lllll.llll.ll..l.l.l...l................................
y31 ......lllllllllll..l...l.l..l..l...l.l.l...lll........................llllllllllll.l.l.l.l.l...l................................
y32 ......lllllllll....l...lll..l..l...ll..ll..l.l........................llllllllllll.ll..l.l.l...l................................
y33 ......lllllllll....l...l.l..l..l...l.l.l...l.l........................llllllllllll.l.l.l.l.l...l................................
y34 ........l....ll.....ll.l.l.lll..ll.l.l.lll.l.l.........................ll.l..lll...ll...ll.lll.lll..............................
y38 ........................................................................llllllll................................................
y39 ......llllllllllll.l....l..l.l..ll.lll..l..lll.l.......................lllllllllll.ll...l..l.l..l...ll.lll.ll...................
y40 ......llllllllllll.l...l.l.lll.l....l..l.l..l..l.......................lllllllllll.l.l.l.l.l.l.l.l.l...l...l.l..................
y41 ......llllllllllll.l...l.l.l.l.l.l..l..lll..l..l.......................lllllllllll.ll..lll.l.l.lll.l.l.ll..ll...................
y42 ......lllllllll....l...l.l.l.l.l.l..l..l.l..l..l.......................lllllllllll.l.l.l.l.l.l.l.l.l.l.l...l.l..................
y43 .........llllllll..lll..l..l.l..ll..l..l.l.lll.lll.................................l.l.l.l..l..l.l..ll.lll.l.l..................
y47 ......llllllllllll..............................................................................................................
y48 ......llllllllllll.ll...l..l...lll..............................................................................................
y49 ......llllllllllll.l.l.l.l.l...l................................................................................................
y50 .......llllllllll..ll..l..l.l...ll..............................................................................................
y51 .......llllllllll..l...l.l.l...l................................................................................................
y52 .......llllllllll..l....l..lll.lll..............................................................................................
y56 .....................................................W......W.W.W.W.W.W.WWW.....................................................
y57 ....................................................W.W.....W.W.W.W.WWW..W......................................................
y58 ....................................................WWW.....WWW.W.W.W.W..W......................................................
y59 ....................................................W.W.....W.W.W.W.W.W..W......................................................
y60 ....................................................W.W.....W.W..WW.W.W..W......................................................
```
(rows y18..21, y27..28, y35..37, y44..46, y53..55, y61..63 blank are omitted)

## Deviations / notes

- Layout: replaced the 3+2 underlined text rows (underlines gone) with a
  WEAPON row at y13 and a 2-column monster grid at y29/38/47 + footer y56, so
  all five targets show full names (LONGTAIL is the 32 px max at x=19).
  `menu_state` counts/semantics unchanged, so all state/nav tests are
  untouched.
- Target labels LUNGE/SWEEP/HEAVY -> CHICKEN/BULL/LONGTAIL (the menu names the
  beast art); the picked kind still maps through `menuMonsterKind` (0..3) and
  RAVAGER/POLE unchanged.
- Footer changed `A HUB` -> `A HUNT`, matching the 5r1 demo flow (A launches
  the hunt from the menu).
- Old `images/menu/mh_menu_sel_28x16.png` deleted; two new generated PNGs
  (`mh_menu_wsel_32x8.png`, `mh_menu_msel_64x8.png`) are untracked in the
  working tree (orchestrator stages the generated set together).
- Host assert count unchanged (3521); device `test_assets` 270 -> 276 (three
  new blob-header checks), `test_menu` 59 unchanged, new `test_menu_art` 42.
- Flash -2 B / RAM +0 / cart -256 B. No `PROGMEM` arrays added (arithmetic
  instead) to keep the RAM line flat.
- Untracked/none beyond the intended new files; no commit made.
