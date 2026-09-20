# monhun-ardu-feel.18 — input: double-tap roll is universal

HEAD at start: `87afd41` (feel.17), clean tree. No commit/push (orchestrator
commits). Base flash `28614/29696 (1082 free)`, RAM `1743/2560`.

## What changed

- `src/core/player.hpp`
  - New `startDodgeRoll(Game&, int16_t dx, int16_t dy)`: facing set to `(dx,dy)`,
    `PS_DODGE`, `t 16`, `iT 14`, `vx/vy = dir*54>>4`, `stam 14`, `exitStance`;
    returns false (no state touched) when stamina is short.
  - New `tapDefenseReady(const Player&, const WeaponDef*)`: the early gate
    (no new move while `PS_DODGE/DEFLECT/SHOVE/STUN/SPECIAL`; out of an attack
    only when `weaponCanCancel`). Shared by the B tap and the double-tap detector.
  - `tapDefense`: gate now via `tapDefenseReady`; sheathed and sword branches
    both call `startDodgeRoll` (byte-identical B-tap behavior). Flail/gun
    branches unchanged.
  - Double-tap detector: `trySheathe` first (B held + Down, feel.17); otherwise
    `tapDefenseReady(p, def) && startDodgeRoll(g, dir8X(dNow), dir8Y(dNow))` for
    every weapon and while sheathed.
- `tst/player_test.hpp`
  - Rewrote the feel.16 suite: double-tap E -> `PS_DODGE`, `iT 14`, `t 15`,
    cost 14 for sword/flail/gun; added tapped-direction + facing assertions for
    flail (south) and gun (west); added a B-tap-specific test (sword dodge /
    flail deflect / gun shove).
  - feel.17 sheathe suites unchanged and green.
- `README.md` — controls row: d-pad double-tap = dodge roll (all weapons,
  sheathed too).
- `docs/feel-design.md` — feel.16 input line updated to universal roll; B tap
  stays weapon-specific.

## Verification (tails)

- `make gen-check` → `fxdata_manifest: PASS (82 generated artifacts unchanged)`
- `make test` → `Total Passed: 6568 / Total Failed: 0`
- `make test-tools` → `Ran 202 tests ... OK`
- `make fxtest-headless` (full, all 17 suites) → every suite `FAILED=0`;
  `perf_test PASSED=5 FAILED=0`,
  `B pUs=6370 pHz=156 lHz=52 lTk=456 rMx=4772 rAv=4593 ram=582`.
- `make size` → `flash=28720/29696 (976 free)  ram=1743/2560`,
  `.text=28680 .data=40 .bss=1703`.

## Size delta

- Flash `28614 → 28720` = **+106 B**, free `1082 → 976`. Within budget.
- RAM `1743 → 1743` = **+0 B**.
- The helper split (`tapDefenseReady` + `startDodgeRoll` as real out-of-line
  functions) adds call/return scaffolding and splits the old fused branch; net
  +106 B. No data fact flips.

## Notes

- Down is DIR8 index 2; comparison stays inline as before.
- `SHEATHE_ENABLED` still folds the stow call out of the parity image; host
  suite covers the stow path.
