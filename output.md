# monhun-ardu-feel.14 — engine: turn-rate-limited facing

Worker ledger. No commit/push (orchestrator commits). HEAD `0071bd7` at start.

## What changed

- `CombatProfile` grows a u8 `turnRate` (DIR8 steps per facing refresh; `0` =
  legacy snap). Added to the packed profile record (byte 11, between `faceHold`
  and the six u16 timers) and both loader mirrors.
- FaceHold refresh logic in `updateMonster` now steps the cached DIR8 facing
  toward the player's desired index by at most `turnRate` steps along the
  shortest arc (mod 8). `turnRate == 0` still takes the direct-assign path, so
  every shipped profile (all 0) is byte-identical to before. The `faceHold`
  cadence, `faceHold == 0` per-tick path, enrage, and lock/lock-away freezing
  are untouched.

## Files

| Path | Change |
|---|---|
| `tools/gen-combat.py` | `PROFILE` 23→24; `turnRate` schema (0..8, integer-only), optional/default 0; pack after `faceHold`; host struct field; `PROFILE_<ID>_TURN_RATE` expect pins; `--dump` profile line; `HAS_TURN_RATE` fact |
| `src/core/game.hpp` | `CombatProfile.turnRate`; cache comment 23→24 B |
| `src/core/combat.hpp` | `PkProfile.turnRate`; host `combatProfileRead` field; `CombatState` 94→95 B assert; cache-budget comment |
| `src/core/monster.hpp` | turn-rate-limited refresh in the facing block |
| `tst/monster_test.hpp` | snap / 45-deg step / wrap both ways / no-op / faceHold 0 / lock freeze tests |
| `tst/combat_test.hpp`, `tst/combat_pack_test.hpp`, `tst/fxdatatest/combat_test.hpp` | profile field round-trip, size/offset sync, device cache pin |
| `tools/tests/test_gen_combat.py` | schema + emit + fact tests; updated profile payload bytes |
| `docs/feel-design.md` | `profile.turnRate` data-verb row |
| `src/generated/*`, `fxdata/*` | regenerated (blob 1301→1309 B; later raw_t sections +8) |

## Interface

- `CombatProfile::turnRate` (u8), packed profile byte 11, `PROFILE_SIZE` 24.
- `combat_expect::PROFILE_<ID>_TURN_RATE`.
- `combat::HAS_TURN_RATE` fact emitted (false today). Not used to fold: host
  tests drive synthetic `turnRate` values before any kit authors one, so the
  stepping path must stay compiled. Noted per bead.
- No public function signature changes.

## Gates (tails)

```
make gen (x2) && make gen-check
gen-combat: src/generated/combat_meta.hpp (unchanged)
...
fxdata_manifest: PASS (82 generated artifacts unchanged)

make test
Total Passed: 6501
Total Failed: 0

make test-tools
Ran 202 tests in 11.313s
OK

make fxtest-headless
17 suites PASS, exit=0 (test_assets audio boot combat data hub hud menu_art
menu monster_art perf player_art quests screens smith tell zones)
test_perf: B pUs=6369 pHz=157 lHz=52 lTk=456 rMx=4772 rAv=4593 ram=581
perf_test PASSED=5 FAILED=0

make size
size: flash=28720/29696 (976 free)  ram=1744/2560
size: .text=28680 .data=40 .bss=1704
size: data facts: ... HAS_TURN_RATE:false ...
```

## Budget

- Baseline `28610/29696 (1086 free)`, RAM `1743/2560`.
- After: `28720/29696 (976 free)`, RAM `1744/2560` → **+110 B flash, +1 B RAM**.
- Within the <=200 B target. +1 B RAM is the cached `turnRate` byte. The +8 B
  blob/later-section shift is record-size growth, not code.

## Notes / deviation

- `HAS_TURN_RATE` is emitted but deliberately not folded (see Interface); if
  feel.15 authoring turn rates wants the fold, the interpreter branch would
  need a testable seam first.
