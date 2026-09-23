# UI design v2 — list + detail, prebaked detail cards, forge trees (accepted)

Status: accepted with the owner (2026-09). Supersedes the qs.1–qs.4 flat
list-screen look. Budget at design time: flash 29204/29696 (492 free), RAM
1715/2560 — trim wave first (docs/dev-flow.md).

Shipped so far: ui.1–ui.4 (cards, forge trees, save v5, unified card paths).
ui.5 landed the **header zenny** (retiring the HUB ZENNY row + `ROW_F_ZENNY`)
and the docs. ui.5.2 (5co.9) landed the rest of the chrome on the 746 B reclaimed
by ui.5.1: the **HUB HUNT right column**, the **HUB bottom strip**, the list
**page indicator**, and the **blocked-A denied cue**. Measured whole-image deltas
(ui.5.2, with the ui.5.1 trim): HUNT column +248, strip +322, page indicator
+114, denied cue +32; the now-dead per-char label/title tail fallback was
dropped for -128. Net +612 from HEAD 4c5ec9f (28950 -> 29562, 134 B free).

## Interaction model

One screen at a time:

```
list screen --A--> detail card --LEFT/RIGHT--> pages --A--> action --B--> list
```

- The list is navigation only; A opens the item's detail (it no longer fires
  the action directly).
- **Detail pages are prebaked 128x64 images** on the FX cart (one image per
  page per item), blitted like the fie.5 room images instead of drawn from
  text + live values. LEFT/RIGHT swaps the image; B backs out.
- A performs the context action (forge / upgrade / craft / equip / accept),
  shown by the dynamic hint line.
- The action is context-dependent: the same weapon node is forged on FORGE,
  equipped on GEAR.

## Prebaked detail cards

- Generated from the item JSON (title, description, parts, stats, skills) by a
  new art step; deterministic, tooling-tested, reviewable on a contact sheet.
- Because points/recipes/stats are static per item, everything is baked: text,
  boxes, skill bars, part lists, required counts, costs. No runtime text
  layout, no per-row cost math.
- Dynamic values stay minimal:
  - the **hint line** (action or blocker: `A FORGE`, `NEED ORE 2/4`,
    `REQ SWD T2A`) — dynamic text, one line;
  - optional fixed-slot overlays from page metadata (live have-counts on
    PARTS, the quest progress bar) where a baked value would be wrong;
  - the header zenny on list screens (unchanged chrome).
- Page set per item is data: DESC, PARTS, STATS, SKILL (armor), GOAL/PROG/
  REWARD (quests). A page with no data is simply not generated.
- Image format: the existing per-plane 1bpp page-major layers (3 planes ×
  1 KB per 128x64 page); blit reuses the room-image streaming path, so the
  flash cost is the nav + offset lookup, not a renderer.

## Global chrome (list screens)

- Title line: screen title left, zenny right (`$` + digits) — the fake ZENNY
  row and `ROW_F_ZENNY` are retired.
- 6 rows/page at 9 px pitch. The 6-row grid fills y=11..63, so the page
  indicator (`n/m`, gray) sits on the title line just after the title; it is
  drawn only when a screen has more than one page (>6 rows).
- Cursor: chip tile; selected label white, others gray; state tokens per row.
- A blocked A (a gated row, or a card whose hint is `NEED PARTS` / `NEED ZENNY`
  / silent) plays a short denied cue (the low `CUE_HURT` tone; no new cue).

## List screens

### HUB

```
HUB                      1240
____________________________
> HUNT                 2/3
  QUESTS
  FORGE
  GEAR
____________________________
SWD1  ATK12 DEF10
```

`HUNT` right column = active quest progress `p/n` / `READY` / `-`; bottom strip
(y=56) = equipped weapon marker (class abbr + forge-tree tier) + active armor
skill point totals (abbr + points, tiered skills only).

### QUESTS

```
QUESTS 1/2               1240
____________________________
> SLAY LUNGE     2/3 [##..]
  SLAY SWEEP         NEW
  GATHER ORE          --
  CRUSH HEAVY         --
____________________________
```

Cards: GOAL / PROG / REWARD. A = accept (`NEW`) / turn in (`READY`); locked
(`--`) and done (`OK`) dim.

### GEAR (owned gear, equip)

```
GEAR 1/2                 1240
____________________________
  -- WEAPON --
> SWD T2A           E
  SWD T1
  GUN T1
  -- ARMOR --
  HUNTER HELM      E DEF10
  BONE CAP            DEF6
____________________________
```

Cards: weapon `DESC / STATS`; armor `DESC / PARTS (unowned only) / STATS /
SKILL`. A = equip/unequip; uncrafted armor A = craft (parts + zenny, adds to
the crafted bitset; no upgrade path).

### FORGE (full weapon trees, forge/upgrade)

```
FORGE 1/3                1240
____________________________
  -- SWD --
> SWD T1          120  OK
  +- SWD T2A      280  UP
  |  +- SWD T3   540
  |  +- SWD T3B  900  DIR
  +- SWD T2B      320  DIR
  |  +- SWD T4   900
  -- FLAIL --
____________________________
```

The list keeps label + cost only; the live state (equipped / owned /
upgradeable / direct / need-parts / need-zenny) is carried by the card hint
line, not a per-row token column (ui.4.1 trim). Tree prefixes are authored in
the labels.

Cards: `DESC / PARTS / STATS`; PARTS lists every required part, the zenny
cost and `FROM SWD T2A` or `DIRECT`. A = forge (direct, pricier) or upgrade
(transforms the owned parent; an equipped parent moves the equipped id to the
child). Skipped nodes stay unowned.

## Weapon tree model

- Node: `id, class, parent, direct(bool), cost, mats, directCost, directMats,
  dmgMul, spdMul, desc, sheet`.
- Data-driven: any tree shape/branching; the generator emits depth/branch
  metadata; FORGE rows are generated from the tree source.

## Save v5

```
weapon owned bitset  4 B  (32 node slots, all trees combined)
equipped node id     1 B  (u8 node index; replaces the class-index byte)
armor crafted bits   1 B  (8 piece slots)
```

Fixed regions: content below the caps is a data-only change. There is no
migration (ui.4.1): only a version-5 record with a valid checksum decodes;
anything else falls back to the defaults. Pre-release, an old save is
discarded on a version change.

## Data additions

- `desc` (pre-wrapped lines) on armor, weapon nodes, quests, items — used by
  the card baker.
- `abbr` on skills for inline rows.
- Skill display stays active-only (hub strip + per-item SKILL card); no
  all-skills screen. New skill entries are data; new effect kinds are their
  own gameplay/budget bead.

## Cost and phasing

| Piece | Est. flash |
|---|---|
| Card nav + offset lookup + blit reuse + hint line | ~150–250 B |
| Header zenny, tokens, page indicator, denied cue | ~100–180 B |
| Weapon tree + save v5 (bitset, node table, migration, gen rows) | ~200–400 B |
| GEAR owned list + hub strip | ~100–200 B |

Beads:

1. `ui.1` spike: card-blit budget + trim candidates (measure whole-image).
2. `ui.2` trim: adopt the reclaim set.
3. `ui.3` card pipeline: JSON -> 128x64 page images -> FX layers + offset
   meta; card nav/LEFT-RIGHT/hint line; armor + quests content wired first.
4. `ui.4` weapon trees: node table + save v5 + FORGE list/cards + GEAR owned
   list; v4 migration.
5. `ui.5` polish: header zenny + docs landed; `ui.5.1` reclaimed the budget,
   and `ui.5.2` (5co.9) landed the hub HUNT column, hub strip, page indicators
   and the denied cue (see Status above). Items screen later
   (monhun-ardu-prg.13) reuses the same card pipeline.
