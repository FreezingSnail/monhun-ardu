# Weapon art pass — sword / flail / gunshield sheets (design freeze)

Status: landed (2026-09-25, epic monhun-ardu-bhp). All three weapon sheets are
authored and the render draws them; the legacy block overlays are retired.

Goal: every player weapon draws from **its own authored 32x32 sheet** in the
equipment format, with one art pose per **move** (combo chain, special, branch,
roll, alt, charge) instead of the old shared grey boxes / plate, so the
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
rows 2..21   move slots 0..9, two rows each: base = startup, base+1 = active
rows 22..26  state rows (per weapon, below)
```

27 rows x 8 facings = 216 packed frames; `frames: 216` in the record.
`MOVE_ROW[w][slot]` (render, `wpn::MOVE_ROW` PROGMEM u8[3][10]) maps a dense
move slot to its startup row base; `active = base + 1`. Unused slots map to
row 1 (recover), so a stray state cannot blit a blank cell.

### Move slots

Slot ids are dense and weapon-independent. Named moves come from `Attack::id`
(AtkId, extended); plain combo hits use the chain index; the special uses
`PS_SPECIAL`:

| slot | id / state | sword | flail | gunshield |
|---:|---|---|---|---|
| 0 | combo chain 0 | combo 0 | combo 0 | combo 0 |
| 1 | combo chain 1 | combo 1 | combo 1 | combo 1 |
| 2 | combo chain 2 | combo 2 | combo 2 | combo 2 |
| 3 | `PS_SPECIAL` | special (riposte) | special (throw) | special (arrowshot) |
| 4 | `ATK_STEPSLASH` / `ATK_TRIP` / `ATK_POINTBLANK` | step-slash | trip | pointblank |
| 5 | `ATK_SPINCUT` / `ATK_GUARDBASH` | spin-cut | — | guardbash |
| 6 | `ATK_BRANCH2` | branch 2 | branch 2 | branch 2 |
| 7 | `ATK_ALT` | alt (thrust) | alt (wide sweep) | alt (shield charge) |
| 8 | `ATK_ROLL` | roll (rollslash) | roll (rollsweep) | roll (shield bash) |
| 9 | `ATK_CHARGE` | — | charge (chargeslam) | — |

AtkId additions (data only, packed by `gen-fxtables.cpp` unchanged):
`ATK_ALT = 6`, `ATK_ROLL = 7`, `ATK_CHARGE = 8`, `ATK_BRANCH2 = 9`.
`wpn::ATK_SLOT[10]` folds the ids onto slots; pointer comparisons are never
used.

### State rows

| row | sword | flail | gunshield |
|---:|---|---|---|
| 22 | parry | whirl (hand + chain base; ring/ball stay separate parts) | guard |
| 23 | dodge | dodge | dodge |
| 24 | — | deflect | shove |
| 25 | stun | stun | stun |
| 26 | riposte rim (drawn on top of the special active when `riposteT > 0`) | — | — |

`PS_SHOVE` keeps the existing retract offset in the render; `PS_DRAW` and
`PS_CHARGE` use idle art for now.

### Beast variant sheets (2tb)

Each melee class also has four craftable beast-style variants (kind 5..12).
They reuse the class sheet's row table, anchors and box-derived geometry — only
the striking-part pixels and trim differ (`gen-equipment.py`
`sword_variant_cell` / `flail_variant_cell`). The equipped forge node's kind
selects the sheet, and only ever within the weapon's own class
(`src/render.hpp weaponSheet`):

| kind | class | style | sheet | beast |
|---:|---|---|---|---|
| 5 | sword | saber | `mh_weapon_sword_saber` | chicken |
| 6 | sword | cleaver | `mh_weapon_sword_cleaver` | bull |
| 7 | sword | tailblade | `mh_weapon_sword_tailblade` | longtail |
| 8 | sword | fang | `mh_weapon_sword_fang` | ravager |
| 9 | flail | sling | `mh_weapon_flail_sling` | chicken |
| 10 | flail | shell | `mh_weapon_flail_shell` | bull |
| 11 | flail | tail | `mh_weapon_flail_tail` | longtail |
| 12 | flail | spike | `mh_weapon_flail_spike` | ravager |

Kinds 1..4 stay gunshield-only. The blades/heads inverse-map through
`fill_weapon` like the class sheets (45 deg facings stay solid), and the
player_art oracle pins one idle-guard case per variant (cases 44..51).

## Reference point per row

The part record has one anchor (16,16), so the render passes the per-row
reference point (`partDraw(part, row, face, rx, ry)`):

- **active rows**: the move's hitbox centre `cx + (fx*reach)>>4`,
  `cy + (fy*reach)>>4` — the same point `meleeHitbox` resolves against, so the
  drawn shape and the tested rect share one origin. Art is drawn centred in the
  cell, arm side toward the player.
- **every other row**: the player centre `(cx, cy)` — weapon held on the hunter.

The existing `(hw, hh)` from the mask decides the active art's drawn extent: the
blade/mass lies inside the `hw x hh` rect at that centre, rotated to the strike
angle, and the tooling test asserts ink in the box and across its front half.

## Art rules

- **Boxes are the contract.** `src/generated/player_boxes.hpp` (mask
  `images/masks/mh_player_base_16x16.png`) owns reach/hw/hh; never copy numbers
  into the art code.
- **Move records are the source.** `tools/fxdump.cpp` emits every weapon attack
  (startup/active/recover/reach/hw/hh/lunge/push/effect/id; roll/alt/charge
  included) into `build/fxdump.json`; the generator derives each pose from that
  record — identity (spin/step/roll/alt/finisher) first, box geometry second,
  chain order third — and never from a hand-written angle table. AtkId names are
  parsed from `src/core/game.hpp`, so the tool's slot fold cannot drift from
  `src/render.hpp wpn::ATK_SLOT`.
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

- One path for all three weapons: `weaponSheet(g.weapon)` picks the generated
  sheet constant, `weaponRowDraw(sheet, row, face, rx, ry)` draws the row (cell
  32x32 / anchor 16,16 are fixed by the schema, so no cart record read). Attack
  rows come from `wpn::MOVE_ROW[w][weaponMoveSlot(p, a)] + phase`; state rows
  from the table above. Startup/state rows are referenced at the player centre,
  active rows at the hitbox centre.
- Weapon variants (jd1/2tb): the equipped forge node's sheet kind
  (`forge::NODE_SHEET` -> `Game::wpnSheet`) selects the drawn sheet, resolved
  strictly against the equipped weapon's own class — 1..4 pick the
  buckler/kite/tower/brace `SHEET_OFF_MH_WEAPON_GUN_*` constants, 5..8 the
  sword saber/cleaver/tailblade/fang and 9..12 the flail
  sling/shell/tail/spike; 0 (and any kind outside the class's range) keeps the
  class default `mh_weapon_sword` / `mh_weapon_flail` / `mh_weapon_gun`. Every
  variant sheet shares the row/frame layout, so `weaponRowDraw` is unchanged.
- Extras stay per weapon: sword riposte rim (row 26 over the special active),
  flail whirl ring/ball + stun sparkle, gun arrowshot tracer + the guard plate
  staying up while the shot fires from the stance. The gun's arrowshot active
  row is **muzzle-referenced** (hand) because its box is the hitscan reach, not
  a hit area.
- Retired equip records (deleted): `sword_slash`, `sword_parry`,
  `sword_riposte`, `sword_chip`, `chip_ball`, `gun_guard`, `gun_reload`,
  `flail_chain`, `deflect`. Kept: `flail_ring` (24-phase orbit), `flail_ball`,
  `flail_stun`, `erase`.
- Follow-up: the block sheets those records fed (`fxslash`, `fxguard`,
  `fxparry`, `fxripspecial`, `fxchip`, `fxdeflect`, `fxreload`, and the
  chain-only `fxwhirl` frames) are dead cart data and get their own cleanup bead
  (`monhun-ardu-bhp.8`) with the `art_dims` / asset-test sweep.

## Verification

- `test_player_art` goldens regen per bead, with the changed case list +
  replan explanation in `output.md` (existing convention in that file's header).
- `tools/tests/` gains a weapon-art test: sheet dims/rows match the spec, every
  active row inks its mask box and the box front half, the layout plan
  reproduces the shipped frames row-for-row, and the mirror source/row-table
  validations fail loudly.
- Local eyeball artifact (`build/` is untracked): `build/scratch/weapon_review.py
  <0|1|2>` renders body + sheet rows + the mask box per move at 3x for the
  sword/flail/gunshield.
- `tools/gen-hitboxes.py --render` / `build/scratch/` composite review PNG shows
  art + mask boxes for eyeballing before device flash.
- Budget: landed at **29160/29696 (536 free)**, 266 B below the design-freeze
  baseline (29426) — the shared row path + deleted legacy overlay code paid for
  the new tables. Art lives in the cart (16 MB, 404 KB -> 733 KB used); MCU
  flash is the only real budget.
