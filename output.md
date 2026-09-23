# monhun-ardu-dlp.2 — hql.2 device: goal-kind accounting + gather hook + reward payout

Status: **DONE** (no commit/push — orchestrator owns the commit).

## What changed

- `src/core/monster.hpp` — kill accounting now gated on
  `g.questGoalKind == quests::GOAL_KILL` (include `generated/quest_meta.hpp`).
  A `GOAL_GATHER` quest never counts a kill; existing target-kind compare and
  255 clamp unchanged.
- `src/core/items.hpp` `applyGather()` — after `itemAdd`, when
  `g.questGoalKind == GOAL_GATHER && g.questTarget == slot`, adds
  `prop.gatherYield` to `g.questProgress` (clamped 255). Off-item nodes still
  gather but do not count; carves go through `carve.hpp`, never this path.
- `src/screen_state.hpp` — `ScreenRow` gains `uint8_t unlock` (0 = always).
  `COND_QUEST` take path = `questTakeable(save, quest) && questUnlocked(save,
  row.unlock)`; `ACTION_TAKE_QUEST` re-checks `questUnlocked` before
  `questTake` (mirrors the recipe re-check).
- `src/screens.hpp` `screenReadRow()` — for `COND_QUEST` rows reads the quest
  def (`src/quest.hpp`): take rows fill `row.unlock = def.unlockFlag`; turn-in
  rows fill `row.recipe[0] = {def.rewardItem, def.rewardCount}` and
  `row.cost = def.rewardZenny`. `row.unlock` zeroed for all other rows.
- `docs/quests-shops.md` — unlock field + gather/kill accounting wiring.

## Tests

- `tst/quests_test.hpp` — new: gather goal never counts a kill; gather
  completion counts yield with `GOAL_GATHER`; off-item gather no-op; kill goal
  ignores gather; 255 clamp from `254 + yield 2`; chain-unlock take row (cond
  false + action rejected while locked, then live after prior quest done);
  turn-in material reward via `row.recipe[0]`. `killBeast` now arms
  `questGoalKind = GOAL_KILL`; `questRow` zeroes `unlock`/recipe.
- `tst/screens_test.hpp` — `row()` zeroes `unlock`/recipe; new unlock-gated take
  row + turn-in material reward cases.
- `tst/fxdatatest/quests_test.hpp` — arms `questGoalKind` on the manual kill
  setups; asserts the board take row's `unlock` and the turn-in row's
  `recipe[0]` come from the def.
- `tst/smith_test.hpp`, `tst/app_state_test.hpp` — host `ScreenRow` helpers
  zero the new `unlock` + recipe fields (layout change made uninitialized
  `recipe` garbage visible).
- `tst/fxdatatest/smith_test.hpp` — **test-only stack fix (deviation)**: reuse
  the existing file-scope `static Game g_smith` instead of a second 650 B
  stack `Game ag`. The sim leaves ~845 B of stack after globals and this suite's
  frame already sat within 7 B of the limit; growing `ScreenRow` by 1 B tipped
  it. Proven: adding a 7-byte `volatile` pad to the pre-change suite reproduces
  the crash. Behavior of the test is unchanged (helpers re-init `g_smith`).

## Verification (makes)

Baseline (HEAD c0309ca): `size: flash=29098/29696 (598 free)  ram=1709/2560`
(`.text=29074 .data=24 .bss=1685`).

After this bead:
```
size: .text=29248 .data=24 .bss=1685
size: flash=29272/29696 (424 free)  ram=1709/2560
size: data facts: HAS_CARVE:true HAS_ENRAGE:true ... HAS_ZONES:true
```
**Delta for this bead: +174 flash (`+174 .text`, `.data`/`.bss` unchanged),
RAM unchanged.** Fits (424 free).

- `make test` → `Total Passed: 6378  Total Failed: 0`
- `make gen-check` → `fxdata_manifest: PASS (91 generated artifacts unchanged)`
- `FXTEST_ONLY=test_quests make fxtest-headless` → `test_quests PASSED=56 FAILED=0`
- `FXTEST_ONLY=test_hub make fxtest-headless` → `test_hub PASSED=63 FAILED=0`
- Full `make fxtest-headless` (final gate):
  ```
  asset_test PASSED=270 FAILED=0
  test_audio PASSED=9 FAILED=0
  test_boot PASSED=4 FAILED=0
  combat_test PASSED=237 FAILED=0
  data_test PASSED=348 FAILED=0
  test_hub PASSED=63 FAILED=0
  test_hud PASSED=29 FAILED=0
  test_items PASSED=35 FAILED=0
  test_menu_art PASSED=53 FAILED=0
  menu_test PASSED=60 FAILED=0
  test_monster_art PASSED=127 FAILED=0
  perf_test PASSED=5 FAILED=0
  test_player_art PASSED=120 FAILED=0
  test_quests PASSED=56 FAILED=0
  test_screens PASSED=85 FAILED=0
  test_smith PASSED=115 FAILED=0
  test_tell PASSED=18 FAILED=0
  zones_test PASSED=80 FAILED=0
  ```

## Blockers

None. Budget gate cleared. The only deviation is the test-only sim stack fix in
`tst/fxdatatest/smith_test.hpp` (documented above).
