# monhun-ardu-feel.17 — input: sheathe = hold B + double-tap Down

HEAD at start: `23d30bc` (feel.16), clean tree. No commit/push (orchestrator
commits). Base flash `28796/29696 (900 free)`, RAM `1747/2560`.

## What changed

- `src/core/player.hpp`
  - Deleted the A+B chord machinery: `sheatheCombo()` and the whole chord/seq
    block in `updatePlayer` (`sheatheConsumed` plumbing removed from the
    B-branch and A handlers).
  - `trySheathe()` kept as-is minus the dead `seqT`/`seq2` resets; still requires
    `PS_IDLE`, drops an active stance, sets `sheathed` + `sheatheLatch`.
  - In the feel.16 double-tap detector: on the second same-dir edge, if
    `SHEATHE_ENABLED && inp.b && dNow == 2` (Down) it calls `trySheathe(p)`;
    otherwise (or if the stow is refused, e.g. non-idle) it calls
    `tapDefense(g, def, inp)` as before. No B-hold threshold — `inp.b` at the
    second tap is enough.
  - `Player::init` no longer zeroes the four removed fields.
- `src/core/game.hpp`
  - Removed `SHEATHE_SEQ_WIN` and `CHORD_WIN`; `SHEATHE_ENABLED` comment updated
    to the new input (carve unchanged, test_parity still defines `MH_SHEATHE 0`).
  - Removed `seqT`/`seq2`/`chordT`/`pMy` from `Player`; field comment updated.
    `sheathed`/`sheatheLatch` semantics unchanged.
- `tst/player_test.hpp`: `stowWeapon()` now holds B and double-taps Down (ends
  B held). Renamed/updated the two old chord tests and added feel.17 tests:
  stow from idle; stow from an active stance (stance dropped, latch set);
  double-tap Down without B still rolls south; B-held double-tap E/Up does not
  stow (rolls instead); B release after stow does not re-enter stance or roll.
- `README.md` in-game table: new `B held + double-tap Down` sheathe row.
- `docs/feel-design.md`: added the feel.17 sheathe input paragraph.
- `tst/fxdatatest/test_parity.ino`: carve comment updated to the new input.

## Verification (tails)

- `make gen-check` → `fxdata_manifest: PASS (82 generated artifacts unchanged)`
- `make test` → `Total Passed: 6550 / Total Failed: 0`; sheathe feel.17 cases
  all green, feel.16 double-tap suite unchanged and green.
- `make test-tools` → `Ran 202 tests ... OK`
- `make fxtest-headless` → every suite PASS; `test_perf: PASS` line
  `B pUs=6370 pHz=156 lHz=52 lTk=456 rMx=4772 rAv=4593 ram=582`.
- `make size` → `flash=28614/29696 (1082 free)  ram=1743/2560`.

## Size delta

- Flash **−182 B** (`28796 → 28614`), free **900 → 1082**.
- RAM **−4 B** (`1747 → 1743`) from the four removed `Player` fields.
- Expected ≤0 after dead-code removal; measured −182 B.

## Notes

- `SHEATHE_ENABLED` still folds the stow call out of the parity image (its
  scenes never stow); parity hash only reads `sheathed`, always false there, so
  fixtures are unaffected and no regen was needed.
- Down is DIR8 index 2, compared inline with a comment (no new constant).
