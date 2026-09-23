# monhun-ardu-5co.3 — ui.3 cards: prebaked detail pipeline + nav + hint (armor/quests)

Status: DONE. HEAD a226b44 + working tree (no commit/push, per orchestrator
flow). The cancelled run's partial state was kept and completed: data `desc`/
`abbr` additions, `tools/gen-cards.py` (extended), card art, `mhCards` blob,
`src/card_state.hpp`, `src/cards.hpp`, `src/render.hpp cardBlit`, the `.ino`
glue. Added: host/tooling/device suites, the contact sheet, the dense-quest-id
guard, and a flash trim pass that gets the shipping image back under the cap
(the partial state built at 29906/29696 — 210 B over).

## Size

Baseline (HEAD a226b44, ui.2):
```
size: flash=28646/29696 (1050 free)  ram=1715/2560
```
After:
```
size: flash=29680/29696 (16 free)  ram=1762/2560
```
**Delta: +1034 flash, +47 RAM** (the RAM is exactly `s_detail` 9 B +
`s_detailRow` 11 B + `s_card` 27 B). Card code alone, measured with the
`MH_CARD_OFF` gate (same data tree):
```
size: flash=28650/29696  (no card code; +4 vs HEAD is data-offset code noise)
```
→ the card engine is **+1030 B**. The ui.1 spike's +284 covered only the
blit/nav/dummy-string prototype (no per-item meta reader, no overlays, dummy
hint text); the bead's real feature set costs more. The partial state measured
29906; this bead's trim pass removed 226 B:

| Trim | Saved |
|---|---|
| `CardItem` mirrors the packed record byte-for-byte: one bulk `mhFxReadBytes`, no field-decode loop, page offsets read from RAM (was `cardsReadU24` per plane) | ~108 |
| drop the redundant overlay-clear loop (`drawCard` bounds by `OVERLAY_MAX`) | 48 |
| drop `MH_NOINLINE` on `cardHint`/`drawCardHint` (LTO specializes better) | ~32 |
| `inputEdges` `always_inline` (the card + screen machines shared one outlined copy) | 12 |
| drop redundant internal guards (`page < PAGE_MAX`, `hint & 7`, `overlayCount` clamp, 12-char hint loop) | ~26 |

`drawCard` keeps `MH_NOINLINE` (removing it costs +124); `cardLoad` is neutral.

## Files

Production:
- `src/card_state.hpp` — host-testable card machine: `CardItem` (record-shaped,
  static-asserted on device), `DetailState` (kind/index/page/pageCount/pageMask),
  mask helpers, `cardOpen`/`cardClose`/`cardSetMask`, `cardNav` (skips absent
  pages), `detailStep` (LEFT/RIGHT edges + A/B), `cardRowOpens`/`cardRowKind`/
  `cardRowIndex` (armor piece == global index; quest = `QUEST_BASE + id`),
  `cardArmorMask` (crafted drops PARTS), `cardHint`.
- `src/cards.hpp` — device cart glue: `cardReadItem` (one bulk read into the
  byte-identical cache), `cardPageOffset`, `cardLoad` (open/refresh + crafted
  trim), `drawCardHint` (flash strings), `drawCard` (blit + meta overlays +
  hint).
- `src/render.hpp` — `cardBlit(img)`: fixed 128x64 per-plane bulk blit
  (`FX::readDataBytes`, 1024 B layer at `img + plane*1024`), between plane
  blits. `roomAsmCopy` moved up (shared with the room path, unchanged).
- `monhun-ardu.ino` — card tick ahead of the screen tick (B closes, A applies
  `screenApplyAction` with the stored row + EEPROM store, then refreshes the
  card and the GEAR readout), list A opens the card for armor/quest rows, and
  `drawCard` replaces the scene. `MH_CARD_OFF` gates the whole card path for
  whole-image measurement (repo convention, like `MH_ROOM_IMAGE`).
- `src/core/input.hpp` — `inputEdges` `always_inline` (one rule, two call
  sites; an outlined shared copy costs more flash).

Data + generated:
- `data/armor.json` (5x `desc` 2 lines), `data/quests/*.json` (4x `desc`),
  `data/skills.json` (5x `abbr`) — validated by gen-armor/gen-quests, never
  packed into their blobs.
- `tools/gen-cards.py` — bakes armor pages DESC/PARTS/STATS/SKILL and quest
  pages GOAL/PROG/REWARD into `images/cards/*.png` (authored art, regenerated
  every run), `fxdata/cards/Sprites.txt` (3x 1bpp page-major layers, the
  room-image family), `fxdata/tables/cards.bin` (8 B header + 27 B/item: kind,
  mask, 4x u24 page addresses, 2x6 B overlay slots) and
  `src/generated/card_meta.hpp` (ABI, `QUEST_BASE`, item indices/offsets).
  A page with no data is not generated (zenny-only piece: no PARTS; no-reward
  quest: no REWARD). Non-dense quest ids are a hard error (the runtime card
  index is the quest id). `--dump` lists pages/masks/overlays; `--sheet PATH`
  renders the review contact sheet (never committed).
- `tools/gen.sh` + `tools/fxdata_manifest.py` + `fxdata/fxdata.txt` — declare
  `raw_t mhCards` and include `cards/Sprites.txt`; manifest tracks the new
  images/declarations/outputs.
- Generated set: `fxdata/fxdata.bin`, `fxdata/fxdata-data.bin`,
  `fxdata/fxdata.h`, `src/fxdata.h`, `fxdata/manifest.json`,
  `fxdata/tables/equip.bin`, `src/generated/equip_meta.hpp`,
  `src/generated/zone_meta.hpp` (later FX symbols shifted +96 KB of card art),
  plus the new `fxdata/tables/cards.bin`, `fxdata/cards/Sprites.txt`,
  `src/generated/card_meta.hpp`, `images/cards/` (32 pages).

Tests (permanent, co-located, native frameworks, no /tmp):
- `tst/card_state_test.hpp` (+`tst/main.cpp`) — host: mask helpers, open/nav
  skipping absent pages, input edges, row mapping (including quest
  `QUEST_BASE`), crafted trim, hint rule (craft/equip/unequip/needs/quests).
- `tools/tests/test_gen_cards.py` — tooling: header/record ABI, page u24
  offsets resolved from a fixture `fxdata.h`, masks + absent pages not
  generated, overlay slots, byte-identical rerun, `--dump` writes nothing,
  missing-header warning, dense-id failure, contact-sheet layout/determinism,
  layer packing.
- `tools/tests/test_fxdata_manifest.py` + fixture — fixture gained
  `fxdata/tables/cards.bin`; output/artifact counts updated (12→13, 19→20).
- `tst/fxdatatest/cards_test.hpp` + `test_cards.ino` — device: mhCards record
  reads (kind/mask/overlays/addresses, bad index), smith/quests cart rows →
  card index, card A craft E2E + EEPROM roundtrip, quest take/turn-in E2E,
  crafted PARTS trim on refresh, framebuffer pins for the baked page (white
  title lights plane 2, light rule skips plane 2) and both overlays (HAVE
  digit tracks the inventory, PROG bar shade-2 fill lights planes 0/1 and is
  empty at 0 progress).

Docs: `README.md` (status/architecture/pipeline/test tiers), `docs/quests-shops.md`
(new "Detail cards (ui.3)" section + the temporary split).

## Temporary scope split (documented)

GEAR **weapon** rows keep the direct-equip action (`ACTION_EQUIP_WEAPON`) until
the ui.4 forge trees bring weapon cards. Every armor row (smith craft + gear
equip) and every quest row opens a card; the card A reuses `screenApplyAction`
with the stored row. Documented in `README.md`, `docs/quests-shops.md`, the
`.ino` comment and `cardRowOpens`.

## Gate tails

`make gen` (card line):
```
gen-cards: 9 items, 32 pages, 251 B blob (magic 0x4341 version 1)
gen-cards: fxdata/cards/Sprites.txt
gen-cards: fxdata/tables/cards.bin
gen-cards: src/generated/card_meta.hpp
```
`make gen-check`:
```
fxdata_manifest: PASS (122 generated artifacts unchanged)
```
`make test`:
```
Total Passed: 6262
Total Failed: 0
```
`make test-tools`:
```
Ran 322 tests in 17.753s
OK
```
`make fxtest-headless` (full, 17/17):
```
test_assets PASSED=264  test_audio PASSED=9    test_boot PASSED=4
test_cards PASSED=76    test_combat PASSED=237 test_data PASSED=348
test_hub PASSED=83      test_hud PASSED=29     test_items PASSED=35
test_monster_art PASSED=127  test_perf PASSED=5  test_player_art PASSED=120
test_quests PASSED=87   test_screens PASSED=155  test_smith PASSED=84
test_tell PASSED=18     test_zones PASSED=82
```
`make size`:
```
size: .text=29662 .data=18 .bss=1744
size: flash=29680/29696 (16 free)  ram=1762/2560
```

## Blockers

None. Headroom is tight (16 B free), but the card engine now fits; ui.4's own
trims (the ui.2-deferred smith ARMOR craft path, −376 measured in ui.1) are its
budget pool.
