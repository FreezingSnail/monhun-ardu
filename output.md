# monhun-ardu-4ug — qs.3 smith store: upgrade tiers, stat application, purchase flow

Status: **DONE** (all gates green; no commit per worker protocol).
Design: `docs/quests-shops.md` "Smith (content model)" (bead qs.3,
epic monhun-ardu-idg).

A deterministic generator packs six smith upgrade defs (3 weapons x 2 tiers) to
the cart, a smith screen exposes them as gated purchase rows, the purchase
action spends zenny and raises the saved tier, and the tier's integer-percent
damage/speed multipliers are resolved into the sim at hunt start and applied in
the real player damage + move-speed + shell paths.

## Files

New:
- `tools/gen-smith.py` — `data/smith/*.json` -> `src/generated/smith_meta.hpp`
  + `fxdata/tables/smith.bin` (schema-validated, deterministic, `--dump`).
- `data/smith/{sword,flail,gun}_t{1,2}.json` — 6 UpgradeDefs: sword 100/110/105,
  250/125/115; flail 120/112/103, 280/130/110; gun 90/108/100, 220/120/105;
  unlockFlag 0.
- `data/screens/smith.json` — id 2, 7 rows (6 tier rows + LEAVE). Row `param`
  packs `(unlock << 4) | (weapon << 2) | tier`; labels "SWORD T1".."GUN T2".
- `src/upgrade_state.hpp` — host-testable `UpgradeDef`, `upgradeFind`,
  `upgradeResolve`, `upgradeMul` (integer percent, truncating; 100 = identity).
- `src/smith.hpp` — device cart reader + `smithResolve` over the mhSmith blob.
- `tst/smith_test.hpp` (host, 48 asserts), `tst/fxdatatest/smith_test.hpp` +
  `test_smith.ino` (device E2E, 66 asserts), `tools/tests/test_gen_smith.py`
  (14 tests) + `fixtures/gen_smith/clean/`.
- `src/generated/smith_meta.hpp`, `fxdata/tables/smith.bin` (generated).

Changed:
- `src/core/game.hpp` — `Game` gains `dmgMul`/`spdMul` (100 = identity).
- `src/core/player.hpp` — `initGame` defaults 100/100; idle move speed scaled by
  `g.spdMul`; melee (`attackDmg`) and whirl (8) scaled by `g.dmgMul` before the
  existing `Target::onHit -> monsterOnHit` chain.
- `src/core/projectiles.hpp` — shell damage scaled by `g.dmgMul` at spawn.
- `src/screen_state.hpp` — new `COND_UPGRADE` (next-tier + unlockFlag + funds)
  and `screenApplyAction` BUY_UPGRADE decodes the packed (weapon, tier) for
  COND_UPGRADE rows, landing exactly on that tier; legacy hub-stub rows keep the
  incremental behaviour.
- `tools/gen-screens.py` — `COND_UPGRADE` id 5 + buy-action/weapon/tier
  validation; `tools/gen.sh`, `fxdata/fxdata.txt` (`raw_t mhSmith`),
  `tools/fxdata_manifest.py` (OUTPUT_PATHS), manifest fixture + counts.
- `monhun-ardu.ino` — includes `src/smith.hpp`; `upgradeApplyToGame()` resolves
  the current weapon's tier from the save + cart at setup and every menu start
  (no mid-hunt cart reads).
- Generated set re-baked (mhSmith/mhScreens shift the FX image and equip sheet
  offsets); `make gen-check` confirms stability.

## Verification (exact)

1. `make gen` (repeated) + `make gen-check`:
   `gen-smith: 6 upgrades, 50 B blob (magic 0x534D version 1)`;
   `fxdata_manifest: PASS (59 generated artifacts unchanged)`; header copy check
   passed.
2. `make test`: `Total Passed: 3363 / Total Failed: 0` (baseline 3297; +66).
   `make test-tools`: `Ran 135 tests ... OK` (baseline 118; +14 gen_smith,
   +3 gen_screens upgrade-condition).
3. `make fxtest-headless` (full, 13 suites): all PASS —
   assets 262, audio 14, boot 4, combat 184, data 221, hud 17, menu 59,
   parity 660, perf 5, player_art 111, quests 50, screens 68, **smith 66**;
   0 failures. Perf: `B pUs=6376 pHz=156 lHz=52 lTk=528 rMx=4968 rAv=4756
   ram=606` vs baseline `rMx=4968 rAv=4756 pUs=6375` (rMx/rAv/lTk identical;
   pUs +1 us, measurement noise).
4. `make build` + `make size`: `flash=26048/29696 (3648 free)` — **+320 B** vs
   25728 (`free -320` vs 3968); `ram=1855/2560` — **+2 B** vs 1853
   (`.text=25990 .data=58 .bss=1797`).
5. Parity fixtures: `node tools/gen-parity-fixtures.js` -> empty diff
   (`tst/fxdatatest/parity_fixtures.hpp` and `mock/` unchanged);
   `test_parity 660/660` on device.

## Deviations

- **SAVE_VERSION not bumped (stays 2).** The save block already carries
  `tier[3]` (added with the qs.1 framework and kept in v2 by qs.2), so the
  purchase persists with no record-layout change; bumping the version would
  reset every existing save for zero new bytes. `saveDecode` still rejects any
  non-v2/old-length record via magic+version+checksum and falls back to
  defaults, so old-length reads remain safe. Nothing new to persist: damage/
  speed multipliers are derived from `tier` + the mhSmith cart at hunt start.
- pUs is +1 us vs the stated baseline (6375 -> 6376); render rMx/rAv and logic
  lTk are byte-identical and the +1 us is inside the same frame budget.
