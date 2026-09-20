# Boss feel design + attack contract

This records the **Wave-B feel revision** (epic `monhun-ardu-feel`): why the
demo duel read as "roll on the dot, then combo spam", what the three demo kits
are now, and the contract every new attack or kit must satisfy before it lands.
It is the companion to the render/data checklist in `docs/dev-flow.md` and the
schema reference in `docs/creature-framework.md`.

Sources of truth: `src/core/monster.hpp` (FSM, facing, `wallStun`, enrage, hop),
`src/core/combat.hpp` (guards, attack fields, windows), `src/render.hpp` +
`src/render_math.hpp` (tell shapes), and `data/creatures/*.json` (the kits).
Every number in the tables is the current JSON, verified against
`python3 tools/gen-combat.py --dump`; regenerate the blob after any edit
(`make gen`) and review the sheet (`python3 tools/contact_sheet.py`) before
flashing.

---

## 1. Diagnosis — the pre-revision feel problems

| # | Problem | Pre-revision symptom | Revision answer |
|---|---|---|---|
| 1 | **Beast speed vs hunter** | Beast `spd` 3–7 (1/16 px/tick; heavy 3 at the start of the wave) against a hunter who walks 7–14 and runs 24 while stowed. Attacks did not move the beast, so kiting was free and the fight never closed. | Attacks are the mobility. `move.type lunge/charge/hop` closes distance, `keepDist` holds a threat band, and the hop commits a face-relative vector. |
| 2 | **Track re-aim** | Every attack recomputed its facing vector every tick, so a sidestep was erased the next tick and there was no flank to punish. | `facing: lock-at-windup` freezes the windup vector; `lock-away` turns the back to the hunter; `profile.faceHold > 0` refreshes a *tracked* heading only every N ticks. All three make the flank reachable. |
| 3 | **2x2 tell** | A single 2x2 shade-2 dot at the window centre. It said "an attack is coming", not *where*. | Per-attack `tell` shapes (`dot/line/arc/ring/zone`) drawn from the cached hit window. The tell draws the area the attack will cover. |
| 4 | **Metronome cadence** | Shared `cdBase 55`, single-ATK patterns, so every fight had the same pulse and the same one-beat answer. | Per-beast `cdBase`/`cdJitter` plus multi-step patterns with `after`, `wait`, `chance` and HP bands, so the pressure changes across the fight. |
| 5 | **Inert stagger** | `profile.staggerMax 0` on all three beasts — the stagger meter and `MS_STAGGER` were wired but dead, so head/leg pressure had no payoff. | Stagger is opt-in: chicken 60, bull 80 (heavy stays 0 to keep its identity). Hits feed `zone.staggerOnHit`; at threshold the pattern is cancelled into `MS_STAGGER`. feel.19 doubled the two meters for the longer hunt. |
| 6 | **Empty arena** | Room bounds silently clamped a charging beast; corners had no consequence. | `attack.wallStun`: a committed *moving* attack that clamps into a room bound self-stuns for the authored ticks, opening the bait-into-wall punish. |
| 7 | **Soft bodies** | Damage was one body roll-on-dot; head/tail zones had flat multipliers but all attacks tracked, so the flank was never actually reachable. | Behind guards, `faceHold`, locks and hop give positional routing; zone `dmgMul` rewards it and `disableAttacks` gives a break a consequence. |
| 8 | **Cheap roll** | Roll i-frames 14 of the 16 dodge ticks at 14 stamina, and it cancels into the combo — so roll beat every telegraph. | Player commit tune (bead `monhun-ardu-feel.11`, **still open**): i-frames 14→11, stamina 14→16, finisher recovery +2..4 ticks. Recorded here as the pending half of the revision. |

---

## 2. Design pillars

1. **Read the shape.** The windup telegraph draws the area the attack will
   cover, in the same geometry as the hit test. No invisible hitboxes.
2. **Space has an owner.** Movement attacks claim ground; `keepDist` and the
   circle band mean there is no free kiting. The corner is a weapon (`wallStun`).
3. **Commit or eat it.** A committed attack (locked facing, charge, hop) cannot
   re-aim; its recovery is the hunter's damage phase.
4. **Weak points are positions.** Head front, tail/legs flank. A zone is only
   worth hitting if the player can *get* to it — guards and face commitment are
   what make the position real.
5. **Phases change rules.** HP bands and zone breaks swap the pattern list,
   not just the numbers (enrage changes speed and turn commitment; a broken
   zone disables an attack).

---

## 3. Data verbs available to a kit

All are JSON fields compiled by `tools/gen-combat.py`; the `HAS_*` facts in
`src/generated/combat_meta.hpp` fold the unused machinery out of the image.

### Attack

| Field | Values | Semantics |
|---|---|---|
| `facing` | `track` | Recompute the facing unit vector from the player delta every tick (or every `faceHold` ticks). |
| | `lock-at-windup` | Freeze the tracked vector at windup entry; it holds through WINDUP + ATTACK. The hunter can step out of the frozen line. |
| | `lock-away` | Like `lock-at-windup`, but negate the vector once at windup entry: the beast turns its back so a behind window points at the hunter (heavy `tail_spin`). |
| `wallStun` | ticks (0 = off) | A committed moving attack that hits a room bound self-stuns for this many ticks. One-trigger: leaving `MS_ATTACK` latches it, so sustained wall contact cannot stack (`monster.hpp`). |
| `tell` | `dot` / `line` / `arc` / `ring` / `zone` | Windup telegraph shape. `dot` is the legacy 2x2 core; the others draw from the cached window (see below). |
| `move.type` | `none` | Stationary release. |
| | `lunge` + `speedF` | Committed forward velocity `fx*speedF>>4` for the active ticks. |
| | `charge` | Schema-reserved; not consumed by the shipping interpreter. |
| | `hop` + `dx`,`dy` | Face-relative (forward, lateral) velocity in 1/16 px/tick, rotated by the frozen facing once at release, applied for the active ticks. |
| `windows[]` | `t0,t1,box,dmgMul` | Inclusive 1-based hit window. The first window is cached at attack start; contiguous windows refresh in order (`monsterWindowNext`). |

### Pattern / guard

| Field | Semantics |
|---|---|
| steps `{atk, after, chance}` | `after` = PURSUE delay ticks *after* the step resolves before the next step. `chance` is a deterministic tick hash (`hash(tick, creature, pattern, step) % 100 < chance`); a failed step is skipped immediately, `after` still applies. |
| step `{wait, ticks}` | PURSUE pause of `ticks` (+ `after`). |
| guard `facing: behind` | Matches only when the player body centre is behind the beast's facing axis (`facingDot < 0`); `front` is `> 0`; dead-abeam (`0`) matches neither. Decision-time only, no cart read beyond the guard record. |
| guard `minDist/maxDist` | Inclusive integer pixel band. Patterns are evaluated in source order, first match wins. |
| guard `hpBand: [lo,hi]` | Inclusive creature HP percent band. |
| guard `zonesBroken` | All listed zones must be broken (mask compare). |

### Creature / zone

| Field | Semantics |
|---|---|
| `stats.enrage {hpPct, spdMul, faceHold, cue}` | One-shot at `hp*100 <= hpMax*hpPct`: multiply `spd` by `spdMul/100` (floor 1) and replace `profile.faceHold` with `faceHold`. `hpPct 0` disables. `cue` is stored but has no audio wiring yet. |
| `profile.faceHold` | If > 0, tracked facing refreshes only every N ticks, so the heading is committed between refreshes. `0` = every tick. Enrage can overwrite it. |
| `profile.turnRate` | `1..8` = max DIR8 steps the refreshed facing may rotate toward the player from its current heading (shortest arc, mod 8), so the beast commits to a direction and can be out-circled. `0` = snap straight to the player delta (legacy). feel.15 authors rates on the demo kits: chicken/bull/heavy 1, ravager 2. |
| `profile.staggerMax / staggerDecay / staggerRecoverT` | Stagger meter opt-in. `staggerMax 0` folds the whole meter out. Hits add `zone.staggerOnHit`; decay subtracts each tick; at threshold the active pattern is cancelled and the beast enters `MS_STAGGER` for `staggerRecoverT`. |
| `zones.head / appendage` | Face-relative box + `dmgMul` + pool. Highest multiplier wins (tie → body, head, appendage). A drained pool flips one broken bit per zone. |
| `zones.*.broken.disableAttacks[]` | Attacks disabled while the zone is broken (compiled to the zone's `unlockMask` bit per global attack index). |
| `zones.*.broken.hurtOn: false` | Broken zone is no longer a hurtbox (`brokenFlags` bit0); `dmgMul` override applies. |

### Tell shapes (`src/render_math.hpp`)

Drawn only in `MS_WINDUP` from `g.combat.attack.win.box` and the cached
`g.combat.attack.tell` (no cart read during paint). In `MS_ATTACK` the draw is
the unchanged 4x4 shade-3 marker at the window centre.

| `tell` | Draw |
|---|---|
| `dot` (0) | Legacy 2x2 shade-2 core at the window centre. No window needed. |
| `line` (1) | Three 2x2 dashes at 1/4, 2/4, 3/4 along the body-centre → window-centre ray. For lunge/charge reach. |
| `arc` (2) | Three 4x2 segments spanning the window rect width across its vertical middle (centre dropped 2 px). For sweeps/spins. |
| `ring` (3) | 1 px outline expanding from half-extent 2, ~1 px per 2 elapsed windup ticks, clamped to the window half-extent. For slams/AoE. |
| `zone` (4) | Static 1 px outline of the full window rect. Schema-reserved; no demo kit uses it yet. |

Player input verb (feel.16, universal feel.18): a d-pad double-tap fires the
dodge roll toward the tapped direction for every weapon (and sheathed) — the
same sword numbers, so the positional answer is available without reaching for
B. The B tap keeps its weapon-specific tap-defense (sword dodge, flail deflect,
gun shove).

Sheathe (feel.17): hold B and double-tap Down stows the weapon; A draws back
into combo hit 1. B is the stance modifier for every weapon, so the stow rides
the feel.16 double-tap detector instead of the old A+B chord.

Items + gathering (feel.22): a map prop with a `gather` record
(`gatherItem`/`gatherYield`, see `docs/map-zones.md`) is a node. While stowed, a
tap A inside an un-picked node runs `PS_GATHER` (~40 rooted ticks; move/damage
cancels) and on completion adds the yield to the inventory (`items[herb]`) and
picks the node for the rest of the hunt (`newGame` resets nodes + inventory;
`loadRoom` does not). A tap away from a node still draws the weapon. While
stowed, holding B `HOLD_TICKS` runs `PS_ITEM` (~40 rooted ticks) when a herb is
held, healing exactly 20 hp (clamped) and decrementing on completion; a B
release before the hold stays the stowed roll. A camp heal-rect B press wins on
the same press (the heal latches `bLocked`, so the hold cannot also eat).
Gather completion and eat fire `CUE_GATHER`/`CUE_EAT` through the existing audio
edge detector (inventory edge, no new sim event field).

---

## 4. Per-beast kit reference

All values from `data/creatures/*.json`, verified against
`python3 tools/gen-combat.py --dump` at the close of the epic. Boxes are
`(ox, oy, w, h)`, face-relative; timelines are ticks; `wu/act/rec` =
windup/active/recover. Window `t` ranges are inclusive, 1-based.

### Chicken — `data/creatures/lunge.json` (skeleton `chicken`)

**Stats:** w32 h24, hp1800, spd6, spawn (200,40), collide (9,11,12,13), no enrage.

**Zones**

| Zone | Box | dmgMul | HP | Share | Stagger | Break |
|---|---|---:|---:|---:|---:|---|
| head | (18, 0, 11, 7) | 130 | 160 | 100 | 12 | SLASH; broken dmgMul 130, hurtOn false |
| appendage | (9, 0, 9, 24) | 150 | 240 | 40 | 30 | SLASH; broken dmgMul 200, hurtOn false, disables `leap` |

**Profile:** engage 36 / keep 16 / attack 42; faceHold 6; turnRate 1; circle 8/10;
retreat 6/10; cdBase 48 + jitter 60; spawnT 90 / spawnCd 140; stunRecover 24;
staggerMax 60, decay 2, recover 30.

**Attacks**

| id | wu/act/rec | dmg | move | facing | tell | wallStun | windows | positional answer |
|---|---|---|---:|---|---|---:|---|---|
| `peck` | 18/6/26 | 7 | lunge(20) | track | dot | 0 | t[0,6] (14,-6,12,10) | sidestep / out-range (band ≤28) |
| `leap` | 30/12/52 | 13 | lunge(48) | lock-at-windup | line | 0 | t[0,10] (12,-2,18,16) | sidestep / roll-through / behind |
| `wing_beat` | 16/5/34 | 9 | none | track | arc | 0 | t[0,5] (-8,0,26,18) | out-range / roll-through |

**Patterns** (source order)

| id | guard | steps |
|---|---|---|
| `p_flank` | facing **behind**, maxDist 32 | `wing_beat` |
| `p_peck` | maxDist 28 | `peck` |
| `p_leap` | minDist 28..255, hp 51..100 | `leap` |
| `p_leap2` | minDist 28..255, hp 0..50 | `leap` after 10 → `leap` chance 70 |

### Bull — `data/creatures/sweep.json` (skeleton `bull`)

**Stats:** w28 h22, hp1500, spd7, spawn (200,40), collide (1,14,26,8).
**Enrage:** hpPct 40, spdMul 130, faceHold 6, cue 0 (spd 7 → 9, faceHold 10 → 6).

**Zones**

| Zone | Box | dmgMul | HP | Share | Stagger | Break |
|---|---|---:|---:|---:|---:|---|
| head | (17, -4, 12, 10) | 130 | 160 | 100 | 12 | SLASH; broken dmgMul 130, hurtOn false |
| appendage | (4, 12, 20, 10) | 150 | 240 | 40 | 30 | SLASH; broken dmgMul 200, hurtOn false, disables `stomp` |

**Profile:** engage 36 / keep 18 / attack 42; faceHold 10; turnRate 1; circle 8/10;
retreat 6/10; cdBase 55 + jitter 40; spawnT 90 / spawnCd 140; stunRecover 24;
staggerMax 80, decay 1, recover 24.

**Attacks**

| id | wu/act/rec | dmg | move | facing | tell | wallStun | windows | positional answer |
|---|---|---|---:|---|---|---:|---|---|
| `stomp` | 34/11/46 | 9 | none | track | ring | 0 | t[0,10] (0,0,36,26) | out-range (half-extents 18×13) |
| `gore` | 42/14/58 | 14 | lunge(40) | lock-at-windup | line | **70** | t[0,6] (16,-2,16,10); t[7,12] (12,2,20,14) | sidestep / roll-through / behind |
| `rear_kick` | 14/4/30 | 10 | none | track | arc | 0 | t[0,4] (-14,4,22,14) | out-range / roll-through |

**Patterns** (source order)

| id | guard | steps |
|---|---|---|
| `p_rear_kick` | facing **behind**, maxDist 30 | `rear_kick` |
| `p_gore2` | hp 0..40 | `gore` after 18 → `gore` chance 70 |
| `p_stomp` | maxDist 24 | `stomp` |
| `p_gore` | minDist 24..255, hp 41..100 | `gore` |

### Heavy — `data/creatures/heavy.json` (skeleton `longtail`)

**Stats:** w40 h28, hp2800, spd5, spawn (200,40), collide (-8,3,48,22), no enrage.

**Zone**

| Zone | Box | dmgMul | HP | Share | Stagger | Break |
|---|---|---:|---:|---:|---:|---|
| appendage | (-24, 0, 24, 16) | 150 | 240 | 40 | 30 | SLASH; broken dmgMul 200, hurtOn false, disables `tail_spin` |

**Profile:** engage 36 / keep 12 / attack 42; faceHold 10; turnRate 1; circle 8/10;
retreat 6/10; cdBase 55 + jitter 40; spawnT 90 / spawnCd 140; stunRecover 24;
staggerMax 0 (no meter), decay 0, recover 0.

**Attacks**

| id | wu/act/rec | dmg | move | facing | tell | wallStun | windows | positional answer |
|---|---|---|---:|---|---|---:|---|---|
| `bite` | 30/8/40 | 10 | lunge(26) | track | line | 0 | t[0,8] (14,0,18,14) | sidestep / behind / out-range |
| `tail_spin` | 34/18/62 | 8 | none | lock-away | arc | 0 | t[0,5] (-20,0,28,16); t[6,10] (0,-22,16,28); t[11,14] (22,0,28,16); t[15,18] (0,22,16,28) | out-range (long axis reaches ~34 from centre) / roll-through the seams |
| `tail_slam` | 26/8/44 | 12 | hop(-56,0) | lock-at-windup | ring | 0 | t[0,8] (-16,0,36,28) | sidestep / roll-through / behind |

`tail_slam`'s hop is face-relative: with the frozen away-facing of a
`p_tail_slam` flank, it releases backward at `-56/16 = 3.5` px/tick over 8
active ticks = **28 px toward the hunter**.

**Patterns** (source order)

| id | guard | steps |
|---|---|---|
| `p_tail_slam` | facing **behind**, minDist 16..64 | `tail_slam` |
| `p_bite_spin` | maxDist 20 | `bite` after 12 → `wait 16` → `tail_spin` |
| `p_spin` | minDist 21..30 | `tail_spin` |
| `p_bite` | minDist 31..255, hp 0..100 | `bite` |

The front bands are exclusive — `p_bite_spin` 0..20, `p_spin` 21..30,
`p_bite` 31..255 — so `tail_spin` stays a close threat and `bite` covers the
approach. Breaking the tail misses the `tail_spin` step (skipped by
`combatAttackDisabled`), but the combo still opens with `bite`, so a broken-tail
heavy keeps fighting.

---

## 5. The attack contract

Every new attack or kit must pass this list before the device. The render/data
checklist in `docs/dev-flow.md` is the mechanical half of the same review.

- [ ] **Tell shape matches the hit-test geometry.** `line` for a forward
  ray/lunge, `arc` for a sweep or spin, `ring` for an AoE slam, `zone` for a
  full-rect claim. The shape is drawn from the same `win.box` the hit test uses,
  so a shape that implies a wider area than the box is a lie.
- [ ] **Windup floor by damage class.** `dmg > 10` requires `windup ≥ 24`
  (leap 13/30, gore 14/42, tail_slam 12/26). `dmg 8..10` requires `windup ≥ 14`
  (stomp 9/34, heavy bite 10/30, wing_beat 9/16, rear_kick 10/14). `dmg ≤ 7`
  has no floor (peck 7/18). A fast heavy is a design bug.
- [ ] **Recovery floor for committed attacks.** A `lock-*` facing attack, or
  any attack with `dmg > 10`, carries `recover ≥ 40` ticks (leap 52, gore 58,
  tail_spin 62, tail_slam 44). The recovery is the hunter's damage phase; a
  committed attack with a short recovery has no price.
- [ ] **Every pattern has a punish window.** Each attack ends in `recover > 0`
  (minimum 26, peck); combo links each carry their own recovery, so a completed
  combo is punishable after every link. `after` delays run in PURSUE and are
  repositioning, **not** a punish window.
- [ ] **Every attack has a positional answer**: sidestep, roll-through,
  out-range, or being behind it. The per-attack tables above carry the answer
  column; an attack with no answer is a coin flip.
- [ ] **No stationary weak attack.** A stationary attack must own space — an
  area at least body-sized or a behind guard. There is no stationary
  small-reach jab. (`wing_beat` 26×18 behind, `stomp` 36×26, `tail_spin` full
  rotation.)
- [ ] **Telegraph == hit-test window.** `drawMonsterTell` and
  `monsterHitsPlayer` both read the cached `g.combat.attack.win.box`; the
  telegraph must never be a separate hardcoded rect. The `MS_ATTACK` marker is
  the shared 4x4 shade-3 core.
- [ ] **Review before device.** Run `python3 tools/contact_sheet.py` (per
  creature) and cross-check the numbers against
  `python3 tools/gen-combat.py --dump` before flashing. The sheet renders the
  JSON's own timings and window geometry, so a mistyped box or `t0/t1` is
  visible without a device.

---

## 6. Budget ledger

### feel.1 spike — per-item engine measurements

Whole-image deltas from `make size` (LTO; per-symbol math is meaningless),
baseline `26980/29696 flash (2716 free)`, `1732/2560 RAM`.

| Item | Flash delta | RAM delta | Note |
|---|---:|---:|---|
| behind guard (`facing` clause) | +160 B | +0 | `GUARD_SIZE` 8→9; evaluator clause |
| `wallStun` | +134 B | +1 B | `ATTACK_SIZE` 22→23; attack cache +1 |
| tell shapes DOT/LINE/ARC/RING | **+590 B** ⚠ | +1 B | over the 400 B flag; +148 µs worst-plane render |
| enrage (`hpPct/spdMul/faceHold/cue`) | +170 B | +5 B | `CREATURE_SIZE` 25→29; cue unwired |
| hop (`move.type hop`) | +82 B | +2 B | cheapest; `ATTACK_SIZE` unchanged |
| **Engine total** | **+1136 B** | **+9 B** | |

Pattern step facts (chicken combo data, RAM-neutral): `HAS_MULTI_STEP` +98,
`HAS_STEP_AFTER` +124, `HAS_STEP_CHANCE` +246, `HAS_WAIT_STEPS` +156;
individually 624 B, together **+306 B** because the steps share the generic
runner.

### Shipped image at the close of the epic (HEAD `4dade64` + feel.10 data)

```
make size
size: flash=28610/29696 (1086 free)  ram=1743/2560
size: .text=28570 .data=40 .bss=1703
```

Data facts now compiled in (each one is a budget event when it flips):

```
HAS_ENRAGE:true         HAS_GUARD_CHANCE:false   HAS_GUARD_COOLDOWN:false
HAS_GUARD_FACING:true   HAS_GUARD_HP:true        HAS_GUARD_PLAYER:false
HAS_GUARD_ZONES:true    HAS_HIT_STAGGER:false    HAS_MULTI_STEP:true
HAS_MULTI_WINDOW:true   HAS_SIMPLE_GUARDS:false  HAS_STAGGER:true
HAS_STEP_AFTER:true     HAS_STEP_CHANCE:true     HAS_WAIT_STEPS:true
HAS_ZONES:true
```

The wave spent ~1630 B over the 26980 baseline (2716 free → 1086 free):
+1136 B of engine, +306 B of generic step runner, and the rest in kit data and
the tell draw, against the +32 B measured in the final heavy-kit bead. Any
further kit growth should re-run `make size` and treat a `HAS_*` flip as the
same kind of event.

### feel.14/feel.15 turn data (HEAD `23097b1` + feel.15 values)

```
make size
size: flash=28720/29696 (976 free)  ram=1744/2560
size: .text=28680 .data=40 .bss=1704
```

feel.14 added the `profile.turnRate` byte (+8 B flash, +1 B RAM) and folded out
nothing: the stepping path stays compiled so host tests can drive synthetic
rates. feel.15 authors the rates as pure values (chicken 6/1, bull 10/1, heavy
10/1, ravager 8/2) and widens the three behind-guard bands; the packed records
keep their size, so this bead is **+0 B flash / +0 B RAM** over `23097b1`. The
only fact flip is `HAS_TURN_RATE false -> true` (generated for the ledger only,
not folded into the shipping path). No pattern is shadowed: the behind guards
sit ahead of the same-frontage bands in source order, and the non-behind bands
(`p_peck`/`p_leap`, `p_stomp`/`p_gore`, `p_bite_spin`/`p_spin`/`p_bite`) are
unchanged and still tile their ranges.

### Test scope

`test_parity` is **excluded from the default `fxtest-headless` gate**
(decision recorded on bead `monhun-ardu-feel.8`; `AGENTS.md`: the legacy parity
image is diagnostics, not a gate, and its fixtures are frozen). The default run
covers every maintained suite; parity stays on-demand only, and its fixtures
are not regenerated with kit data.
