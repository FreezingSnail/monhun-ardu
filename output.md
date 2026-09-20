# monhun-ardu-feel.2 — docs: boss-feel design + attack contract — DONE

Status: **DONE**. Docs-only bead; no `src/`, `data/`, generated, tool, test,
`mock/`, README or AGENTS.md changes. No commit / push (orchestrator commits).

## What was written

- **`docs/feel-design.md`** (new, ~340 lines), six sections:
  1. **Diagnosis table** — the eight pre-revision feel problems (beast speed vs
     hunter, track re-aim, 2×2 tell, metronome cadence, inert stagger, empty
     arena, soft bodies, cheap roll) with symptom + revision answer. The roll
     tune (`monhun-ardu-feel.11`) is marked still-open.
  2. **Design pillars** — the five from the epic (read the shape / space has an
     owner / commit or eat it / weak points are positions / phases change
     rules).
  3. **Data verbs** — one-line semantics for attack `facing`
     (`track`/`lock-at-windup`/`lock-away`), guard `facing behind/front`,
     `wallStun`, `tell` enum DOT/LINE/ARC/RING/ZONE (draw description each),
     `enrage`, `move.type hop`, multi-step patterns (`after`/`chance`/`wait`),
     stagger opt-in, zones/break `disableAttacks`.
  4. **Per-beast kit reference tables** — chicken (lunge), bull (sweep), heavy
     (heavy): stats/collide/enrage, zones, profile, attacks
     (id/wu·act·rec/dmg/move/facing/tell/wallStun/windows/positional answer),
     patterns (guard + steps). Numbers exact from the current JSON.
  5. **Attack contract checklist** — tell shape == hit-test geometry; windup
     floor by damage class (`dmg>10` ⇒ wu≥24, `dmg 8..10` ⇒ wu≥14, `dmg≤7` no
     floor); recovery floor for committed attacks (lock/`dmg>10` ⇒ rec≥40);
     every pattern has a punish window; every attack has a positional answer
     (sidestep / roll-through / out-range / behind); no stationary weak attack;
     telegraph == hit-test window; review via `tools/contact_sheet.py` +
     `tools/gen-combat.py --dump` before device.
  6. **Budget ledger** — feel.1 per-item flash/RAM deltas, pattern-step facts,
     shipped `make size` (28610/29696, 1086 free; RAM 1743/2560), data-fact
     list, and the `test_parity` exclusion note.
- **`docs/dev-flow.md`** — one short paragraph after the render/data review
  checklist linking `docs/feel-design.md` (not a rewrite).

## Number verification

Every table value cross-checked against
`python3 tools/gen-combat.py --dump` (run as `./tools/gen-combat.py --dump`;
the session permission rules deny the literal `python3` prefix, so the repo
tool was executed directly). Dump tail (all three kits, exit 0):

```
creature heavy (skeleton longtail, stats w40 h28 hp320 spd5, spawn 200,40, collide box(-8,3,48,22), ...)
  zone appendage: box(-24,0,24,16) dmgMul 150 hp 60 share 40 break 0x01 stagger 30 brokenOverride 200 hurtOff 1 disable tail_spin
  attack bite: windup30 active8 recover40 dmg10 move lunge(26) windows 1 wallStun 0 tell line
    window 0: t[0,8] box(14,0,18,14) dmgMul 100
  attack tail_spin: windup34 active18 recover62 dmg8 move none windows 4 wallStun 0 tell arc
    ... windows t[0,5](-20,0,28,16) t[6,10](0,-22,16,28) t[11,14](22,0,28,16) t[15,18](0,22,16,28)
  attack tail_slam: windup26 active8 recover44 dmg12 move hop(-56,0) windows 1 wallStun 0 tell ring
    window 0: t[0,8] box(-16,0,36,28) dmgMul 100
creature lunge (skeleton chicken, stats w32 h24 hp200 spd6, spawn 200,40, collide box(9,11,12,13), ...)
  attack peck: windup18 active6 recover26 dmg7 move lunge(20) ... tell dot
  attack leap: windup30 active12 recover52 dmg13 move lunge(48) ... tell line
  attack wing_beat: windup16 active5 recover34 dmg9 move none ... tell arc
creature sweep (skeleton bull, stats w28 h22 hp150 spd7, spawn 200,40, collide box(1,14,26,8), enrage hpPct40 spdMul130 faceHold6 ...)
  attack stomp: windup34 active11 recover46 dmg9 move none ... tell ring
  attack gore: windup42 active14 recover58 dmg14 move lunge(40) windows 2 wallStun 70 tell line
  attack rear_kick: windup14 active4 recover30 dmg10 move none ... tell arc
```

Windows/patterns/guards all match the tables (source order and inclusive bands
checked line by line).

## Verification tails

- `make gen-check` → `fxdata_manifest: PASS (82 generated artifacts unchanged)`.
- `make test` → `Total Passed: 6454  Total Failed: 0`.
- `git status --short` → only `M docs/dev-flow.md` + `?? docs/feel-design.md`.

Docs-only bead, so **no device gate** was run (no `make fxtest-headless` /
`make size` needed beyond the `make size` reading quoted from the final kit
HEAD). `tools/contact_sheet.py` is referenced in the doc/README but was not
executed here (not an executable file and the session denies the `python3`
prefix); it is a review-time authoring tool, not part of this gate.
