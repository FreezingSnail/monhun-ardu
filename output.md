# monhun-ardu-arm.3 — armor effects: defense + skills in combat

Status: DONE. Shipping flash +420 B (29170 → 29590/29696, 106 free), RAM +6 B
(1631 → 1637/2560). Repo left dirty on purpose (orchestrator commits); no
commit/push.

## What changed

- `src/armor_state.hpp`: `ArmorSkill` (kind/maxPoints/perPoint view),
  `ArmorEffects { defense u16, hpMax, stamMax, dmgMul, iT }`,
  `armorEffectsBase()` (identity), `armorSkillBonus()` (active tier ->
  `min(points,maxPoints)*perPoint`), `armorEffects()` (resolves the aggregation
  + skill table by kind), `armorReduce()` (`dmg*100/(100+def)`, floor 1, int32
  denom), `armorRestoreStats()` (arms live hp/stam maxes, tops up, clamps 255).
- `src/upgrade_state.hpp`: `attackMulFold(base, smithMul, armorMul)` — the
  ATTACK_UP percent folded after the smith tier mul, truncating each step.
- `src/core/game.hpp`: `Game::armorFx` (`ArmorEffects`) appended after
  `armorHead`; `initGame` seeds the identity block.
- `src/core/player.hpp`: `playerHurt` reduces damage with
  `g.armorFx.defense` **before** the guard/parry branches; `startDodgeRoll`
  i-frames = `14 + g.armorFx.iT`; melee + whirl damage use `attackMulFold`.
- `src/core/projectiles.hpp`: shell spawn damage uses `attackMulFold`.
- `src/armor.hpp`: `armorReadSkill`/skills loop, `armorEffects` into
  `g.armorFx`, `armorRestoreStats` on the live player, all in
  `armorApplyToGame` (unchanged call sites: hunt start / camp return).
- `docs/equipment-framework.md`: effects table + arm.3 section; bead list.
- Tests: `tst/armor_effect_test.hpp` (new, registered in `tst/main.cpp`);
  `tst/fxdatatest/smith_test.hpp` device pins.

Interfaces: `Game::armorFx: ArmorEffects`; `armorEffects(agg, skills, out)`;
`armorReduce(dmg, def)`; `armorRestoreStats(fx, hp, hpMax, stam, stamMax)`;
`attackMulFold(base, smithMul, armorMul)`.

## Tests (permanent, native)

Host (`tst/armor_effect_test.hpp`): defense 0/50/100 exact ints, floor 1, absurd
def, zero damage; empty-armor identity; ATTACK_UP 100/120/130 + inert; fold
`9×125×130 → 14`, `17×110×120 → 21`; HEALTH/STAMINA +15; clamp at 255;
DEFENSE_UP `10+30=40`; EVADE_WINDOW +15; S/M thresholds through
`armorAggregate` (9 inert → 100, 9+6 M → 130); restore/top-up/clamp; live
`playerHurt` def 0/50/65535 no-op/reduction/floor; guard chip 95 hp + 78 stam
and hp never underflows; dodge i-frames 14 → 19.

Device (`tst/fxdatatest/smith_test.hpp`): after `armorApplyToGame` with the
crafted helm — `armorFx.defense 10`, `dmgMul/hpMax/stamMax 100`, `iT 0`, live
`player.hpMax/hp 100`; after unequip `armorFx.defense 0`.

## Gate tails

- `make gen-check`: `fxdata_manifest: PASS (90 generated artifacts unchanged)`
  (no generated files changed).
- `make test`: `Total Passed: 6244  Total Failed: 0`.
- `make test-tools`: `Ran 300 tests in 16.729s  OK`.
- `make fxtest-headless`: all suites PASS (combat 237, data 343, hub 63, hud 25,
  items 35, menu_art 53, menu 60, monster_art 111, player_art 120, quests 50,
  screens 85, smith 113, tell 14, zones 80; parity excluded as frozen).
  `test_perf`: `B pUs=6347 pHz=157 lHz=52 lTk=524 rMx=3348 rAv=3075 ram=683`
  `PASSED=5 FAILED=0`.
- `make size`: `size: flash=29590/29696 (106 free)  ram=1637/2560`; data facts
  unchanged (HAS_CARVE/HAS_ZONES/HAS_STAGGER/... same as arm.2).

Delta vs arm.2 baseline (29170 flash, 1631 RAM): **+420 B flash, +6 B RAM**
(within the ≤450 B target; fits with 106 B headroom).
