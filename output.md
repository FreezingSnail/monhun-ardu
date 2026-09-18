# monhun-ardu-me6 — qs.2 quest board + kill accounting + payout

Status: **DONE** (all gates green; no commit per worker protocol).
Design: `docs/quests-shops.md` "Quests (content model)" / "Save block" (beads qs.2).

Three starter kill-quests are packed to the cart via a new deterministic
generator, a quest board screen drives TAKE/TURN_IN through the existing action
switch, `Game` counts target-kind kills on the monster-death path, and the save
block persists active quest + progress (v2) for a u16-clamped turn-in payout.

## Files

New:
- `tools/gen-quests.py` — `data/quests/*.json` -> `src/generated/quest_meta.hpp`
  + `fxdata/tables/quests.bin` (schema-validated, deterministic, `--dump`).
- `data/quests/{slay_lunge,slay_sweep,crush_heavy}.json` — 3 starter quests:
  slay 3 LUNGE (150), slay 2 SWEEP (250), slay 1 HEAVY (400); unlockFlag 0.
- `data/screens/quests.json` — 6-row board (take + reward rows per quest),
  `COND_QUEST`, reward in `cost`, `param` packs `(need << 4) | questId`.
- `src/quest_state.hpp` — host-testable `QuestDef`, status/takeable/ready
  helpers, take / addKill / turn-in, u16-saturating `zennyAdd`.
- `src/quest.hpp` — device cart reader for the mhQuests QuestDef records.
- `tst/quests_test.hpp` (host, 67 asserts), `tst/fxdatatest/quests_test.hpp` +
  `test_quests.ino` (device E2E, 50 asserts), `tools/tests/test_gen_quests.py`
  (10 tests) + `fixtures/gen_quests/clean/`.
- `src/generated/quest_meta.hpp`, `fxdata/tables/quests.bin` (generated).

Changed:
- `src/core/save.hpp` — **SAVE_VERSION 2**, 15 B record: new `activeQuest` (0xFF
  none) + `progress` bytes at 9/10, tier moves to 11, checksum 14.
- `src/core/game.hpp` / `player.hpp` — `Game` gains `questTarget`/`questNeed`/
  `questProgress` (init -1/0/0 in `initGame`).
- `src/core/monster.hpp` — `damageMonster` death path counts a kill when
  `monsterKind == questTarget` (progress saturates at 255).
- `src/screen_state.hpp` — `COND_QUEST` (take rows -> takeable, turn rows ->
  ready) and TAKE/TURN_IN actions route through `quest_state.hpp`; turn-in pays
  the row cost.
- `tools/gen-screens.py` — `COND_QUEST` id + take/need-nibble validation;
  `tools/gen.sh`, `fxdata/fxdata.txt` (`raw_t mhQuests`),
  `tools/fxdata_manifest.py` (OUTPUT_PATHS), manifest fixture + its 2 count
  assertions, `tst/main.cpp`.
- `monhun-ardu.ino` — includes `src/quest.hpp`; `questApplyToGame()` arms the
  core counter from the active cart def at every menu start; hunt-end edge
  commits progress once (never mid-hunt).
- Generated set re-baked (adding `mhQuests` shifts the FX image; two-pass note);
  `make gen-check` confirms stability.

## Verification (exact)

1. `make gen` (repeated) + `make gen-check`:
   `fxdata_manifest: PASS (57 generated artifacts unchanged)`; header copy check
   passed; `gen-quests: 3 quests, 26 B blob (magic 0x5153 version 1)`.
2. `make test`: `Total Passed: 3297 / Total Failed: 0`.
   Baseline per dispatch = 3230; the new host suite adds exactly **+67**.
   `make test-tools`: `Ran 118 tests ... OK` (baseline 101; +10 gen_quests,
   +3 gen_screens quest-condition, +4 manifest? = +17).
3. `make fxtest-headless` (full): all **12** suites PASS —
   assets 262, audio 14, boot 4, combat 184, data 221, hud 17, menu 59,
   parity 660, perf 5, player_art 111, **quests 50**, **screens 67**; 0 failures.
   Perf identical to baseline:
   `B pUs=6375 pHz=156 lHz=52 lTk=488 rMx=4968 rAv=4756 ram=608`
   (baseline `rMx=4968 rAv=4756 pUs=6375`; hunt path untouched).
4. `make build` + `make size`:
   `Sketch uses 25728 bytes (86%)` / `Global variables use 1853 bytes`.
   `size: .text=25670 .data=58 .bss=1795`
   `size: flash=25728/29696 (3968 free)  ram=1853/2560`.
5. `node tools/gen-parity-fixtures.js` -> `git diff --stat --
   tst/fxdatatest/parity_fixtures.hpp` empty (byte-identical; 20 scenes / 1269
   ticks / 32 snapshots).

## Budget (measured, whole-image)

| | flash | RAM |
|---|---|---|
| baseline (HEAD 04f4a76, screens qs.1) | 25250 | 1847 |
| qs.2 | **25728** | **1853** |
| delta | **+478** | **+6** |

3968 B flash free. No data fact flipped; no gameplay/parity change (hashState
never reads the new fields and questTarget is -1 by default). RAM +6 = SaveBlock
+2 (wire) + Game +3 + alignment.

## Deviations / notes

- **Board entry is qs.4**: the quests screen has no menu/hub route yet (the
  framework's action enum has no "open screen"); tests `screenEnter` it directly
  and the hub still has its qs.1 "TAKE QUEST" row (now backed by the active-quest
  model). The title->hub B route and full HUNT/QUESTS/SMITH hub menu remain qs.4.
- **One active quest at a time**: `activeQuest` + `progress` are singular; taking
  a second quest while one is active is rejected by the takeable guard, matching
  the design's save sketch. `unlockFlag` is validated/emitted and `questUnlocked`
  is host-tested (0 = always; else the 1-based prior quest's done bit), but all
  three starters are 0 so no runtime branch is consumed yet.
- **Need duplication**: the board `param` carries `(need << 4) | questId` and
  `cost` the reward so the generic row condition/action needs no cart access on
  the host; the QuestDef table remains the kill-accounting/reward source. The
  shipped board is authored to match the three defs (device suite asserts
  `turn.cost == d.reward` and the need nibble).
- Save v2 rejects v1 EEPROM records (safe defaults) — one-time reset on upgrade.
- No `/tmp` test code; tests are co-located (`tst/`, `tst/fxdatatest/`,
  `tools/tests/`) and permanent. Generated set must be staged together (`git add
  -A`).
