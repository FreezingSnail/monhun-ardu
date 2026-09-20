# monhun-ardu-feel.10 — data: heavy kit — BLOCKED at generator ABI

Status: **BLOCKED** (bead left OPEN). No commit made. Working tree has the
drafted `data/creatures/heavy.json` only; `make gen` fails on it.

## Exact failure

```
$ ./tools/gen.sh            # make gen
gen-art: wrote 35 block sheets ... OK
gen-fxtables: ... OK
gen-combat: error: data: unlockMask overflows u8 for sweep appendage
gen-combat: FAIL (1 error)
make: *** [gen] Error 1
```

Root cause: `tools/gen-combat.py::zone_unlock_mask` stores a zone's
`broken.disableAttacks` list as a **u8 bitmask keyed by the attack's global
index** (`unlockMask (bit per global attack index)`, gen-combat.py:35 and
839-843; runtime `combatAttackDisabled` returns false for `attackIdx >= 8`).
Adding the required 11th attack (`tail_slam`, heavy's 3rd) shifts every
following attack's global index:

| idx | attack |
|---|---|
| 0 | HEAVY_BITE |
| 1 | HEAVY_TAIL_SPIN (heavy appendage disables this) |
| 2 | HEAVY_TAIL_SLAM **(new)** |
| 3 | LUNGE_PECK |
| 4 | LUNGE_LEAP (lunge appendage disables this) |
| 5 | LUNGE_WING_BEAT |
| 6 | RAVAGER_BITE |
| 7 | RAVAGER_TAIL_SWEEP (ravager appendage disables this) |
| 8 | SWEEP_STOMP (sweep appendage disables this) → `1<<8 = 256 > 255` |

The four shipped `disableAttacks` targets are tail_spin, leap, tail_sweep and
stomp. Three land at ≤7; `sweep.stomp` is unavoidably ≥8 (three creatures with
attacks precede `sweep` in the id-sorted creature list: heavy 3 + lunge 3 +
ravager 2 = 8). No ordering inside `data/creatures/heavy.json` can move it, and
moving attacks between creatures is not expressible. **There is no data-only
fix scoped to heavy.json.**

## Drafted data (ready; `data/creatures/heavy.json`, uncommitted)

Implements everything except it cannot generate because of the above:

- `stats.spd` 3 → 5.
- `profile.faceHold` 10 → 8.
- `bite`: `tell: line` added (timings/dmg/speedF/window unchanged).
- `tail_spin`: windup 42→34, active 20→18, recover 55→62, `tell: arc`; the 4
  contiguous lock-away windows re-timed to active 18 and widened +4 px on their
  long axis:
  - w0 `t[0,5]  box(-20,0,28,16)`
  - w1 `t[6,10] box(0,-22,16,28)`
  - w2 `t[11,14] box(22,0,28,16)`
  - w3 `t[15,18] box(0,22,16,28)`
- `tail_slam` (new, heavy's 3rd attack): windup 26 / active 8 / recover 44 /
  dmg 12, `move {type: hop, dx: -56, dy: 0}` (3.5 px/tick backward over 8 active
  ticks = 28 px pounce toward the hunter, who is behind the frozen facing),
  `facing: lock-at-windup`, `tell: ring`, window `t[0,8] box(-16,0,36,28)`
  (behind/around the 40x28 body; covers the hunter for the whole 20..41 px
  decision band).
- patterns, source order: `p_tail_slam` first
  `{facing: behind, minDist: 20, maxDist: 60}`, then `p_bite_spin`
  `{maxDist: 20}` = `bite after 12` → `WAIT 16` → `tail_spin`, then `p_spin`
  `{minDist: 21, maxDist: 30}`, then `p_bite` `{minDist: 31, maxDist: 255}`.
  Bands are exclusive; `tail_spin` stays reachable ≤30 (21..30) and `bite`
  >30 (31+); the close combo (0..20) opens with `bite` so a broken tail still
  fights.

## Options to unblock (need an orchestrator decision)

**A. Widen `unlockMask` u8 → u16 (faithful, global-index semantics unchanged).**
- `tools/gen-combat.py`: `SIZES["ZONE"] 12→13`, emit `u16(unlock)`, meta struct.
- `src/core/combat.hpp`: `CombatZone`/`PkZone` last field `u16`; `CombatZoneCache`
  +1 B (11→12); `CombatState` 92→94; `combatAttackDisabled` bit u16, guard
  `attackIdx >= 16`; remove the 0..7 comment/check.
- Generated section offsets shift (11 zones × 1 B); `combat_expect` regen.
- Tests: `combat_pack_test` zone decode `b8`→`b16` at zone+11;
  `fxdatatest kZones` sweep mask needs `uint16_t` (256); tool fixture zone byte
  expectations grow one byte.
- Measured cost unknown until built (~2 B RAM, small flash). Budget event.

**B. Re-base `unlockMask` to a per-creature attack ordinal (no ABI size change).**
- Generator stores `1 << local_ordinal`; add a per-creature ≤8-attack check.
- Core: cache the creature's `firstAttack` (CombatState 92→93) and compute
  `local = attackIdx - firstAttack` in `combatAttackDisabled` (underflow → false).
- Behavior-identical for every shipped fight, but existing mask *values* change
  (lunge leap 8→2, ravager tail_sweep 64→2, sweep stomp 128→1; heavy tail_spin
  stays 2). Host/device assertions and possibly new generated unlock constants.

**C. Data-only workaround (not recommended): remove the bull's stomp disable and
express it as a `zonesBroken`-guarded gore pattern in `sweep.json`.** Unblocks
gen with no engine change, but edits the closed feel.9 kit (out of scope here),
changes the broken-hooves response from step-skip to guard-swap, and reduces
coverage of the `unlockMask` path (heavy/lunge/ravager still cover it).

## Recommended

If the intent is to keep the global-index ABI, take **A** (smallest semantic
churn: no mask values change, `1u << ATTACK_*` stays valid in tests). **B** is
smaller in RAM/ABI but changes documented mask semantics. **C** avoids engine
work but touches another kit.

## Tree state

- HEAD `32c9aa5`, clean except `data/creatures/heavy.json` (the draft above).
- `make gen` currently fails with the error above; `make gen-check`,
  `make test`, `make test-tools`, `make fxtest-headless` and `make size` were
  **not** run against a failing generation.
- No commit / push.
