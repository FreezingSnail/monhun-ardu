# monhun-ardu-dx5.1 — perf: runtime shift -> LUTs

Status: **DONE (partial by budget rule)**. The shipping image stays within the
flash baseline; 6 runtime-shift sites are replaced by constexpr flash LUTs.
The three remaining audit sites (combat, items, audio was kept; combat/items
cost flash) were **dropped** after measurement — see §4.

Tree left dirty (no commit/push), as instructed.

## 1. What changed

New shared accessor header:

- `src/core/bitlut.hpp` (new) — `mhBit8(n)`: `1u << n` for `n & 7` via an
  8-byte flash LUT. The table is a function-local `static const` inside an
  `inline` function, so C++ gives it one linker-merged object (8 B total) no
  matter how many TUs include the header. Reads go through `mhPgmReadU8`
  (`MH_PROGMEM`), so host suites exercise the same code path with plain loads.

Sites converted to `mhBit8`:

- `src/core/save.hpp:39` — include.
- `src/core/save.hpp:89` — `saveCrafted` (`0x01u << (SAVE_CRAFTED_BIT_BASE + piece)`).
- `src/core/save.hpp:93` — `saveSetCrafted` (same form).
- `src/core/save.hpp:185` — `saveQuestGet` (`1u << (bit & 7)`).
- `src/core/save.hpp:189` — `saveQuestSet` (same form).
- `src/core/save.hpp:193` — `saveQuestClear` (`~(1u << (bit & 7))`).
- `src/audio.hpp:30` — include.
- `src/audio.hpp:156` — `audioCue` `firedMask |= 1u << cue`.

Render site converted with its own table (not a `1u << n` LUT; it maps
`v -> (v==0) ? 0 : 1<<(8-v)`):

- `src/render.hpp:381` — `ROOM_ROW_COEF[8]` (`MH_PROGMEM`).
- `src/render.hpp:406` — `coef = mhPgmReadU8(&ROOM_ROW_COEF[v & 7])`.

Behavior is unchanged: every index was already in range, and `mhBit8` masks to
3 bits (host suite pins save/audio behavior; 6285 host tests green).
`ROOM_ROW_COEF[0] == 0` reproduces the old ternary; `drawRoom` is compiled only
under `MH_ROOM_IMAGE`, so this edit is neutral on the shipping image and is
exercised by `test_zones` (which forces `MH_ROOM_IMAGE=1`).

No float, no new mutable globals, no new RAM.

## 2. Flash/RAM (order 2: `make size`)

Baseline (clean tree, HEAD 7452e69): `flash=28448/29696 (1248 free) ram=1638/2560`.

After: **`flash=28446/29696 (1250 free) ram=1638/2560`** — **−2 B flash, RAM flat**.
`.text=28426 .data=20 .bss=1618`.

## 3. Gates (tails)

1. `make test` → `Total Passed: 6285 / Total Failed: 0`.
2. `make size` → `size: flash=28446/29696 (1250 free)  ram=1638/2560`.
3. `make fxtest-headless FXTEST_ONLY=test_combat` → `combat_test PASSED=237 FAILED=0` / `test_combat: PASS`.
4. `make fxtest-headless FXTEST_ONLY=test_items` → `test_items PASSED=35 FAILED=0` / `test_items: PASS`.
5. `make fxtest-headless FXTEST_ONLY=test_audio` → `test_audio PASSED=9 FAILED=0` / `test_audio: PASS`.
6. `make fxtest-headless FXTEST_ONLY=test_hud` → `test_hud PASSED=29 FAILED=0` / `test_hud: PASS`.
7. `make fxtest-headless` (full) → every suite PASS (combat 237, data 348,
   hub 63, hud 29, items 35, menu_art 53, menu 60, monster_art 127, perf 5,
   player_art 120, quests 50, screens 85, smith 115, tell 18, zones 80).

Perf bench (inside full run): **`B pUs=6344 pHz=157 lHz=52 lTk=184 rMx=3156
rAv=2761 ram=680`** — byte-identical to baseline. Expected: `save.hpp` is
EEPROM/boot-path only, `audioCue`'s shift only runs on a cue edge, and the
render site is compiled out at `MH_ROOM_IMAGE=0`.

## 4. Dropped sites (budget rule) and measured deltas

Rule applied: LTO makes per-symbol math meaningless, so every delta below is a
whole-image `make size` measurement. Sites that grow flash are dropped.

Measured marginal costs (each added on top of the previous, same tree):

| site group | form tried | delta vs 28448 |
|---|---|---|
| `src/core/save.hpp` (5 sites) | own 8-B table + `mhPgmReadU8` | **−4 B** |
| `src/audio.hpp:155` (1 site) | shared `mhBit8` (8-B table already emitted) | **+2 B** |
| → `save` + `audio` kept together | shared `mhBit8` | **−2 B** |
| `src/core/combat.hpp:1197` | shared `mhBit16` (32-B u16 table), inline | **+24 B** |
| `src/core/items.hpp:123,185` | shared `mhBit16` (32-B u16 table), inline | **+16 B** |
| all four together | per-file tables | +78 B |
| all four together | shared `mhBit16` | +38 B |

Variants that did **not** recover the cost (all still over baseline):

- **8-byte `mhBit16` + high-byte branch** (`b = mhBit8(n); bit = (n&8) ? b<<8 : b`):
  items went from **+16 B** to **+28 B** — the branch is more expensive than the
  32-byte u16 table it saves.
- **Per-file tables instead of the shared header**: audio alone rose to **+14 B**
  (own 8-B table), pushing `save`+`audio` to +10 B. The shared
  function-local-static table is what makes the audio site fit.
- `MH_NOINLINE` on the item helper and on `mhPgmReadU8`-style reads: no change
  (±0 B).

Conclusion: with 1248 B free at baseline, only the byte-width sites
(`save` ×5, `audio` ×1) are neutral/negative. The two `uint16_t` sites
(`combat`, `items`) and the audio site on their own each exceed the budget.
`combat`/`items` were reverted to the original `1u << idx`. The render site is
kept because it is compiled out of the shipping image (delta 0 there) and is
still a valid fix for `MH_ROOM_IMAGE=1`.

If the orchestrator wants the two u16 sites as well, it needs ~+40 B of flash
headroom (e.g. a size bead) or a different encoding; the LUT itself cannot be
made free here.
