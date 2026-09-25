# monhun-ardu-dzr — chicken attack-chain pacing (restAfter/restT)

## Result: DONE (all gates green)

Chain-gated stationary rest, data-driven by two new optional profile bytes.
Chicken (lunge) authors `restAfter 2` / `restT 110` (~2.1 s at 52 Hz); every
other creature packs 0/0 = disabled, so its FSM path is byte-identical.

## Files

Source
- `tools/gen-combat.py` — `PROFILE` 24 -> 26; `PROFILE_OPTIONAL` +=
  restAfter/restT; `read_int 0..255 default 0`; `zero_profile` 0/0; pack emits
  `u8(restAfter), u8(restT)` after `staggerRecoverT`; host `Profile` struct +
  initializer (18 -> 20 fields); `PROFILE_<CID>_REST_AFTER/_REST_T` expect pins;
  `--dump` prints `rest%d/%d`.
- `data/creatures/lunge.json` — `"restAfter": 2, "restT": 110`.
- `src/core/combat.hpp` — `detail::PkProfile` gets the two bytes (packed ABI);
  host `combatProfileRead` maps them; cache-budget / mirror comments; the
  `CombatState` size pin 110 -> 112 (profile grew 2 B).
- `src/core/game.hpp` — `CombatProfile` gets `restAfter, restT` (20 fields);
  `Monster` gets `uint8_t chain` (completed attacks since the last rest).
- `src/core/monster.hpp` — `initMonster` sets `m.chain = 0`; `MS_ATTACK`
  completion: `++chain`, and when `restAfter != 0 && chain >= restAfter` ->
  `chain = 0; state = MS_IDLE; m.t = restT; m.cd = 0;` else the shipped
  `MS_PURSUE` release (`cdBase + tick%jitter` + circle flip). `MS_IDLE` expiry
  is untouched (`state = MS_PURSUE`, no cd write), so spawn IDLE keeps spawnCd
  and a rest resumes into PURSUE with cd 0.

Tests (permanent, co-located, generated symbols only)
- `tst/monster_test.hpp` — new "dzr: chicken rests stationary after restAfter
  attacks, then resumes": parks two peck attacks through `updateMonster`
  (`monsterAttackSet` + `MS_ATTACK`), asserts pursue/chain 1 after attack 1,
  idle/chain 0/`t == restT`/`cd 0` after attack 2, x/y frozen over 20
  in-range idle ticks, PURSUE with cd 0 at expiry, and bull restAfter 0.
- `tst/combat_pack_test.hpp` — profile blob offsets `o+24`/`o+25`; `PROFILE_SIZE
  26`; chicken 2/110 + disabled-kit 0 pins (generated `combat_expect` symbols).
- `tst/combat_test.hpp` — profile accessor loop + chicken restAfter/restT pins.
- `tst/fxdatatest/combat_test.hpp` — device cache pins for chicken restAfter/restT.
- `tools/tests/test_gen_combat.py` — `PROFILE_SIZE 26`; profile byte tuples
  24 -> 26; new default/emit/range/dump test for restAfter/restT; expect pins.

Docs
- `docs/creature-framework.md` — profile field list + a chain-gated-rest bullet
  (0 = disabled).
- `docs/feel-design.md` — chicken profile line: restAfter 2 / restT 110.

Generated (`make gen`, never hand-edited): `fxdata/tables/combat.bin` 1257 ->
1267 B (+2 B x 5 profiles), `fxdata/fxdata-data.bin` +10 B, `src/fxdata.h` /
`fxdata/fxdata.h`, `combat_data.hpp`, `combat_meta.hpp`, `combat_expect.hpp`,
`manifest.json`. The +10 B combat section shifts every later cart offset by 10,
so `art_sheets.hpp`, `equip_meta.hpp`, `zone_meta.hpp` and the equip/cards/
screens table blobs also regenerate (expected, same sizes).

## Size (shipping, vs baseline 29118/29696, 578 free)

```
size: .text=29132 .data=50 .bss=1767
size: flash=29182/29696 (514 free)  ram=1817/2560
```
Flash **+64 B**, free 514 (>= 150 required). RAM 1817/2560 (+2 profile bytes
+1 chain byte). dev-hitboxes: flash 29276/29696 (420 free), ram 1817/2560.

## Gate tails

- `make gen-check` -> `fxdata_manifest: PASS (164 generated artifacts unchanged)`;
  `cmp fxdata/fxdata.h src/fxdata.h` clean.
- `make test` -> `Total Passed: 6877  Total Failed: 0`.
- `make test-tools` -> `Ran 390 tests in 23.262s  OK`.
- `FXTEST_ONLY="test_combat" make fxtest-headless` -> `combat_test PASSED=254 FAILED=0` / `test_combat: PASS`.
- `make size` -> above.
- `ARDENS=/usr/bin/true make dev-hitboxes` -> builds, 29276/29696 (420 free).

## Notes / deviations

- **"does not move or turn"**: the MS_IDLE switch case performs no locomotion
  (the punish window). The generic per-tick facing tracker still runs before the
  switch, so the beast keeps facing the hunter while parked -- same as the
  existing spawn IDLE. The frozen design's FSM delta lists no facing gate, and
  gating it would change spawn-IDLE facing, so it is left unchanged.
- Cascade handled: `CombatState` size pin 110 -> 112 and the stale profile
  cache-budget/mirror comments were updated; the +10 B cart shift regenerates
  the dependent tables exactly as expected.
- Not run per the bead: the full device gate and parity (parity is legacy
  diagnostics, not a gate per AGENTS.md).

## Wall time

~10 min (worker: recon ~3.5 min; implement+regen ~1.5 min; host/tools/device
gates + 3 Arduino builds ~5 min).
