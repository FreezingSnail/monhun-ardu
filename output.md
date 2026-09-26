# monhun-ardu-dap — trim wave: weaponSheet switch + beast-presence cache

STATUS: DONE (landed; measured reclaim is FAR BELOW the frozen lead — see Size).

## What changed

### A) weaponSheet switch -> resolved FX sheet offset (frozen trim)
- `tools/gen-forge.py`: resolves each node's `sheet` symbol against the committed
  `fxdata/fxdata.h` (`FX_SYMBOL_RE`) and emits `NODE_SHEET_OFF[NODE_COUNT]` —
  the absolute FX sheet address per node; a node with no sheet (and any
  unequipped fallback) uses its class default (`mh_weapon_sword/flail/gun`).
  AVR: `MH_PROGMEM constexpr uint32_t` (flash-only); host: plain `constexpr
  uint32_t`. A synthetic tree with no part header / undeclared symbol resolves
  to 0 without failing (gen-cards/gen-screens fixtures reuse the loader).
- `src/generated/forge_meta.hpp`: regenerated (new table + AVR includes
  `../core/progmem.hpp`).
- `src/forge_state.hpp`: new `forgeSheetOff(node)`
  (`mhPgmReadU32(&NODE_SHEET_OFF[node])`, `MH_NOINLINE`),
  `forgeEquippedSheetOff(save, cls)` (equipped node else
  `NODE_SHEET_OFF[NODE_CLASS_FIRST[cls]]`), `forgeClassSheetOff(cls)`.
  Kept `NODE_SHEET` + `forgeEquippedSheet` for the kind-pinning host/device
  tests (LTO drops the now-unused function + table from shipping).
- `src/core/game.hpp`: `using SheetOff = __uint24` (host: `uint32_t`);
  `Game::wpnSheet` is now `SheetOff` (was `uint8_t` kind).
- `src/core/player.hpp`: `initGame` arms the class default address
  (`forgeClassSheetOff(weapon)`).
- `src/app_setup.hpp`: `upgradeApplyToGame` arms
  `forgeEquippedSheetOff(save, g.weapon)`; static_assert pins
  `WeaponId == forge::WEAPON_*` order.
- `src/render.hpp`: `weaponSheet(g)` is `return static_cast<uint24_t>(g.wpnSheet);`
  (3-level kind switch + class guard gone).
- `tst/fxdatatest/player_art_test.hpp`: `Case.wpnSheet` is a `uint32_t` sheet
  ADDRESS; the 12 variant rows carry the `equip::SHEET_OFF_MH_WEAPON_*`
  constant, kind-0 rows map to the class default via `defaultWpnSheet`. All 52
  goldens unchanged.
- `tst/forge_state_test.hpp` / `tst/fxdatatest/forge_test.hpp` /
  `tools/tests/test_gen_forge.py`: pin `NODE_SHEET_OFF` against the equip
  catalog and the resolver fallbacks; tooling pins the emitted table + the
  no-header / undeclared-symbol -> 0 fallback.

### B) Beast-presence cache (frozen trim)
- `src/core/game.hpp`: `Game::beastHere` byte (appended at the end).
- `src/core/zones.hpp`: `refreshBeastHere(g)` computes the old predicate once;
  `beastHere(g)` is `return g.beastHere != 0;`.
- `src/core/world.hpp`: `refreshBeastHere` runs inside `updateActiveTarget`,
  the single path called by `newGame` (and thus `withWeapon`/`resetHunt`) and
  `loadRoom` — the only sites that move `monsterKind`/`roomId`/`roomMonsterKind`.

## Verification

- `make gen-check`: PASS (217 generated artifacts unchanged; `fxdata/fxdata.h
  == src/fxdata.h`).
- `make test`: Total Passed: 7000, Failed: 0.
- `make test-tools`: Ran 411 tests — OK.
- `FXTEST_ONLY="test_player_art test_monster_art test_zones test_hub test_forge"`:
  - test_forge PASSED=79 FAILED=0
  - test_hub PASSED=86 FAILED=0
  - test_monster_art PASSED=182 FAILED=0
  - test_player_art PASSED=156 FAILED=0
  - test_zones (zones_test) PASSED=77 FAILED=0
  (also ran test_wire PASSED=31, test_hud PASSED=29 earlier in the loop)
- `make size-line` tail:
  `size: flash=29616/29696 (80 free)  ram=1867/2560`
  Baseline (HEAD 4fb8e10): `flash=29634/29696 (62 free)  ram=1920/2560`.
  Delta: **flash -18 B (62 -> 80 free), RAM -53 B (1920 -> 1867)**.

## Size verdict (far off the lead — reported, not forced)

Frozen lead: A ~114 B, B ~16 B, target >= ~180 B free.
Measured: **+18 B free** (flash), +53 B RAM free.

Diagnosis (isolated by stub builds):
- Removing the render switch alone: **-144 B flash** (spike: 29634 -> 29490),
  -36 B RAM.
- The generated `NODE_SHEET_OFF[21]` table costs **84 B flash** (PROGMEM), and
  the resolver glue (noinline `forgeSheetOff` + `forgeEquippedSheetOff` +
  `forgeClassSheetOff` + the `__uint24` store) ~48 B flash — together ~132 B.
  Net A ~= -12 B.
- B: the cached `beastHere` load saves ~74 B across the 3 hot gates, but
  `refreshBeastHere` (+2 calls via `updateActiveTarget`) costs ~44-60 B; net
  B ~= +6..+30 B depending on inlining (chosen: inline, single call site).
- Conclusion: the frozen A lead (~114 B) appears to assume the offset table is
  free; the absolute-address data is real sketch flash here. Neither trim was
  forced to hit the target.

Option to realize the lead in a follow-up (NOT implemented — out of the frozen
scope): pack the 3-byte sheet offset into the FX-cart forge record
(`fxdata/tables/forge.bin`) and read it via `src/forge.hpp` at hunt start; the
blob lives on the SPI flash, so the address data costs 0 sketch bytes and the
render change alone reclaims ~144 B.

## Files

- src/core/game.hpp, src/core/player.hpp, src/core/world.hpp, src/core/zones.hpp
- src/forge_state.hpp, src/app_setup.hpp, src/render.hpp
- src/generated/forge_meta.hpp (+ fxdata/manifest.json)
- tools/gen-forge.py, tools/tests/test_gen_forge.py
- tst/forge_state_test.hpp, tst/fxdatatest/forge_test.hpp,
  tst/fxdatatest/player_art_test.hpp

No commit/push (orchestrator commits). mock/ + parity untouched.

## Wall time (approx, worker)

- recon/read (bead + AGENTS + dev-flow + key headers): ~8 min
- spike (switch removal, table/refresh isolation builds): ~18 min
- code + generated + test edits: ~30 min
- `make gen` (x4 converge) + gen-check: ~14 min
- `make test` + `make test-tools`: ~5 min
- device suites (7, one iteration): ~6 min
- final re-verify (host + gen-check + tools + size) + report: ~10 min
- total: ~1 h 30 min
