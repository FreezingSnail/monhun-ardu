# monhun-ardu-nch.9 — bull: stomp + gore kit, zones, hooves collide (7vr)

Status: **PASS** (not committed; orchestrator commits)

## What changed

- `data/creatures/sweep.json`: replaced the inherited `lunge` + `sweep` with the
  owner-authored `stomp` (windup 36 / active 10 / recover 44, dmg 9, `move none`,
  `facing track`, window 24x14 @ ox 10 oy 2) and `gore` (windup 46 / active 12 /
  recover 55, dmg 14, lunge speedF 34, `facing lock-at-windup`, windows 16x10 @
  ox 16 oy -2 t0-6 then 20x14 @ ox 12 oy 2 t7-12). Profile `keepDist` 18 +
  `faceHold` 10. Zones head/horns (ox 17, oy -4, 12x10, mul 130, hp 40, share 100,
  SLASH, stagger 12) and appendage/hooves (ox 4, oy 12, 20x10, mul 150, hp 60,
  share 40, SLASH, stagger 30; broken `{dmgMul 200, hurtOn false, cue part_break,
  disableAttacks ["stomp"]}`). `collide` `(1, 14, 26, 8)`. Patterns p_stomp
  (`maxDist 24`) then p_gore (`minDist 24`, `maxDist 255`, `hpBand [0,100]`).
- `mock/game.js`: `MONSTER_ATTACKS` gains `stomp`/`gore` (windows path,
  `reach/hw/hh` mirror window 0 for the fixture hash); legacy `lunge`/`sweep`
  entries kept. `MONSTER_DEFS[1]` adds `keepDist 18`, `faceHold 10`, hooves
  `collide`. `MONSTER_ZONES.sweep` gains the head + appendage records.
  `chooseAttack` sweep branch `dist <= 24 ? stomp : gore`, with a
  `monsterAttackDisabled` fallback (mirrors the chicken branch).
- `mock/game.test.js`: permanent nch.9 tests (a) stomp <= 24 / gore 25..41,
  (b) gore lock-at-windup commits facing, (c) gore trample window catches after
  the horns miss, (d) broken hooves skip the disabled stomp, (e) hooves collide
  blocks while the head zone routes horn hits. Retitled the variant test.
- `docs/creature-framework.md`: bull collide-table row updated + "Bull attack
  kit" subsection. `README.md`: SWEEP roster row.
- `tools/gen-parity-fixtures.js`: `MODO` gains `stomp:1, gore:0` (move-type fold);
  `monster_sweep_hit` now preloads `MONSTER_ATTACKS.stomp` (single-window; the
  device no longer ships the legacy sweep record).
- `tst/fxdatatest/parity_test.hpp`: attack override mapping
  `kind == MK_LUNGE ? ATTACK_SWEEP_GORE : ATTACK_SWEEP_STOMP`.
- Regenerated `src/generated/combat_{data,expect,meta}.hpp`, `fxdata/**`,
  `src/fxdata.h`. Blob: SIZE 970 -> 1019, ZONES 9 -> 11, WINDOWS 12 -> 13,
  PATTERNS/GUARDS/STEPS 8 -> 9. New symbolic names
  `ATTACK_SWEEP_STOMP/GORE`, `WINDOW_SWEEP_STOMP_0`/`WINDOW_SWEEP_GORE_0/1`,
  `PATTERN_/GUARD_/STEP_SWEEP_P_STOMP|GORE`, `ZONE_SWEEP_HEAD/APPENDAGE`.
- Host/device tests updated with symbolic constants only: `tst/combat_test.hpp`
  (counts, bull collide, profile, guard bands, bull zone records,
  `combatAttackDisabled`), `tst/monster_test.hpp` (chooseAttack + synced hurt
  box), `tst/fxdatatest/combat_test.hpp` (bull zones/attacks/patterns),
  `tst/combat_pack_test.hpp` (MetaRecord list + spot check).

## Verification (exact tails)

`node --test mock/game.test.js`:

```
ℹ tests 76
ℹ pass 76
ℹ fail 0
```

`node tools/gen-parity-fixtures.js` (re-run determinism; `git diff --stat` still
shows the intended scene move vs HEAD `0f01caf`):

```
wrote tst/fxdatatest/parity_fixtures.hpp
scenes=20 ticks=1269 snapshots=32 cpFields=20
sha256 4e0a86d15e8ca8e1161b0acf4ec23f58408770d748e6ef3947835fc716db0d4c (identical after re-run)
 tst/fxdatatest/parity_fixtures.hpp | 4 ++--
```

`make gen-check` (second consecutive run; first run hit the known snapshot race
noted in nch.8 and was re-run):

```
fxdata_manifest: fxdata/manifest.json up to date (43 images, 59 inputs, 17 outputs)
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (69 generated artifacts unchanged)
```

`make test`:

```
Total Passed: 5171
Total Failed: 0
```

`make fxtest-headless FXTEST_ONLY=test_combat`:

```
=== test_combat ===
combat_test PASSED=289 FAILED=0
P
test_combat: PASS
```

`make fxtest-headless FXTEST_ONLY=test_parity`:

```
=== test_parity ===
parity_test PASSED=660 FAILED=0
P
test_parity: PASS
```

Extra (full device gate, all suites): `make fxtest-headless` exit 0 —
asset 260, audio 17, boot 4, combat 289, data 368, hub 57, hud 17, menu_art 81,
menu 78, monster_art 53, parity 660, perf 5, player_art 111, quests 50,
screens 78, smith 66; all FAILED=0.

`make size`:

```
size: .text=27002 .data=40 .bss=1719
size: flash=27042/29696 (2654 free)  ram=1759/2560
size: data facts: HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false HAS_GUARD_HP:false HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true HAS_HIT_STAGGER:false HAS_MULTI_STEP:false HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false HAS_STAGGER:true HAS_STEP_AFTER:false HAS_STEP_CHANCE:false HAS_WAIT_STEPS:false HAS_ZONES:true
```

## Parity changed-scene list

Per-scene hash comparison HEAD `0f01caf` -> regen (all 20 scenes):

- **scene 12 `monster_sweep_hit`** — all 10 ticks changed (hashOff 387).
- No other scene moved (0/10, 0/16, ... unchanged).

## Size / budget

- Flash **unchanged**: 27042/29696 (2654 free); RAM unchanged 1759/2560 (801 free).
- **No `HAS_*` fact flipped** (`HAS_MULTI_WINDOW`/`HAS_ZONES`/`HAS_GUARD_ZONES`
  were already true).
- FX cart blob +49 B (`combat.bin` 970 -> 1019; `fxdata-data.bin`/`fxdata.bin`
  177116 -> 177165), rides the cart, not MCU flash.

## Deviations / notes

- The design's parity note ("`monster_sweep_hit` loads `sweep` directly",
  "def 1 collide affects pushApart there") cannot hold once `sweep.json` replaces
  its own records: the device no longer has a legacy sweep record, and the mock
  creature stays def 0 (chicken) exactly as in nch.7. The scene therefore loads
  the bull's single-window `stomp` and the C++ override maps MODO kind 1 ->
  `ATTACK_SWEEP_STOMP` (kind 0 -> the two-window `ATTACK_SWEEP_GORE`). Multi-window
  gore cannot be a preload parity scene because the fixture hash reads the attack's
  static `reach/hw/hh` while the device hashes the live cached window; stomp is
  single-window, so the two agree.
- Design item (d) says "broken hooves force stomp at gore range", but the
  owner-authored zone data disables `stomp` (`broken.disableAttacks: ["stomp"]`).
  Implemented the data exactly; the coherent behavior (and the permanent test) is
  that broken hooves **skip the disabled stomp** and fall back to gore.
- First `make gen-check` after `make gen` reported stale fxdata/equip artifacts
  (the nch.8-documented snapshot race); a second consecutive `make gen` +
  `make gen-check` is byte-identical and passes.
