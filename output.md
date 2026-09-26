# monhun-ardu-jd1 — wire equipped forge node sheet to the render

Status: DONE. Gate green; no commit (orchestrator commits).

## What changed

- `tools/gen-forge.py`: added `SHEET_KINDS` (sword/flail/gun -> 0,
  gun buckler/kite/tower/brace -> 1..4); an unknown `sheet` symbol is a hard
  error (`sheet: unknown sheet symbol 'X'`). Each node carries `sheetKind`; the
  generator emits `constexpr uint8_t NODE_SHEET[NODE_COUNT]` into
  `src/generated/forge_meta.hpp` (real data: `{0,0,0,0,0,0,0,0,0,1,2,3,4}`).
- `src/core/game.hpp`: `Game::wpnSheet` (u8) beside `dmgMul/spdMul`.
- `src/core/player.hpp`: `initGame` zeroes `wpnSheet` (class default sheet).
- `src/forge.hpp`: `forgeEquippedSheet(const SaveBlock&)` -> `NODE_SHEET[node]`,
  `0` for `SAVE_NODE_NONE` / out-of-range (constexpr table, no cart read).
- `src/app_setup.hpp`: `upgradeApplyToGame` also sets `g.wpnSheet`.
- `src/render.hpp`: `weaponSheet(const mh::Game&)`; for W_GUN switches on
  `g.wpnSheet` to the buckler/kite/tower/brace `SHEET_OFF_MH_WEAPON_GUN_*`
  constant (default `mh_weapon_gun`). Sword/flail unchanged. Call site updated.
- Docs: `docs/weapon-art.md` (variant sheet selection), `docs/ui-design.md`
  (kind byte).
- Generated set regenerated with `make gen` (forge_meta.hpp + fxdata manifest
  hash).

## Tests added (permanent, native frameworks)

- `tools/tests/test_gen_forge.py`: `test_node_sheet_kinds` (gun fixture with all
  four variants; pins `NODE_SHEET = {0,0,0,0,0,0,0,1,2,3,4}` over the synthetic
  tree and the per-node kinds) + `test_unknown_sheet_symbol_rejected`.
- `tst/forge_state_test.hpp`: "node sheet kinds" pins the four variant kinds +
  class defaults + `sizeof(NODE_SHEET) == NODE_COUNT`.
- `tst/fxdatatest/forge_test.hpp`: `forgeEquippedSheet` flow — kinds 1..4 via the
  equipped node, default 0 for base/none/out-of-range.
- `tst/fxdatatest/player_art_test.hpp`: `Case::wpnSheet` + 4 appended cases 40..43
  (W_GUN, PS_IDLE, ST_GUARD, fx=16, kinds 1..4), `CASE_COUNT` 40 -> 44, goldens
  regenerated via the documented PRINT_GOLDENS flow; regen-history note added.

## Verification (exact commands)

- `make gen-check` -> `fxdata_manifest: PASS (181 generated artifacts unchanged)`.
- `make test` -> `Total Passed: 6885  Total Failed: 0`.
- `make test-tools` -> `Ran 392 tests ... OK` (incl. both new forge tests).
- `FXTEST_ONLY="test_player_art test_forge" make fxtest-headless` ->
  `test_forge PASSED=70 FAILED=0`, `test_player_art PASSED=132 FAILED=0` (44
  cases x 3 planes).
- `make size` -> `flash=29278/29696 (418 free)  ram=1856/2560`.
  Baseline 29196 (500 free): **+82 B**, 418 free (>= the 150 B reserve).
- `ARDENS=/usr/bin/true make dev-hitboxes` -> `flash=29376/29696 (320 free)`.

## Golden regen

`PRINT_GOLDENS=true` run emitted all 44 lines; cases 0..39 were byte-identical
to the existing goldens (all prior rows carry `wpnSheet 0` -> class default).
New case hashes:

```
G 40 b8679899 b6322ac0 3a2e3ee9
G 41 6cf6bb00 6c89d9ac 5be309de
G 42 aed61621 62e32a39 5d595e89
G 43 9d80d12f 17ccce82 a0fa3755
```

All four differ from each other and from the kind-0 guard case 29
(`60f18d7f 09458678 b6d14f15`), i.e. each variant sheet actually draws.

## Notes

- `wpnSheet` is not in the parity state hash (same as `dmgMul/spdMul`); sim
  behaviour is unchanged.
- Wall time (worker, approx; not instrumented): ~20 min.
