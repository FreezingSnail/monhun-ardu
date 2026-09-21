# monhun-ardu-prg.12 — art: per-attack windup frames for animation tells

Status: DONE. Bead scope is cart art + the tell-selection wiring at the call site
(no new MCU machinery beyond the consolidated selector branch). Shipping flash
went **down** −70 B vs the arm.3 baseline (29590 → 29520/29696, 176 free) because
the cancelled worker's consolidated `beastAtk` branch replaced the two separate
chicken/bull branches. Repo left dirty on purpose (orchestrator commits); no
commit/push.

Inherited the cancelled worker's dirty tree (HEAD `781692a`), reconciled its four
failing host probes, registered a dead test, wired/verified the tell->frame
selector, and re-ran the full gate.

## What changed

- `tools/gen-art.py` (inherited + reconciled): per-attack windup poses.
  - `fxchickenatk` 4 → **6 frames** 32x24 `[peck E/W, leap E/W, wing_beat E/W]`
    (`_chicken_attack_east(mode)`, mode 2 = wing panel + level head).
  - `fxbullatk` 4 → **8 frames** 32x24
    `[stomp E/W, gore E/W, rear_kick E/W, stomp_windup E/W]` (tell-slot ordinal
    order; mode 3 = reared ground-slam windup).
  - `fxheavyatk` **new** 8 frames 32x24
    `[bite E/W, bite_windup E/W, spin_windup E/W, slam_windup E/W]` for the
    longtail's non-locked attacks.
- `images/blocks/fxbullatk_32x24.png`, `fxchickenatk_32x24.png` updated;
  `images/blocks/fxheavyatk_32x24.png` new. Generated set regenerated together
  (`fxdata/*`, `src/fxdata.h`, `src/generated/{art_dims,equip_meta,zone_meta}.hpp`,
  `fxdata/blocks/Sprites.txt`).
- `src/render_math.hpp`: `TELL_FRAMES_AUTHORED` 0 → **3** (the selector hook the
  prg.11 carve left at 0). `tellWindupFrame`/`tellHasAuthoredFrame` unchanged.
- `src/render.hpp`: `drawMonster` consolidates the chicken/bull branches into one
  `beastAtk` branch that picks the sheet + first authored attack by roster kind
  and the ordinal by `tellSlot` (windup) or attack-order offset (attack);
  `drawAttackMarker` already suppresses the 2x2 core for an authored tell.
- `tst/art_dims_test.hpp`: reconciled probes; registered `testHeavyAttackSheet`
  (it was defined but never added to the suite — the heavy sheet was untested).
- `tst/render_math_test.hpp`, `tst/fxdatatest/tell_test.hpp`: selector shipping
  count 0 → 3 (authored LINE/ARC/RING, DOT/ZONE fall back).
- `tst/fxdatatest/monster_art_test.hpp`: setup helpers take a `tell`, reset the
  cached facing, and pin the authored windup frames per attack.

Interfaces: no new sim/ABI symbols. `mh::TELL_FRAMES_AUTHORED` is now 3;
`fxheavyatk` + `art_dims::heavyatk_*` are new generated constants; `drawMonster`
consumes the existing `combat::ATTACK_<CID>_<FIRST>` constants.

## 1. Reconciliation of the 4 failing host probes

All four were probe coordinates landing on authored art features, not art bugs —
the assertion names ("head not high", "head low/high white", "foot planted") are
all satisfied by the intentional silhouettes (eyes are BLACK erasers, the comb is
white, the feet sit under the body). Corrected the probes:

- `wing head not high`: sampled (25,2), the white comb horn. → (27,2), clear of
  the combs at x21..23/x25..26; the level head starts at y3, so it reads 0.
- `wing foot planted`: sampled (0,21), behind the body. → (12,21), the planted
  near foot (the peck/leap planted column).
- `rear_kick head low white`: sampled (25,18), the BLACK eye. → (23,17).
- `stomp windup head high white`: sampled (25,3), the BLACK eye. → (23,3).

No `tools/gen-art.py` pose change was needed: the eyes/comb are deliberate and
the windup silhouettes already match the assertion names.

## 2. Selector wiring (verified in code + host + device)

`drawMonster` computes `tellSlot = tellWindupFrame(g.combat.attack.tell, 3)` only
during `MS_WINDUP`, and uses it as the sheet ordinal when non-NONE; otherwise the
attack-order offset `m.atkIdx - first` (generated `ATTACK_<CID>_<FIRST>`) for the
release pose. `drawAttackMarker` draws the legacy 2x2 shade-2 core iff
`!tellHasAuthoredFrame(...)`; `MS_ATTACK` keeps the 4x4 shade-3 marker. The
generated attack tells land exactly on their slots:

| Beast | Attack | tell | slot selected | sheet |
|---|---|---|---|---|
| chicken | peck | dot 0 | none → ordinal 0 (release) | fxchickenatk f0/f1 |
| chicken | leap | line 1 | 1 (leap) | fxchickenatk f2/f3 |
| chicken | wing_beat | arc 2 | 2 (wing) | fxchickenatk f4/f5 |
| bull | stomp | ring 3 | 3 (stomp windup) | fxbullatk f6/f7 |
| bull | gore | line 1 | 1 (gore) | fxbullatk f2/f3 |
| bull | rear_kick | arc 2 | 2 (rear_kick) | fxbullatk f4/f5 |
| heavy | bite | line 1 | 1 (bite windup) | fxheavyatk f2/f3 |
| heavy | tail_spin | arc 2 | (locked spin branch wins) | fxtailspin |
| heavy | tail_slam | ring 3 | (locked spin branch wins) | fxtailspin |

Host pin (`tst/render_math_test.hpp`): `TELL_FRAMES_AUTHORED == 3`,
DOT/ZONE unauthored, LINE/ARC/RING authored, slot == tell, zone falls back.
Device pins (`tst/fxdatatest/tell_test.hpp`): authored LINE/ARC/RING draw no core
marker; DOT/ZONE fall back to the 0x18 core; MS_ATTACK keeps the 0x3C 4x4 marker.
Device pose pins (`tst/fxdatatest/monster_art_test.hpp`): wing/leap chicken,
stomp-windup/rear_kick/gore bull and heavy bite windup select and draw the
authored slots (127 PASS).

Note (pre-existing nch.3/5 trade, not introduced here): heavy `tail_spin` and
`tail_slam` are both `lock-*`, so the `spinning` branch draws `fxtailspin` and the
heavy sheet's slots 2/3 are authored for selector completeness but never reached
at runtime. Their windup read is shared.

## 3. Bull stomp area read (item 3)

`fxbullatk` slot 3 (reared on the planted hind legs, both front hooves high and
spread, head high) reads as an imminent upward slam and is clearly distinct from
the release stomp (hooves tucked low) and the gore (head down). It does **not**
convey the stomp's `ring` area: the hit window is `(0,0,36,26)` — half-extents
18×13 body-centred — which is larger than the 32×24 cell and extends behind the
beast, so a single 2-facing pose cannot encode the AoE extent. The prg.11 ledger
removed the static window outline to hit the reclaim target.

Cheapest fix proposal (not applied, per the bead note): restore the 1-px ring
outline at the cached `g.combat.attack.win.box` for ring tells during `MS_WINDUP`.
The prg.11 spike measured that outline at **+144 B**; current headroom is 176 B,
so it would fit with ~32 B to spare, but per `docs/dev-flow.md` "budget-first /
split implement from make it fit" it belongs in its own spike'd bead rather than
being silently re-added here.

## 4. Contact-sheet review (pose class vs window class)

Reviewed per attack against `data/creatures/*.json` and the feel-design tables
(the tooling contact sheet is exercised by `make test-tools`; the direct
`python3 tools/contact_sheet.py` entry is not available in this sandbox, so the
review is by inspection of the generated/resolved numbers).

- chicken `peck` dot → `(14,-6,12,10)` ray: generic coil + core marker (dmg 7, no
  windup floor, so a generic tell is per contract).
- chicken `leap` line → `(12,-2,18,16)` ray: leap pose (head lowered, body
  raised, feet tucked) matches the forward ray.
- chicken `wing_beat` arc → `(-8,0,26,18)` behind sweep: wing panel swept out
  behind matches the arc/sweep.
- bull `stomp` ring → `(0,0,36,26)` AoE: reared slam pose is class-correct but
  area-incomplete (see item 3).
- bull `gore` line → `(16,-2,16,10)` ray: head-down horns-forward matches.
- bull `rear_kick` arc → `(-14,4,22,14)` behind: hind legs kicked back matches.
- heavy `bite` line → `(14,0,18,14)` ray: drawn-back head/snout-up windup matches.
- heavy `tail_spin` arc → full rotation: fxtailspin 8-dir sheet matches.
- heavy `tail_slam` ring → `(-16,0,36,28)` AoE: locked spin sheet (shared read).

## Gate tails

- `make gen` (run twice): `gen exit=0`; `gen.sh: FX data + src/fxdata.h regenerated`.
- `make gen-check`: `fxdata_manifest: PASS (91 generated artifacts unchanged)`.
- `make test` (host): `Total Passed: 6347  Total Failed: 0` (was 6299/4).
- `make test-tools`: `Ran 300 tests in 19.373s  OK`.
- `make fxtest-headless` (full): all suites PASS — asset 270, audio 10, boot 4,
  combat 237, data 343, hub 63, hud 25, items 35, menu_art 53, menu 60,
  monster_art **127** (was 111), player_art 120, quests 50, screens 85, smith 113,
  tell **18**, zones 80; parity excluded as frozen.
  `test_perf`: `B pUs=6347 pHz=157 lHz=52 lTk=524 rMx=3300 rAv=3027 ram=687`
  `PASSED=5 FAILED=0`.
- `make size`: `size: flash=29520/29696 (176 free)  ram=1637/2560`;
  `.text=29500 .data=20 .bss=1617`. Data facts unchanged
  (`HAS_CARVE/HAS_ENRAGE/HAS_STAGGER/HAS_TURN_RATE/HAS_ZONES/...` same as arm.3).

Budget delta vs arm.3 baseline (29590 flash, 1637 RAM): **−70 B flash, +0 B RAM**.
The cart grew ~8.2 KB (`fxdata.bin` 200448 → 208640 B) — cart, not MCU flash.
