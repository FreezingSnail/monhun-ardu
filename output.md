# monhun-ardu-5co.5 — ui.5 polish: header zenny, tokens, page indicator, hub strip, docs

Status: **BLOCKED (partial).** Priority 1 (header zenny) and the docs landed and
the full gate is green; the hub chrome (HUNT right column, bottom strip), the
list page indicator, and the denied cue do **not fit** the 190 B free at
HEAD addb103 and are deferred with measured deficits below. No commit/push
(orchestrator commits).

```
size: .text=29550 .data=32 .bss=1769
size: flash=29582/29696 (114 free)  ram=1801/2560
```

| | flash |
|---|---|
| HEAD addb103 (baseline) | 29506 (190 free) |
| this wave (header zenny + docs) | **29582 (114 free)** |
| delta | **+76 B** |

## What landed

### 1. Header zenny, ZENNY row + `ROW_F_ZENNY` retired (priority 1)

- `src/screens.hpp drawScreen()` draws the live balance right-aligned on the
  title line: `$` glyph + `hudDigits`/`drawNumber` at `SCREEN_COST_RIGHT`
  (white). The old `ROW_F_ZENNY` ternary in the cost column is gone.
- `data/screens/hub.json`: the `ZENNY` row is removed — the hub is now
  HUNT / QUESTS / FORGE / GEAR (`SCREEN_HUB_ROWS` 5 → 4, blob 841 → 829 B).
- `tools/gen-screens.py`: the `zenny` row flag is dropped from `ROW_FLAGS`
  (generated `ROW_F_ZENNY` const removed); `COND_ZENNY` stays in the ABI.
- `tools/gen-art.py`: a 3x5 `$` glyph added to `GLYPHS` (tile 36); the font
  sheets were always 128 fixed tiles, so this is a data-only art regen.

### 2. Docs (priority 5)

- `README.md` UI section: hub rows HUNT/QUESTS/FORGE/GEAR + header zenny; v5
  equipped-node loadout; FORGE/GEAR card flow; 44-byte save; **shipped caps 32
  weapon-node / 8 armor-piece slots, no migration**; no stale smith/token text.
- `docs/quests-shops.md`: layers/screen-data/actions updated (FORGE_NODE,
  OPEN_FORGE), a new **FORGE** section, the v5 save layout (44 B) + caps + no
  migration, the smith section marked removed, GEAR updated to weapon cards.
- `docs/ui-design.md`: Status records what ui.5 shipped and what is deferred
  with the per-feature deltas.

### 3. Tests (permanent, co-located, native)

- `tst/screens_test.hpp`: `ROW_F_ZENNY` → `ROW_F_FORGE` stable-value check.
- `tst/fxdatatest/screens_test.hpp`: hub row count 4, hub rows r0..r3, header
  zenny pixel assertions (1234 → `$`+digits at x 104..123, and the empty-balance
  control at x 116..123), HUNT/QUESTS/FORGE/GEAR label pixels.
- `tst/fxdatatest/hub_test.hpp`: hub row count + label pixels + header zenny.
- `tools/tests/test_gen_screens.py`: dropped the `ROW_F_ZENNY` const assertion;
  the zenny-flag test now asserts the flag name is rejected (retired).

## Blocked work + measured deficits

Each new chrome piece was built in isolation with `-DMH_UI5_*=0/1` overrides
through `SIZE_FLAGS` and measured with the whole-image linker total (LTO makes
per-symbol math meaningless). Baseline for the split: header-only build =
29596 B; the delta is the piece's own cost.

| Piece | flash | free after header+piece | note |
|---|---|---|---|
| header zenny (landed) | **+90** | 100 | incl. retiring the `ROW_F_ZENNY` branch |
| HUB HUNT right column (progress / READY / -) | **+238** | over | `questReadDef` + `READY` string + digit pair |
| HUB bottom strip (weapon marker + active skills) | **+276** | over | `forgeEquippedClass` + skill-abbr table loop + marker blit |
| list page indicator (`n/m`) | **+144** | over | two `/6` pages + `drawNumber`/`textPut` call sites |
| all four together | **+748** | 558 over | |

Deficit to fit the full polish: **≈ 558 B** (needs free ≥ ~750 B; only 190 at
baseline). The ui.1 trim table's large pools (smith UPGRADE −462, armor craft
−376) were already spent by ui.2/ui.3.1; no comparable dead code remains.

### Options (ranked)

1. **Split a trim bead** (preferred): reclaim ~600 B of shipping code, then land
   ui.5b (hub chrome + page indicator). The two remaining big pools are the
   `armorApplyToGame` aggregate path (724 B) and the `MH_NOINLINE` sweep — both
   need their own spike (perf/behavior risk).
2. **Land in priority order as budget allows**: header zenny is landed; the page
   indicator (+144) is the next cheapest and fits only after a ~30 B trim; the
   HUNT column (+238) and strip (+276) need the trim bead.
3. **Drop scope**: weapon-only strip (no skill totals) ≈ −120 B, but the HUNT
   column + page still exceed the current budget.

## Item 6 — bitset widening

Free after the landed work is **114 B**, below the ~120 B threshold, so the
bitsets stay at the shipped caps and are documented as such:
**32 owned weapon-node slots (4 B)** and **8 crafted armor-piece slots (1 B)**,
no migration (v5 + checksum only; anything else falls back to defaults).

## Gates (all green, tree with the partial landing)

- `make gen-check` — PASS (148 generated artifacts unchanged)
- `make test` — 6306 passed / 0 failed
- `make test-tools` — Ran 344 tests, OK
- `make fxtest-headless` — 18/18 suites PASS (assets, audio, boot, cards,
  combat, data, forge, hub, hud, items, monster_art, perf, player_art, quests,
  screens, smith, tell, zones), exit 0
- `make size` — `flash=29582/29696 (114 free)  ram=1801/2560` (+76 B)

## Files

```
 M data/screens/hub.json              ZENNY row removed (4-row hub)
 M src/screens.hpp                    header zenny, ROW_F_ZENNY branch removed
 M tools/gen-art.py                   $ glyph
 M tools/gen-screens.py               zenny row flag retired
 M tools/tests/test_gen_screens.py    flag assertion/test updated
 M tst/screens_test.hpp               ROW_F_FORGE stable-value check
 M tst/fxdatatest/screens_test.hpp    hub rows + header zenny pixels
 M tst/fxdatatest/hub_test.hpp        hub rows + header zenny pixels
 M README.md docs/quests-shops.md docs/ui-design.md
 M src/generated/{screen_meta,equip_meta,zone_meta}.hpp
 M fxdata/* (font sheets + fxdata bins + manifest, screens/cards/equip tables)
 M src/fxdata.h
```

No commit/push.
