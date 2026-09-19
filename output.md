# monhun-ardu-nch.7 — chicken: peck + leap kit, turn commitment, window telegraphs

Baseline HEAD `1eabbc0`. Worker spawn was cancelled mid-run; the tree was
inspected, verification finished inline, and the result below is what is
committed. Pre-existing unrelated dirty files (`.gitignore`, `docs/dev-flow.md`,
`.github/`, `recording_20260918184558.gif`, `tools/package-arduboy.py`,
`tools/tests/test_package_arduboy.py`) were left untouched.

## What changed

Data `data/creatures/lunge.json` (owner-approved kit, replaces inherited
generic `lunge`+`sweep`):

- `peck`: W22 / A6 / R30, dmg 7, BLUNT, lunge speedF 18, `facing track`,
  window 12x10 @ (ox 14, oy -6).
- `leap`: W34 / A10 / R48, dmg 12, BLUNT, lunge speedF 42,
  `facing lock-at-windup`, window 18x16 @ (ox 12, oy -2).
- profile `keepDist` 24 -> 16, `faceHold` 0 -> 6.
- patterns `p_peck` (maxDist 28) / `p_leap` (minDist 28..255, hpBand 0..100).
- `zones.appendage.broken.disableAttacks` ["sweep"] -> ["leap"].

Mock `mock/game.js`:

- `MONSTER_ATTACKS.peck`/`leap` windows-path entries (legacy `lunge`/`sweep`
  kept: parity scene `monster_sweep_hit` loads `sweep` directly).
- `MONSTER_DEFS[0]` keepDist/faceHold; chicken branch in `chooseAttack`; new
  `monsterAttackDisabled()` mirroring C++ `combatAttackDisabled`; zone
  `disableAttacks` list on the chicken legs.
- `mock/game.test.js`: 3 new permanent tests (range split, leap
  lock-at-windup + flank, broken legs fall back to peck).

Generated + tests: combat blob (`fxdata/tables/combat.bin`, `fxdata*.bin`,
`manifest.json`), `src/generated/combat_*.hpp`, parity fixtures; host/device
test expectations updated to the new symbolic records (`ATTACK_LUNGE_PECK` /
`ATTACK_LUNGE_LEAP`, etc.), parity override mapping now loads the SWEEP
creature's legacy records for fixture attack kind 0/1.

Docs: `docs/creature-framework.md` chicken kit section; README LUNGE row.

## Verification (exact)

- `node --test mock/game.test.js` -> tests 72, pass 72, fail 0
- `node tools/gen-parity-fixtures.js` twice -> md5
  `fab30655c9fa53efedf14664195ef4ea` both runs (idempotent)
- `make gen-check` -> `fxdata_manifest: PASS (68 generated artifacts unchanged)`
- `make test` -> `Total Passed: 4990`, `Total Failed: 0` (baseline 4981)
- `make fxtest-headless FXTEST_ONLY=test_parity` -> `PASSED=660 FAILED=0`
- full `make fxtest-headless` -> 16/16 suites PASS, all FAILED=0:
  assets 258, audio 17, boot 4, combat 237, data 368, hub 57, hud 17,
  menu_art 81, menu 78, monster_art 38, parity 660, perf 5, player_art 111,
  quests 50, screens 78, smith 66
- `make size` -> `.text=26914 .data=40 .bss=1719`;
  `flash=26954/29696 (2742 free)`, `ram=1759/2560 (801 free)`; HAS_* facts
  unchanged. Data-only bead: zero MCU delta.

## Parity scene movement (def 0 default, per-scene hash ranges)

- `beast_no_shove_idle` 40/40 ticks moved (faceHold cadence)
- `camera_world_clamp` 3/220 moved
- other 18 scenes byte-identical, `monster_sweep_hit` included.

## Deviations

- Worker cancelled mid-run; inline completion + full gate by orchestrator, no
  partial work left.
- `docs/creature-framework.md` note corrected to the measured 2/20 moved
  scenes (draft wording implied all 20).
- No engine code change (data-only, as designed).
