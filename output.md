# monhun-ardu-nch.4 — longtail: reachable spin + turn commitment (flank)

Report. One implement-verify loop. No commit/push (orchestrator commits).

## What changed

- **Data-driven turn commitment (`faceHold`).** Optional creature profile field
  `faceHold` (u8 ticks, default 0). `0` recomputes the tracked facing every tick
  (shipped lunge/sweep unchanged); `>0` refreshes the facing only every
  `faceHold` ticks via `Monster.faceT` (set to `faceHold` on refresh, decremented
  per tick, refresh at 0). `dist`/`di` still recomputed every tick; lock modes
  (windup/attack of a lock attack) still freeze facing.
  - `tools/gen-combat.py`: validates 0..255, emits the byte after `zoneFlags`
    (`PROFILE_SIZE` 22 -> 23), plus `PROFILE_<CID>_FACE_HOLD` in
    `combat_expect.hpp`.
  - `src/core/game.hpp` `CombatProfile` + `Monster.faceT`; `src/core/combat.hpp`
    `PkProfile`/host decode; `src/core/monster.hpp` cadence in `updateMonster`
    and `initMonster` init.
  - `mock/game.js` mirrors with per-def `faceHold` (default 0 for shipped kinds);
    heavy `faceHold: 10`.
- **HEAVY**: `data/creatures/heavy.json` `keepDist` 24 -> 12, `faceHold` 10;
  `p_spin` guard `maxDist` 24 -> 30; `p_bite` guard `minDist` 25 -> 30.
  Mock mirrors: heavy `keepDist: 12`, `spinDist: 30`.
- **Mock heavy tail zone**: `MONSTER_ZONES.heavy.appendage` mirrors
  `heavy.json` (ox -24, 24x16, mul 150, share 40, SLASH, stagger 30) so the
  from-behind tail hit is testable; `zoneHitResolve`/`zoneContains` exported.
- Registry of generated artifacts changes because `combat.bin` grows 962 -> 970 B
  inside the single FX image (all later sheet offsets shift +8).

## Probe — heavy tail_spin reachability (2000 ticks, hunter pressing in)

| probe | before (keepDist 24 / spin<=24 / faceHold 0) | after (keepDist 12 / spin<=30 / faceHold 10) |
|---|---|---|
| free chase from spawn, hunter holds `mx` toward beast | 0 tailSpin / 12 bite | 6 tailSpin / 4 bite |
| band hold: hunter held 18 px east, pressing in | 9 tailSpin / 0 bite | 9 tailSpin / 0 bite |

The pre-nch.4 beast backed out to `keepDist` 24 during a free chase and only bit;
with `keepDist` 12 it stays in the 12..30 band and the spin repeats. The band
hold shows 9 spins / 2000 with zero bites (no retreat out of the band). Both are
permanent mock tests (`mock/game.test.js`).

## Tests added / updated (permanent, co-located)

- `tools/tests/test_gen_combat.py`: faceHold optional/default byte, emit + expect
  pin, range reject; profile byte vectors updated to 23 B.
- `tst/combat_test.hpp`: profile decode `faceHold`; heavy faceHold 10 / keepDist
  12 / lunge 0; heavy guard band 30/31.
- `tst/monster_test.hpp`: heavy spin at <=30 / bite at 31; new faceHold cadence +
  flank test (facing stale for the full hold, from-behind hit lands
  `COMBAT_ZONE_APPENDAGE`, refresh W rotates the tail back to body).
- `tst/combat_pack_test.hpp`: profile packed bytes incl. faceHold byte 10, u16
  offsets shifted +1; heavy bite minDist 30.
- `tst/fxdatatest/combat_test.hpp`: heavy cached keepDist 12 + faceHold pin;
  guard 30/31.
- `mock/game.test.js`: heavy ranges 30/31; faceHold cadence + tail hit; two
  spin-frequency probes.
- `docs/creature-framework.md`: profile fields (`faceHold`), facing cadence,
  heavy selection band.

## Verification (exact)

1. `make gen` x2 + `make gen-check` -> exit 0, `fxdata_manifest: PASS (68
   generated artifacts unchanged)`. Parity regen
   (`node tools/gen-parity-fixtures.js`) -> empty diff (`scenes=20 ticks=1269
   snapshots=32 cpFields=20`; `git diff` clean).
2. `make test` -> **Total Passed: 4788, Total Failed: 0**.
3. `make test-tools` -> **Ran 143 tests ... OK**.
4. `node --test mock/game.test.js` -> **tests 41 / pass 41 / fail 0**.
5. Device (Ardens): `test_parity` **660/0**; `test_combat` **235/0**;
   `test_data` **221/0**; `test_monster_art` **32/0**.
6. `make size` -> before **flash 25444/29696 (4252 free), RAM 1744/2560**;
   after **flash 25500/29696 (4196 free), RAM 1746/2560** (+56 B flash,
   +2 B RAM). Data facts unchanged (`HAS_*` set identical).

No float; no `/tmp`; tests permanent and co-located; generated files only via
`make gen`.
