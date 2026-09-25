# monhun-ardu-q0o — hurt-entry rect = body ∪ zones (not the collide box)

Owner bug: sword visibly overlapped the chicken's drawn body but no damage
landed. Root cause: `syncMonsterTarget` set `Game::target.rect` to the authored
`collide` box (chicken legs 9x9, bull hooves), so the melee/whirl entry gate
only connected on the legs. Core fix (orchestrator, already in tree): added
`monsterZoneRect` + `monsterHurtRect` in `src/core/monster.hpp`;
`syncMonsterTarget` now uses `monsterHurtRect`. Worker scope = tests + docs +
Makefile comment + verification only. Core files untouched by the worker.

## Files changed (worker)

- `tst/monster_test.hpp`
  - Bull init pin (line ~150): target rect is now the hurt-union rect, not the
    collide rect. Asserts origin `m.x/m.y`, union `w = m.w` (28), `h = 23`, plus
    containment of the body box and every present (mirrored) zone rect.
  - New `Melee0(g)` helper: rebuilds the player combo hit-1 melee rect from
    `WEAPON_DEFS` + `attackReach/Hw/Hh`, exactly as `player.hpp` does.
  - T1 chicken body-only melee (legs clear): melee overlaps body, clear of
    `monsterCollideRect`; hp drops (9, no crit).
  - T2 heavy tail-only melee (east): overlaps the east appendage zone, clear of
    the body; hp drops.
  - T3 heavy tail-only melee (west mirror, default fx -16): pins
    `monsterZoneRect` against `ZONE_CELL_W - ox - w`; hp drops.
  - T4 negative: out-of-reach swing leaves hp untouched.
- `tst/world_test.hpp` — activeTarget pin updated from the 9x9 legs rect to the
  32x24 body hurt rect.
- `tst/fxdatatest/combat_test.hpp` — HEAVY target-rect device pin: body+zone
  union 56x28 at the body origin (spawn fx -16 mirror), not the collide box.
- `docs/creature-framework.md` — "Demo monster collide + hurt boxes" notes:
  collide box is collision-only (pushApart); the player hit entry is the hurt
  rect (body + zones) via `target.rect`; owner-bug note; hit resolve stays
  point-based.
- `Makefile` — dev-hitboxes comment refreshed to the post-fix numbers.

## Gate numbers

| command | result |
|---|---|
| `make test` | Total Passed 6830, Failed 0 |
| `make test-tools` | Ran 388 tests, OK |
| `FXTEST_ONLY="test_combat" make fxtest-headless` | combat_test PASSED=252 FAILED=0 |
| `make size` | flash 29316/29696 (380 free), ram 1814/2560 |
| `ARDENS=/usr/bin/true make dev-hitboxes` | exit 0, flash 29408/29696 (288 free), ram 1814/2560 |

No gen/gen-check (no data change). No commit/push.

## Wall time

~35 min (single worker; host-test iteration dominated, ~6 `make test` runs).
