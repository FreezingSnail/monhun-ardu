# Weapon art pass — sword / flail / gunshield sheets (design freeze)

Status: design accepted (2026-09-25). Supersedes the legacy block-overlay weapon
draw (fxslash / fxguard / fxwhirl chain) for the three player weapons.

Goal: every player weapon draws from **its own authored 32x32 sheet** in the
equipment format, with one art pose per **move** (combo chain, special, branch,
roll, alt, charge) instead of the current shared grey boxes / plate, so the
silhouette tells the move before the box lands.

## Sheet format (the "new image format" applied to equipment)

- Source: `images/equip/<sheet>_32x32.png`, cell 32x32, anchor (16,16) (grip /
  row reference point).
- Source is **one-facing-ish**: each row authors 5 columns in source order
  `E, SE, S, N, NE`; `images/equip/layout.json` (written by gen-equipment,
  consumed by `tools/convert-sprite.py`, same plan format as
  `images/blocks/layout.json`) packs the shipped 8 columns as
  `E, SE, S, SW=mirror(SE), W=mirror(E), NW=mirror(NE), N, NE`. Shipped frame
  index stays `row * 8 + facing`, so the render `partFrame` math is unchanged.
- Rows = poses (below). Blank rows are allowed (unused move slots) and pack as
  empty frames; the generator only draws the rows a weapon uses.
- Palette: the L4 triplane 4 shades exactly (black erase / dark / light /
  white); no new colors. `docs/equipment-framework.md` owns the record schema;
  this doc owns the row tables and the art rules.

## Row table (all three sheets share the numbering)

```
row 0        idle
row 1        recover (shared)
rows 2..25   move slots 0..11, two rows each: base = startup, base+1 = active
rows 26..31  state rows (per weapon, below)
```

`MOVE_ROWS[w][slot]` (render, PROGMEM u8[3][12]) maps a dense move slot to its
startup row base; `active = base + 1`. Unused slots map to row 1 (recover), so a
stray state cannot blit a blank cell.

### Move slots

Slot ids are dense per weapon and come from `Attack::id` (AtkId, extended) plus
`state`/`chain`:

| slot | sword | flail | gunshield |
|---:|---|---|---|
| 0 | combo 0 | combo 0 | combo 0 |
| 1 | combo 1 | combo 1 | combo 1 |
| 2 | combo 2 | combo 2 | combo 2 |
| 3 | special (riposte) | special (throw) | special (arrowshot) |
| 4 | step-slash | trip | pointblank |
| 5 | spin-cut | branch 2 | guardbash |
| 6 | branch 2 | alt (wide sweep) | branch 2 |
| 7 | alt (thrust) | roll (rollsweep) | alt (shield charge) |
| 8 | roll (rollslash) | charge (chargeslam) | roll (shield bash) |

AtkId additions (data only, packed by `gen-fxtables.cpp` unchanged):
`ATK_ALT = 6`, `ATK_ROLL = 7`, `ATK_CHARGE = 8`, `ATK_BRANCH2 = 9`.
Slot resolution (render, one helper):

```
id != ATK_NONE          -> id -> slot (STEP=4, SPIN=5, TRIP=4, POINTBLANK=4,
                            GUARDBASH=5, ALT=slot 7/8 per weapon, ROLL=8,
                            CHARGE=8, BRANCH2=6)
id == ATK_NONE + PS_SPECIAL -> slot 3
else                    -> combo slot = min(p.chain, 2)
```

The per-weapon `id -> slot` fold is the small `MOVE_SLOT[w]` table; pointer
comparisons are not used.

### State rows

| row | sword | flail | gunshield |
|---:|---|---|---|
| 26 | parry | whirl (hand + chain base; ring/ball stay separate parts) | guard |
| 27 | dodge | deflect | shove |
| 28 | stun | dodge | dodge |
| 29 | riposte rim (drawn on top of the special active when `riposteT > 0`) | stun | stun |

`PS_SHOVE` keeps the existing retract offset in the render; `PS_DRAW` uses idle.

## Reference point per row

The part record has one anchor (16,16), so the render passes the per-row
reference point (`partDraw(part, row, face, rx, ry)`):

- **active rows**: the move's hitbox centre `cx + (fx*reach)>>4`,
  `cy + (fy*reach)>>4` — the same point `meleeHitbox` resolves against, so the
  drawn shape and the tested rect share one origin. Art is drawn centred in the
  cell, arm side toward the player.
- **every other row**: the player centre `(cx, cy)` — weapon held on the hunter.

The existing `(hw, hh)` from the mask decides the active art's drawn extent:
the generated art must paint ink across the full `hw x hh` rect at that centre
(checked by the tooling test, see below).

## Art rules

- **Boxes are the contract.** `src/generated/player_boxes.hpp` (mask
  `images/masks/mh_player_base_16x16.png`) owns reach/hw/hh; never copy numbers
  into the art code. The generator reads `build/hitboxes.json` /
  `player_boxes.hpp` and derives each active pose from the box.
- **Telegraph discipline** (same rule as creatures, `docs/dev-flow.md`): the
  startup row must read as "wind-up" (weapon pulled off the hit vector), the
  active row must read as the strike; the hitbox rect is covered on active rows
  only.
- **Critical reads in black/white**: white = blade edge / ball core / shield
  face glint; black = the shaft/frame shadow that separates the weapon from the
  body. Grays carry depth only.
- Motion arcs on active rows are dark (shade 1) and stay inside the cell; no
  ink outside the 32x32 cell.

## Pipeline changes

- `gen-equipment.py` authors weapon sheets from a per-weapon pose spec (like the
  existing player/body/head placeholder primitives), mirror-aware: 5 authored
  columns per row + the emitted `images/equip/layout.json` plan. Same run emits
  the sheet PNG, `fxdata/equip/Sprites.txt` input, `equip_meta.hpp`, and the
  packed blob; `gen-check` stays the determinism gate.
- The record gains `poseMap` rows for the state rows above; `variants` is not
  used by the weapon sheets (the render resolves rows itself).
- `fxdata/fxdata.txt` / `src/fxdata.h` regenerate as usual; the sheet offsets
  shift, which the existing static asserts + gen pipeline already handle.

## Render changes

- `drawPlayer` picks the weapon part from `g.weapon`
  (`PART_WEAPON_SWORD / PART_WEAPON_FLAIL / PART_WEAPON_GUN`) and draws
  `MOVE_ROWS[w][slot] + phase` for attacks and the state rows otherwise.
- Retire the legacy gen-art records once the sheets cover their draw sites:
  `sword_slash`, `sword_parry`, `sword_riposte`, `sword_chip`, `gun_guard`,
  `gun_reload`, `flail_chain`, `flail_ball`, `chip_ball`, `deflect`, and the
  fxslash/fxguard/fxparry/fxripspecial/fxwhirl(chain/ball) block sheets.
  Kept: `flail_ring` (24-phase orbit, 48x32 cell), `flail_stun`, `erase`.
- Delete the per-weapon overlay branches in `drawPlayer` that those records fed.

## Verification

- `test_player_art` goldens regen per bead, with the changed case list +
  replan explanation in `output.md` (existing convention in that file's header).
- `tools/tests/` gains a weapon-art test: sheet dims/rows match the spec, every
  active row's ink covers its mask box, every non-active row paints nothing in
  the active row's box region, and the mirror plan reproduces the shipped
  frames.
- `tools/gen-hitboxes.py --render` / `build/scratch/` composite review PNG shows
  art + mask boxes for eyeballing before device flash.
- Budget: `make size-line` after each layer; the wave plans against
  `free - 300 B` (baseline 29426/29696, 270 free at design freeze) and must not
  land below ~150 B free. Spike reports the whole-image delta before the wave.
