# Creature Framework — design draft (v1)

> **Shipping note (monhun-ardu-cgk):** the generic parts/stages/elements/
> predicate schema described below was replaced in the shipping build by the
> fixed 3-hitzone model — body (implicit, wins ties) + head + appendage
> records, a single broken record per zone, `zonesBroken` guard mask, head
> hits feeding the stagger meter. See `build/zones-design.md` and
> `src/core/combat.hpp`. Sections below are the superseded reference for the
> generic model; keep them only for historical context.

Status: draft for review. No code filed yet. Companion spikes recorded in
`/var/folders/.../opencode/SPIKE-REPORT.md` (1a flash budget, 1b FX reads,
1c LUNGE parity proof).

Goal: a data-driven creature system — shared skeletons, per-creature attacks,
patterns and behavior — that scales to many fights without new engine code per
creature. Everything runtime stays integer/fixed-point (no floats).

Scope decisions already made:

- One creature active at a time.
- The 3 shipped beasts (LUNGE/SWEEP/HEAVY) stay behaviorally byte-identical;
  parity fixtures gate every migration step.
- Demo menu stays as-is (temporary); roster/menu scaling deferred.
- VM deferred; the data model is designed so a decision-layer VM is additive.

---

## 1. Layers

```
Skeleton  : reusable body shape (parts + anchors)
Parts     : hurtboxes, separate HP pools, breakable stages, multipliers
Attacks   : timers, movement, damage, phys/elem, hit windows
Patterns  : ordered steps + guards (combos, distance game, enrage)
Behavior  : native FSM profile (T1) + creature profile params
            (+ optional guard expr programs via VM later, T2)
```

Source of truth: JSON under `data/`. The mock loads the same JSON and mirrors
runtime semantics; parity fixtures are generated from the mock.

## 2. Budgets (spike-measured)

| Resource | Value |
|---|---|
| Shipping flash now | 28378 / 29696 B (1318 free) |
| Current hardcoded FSM | 2474 B `.text` (whole-build delta) |
| Interpreter + data ceiling | 3792 B (1318 + 2474) |
| Interpreter target | ≤ 600 B → >3 KB slack for verbs/data |
| Worst logic tick reads | 14 FX accesses ≈ 126 µs (13% of 988 µs tick) |
| Spawn burst | ~40 reads ≈ 360 µs, one-time |
| RAM caches | profile ~26 B + attack/window ~22 B ≈ 48 B of 610 free |

Reads only happen in `run()` between plane blits, never during paint. The
interpreter validates end-to-end (`.text` + perf gates), not by symbol math.

## 3. Types

```
DmgType (phys) : bits0..2 = SLASH | BLUNT | SHOT        (mask; break gating)
Element        : bits0..3 = NONE | FIRE | WATER | ICE | THUNDER | ... (16 slots)
Multipliers    : u8 percent, 100 = neutral (0..255)
Box            : { ox, oy, w, h } face-relative px; axis-aligned, DIR8 position
```

Physical and element layer independently (e.g. "blunt fire"): damage math applies
both. Elements ship inert in v1 (no effect) but are carried through data and
loaders so enabling them is a multiplier table lookup, no schema break.

Damage (integer, 32-bit intermediate, truncating division at each step):

```
out  = base * dmgMul / 100
out  = out * physMul(part, phys) / 100
out  = out * elemMul(part, elem) / 100
body += out * bodyShare / 100
```

## 4. Skeleton / parts (superseded by the 3-hitzone model)

Shipping schema is `"zones"` on the creature (head/appendage records; body
implicit); skeletons only carry the body box + anchors now. The `"parts"`
schema below is the retired generic draft.

```jsonc
// data/skeletons.json — all skeletons
{ "id": "quad_32x24",
  "parts": [
    { "id": "body", "box": { "ox": 0, "oy": 0, "w": 32, "h": 24 },
      "dmgMul": 100, "hp": 0, "bodyShare": 100, "breakTypes": 0,
      "hurtOn": true, "stages": [] }
  ],
  "anchors": [ { "id": "origin", "ox": 0, "oy": 0 },
               { "id": "head",   "ox": 22, "oy": 6 } ] }
```

```jsonc
// data/creatures/<id>.json — per-creature part overrides
{ "id": "tail",
  "box": { "ox": -14, "oy": 8, "w": 18, "h": 10 },
  "dmgMul": 150, "physMul": { "SLASH": 150, "BLUNT": 75 },
  "elemMul": { "FIRE": 200 },
  "hp": 60, "bodyShare": 40, "breakTypes": ["SLASH"], "hurtOn": true,
  "stages": [
    { "at": 30, "stagger": 30, "disableAttacks": ["tail_sweep"],
      "speedMul": 100, "cue": "part_break" },
    { "at": 0, "dmgMulOverride": 200, "hurtOn": false } ] }
```

- `hp: 0` = no pool (legacy body part). No parts at all = legacy single-hurtbox
  path (used by the shipped 3 during migration).
- Part boxes are face-relative **origins**: the world rect is the body anchor
  plus the DIR8 rotation of `(ox, oy)`, box `w x h` stays axis-aligned (the
  body box `{0,0,w,h}` is therefore the fixed body rect for every facing).
  Attack windows are face-relative **centres** (section 5) — the two conventions
  coexist because the body box is its own anchor.
- `breakTypes` gates break progress only; wrong-type damage still applies
  (physMul still scales it).
- Stages are ordered by `at` (remaining-hp thresholds); ship with 2, schema
  allows N.
- Stage effects catalog v1: `stagger`, `speedMul`, `dmgMulOverride`, `hurtOn`,
  `disableAttacks[]`, `enableAttacks[]`, `cue`.
- Overlap resolution: highest final multiplier wins; tie → lowest part id.
- Part multipliers are resolved natively at hit time (~2 FX reads), never in
  the per-tick interpreter loop.

## 5. Attacks

```jsonc
{ "id": "tail_sweep",
  "windup": 48, "active": 12, "recover": 60,
  "dmg": 9, "phys": "BLUNT", "elem": "NONE",
  "move": { "type": "none" },              // none | lunge | charge | hop
  "facing": "track",                        // track | lock-at-windup
  "windows": [ { "t0": 0, "t1": 3, "box": { "ox": 17, "oy": -6, "w": 24, "h": 10 }, "dmgMul": 100 } ],
  "onHit": { "effect": "trip", "push": 0, "stun": 0 },
  "stagger": 10,
  "cue": "windup" }
```

- Windows are the single source for hit tests, telegraph and debug wire.
- Window timing is inclusive `[t0, t1]` with `t` 1-based (incremented before
  tests), matching the current sim exactly.
- `move.type` semantics: `lunge speedF` (current), `charge speedF until window
  end`, `hop dx/dy`.

## 6. Patterns

```jsonc
{ "id": "p_lunge",
  "guard": { "minDist": 33, "maxDist": 255, "hpBand": [0, 100],
             "parts": { "tail": ">=1" }, "player": { "attacking": true },
             "cooldown": 0, "chance": 100 },
  "steps": [ { "atk": "lunge", "after": 0 },
             { "wait": 30, "after": 0 } ] }
```

- Pattern = ordered steps: `ATK {atk, after, chance}` | `WAIT {ticks, after}`.
  No GOTO in v1.
- `after` = delay after the step resolves; creature is in PURSUE during it, so
  combos reposition naturally.
- Selection: patterns evaluated **in list order, first matching guard wins**.
  Order is semantic.
- Guard clauses are inclusive integer distances (`minDist`..`maxDist`); mock
  splits `dist > split` → compile to `minDist = split+1`, `maxDist = split`.
- "No lunge" is expressed as pattern absence — never a `-1` sentinel.
- `chance`: deterministic, tick-derived — `hash(tick, creatureId, patternId,
  stepIdx) % 100 < chance`; fail = skip step immediately. No RNG state.
- Pattern end → decision state; profile cooldown applies.
- Escape hatch reserved: `"expr": "<programId>"` in a guard, evaluated at
  decision time only (T2 VM). Not implemented in v1.

## 7. Behavior (T1 native FSM)

States: `IDLE → PURSUE → WINDUP → ACTIVE → RECOVER`, plus `STUN`, `DEAD`.

Profile (per creature, RAM-cached, ~26 B):

```
engageDist, keepDist, attackDist, circleNum/Den, retreatNum/Den,
cdBase, cdJitter, spawnT, spawnCd, stunRecoverT, faceHold
```

- Decision tick: in PURSUE with `cd <= 0 && dist < attackDist`, evaluate
  patterns (first match) and run steps.
- Facing: recomputed every tick from player delta (as today); `facing: lock`
  freezes at windup start. `faceHold > 0` commits the tracked vector for that
  many ticks (the heavy's 10): the hunter can cross behind and hit the tail
  before the beast re-aims. `faceHold 0` keeps the every-tick default.
- Interrupts (native, order is observable and must stay verbatim):
  `hitFlash--` → dead return → face/dist recompute → stun check/return → FSM.
- `pushApart`, world clamp and deflect/parry stuns stay native — not
  pattern-expressible.
- Stagger meter: `profile.staggerMax` (0 = disabled → shipped 3 unchanged),
  `staggerDecay`, `staggerRecoverT`. Hits add attack `stagger` × part mods; on
  reaching max → cancel pattern → `STAGGER` for `staggerRecoverT`, reset meter.
  hitFlash alone never cancels a pattern; part-break stagger and stun cancel.

## 8. Pipeline (single FX image)

```
data/skeletons.json
data/creatures/<id>.json
        │  tools/gen-combat.py   (schema validate, id cross-refs, integer-only
        │                          quantized fields, deterministic packing)
        ├─► fxdata/tables/combat.bin        (packed blob; build intermediate)
        ├─► src/generated/combat_data.hpp   (host plain structs, tests/sim)
        ├─► src/generated/combat_meta.hpp   (VERSION, SIZE, record offsets)
        └─► src/generated/combat_expect.hpp (device byte-level expectations)

fxdata/fxdata.txt:  raw_t mhCombat = "tables/combat.bin"
make gen → fxdata-build.py → fxdata/fxdata.bin (THE single FX image) + src/fxdata.h
         └► fxdata/manifest.json (inputs/outputs + sha256) → make gen-check
```

- `combat.bin` is never flashed separately; it is a section of the one
  `fxdata/fxdata.bin` (same file the device tests load on `d1`).
- Offsets are compile-time (`mhCombat + combat::CREATURE_ravager`), zero
  runtime lookup cost. Content change ⇒ `make gen` + rebuild (`gen-check`
  enforces).
- Blob format: header (`magic u16`, `version u8`, `flags u8`, counts), then
  section arrays (skeletons, parts, stages, attacks, windows, patterns, guards,
  predicates, steps) with `u16` offsets stored in generated meta, little-endian,
  aligned 1. Rough sizes: attack ≈ 22 B + 12 B/window; part ≈ 20 B + 10 B/stage;
  creature ≈ 24 B + lists; 20 fights ≈ 2–4 KB.
- Multiplier fields are `u8` percent; if finer tuning is needed, permille `u16`
  via a version bump.

## 9. Runtime load model

- `newGame(creatureId)`: read creature record + profile → cache in RAM
  (~47 B total). Part stages live as 2 bits × N parts in the live state.
- Attack start: read attack scalars + current window into RAM (~22 B); window
  change during active costs ~2 reads; nothing per tick otherwise.
- Pattern decision: guard clauses read field-by-field (~4–8 reads per decision).
- All reads in `run()` (between plane blits); render consumes the same RAM
  scalars for the telegraph.

## 10. Testing

| Tier | Suite | Covers |
|---|---|---|
| Tools | `tools/tests/test_gen_combat.py` (Python unittest) | schema errors, id cross-refs, determinism, integer-only fields, size limits |
| Host C++ | `tst/combat_test.hpp` | real loader (host path) vs `combat_data.hpp`; guard eval; damage/stagger routing; part break → pattern swap |
| Host C++ pack parity | `tst/combat_pack_test.hpp` | reads `fxdata/tables/combat.bin`/packed image from disk; sha256 baseline; offsets land on the right records |
| Device (Ardens) | `tst/fxdatatest/test_combat.ino` + `combat_test.hpp` | header/version/size; per-record spot checks vs `combat_expect.hpp`; cross-refs; break-tail → pattern swap; bad-id fallback; reads inside FX/OLED bracket; final bare `P` |
| Manifest | `make gen-check` | regen byte-identical; hashes recorded |

Loader is production code (`src/core/combat.hpp`, host identity / AVR
`mhFxRead*`), shared verbatim by game, host tests and device tests — no
test-only copy.

## 11. Migration of the shipped beasts (byte-identical)

Reference values (current code):

```
skeleton quad_32x24: body {w32,h24, dmgMul100, hp0, bodyShare100, breakTypes0}
stats: hp200 spd5 spawn(200,40)
attacks: lunge {windup40, active10, recover55, speedF34, dmg12,
                window {ox12,oy0,w24,h22}}
         sweep {windup48, active12, recover60, move none, dmg9,
                window {ox17,oy0,w32,h24}}
profile: engage36 keep24 attack42 circle8/10 retreat6/10
         cdBase55 cdJitter40 spawnT90 spawnCd140 stunRecover24 lungeSplit32
patterns: p_lunge guard {minDist:33} → lunge;  p_sweep guard {maxDist:32} → sweep
variants: SWEEP lungeSplit pattern absent (always sweep); HEAVY lungeSplit24
```

Phases (each gated by `make test` + `fxtest-headless` parity 660/0):

- **A** — attacks become window data; interpreter reads them. Same behavior.
- **B** — body hurt/collide boxes come from the skeleton record. Same values.
- **C** — profile + patterns replace the hardcoded literals and FSM.
- **D** — new creatures on the same skeleton; then new skeletons/art.

## 12. Spike findings baked into this doc

1. No-lunge = pattern absence (schema fix).
2. Guards inclusive; strict `>` would break parity.
3. Pattern order is semantic.
4. Chance deterministic and tick-derived.
5. Window encoding `[t0,t1]`, `t` 1-based.
6. Interrupt order verbatim; `pushApart`/clamp/deflect-parry stuns native.
7. Part multipliers resolved natively at hit time.
8. Budget validated end-to-end (LTO makes symbol math meaningless).

## 13. Open items

- `hitOnce` per window, cancel windows, telegraph overrides — deferred.
- Pack-parity baseline management; blob version mismatch policy (hard fail vs
  fallback creature).
- Broken-part art path (gen-art code-mock vs hand PNG).
- VM revisit trigger (define the first fight that declarative guards cannot
  express).
- Docs/authoring ownership: agents draft JSON, owner tunes numbers.

## Demo monster collide + hurt boxes (owner note, 2026-09-18)

Each of the three demo monsters should carry its **own collide box and hurt
boxes**, authored per creature (not inherited as one generic body box):

| creature | collide box | hurt zones |
|---|---|---|
| chicken (lunge) | legs only — player walks under the raised body | head + legs (appendage) — **done** (monhun-ardu-76y) |
| bull (sweep) | legs/hooves region `(ox 1, oy 14, 26x8)`, wide low stance | head/horns + hooves (appendage) — **done** (monhun-ardu-nch.9) |
| long-tail (heavy) | body + tail base `(ox -8, oy 3, 48x22)`, so the tail is hittable behind the body | head + long tail (appendage) — **done** (monhun-ardu-nch.11) |

Implementation notes:
- Data: creature `stats.collide` box (optional; default body box) + `zones`
  head/appendage records. Both are already in the schema.
- The zone machinery is reused, so per-creature boxes are data-only changes;
  collision support (legs-only overlap) is the part that costs flash (~220 B
  for the current chicken implementation, mostly the collide cart read +
  target-rect selection).
- Parity: bull and long-tail collide changes move their scenes; mirror in
  mock/game.js and regenerate fixtures in the same change, and keep the
  changed-scene list explicit in the bead report.

## Long-tail attack kit (owner ask, 2026-09-18 — bead nch.1)

The long-tail's inherited generic `lunge`/`sweep` (initial-creature boxes, the
32x24 sweep telegraph) are replaced by a two-attack kit:

- `bite` — short forward lunge (windup 30 / active 8 / recover 40, dmg 10,
  window 18x14 @ ox 14), track facing.
- `tail_spin` — stationary 360 tail whip (windup 42 / active 20 / recover 55,
  dmg 8), **`facing: lock-away`** (nch.2; `lock-at-windup` keeps the tracked
  vector, `lock-away` negates it once at windup entry so the beast turns its back
  to the hunter and window 0's behind-the-back tail points at them), four
  contiguous windows (24x16 behind -> 16x24 side -> 24x16 front -> 16x24 side).
  The active window drives the hit test; a 4-frame `fxtail_spin` overlay is drawn
  through windup + attack, its frame chosen from the cached window's world
  direction. A lock-away hit knocks the hunter radially away from the beast
  (legacy attacks keep the facing-vector push).
- Selection: `tail_spin` at dist <= 30 (nch.4; `keepDist` 12 holds the band),
  `bite` beyond; breaking the tail (`zones.appendage`) disables `tail_spin` and
  forces `bite`. `faceHold` 10 commits the facing so the hunter can flank.
- Telegraphs are the core marker at the cached window centre (windup 2x2 shade 2,
  attack 4x4 shade 3). The nch.1 full-window block fill read as a debug hurt
  zone on playtest and was removed in nch.2; the fixed 32x24 telegraph sheet is
  gone since nch.1.
- Mock parity: bite/tailSpin use a new `windows[]`/lock path in `MONSTER_ATTACKS`
  (`monsterActiveWindow`/`monsterTellWindow`); the legacy lunge/sweep path is
  value-identical, so all 20 parity scenes stay byte-identical.

## Chicken attack kit (owner design, 2026-09-18 — bead nch.7)

The chicken's inherited generic `lunge`/`sweep` (the 32x24 sweep telegraph) are
replaced by a two-attack kit, same treatment as the long-tail nch.1:

- `peck` — close jab (windup 22 / active 6 / recover 30, dmg 7, lunge speedF 18),
  window 12x10 @ ox 14, oy -6, **`facing: track`**. The window box is the real
  hit test and the telegraph (the device draws the cached window box for every
  `windows[]` attack; the fixed 32x24 sheet is gone since nch.1).
- `leap` — committed long lunge (windup 34 / active 10 / recover 48, dmg 12,
  lunge speedF 42), window 18x16 @ ox 12, oy -2, **`facing: lock-at-windup`**:
  the tracked vector is frozen through windup + attack, so the hunter can step
  behind the committed jump.
- Selection (source order): `p_peck` guard `maxDist 28` → peck; `p_leap` guard
  `minDist 28` plus `hpBand [0,100]` → leap. So peck ≤ 28 px, leap 29..41 px
  (pursue re-decides under `attackDist` 42).
- `keepDist` 16 holds the peck/threat band; `faceHold` 6 commits the tracked
  facing for six ticks (same mechanism as the long-tail nch.4) so a leap can be
  flanked. Breaking the legs (`zones.appendage`) disables `leap` and forces the
  close peck.
- Mock parity: peck/leap use the `windows[]` path in `MONSTER_ATTACKS`; their
  `reach/hw/hh` mirror window 0 so the fixture hash matches the C++ cached
  `combat.attack.win.box`. `faceHold` 6 moves the per-tick face cadence, so two
  scene hashes move (`beast_no_shove_idle` 40/40 ticks, `camera_world_clamp`
  3/220) and `tst/fxdatatest/parity_fixtures.hpp` is regenerated in the same
  change.

## Bull attack kit (owner design, 2026-09-18 — bead nch.9)

The bull's inherited generic `lunge`/`sweep` (the 32x24 sweep telegraph) are
replaced by a two-attack kit, same treatment as the long-tail nch.1/chicken
nch.7:

- `stomp` — stationary close slam (windup 36 / active 10 / recover 44, dmg 9,
  `move none`), window 24x14 @ ox 10, oy 2, **`facing: track`**. The window box
  is the real hit test and the telegraph.
- `gore` — committed charge (windup 46 / active 12 / recover 55, dmg 14, lunge
  speedF 34), two contiguous windows, **`facing: lock-at-windup`**: horns 16x10
  @ ox 16, oy -2 (t 1..6) then trample 20x14 @ ox 12, oy 2 (t 7..12). The
  committed vector is frozen through windup + attack, so the hunter can step
  behind the charge.
- Selection (source order): `p_stomp` guard `maxDist 24` → stomp; `p_gore` guard
  `minDist 24` plus `hpBand [0,100]` → gore. So stomp ≤ 24 px, gore 25..41 px
  (pursue re-decides under `attackDist` 42).
- `keepDist` 18 holds the stomp/threat band; `faceHold` 10 commits the tracked
  facing for ten ticks (same mechanism as the long-tail nch.4) so a gore can be
  flanked.
- Zones (7vr): head/horns 12x10 @ ox 17, oy -4 (dmgMul 130, hp 40, share 100,
  SLASH, stagger 12) and hooves/appendage 20x10 @ ox 4, oy 12 (dmgMul 150,
  hp 60, share 40, SLASH, stagger 30). Breaking the hooves disables `stomp`
  (`broken.disableAttacks`); the head has no disable list. `collide`
  `(ox 1, oy 14, 26x8)` covers only the wide low leg stance, so the body region
  passes over the hunter.
- Mock parity: stomp/gore use the `windows[]` path in `MONSTER_ATTACKS`; their
  `reach/hw/hh` mirror window 0 so the fixture hash matches the C++ cached
  `combat.attack.win.box`. `keepDist` 18, `faceHold` 10 and the hooves collide
  box move the `monster_sweep_hit` scene, where the mock loads the bull's
  single-window `stomp` (the C++ override loads the same record) and
  `tst/fxdatatest/parity_fixtures.hpp` is regenerated in the same change.
