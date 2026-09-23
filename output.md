# monhun-ardu-5co.9 — ui.5.2 chrome: HUB quest column + skill strip, page indicator, denied cue

Status: **DONE.** All four chrome pieces landed, the full gate is green, and
free flash is **134 B** (target >= ~50 B). Net +612 B from HEAD 4c5ec9f
(28950 -> 29562), including a -128 B dead-code trim adopted to make it fit.

```
baseline (HEAD 4c5ec9f):
size: .text=28918 .data=32 .bss=1766
size: flash=28950/29696 (746 free)  ram=1798/2560

after:
size: .text=29530 .data=32 .bss=1766
size: flash=29562/29696 (134 free)  ram=1798/2560
```

| | flash |
|---|---|
| HEAD 4c5ec9f (baseline) | 28950 (746 free) |
| this wave | **29562 (134 free)** |
| delta | **+612 B** |

RAM is unchanged (1798/2560); no new Game/ScreenState fields.

## Measured chrome deltas (whole-image `make size`, isolated per `-DMH_UI52_*`)

Each piece was built alone against the gated baseline (28954 = baseline + the
always-on `armorApplyToGame` boot call + the new includes).

| Piece | flash | notes |
|---|---|---|
| HUB HUNT right column | **+248** | `questReadDef` + `READY` string + `p/n` digit pair |
| HUB bottom strip | **+322** | weapon marker +164 (incl. the tier glyph), active skills +146 |
| list page indicator | **+114** | loop-based page walk (no u8 divide) + `n/m` |
| blocked-A denied cue | **+32** | `cardDenied` + two `audioPlay(CUE_HURT)` sites (reused tone) |
| dead label/title tail trim | **-128** | removed the never-run per-char fallback loops |
| **net** | **+612** | 134 free after |

The ui.5 spike estimated +748 for the same four pieces; this landed at +740
gross by (a) drawing the tree tier as a single glyph (`SWD2`) instead of the
`" T"` + `drawNumber` form (-82), (b) a 6-step page walk instead of two u8
divisions (-32), and (c) the flat PROGMEM abbr tables + one shared string loop.

## What changed

### 1. HUB HUNT right column (`src/screens.hpp`)

`drawHubQuestColumn()` replaces the row-0 packed cost on the hub (the hub cost
is always 0): `-` with no active quest, `READY` once `save.progress >= def.need`,
else the right-aligned `p/n` pair. `need` comes from the cart `QuestDef`
(`questReadDef`), `progress` from the save. `drawScreen` branches on
`s.screen == SCREEN_HUB && i == 0` inside the row loop.

### 2. HUB bottom strip (`src/screens.hpp`)

`drawHubStrip()` runs on the free y=56 line (below the four hub rows, no row
overlap): the equipped weapon marker (class abbreviation from the generated tree
block starts + the tree tier as a digit, `SWD1`) followed by every **active**
armor skill's point total (`ATK12`, `armor::SKILL_*` order, tier != 0 only; no
all-skills screen). `Game::armor` is the source; `drawScreen` gained a
`const Game &` parameter and the sketch arms `g.armor` at boot
(`setup()` -> `armorApplyToGame`), so the strip is correct on first frame.

### 3. Page indicator (`src/screens.hpp`)

For any list with `rowCount > 6`, `n/m` is drawn after the title (gray). The
6-row grid fills y=11..63, so the title line is the only non-overlapping lane;
the header zenny owns the far right, the indicator stays left of it. Pages are
counted with a 6-step walk (AVR has no divide).

### 4. Denied cue (`monhun-ardu.ino`, `src/card_state.hpp`)

`cardDenied()` (pure) is true when the cached card hint is not an actionable
verb (`HINT_NONE` / `NEED PARTS` / `NEED ZENNY`). The sketch plays the existing
low `CUE_HURT` tone on the A edge when a card A is blocked, and on a gated list
row (`!screenCondOk`). No new cue row (reuse, per the bead).

### 5. Dead label/title tail trim (`src/screens.hpp`)

`tools/gen-screens.py` caps titles and row labels at 16 chars
(`TITLE_MAX`/`LABEL_MAX`) == `SCREEN_TEXT_BUF`, so the per-char tail fallback
loops in `drawScreen` could never execute. Removed (measured -128 B). A corrupt
cart with a longer string is still truncated safely by `screenReadText` (bounded
read), never over-read.

### 6. Docs

`README.md` hub/GEAR sections and `docs/ui-design.md` (Status, global chrome,
HUB/QUESTS/GEAR/FORGE examples, phasing/bead list) now describe the shipped
chrome.

## Tests (permanent, co-located, native frameworks)

- **Host** `tst/card_state_test.hpp`: new `cardDenied` suite — craftable -> not
  denied, NEED ZENNY -> denied, NEED PARTS -> denied, crafted+equipped
  (A UNEQUIP) -> not denied, silent (already-taken quest) -> denied.
- **Device** `tst/fxdatatest/screens_test.hpp`: pixel assertions for the HUNT
  column (`p/n` at x 112..123 y 11, READY at 104..123, `-` at 120..123 with the
  digit span empty), the bottom strip (`SWD1` at x 2..17 y 56, `ATK12` at
  x 26..49, inert skills leave the span empty), and the page indicator
  (`1/2` and `2/2` at x 30..41 on the QUESTS title line; HUB has none).
- `tst/fxdatatest/hub_test.hpp`: `drawScreen` call updated for the new Game arg.

No tests removed or weakened.

## Gate tails

`make gen-check`:
```
fxdata_manifest: PASS (148 generated artifacts unchanged)
```

`make test`:
```
Total Passed: 6316
Total Failed: 0
```

`make test-tools`:
```
Ran 344 tests in 19.4s
OK
```

`make fxtest-headless` (full, 18/18; log `build/5co9-fxtest.log`):
```
asset_test PASSED=264  test_audio PASSED=9    test_boot PASSED=4
test_cards PASSED=85   combat_test PASSED=237 data_test PASSED=348
test_forge PASSED=58   test_hub PASSED=81     test_hud PASSED=29
test_items PASSED=35   test_monster_art PASSED=127  test_perf PASSED=5
test_player_art PASSED=120  test_quests PASSED=87  test_screens PASSED=142
test_smith PASSED=51   test_tell PASSED=18     zones_test PASSED=82
```

`make size`:
```
size: .text=29530 .data=32 .bss=1766
size: flash=29562/29696 (134 free)  ram=1798/2560
```

`make mini` also builds clean at 29562 / ram 1798.

## Files

```
 M src/screens.hpp                  HUNT column + strip + page indicator, drawScreen(Game)
 M src/card_state.hpp               cardDenied (pure blocked-A predicate)
 M monhun-ardu.ino                  drawScreen(g), denied cue, boot armor arm
 M tst/card_state_test.hpp          cardDenied suite
 M tst/fxdatatest/screens_test.hpp  chrome pixel assertions
 M tst/fxdatatest/hub_test.hpp      drawScreen signature
 M README.md docs/ui-design.md      chrome as shipped
```

No commit/push (orchestrator commits between bead waves).

## Blockers

None. The chrome fits with 134 B free (target >= ~50 B). The tier is drawn as a
single digit (`SWD1`) rather than the design mock's `SWD T2A` to keep the
marker affordable; the tree is linear so no branch suffix is needed. If a later
wave wants the full `SWD T2A` label, a generated per-node marker table would be
the clean path (a cart/generator change, not an MCU-flash one).
