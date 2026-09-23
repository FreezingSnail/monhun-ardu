# UI design v2 — list + detail, prebaked detail cards, forge trees (accepted)

Status: accepted with the owner (2026-09). Supersedes the qs.1–qs.4 flat
list-screen look. Budget at design time: flash 29204/29696 (492 free), RAM
1715/2560 — trim wave first (docs/dev-flow.md).

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
- 6 rows/page at 9 px pitch; page indicator (`n/m`) when a screen has more
  than one page.
- Cursor: chip tile; selected label white, others gray; state tokens per row.

## List screens

### HUB

```
HUB                      1240
____________________________
> HUNT            QUEST 2/3
  QUESTS
  FORGE
  GEAR
____________________________
SWD T2A  ATK12 DEF10 EVA4
```

`HUNT` right column = active quest progress / `READY` / `-`; bottom strip =
equipped weapon + top active skill totals.

### QUESTS

```
QUESTS                   1240
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
GEAR                     1240
____________________________
  -- WEAPON --
> SWD T2A           E
  SWD T1
  GUN T1
  -- ARMOR --
  HUNTER HELM      E DEF10
  BONE CAP            DEF6
____________________________
                   page 1/2
```

Cards: weapon `DESC / STATS`; armor `DESC / PARTS (unowned only) / STATS /
SKILL`. A = equip/unequip; uncrafted armor A = craft (parts + zenny, adds to
the crafted bitset; no upgrade path).

### FORGE (full weapon trees, forge/upgrade)

```
FORGE                    1240
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
                   page 1/3
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
5. `ui.5` polish: hub strip, page indicators, docs; items screen later
   (monhun-ardu-prg.13) reuses the same card pipeline.
