# monhun-ardu-5co.1 — ui.1 spike: card-blit budget + trim candidate table

Measurement bead. No feature landed; every probe was built, measured with
`make size`, and reverted. HEAD 4423931, working tree clean (only
`docs/ui-design.md` untracked). Measurement method: whole-image `avr-size` via
`make size` with per-candidate `-D` overrides passed through `SIZE_FLAGS`
(LTO makes per-symbol math meaningless). The shipping default flags are
unchanged; final state is byte-identical to baseline.

## Baseline (HEAD, all probes reverted)

```
size: .text=29186 .data=18 .bss=1697
size: flash=29204/29696 (492 free)  ram=1715/2560
```

## 1. Card blit (128x64 per-plane detail card + nav + hint line)

Implemented a temporary self-contained card engine (`MH_CARD_MEASURE`, default 0):

- `cardBlit(img, page)`: streams one 128x64 1bpp page-major layer per plane
  (1024 B/plane, 8 source pages of 128 B) into framebuffer pages 0..7 through
  the existing `roomAsmCopy` streaming reader (`drawRoom`'s fused SPI path).
  Card pages are page-aligned (`camY&7 == 0`), so only the copy reader is needed.
- `CardNav {page, pageCount}` + `cardNavStep` — minimal LEFT/RIGHT page nav.
- `cardHint(str, len)` — one cart text line at y=56 (screenReadText-style bulk
  read + `textPut`).
- `drawCard` wired additively into `drawScreen` (list render stays live, so the
  delta is additive card cost, not a list-renderer replacement — an early-return
  variant measured -456 because it let LTO drop the list renderer; discarded).

**Dummy image method:** no cart data added. The existing `mh_map_area` FX symbol
is used as the dummy page source (`img + page*1024`, plane*1024); the real engine
gets a generated (item,page) offset table. The dummy string for the hint is the
first 8 bytes of the `mhScreens` blob. Both reverted.

| Build | flash | free | delta |
|---|---|---|---|
| baseline | 29204 | 492 | — |
| card engine only (`MH_CARD_MEASURE=1`) | 29488 | 208 | **+284 flash, +0 RAM** |
| room-image ground only (`MH_ROOM_IMAGE=1`) | 29360 | 336 | +156 flash |
| both (room ground + card) | 29648 | 48 | +444 flash |
| card + header-zenny (no trims) | 29556 | 140 | +352 flash |

Card increment on top of an already-room-image build = +288. RAM unchanged
(+0 measured): the 2-byte `CardNav` static landed in existing `.bss` padding;
a real `ScreenState` nav is expected to cost +2 RAM.

Exact line (card only):

```
size: flash=29488/29696 (208 free)  ram=1715/2560
```

## 2. Header zenny on list screens

Temporary `MH_ZENNY_HEADER` toggle: draw `$` (ASCII 36) + right-aligned
`hudDigits`/`drawNumber` on the title line at `SCREEN_COST_RIGHT`, and drop the
`ROW_F_ZENNY` ternary from the cost column.

```
size: flash=29272/29696 (424 free)  ram=1715/2560
```

**Header zenny: +68 flash, +0 RAM.** Retiring the HUB `ZENNY` row is a cart-data
change (`data/screens/hub.json`) with no MCU-flash effect; the +68 is pure render
code (extra digits + glyph + call). Net feature spend is affordable only with
trims below.

## 3. Trim candidates (ranked by reclaim)

Each stub built separately, measured, reverted. "Breaks (if adopted)" lists the
suites that assert the removed path — they were NOT run with the stub macros on;
the final tree keeps them all green.

| # | Candidate | flash | delta | free after | breaks (if adopted) |
|---|---|---|---|---|---|
| 1 | smith **UPGRADE** path: `COND_UPGRADE` + `ACTION_BUY_UPGRADE` + `screenRowRecipe` (weapon tiers replaced by trees) | 28742 | **−462** | 954 | `tst/smith_test.hpp`, `tst/screens_test.hpp`, `tst/app_state_test.hpp`, `tst/fxdatatest/smith_test.ino` |
| 2 | smith **ARMOR craft** path: `COND_ARMOR` + `ACTION_CRAFT_ARMOR` + `screenRowArmorRecipe` (FORGE trees replace armor crafting) | 28828 | **−376** | 868 | `tst/smith_test.hpp`, `tst/screens_test.hpp`, `tst/armor_engine_test.hpp` (GEAR equip path stays) |
| 3 | **Dead conditions** `COND_ZENNY`/`COND_FLAG`/`COND_TIER` (unused in `data/screens/*.json`) | 29142 | **−62** | 554 | `tst/smith_test.hpp`, `tst/screens_test.hpp` |
| — | 1+2+3 combined (shared `screenRecipeOk/Debit` also drops) | 28088 | **−1116** | 1608 | as above |
| 4 | **int-width narrowing** in `drawScreen` (x/y/lx/costX int16→uint8) | 29190 | −14 | 506 | none |
| 5 | `ROW_F_HIDE_LOCKED` + dead flags | 29204 | **0** | 492 | none (unreferenced constexpr, no emitted code; `tst/screens_test.hpp:540` + `tools/tests/test_gen_screens.py` pin the constant) |
| 6 | **noinline sweep** — all 33 `MH_NOINLINE` off | 29664 | **+460** | 32 | perf/behavior not measured; regresses |
| 6b | noinline warm subset (`hudDigits`, `screenDefOff`, `screenUpgradeWeapon`) | 29232 | **+28** | 464 | regresses |

No further real-delta candidate found by inspection: the remaining flags
(`ROW_F_SKILL`, `ROW_F_ZENNY`) are live, both smith decoders are used, and the
room-image toggle (+156) is a look/budget choice, not dead code.

**Combined spend check** (trim set + card + header zenny):

```
size: flash=28450/29696 (1246 free)  ram=1715/2560
```

## 4. Recommended ui.2 reclaim target

Adopt candidates **1 + 2 + 3** (the two smith paths + dead conditions). Ranked
candidates: UPGRADE (−462) > ARMOR craft (−376) > DEADCOND (−62) > narrowing
(−14) > ROW_F_HIDE_LOCKED (0) > noinline sweep (regresses, do not touch).

- **Target: free ≥ 900 B reclaimed, ≥ 1.6 KB free before the card/tree wave.**
- Measured: free 492 → **1608** on the trim set alone (−1116 combined);
  **1246 free** after also adopting the card engine (+284) and header zenny (+68).
- That headroom covers the design estimates (cards ~150–250 B, header/tokens/
  page-indicator ~100–180 B, trees + save v5 ~200–400 B, gear list ~100–200 B)
  with margin. The two smith trims are the only large, low-risk pool; the
  dead-condition and narrowing nibbles are freebies on top.
- Note: trimming 1/2/3 deliberately deletes the code the smith host/device
  suites assert, so ui.2 must update those suites in the same bead (the
  FORGE-tree replacement is the intended owner of that behavior).

## Final gate (tree reverted, no probes)

- `make gen-check` — PASS (87 generated artifacts unchanged)
- `make test` — 6240 passed / 0 failed
- `make test-tools` — Ran 310 tests, OK
- `make fxtest-headless` — 16/16 suites PASS (assets, audio, boot, combat, data,
  hub, hud, items, monster_art, perf, player_art, quests, screens, smith, tell,
  zones)
- `make size` — `flash=29204/29696 (492 free) ram=1715/2560` (baseline,
  byte-identical)

No commit/push. All probe scaffolding (`MH_CARD_MEASURE`, `MH_ZENNY_HEADER`,
`MH_TRIM_*` in `src/render.hpp`, `src/screens.hpp`, `src/screen_state.hpp`,
`src/core/progmem.hpp`) removed via `git checkout`.
