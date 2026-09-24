# monhun-ardu-ryh.1 — fix: re-enable the player-move push rule in shipping

## What changed

Owner bug: "roll pushes monster". Root cause (already diagnosed in the bead):
prg.11 carved the per-tick player-move flag out of the shipping build
(`MH_PUSH_MOVE` default 0), so `g.playerMoved` was never set and `pushApart`
fell back to the pre-fix give-way rule — any moving hunter (walk, dodge roll,
shove) could shove the beast. The host suites forced the flag on via
`TEST_FLAGS`, so they never covered the shipped default.

Fix: flip the default to 1 (the default is the fix), and stop forcing it in the
host build so the suites exercise the real default.

- `src/core/game.hpp`: `MH_PUSH_MOVE` default `0 -> 1`; carve comment rewritten
  (it is no longer a shipping-0 carve — test_parity carves it explicitly).
- `Makefile`: header comment updated (MH_PUSH_MOVE is no longer a prg.11
  carve); dropped the now-redundant `-DMH_PUSH_MOVE=1` from `TEST_FLAGS` so the
  host suite covers the shipping default instead of an override. The other three
  carves (`MH_STAGE3`/`MH_ROLL_ALT`/`MH_B_BRANCH_BUFFER`) are untouched.
- `tst/monster_test.hpp`: added the reported regression — a double-tap dodge
  roll into a parked beast must not move the beast (sampled every tick across
  the whole roll) and must displace the hunter. The walking case is untouched
  and stays green.
- `tst/fxdatatest/combat_test.hpp`: added a shipping-flag guard —
  `static_assert(PUSH_MOVE_ENABLED, ...)` plus a reported runtime
  `expectEq(PUSH_MOVE_ENABLED ? 1 : 0, 1, "push-move shipping on")` in
  `test_combat`, so the default cannot silently regress.
- Docs corrected where they still claimed the carve was active:
  `README.md` (shipping note), `docs/feel-design.md` (prg.11 trim table row).

`tst/fxdatatest/test_parity.ino` (`#define MH_PUSH_MOVE 0`) is left alone.

## Regression proof (test actually bites)

Host build with the flag forced off
(`make test TEST_FLAGS="... -DMH_PUSH_MOVE=0"`): `Total Passed: 6793 /
Total Failed: 2` — the walking case and the new roll case both fail, confirming
the new test detects the exact shipping regression. Default build: 6795/0.

## Gate (exact tails)

`make gen-check`:
```
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (162 generated artifacts unchanged)
```

`make test`:
```
Total Passed: 6795
Total Failed: 0
```

`FXTEST_ONLY=test_combat make fxtest-headless`:
```
combat_test PASSED=252 FAILED=0
test_combat: PASS
```

`FXTEST_ONLY=test_monster_art make fxtest-headless`:
```
test_monster_art PASSED=180 FAILED=0
test_monster_art: PASS
```

`make size`:
```
size: .text=29420 .data=50 .bss=1764
size: flash=29470/29696 (226 free)  ram=1814/2560
```

`make size-line`:
```
size: flash=29470/29696 (226 free)  ram=1814/2560
```

## Size delta

| | before | after | delta |
|---|---|---|---|
| flash | 29352/29696 (344 free) | 29470/29696 (226 free) | **+118 B** |
| RAM | 1814/2560 | 1814/2560 | +0 B |

Matches the bead's measured +118 B exactly. Headroom 226 free > the ~150 B floor
(AGENTS.md wave rule) — no trim needed.

## Notes

- No `-DMH_PUSH_MOVE=1` added to `SIZE_FLAGS`; the flipped default is the fix.
- `MH_STAGE3`/`MH_ROLL_ALT`/`MH_B_BRANCH_BUFFER` carves left as-is.
- clang-format clean on all changed files.
- No commit/push (orchestrator owns the wave commit).
