# monhun-ardu-arm.4 — data: two skills per armor piece + stackable points

Status: DONE (with one flagged design deviation, §4). Cart data + pins + tooling;
the only code change is the EVADE_WINDOW i-frame cap in `armorEffects`. Shipping
flash is flat within LTO noise: **29520 → 29518/29696 (178 free, −2 B), RAM
1637/2560 (+0)**. Repo left dirty on purpose (orchestrator commits); no
commit/push.

## What changed

- `data/armor.json`: every piece now carries 2 skills (recipes/defense/resist/
  sheets unchanged):
  - `hunter_helm` attack_up 6 + defense_up 4
  - `hunter_mail` attack_up 6 + health_up 4
  - `bone_cap` health_up 6 + stamina_up 4
  - `bone_mail` stamina_up 6 + defense_up 4
  - `evade_charm` evade_window 6 + attack_up 4
- `src/armor_state.hpp`: new `ARMOR_EVADE_IT_CAP = 4`; `KIND_EVADE_WINDOW`
  clamps the resolved `iT` bonus to the cap (`bonus > cap ? cap : bonus`). Data
  keeps `perPoint` 1.
- `tools/gen-armor.py`:
  - cross-check relaxed from `total > maxPoints` to `total > 255` (the pre-clamp
    u8 accumulator capacity). The runtime clamps totals to `THRESHOLD_M`, so
    `attack_up` 16 is authored data that clamps to 15/M — over-grant impossible.
  - `--dump` now prints a representative best-stack loadout per skill with the
    clamped total + tier, e.g.
    `loadout attack_up: hunter_helm 6 + hunter_mail 6 + evade_charm 4 = 16 -> 15 tier 2 (M)`.
- Generated set regenerated together: `fxdata/tables/armor.bin` (same 113 B,
  new content), `src/generated/armor_{data,expect}.hpp`, `fxdata/fxdata{,-data}.bin`,
  `fxdata/manifest.json`. `src/generated/armor_meta.hpp` unchanged (ABI offsets
  and ids are stable).
- `docs/equipment-framework.md`: 2-skill JSON example, threshold-rule text
  (u8-capacity check + runtime clamp), EVADE_WINDOW cap row, and a new
  "Skill allocation + activation (arm.4)" table with the loadout tiers.

Interfaces: no ABI/sim symbols added. `mh::ARMOR_EVADE_IT_CAP` (uint8 = 4) is
new; `armorEffects()` signature unchanged; `gen-armor --dump` gains `loadout`
lines; `armor_expect::ARMOR_*_SKILL1[_POINTS]` now nonzero for every piece.

## 1. Representative loadouts (`gen-armor --dump` / host pins)

| loadout | skill | points | tier |
|---|---|---|---|
| hunter_helm + hunter_mail | attack_up | 12 | S (1) |
| + evade_charm | attack_up | 16 → clamped 15 | M (2) |
| bone_cap + hunter_mail | health_up | 10 | S (1) |
| bone_mail + bone_cap | stamina_up | 10 | S (1) |
| hunter_helm + bone_mail | defense_up | 8 | inert (0) |
| evade_charm | evade_window | 6 | inert (0) |

Host pins in `tst/armor_engine_test.hpp` (new `equipPieces` helper) assert all
of the above plus the wrong-slot-save guard. `tst/armor_test.hpp` pins both
skill slots per piece; `tst/armor_effect_test.hpp` pins the evade cap (15→4,
10→4, 3→3) and `ARMOR_EVADE_IT_CAP == 4`.

## 2. Evade rescale: cap in `armorEffects` (chosen)

`perPoint` is a straight multiplier, so lowering it cannot make +15 sublinear
(`15 * perPoint`). Capping the resolved bonus in `armorEffects` is the only
change that bounds M on the 14-tick base roll, and it also guards a hand-built
skill table. Whole-image delta is **−2 B** (flat/noise), so the cap is free.
Device dodge path is unchanged: `startDodgeRoll` still adds `armorFx.iT`, and
the existing live test injects `iT = 5` directly (uncapped there), so only the
`armorEffects` resolver is bounded.

## 3. Pins updated (coverage grew, none weakened)

- `tools/tests/test_gen_armor.py`: clean fixture extended to 2 skills/piece
  (exercises `skillCount = 2` packing); blob-layout + meta pins updated; `--dump`
  test asserts the loadout/tier lines; the old `> maxPoints` rejection test is
  replaced by a clamp-accepted test (16 → M) plus a `> 255` u8-capacity
  rejection test. `make test-tools`: **301 tests, OK** (was 300).
- Host: `make test` **6369 passed / 0 failed** (was 6347): +6 loadout/threshold
  pins and +16 table pins.
- Device: `tst/fxdatatest/smith_test.hpp` helm pins 3 → 6 attack_up points and a
  new defense_up 4/inert pin (cart read path). `test_smith` **115 PASS** (was 113).
- `tst/fxdatatest/parity_fixtures.hpp` and `mock/` untouched; parity remained
  excluded from the gate.

## 4. FLAGGED DEVIATION — `defense_up` / `evade_window` cannot reach S

The dispatched task asked the host suite to assert `mail+helm → defense S` and
`charm → evade S`. Under the authored per-piece points and `thresholds.s = 10`
those are arithmetically impossible:

- `defense_up` is granted by exactly two pieces: `hunter_helm 4` + `bone_mail 4`
  = **8 < 10**. Best legal stack is 8 → tier 0.
- `evade_window` is granted by exactly one piece: `evade_charm 6` = **6 < 10**.
  Best stack is 6 → tier 0.

The data was implemented exactly as the DESIGN lists (4 and 6), so the host tests
pin the true totals (8/6, both inert) with comments; no test asserts a false S.
To make those two criteria hold, the minimum balance-only change (no code) is:

- `hunter_helm` defense_up 4 → **6** (then helm + bone_mail = 10 → S), and
- `evade_charm` evade_window 6 → **10** (then charm alone = 10 → S; the iT cap
  still holds it at +4).

That is a data rebalance, not a code fix, so it was left to the orchestrator
rather than silently changing the DESIGN's numbers. `attack_up` (16→15 M),
`health_up` (10 S) and `stamina_up` (10 S) activate as specified.

## Gate tails

- `make gen` (run twice): `gen-armor: 5 pieces, 5 skills, 113 B blob` then all
  four armor outputs `(unchanged)`; second run deterministic.
- `make gen-check`: `fxdata_manifest: PASS (91 generated artifacts unchanged)`.
- `make test` (host): `Total Passed: 6369  Total Failed: 0`.
- `make test-tools`: `Ran 301 tests in 17.401s  OK`.
- `make fxtest-headless` (full, parity excluded): all PASS — asset 270, audio
  10, boot 4, combat 237, data 343, hub 63, hud 25, items 35, menu_art 53,
  menu 60, monster_art 127, player_art 120, quests 50, screens 85, smith 115,
  tell 18, zones 80.
  `test_perf`: `B pUs=6347 pHz=157 lHz=52 lTk=524 rMx=3300 rAv=3027 ram=687`
  `PASSED=5 FAILED=0`.
- `make size`: `size: flash=29518/29696 (178 free)  ram=1637/2560`;
  `.text=29498 .data=20 .bss=1617`. Data facts unchanged.

Budget delta vs baseline (29520 flash, 1637 RAM): **−2 B flash, +0 B RAM**
(LTO noise; the cap folds). Cart image unchanged in size (208640 B); the
113-byte armor blob content changed in place.
