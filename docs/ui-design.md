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

---

# Screen prebake v2 (epic hbk) — baked 4-shade list pages, dev feel mode

Status: accepted with the owner (2026-09). The list screens are procedural
text (one shade, no grouping, no per-row state); the owner asked for prebaked
4-shade pages "for clarity", starting with FORGE, plus a dev mode with
unlimited crafting resources to feel-test the menus.

Budget finding (hbk.3, 2026-09): a FORGE-only prebake *adds* ~390 B on a
134 B-free image (blocked). With **every** screen prebaked the legacy title /
row-label / cost column fold out, and with the page indicator + hub strip
labels baked the wave nets out **29506 B (190 free)** vs HEAD 29562 (134 free):
the whole prebake + overlays wave is 56 B *cheaper* than the legacy text
renderer. The armor-row markers were measured at +120 B and deferred.

## Model

A list screen keeps its rows in the `mhScreens` blob (labels, costs, actions —
unchanged ABI), but the *chrome* is baked: one 128x64 4-shade image per 6-row
page, the same 3x 1bpp page-major layer family as the detail cards
(`cardBlit` reuse, 3 KB FX per page). **Every** shipped screen opts in with
`"prebake": true`; the legacy text renderer is deleted (`hbk.3`), so a screen
with no baked pages renders as an empty page plus live chrome (pinned by
`test_screens`: all four screens must have `pageCount > 0`).

Page = `scroll / 6` (scroll is always a multiple of 6), so a baked page maps
1:1 onto the visible window. Pages are addressed through a page table appended
to the screens blob: per screen, `u8 pageCount` + `pageCount x u24` absolute FX
addresses of the `mh_screen_<name>_<page>` layer arrays.

### Baked page layout (frozen; gen-screens.py is the source of truth)

| Zone | Spec |
|---|---|
| Title band | rect (0,0,128,8) shade 1 (dark); title text shade 3 (white) at (2,0) |
| Page indicator | `n/m` shade 2 after the title (2 + title width + 4) when a screen spans >1 page; static per page, so baked |
| Page table | fixed 13-byte slot per screen (`u8 pageCount` + 4 x `u24` addresses, `SCREEN_PAGE_STRIDE`; hbk.9) -- the device indexes it, no walk |
| Rule | y=8, full width, shade 2 (light) |
| Rows | y = 11 + 9*i, 6 per page (same grid as the legacy path) |
| Section header rows (`action == none`, label starts `--`) | band (0,y-1,128,8) shade 1, frame-stripped text shade 3, centered |
| Node label | tree prefix chars (` +-|`) shade 2, name shade 2, x=10 |
| Cost | digits shade 3, right-aligned ending at x=112 (3-digit cap, 999 max) |
| Marker column | x=118..121, baked empty; live-owned markers only |
| Bottom | free (live lanes only; the hub strip was deleted in hbk.9 -- the GEAR slot view owns the loadout readout) |

### Live overlays (device, per plane, on top of the blit)

- Cursor chip (existing `fxchip` sprDraw).
- Selected row: the label is re-drawn in white (shade 3) at the same x/y —
  exactly covering its baked shade-2 copy. Skipped for section header rows
  (`ACTION_NONE` without the skill flag), whose baked text is centered.
- Marker column, FORGE + GEAR weapon rows (`ROW_F_FORGE`): one 4x4
  `hudBlk(118, y+3, 4, 4, shade)` — white (3) when equipped, light gray (2)
  when owned, nothing otherwise. Pure save bits, no cart read per row.
  GEAR armor-row markers are deferred (measured +120 B; budget).
- GEAR skill rows (`ROW_F_SKILL`): the live points number + S/M tier letter at
  the baked cost column (right-aligned at x=112).
- (hbk.9 deleted the hub strip: the loadout readout lives on the GEAR slot view.)
- Live chrome: header zenny. (hbk.9 leaned the hub quest column to active-quest
  `p/n`; hbk.13 deleted it -- quest progress shows on the quest card's PROG
  page and the QUESTS screen.)

Everything else (labels, costs, section bands, tree prefixes) is baked: no
runtime text layout, no per-row cost math for prebaked screens.

## MH-flow smithy + equipment box (hbk.9–.12)

The baked-page model cannot filter a list cheaply (a dynamic owned-only list
measured **+590 B** whole-image in the hbk.8 spike: live row render 342, row-map
walk 126, equip-in-place 126). The wave therefore restructures the screens so
every list stays static and only small live overlays remain:

- **FORGE (id 3) becomes a submenu**: WEAPON CRAFT / WEAPON UPGRADE / ARMOR
  FORGE / LEAVE — the MH smithy's separate menus.
- **CRAFT (id 4)**: flat list of all 9 weapon nodes at their **direct** bill
  (the pricier from-scratch path), no tree prefixes, no class headers. Cards
  opened here use the direct bill only, so the row cost equals the charged cost.
- **UPGRADE (id 5)**: three class rows (SWORD / FLAIL / GUN). Live per row: the
  owned tier, `>`, the next tier and the upgrade cost — resolved from the
  highest owned node of that class; blank when nothing is upgradeable. A opens
  the next node's card (upgrade bill).
- **ARMOR FORGE (id 6)**: flat list of the five pieces at their recipe zenny;
  A opens the armor card (craft bill).
- **GEAR (id 2) becomes the equipment box**: four slot rows (WEAPON / HEAD /
  BODY / CHARM) + the skill readout + LEAVE. Each slot row shows the *shown
  candidate's* name (from a generated per-slot candidate table: node/piece ids +
  label offsets into the screens blob) and its state marker (white = equipped,
  gray = owned). LR cycles **owned** candidates only, A equips in place (no
  card). Unowned gear is never listed.
- Budget: the wave is funded by hbk.9 (hub strip deleted, quest column leaned to
  active-quest `p/n`, fixed-stride page table) — 29142 B, 554 free before the
  screens land.

## Dev feel mode (`make dev`)

`-DMH_DEV=1` (default 0, zero shipping cost — constant-folded) makes the build
a feel-test harness: `saveLoad` always returns fresh defaults (never reads
EEPROM), `saveStore` never writes, defaults carry 9999 zenny + 99 of every
item, and the bill gate/debit (`billShort`/`billDebit`) always passes/does
nothing — every craft and forge is free and repeatable. The player's real save
is untouched by a dev session.

## Phasing

1. `hbk.1` dev mode (`make dev`, MH_DEV).
2. `hbk.2` page baker: gen-screens prebake pipeline + page table + meta +
   tooling tests + `--sheet` review PNG (landed).
3. `hbk.3` baked `drawScreen` for all four screens: blit + live overlays,
   legacy text path deleted (hbk.4–.7 merged here — the budget only works with
   every screen baked).
4. `hbk.9` trims: hub strip deleted, quest column leaned to active-quest `p/n`,
   fixed-stride page table (29142 B, 554 free).
5. `hbk.10` smithy split: FORGE submenu + CRAFT (flat, direct bill) + ARMOR
   FORGE, plus the direct-bill flag through the card path.
6. `hbk.11` UPGRADE screen (3 class rows, live tier + cost) — measured +528 B,
   so `hbk.13` slimmed it (per-plane resolve from the generated
   `NODE_UPGRADE_COST[]`, no ScreenState cache, no cart reads) and deleted the
   hub quest column (progress stays on the quest card's PROG page).
7. `hbk.12` GEAR equipment-box slot view (owned-only candidates).
8. `hbk.8` spike (rejected): the dynamic owned-only list measured +590 B
   whole-image — see "MH-flow smithy" above.

Device-suite note: a pixel suite must call `arduboy.startGray()` before any
`waitPlane()` — without the plane ISR running, `waitPlane(n)` spins forever
(that is what the hbk.10 `test_screens_smithy` hang was). Keep each pixel suite
in its own `test_*.ino` so its setup frame stays small.
