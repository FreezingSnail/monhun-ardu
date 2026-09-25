# monhun-ardu-ht8 — four craftable gunshields: art, configs, forge branches

## Status: art/data DONE (core wiring = second bead)

Owner: the four E-facing shield candidates are all good; make a craftable weapon
config for each. Owner calls: 4 branches off `gun_base`, agents draft
names/costs/stats (owner tunes).

## Landed

- `tools/gen-equipment.py`: `gun_plate(style, ...)` + `gun_variant_cell(style, ...)`
  — the four approved silhouettes (buckler r8 / kite tapered / tower 9x18 +
  taper + chamfer / brace slab + two braces + point), screen-space anchored at
  `wpt(facing, u, v)`, dark rim, lit face, lead-edge highlight, 4x4 gunport on
  the barrel line. Shot rows start the muzzle past the plate's forward edge.
- `data/equipment/weapon_gun_{buckler,kite,tower,brace}.json` (weapon_gun.json
  clone with the new sheet symbol) → four generated sheets
  `mh_weapon_gun_*_32x32.png`.
- `data/forge/gun.json`: four direct branches off `gun_base` (appended after the
  spine so `screenClassFirst`/the linear UPGRADE walk stay valid). Drafts:
  GN BUCKLER 120/96/118, GN KITE 150/104/104, GN TOWER 220/114/94,
  GN BRACE 280/124/86 (cost/dmg/spd; owner tunes).
- Regenerated: sheets, equip blob (offsets 118262/624.../1.18M), forge blob +
  `forge_meta` (NODE_COUNT 13, NODE_SHEET_* ids 9-12, NODE_DEPTH/BRANCH,
  NODE_UPGRADE_COST), card meta (23 items; last card = CARD_WEAPON_GUN_BRACE),
  screens (FORGE/GEAR rows + prefixes, CRAFT 14 rows / 3 pages, GEAR slot 0 = 13
  nodes).
- Device pins updated: cards_test (23, last card GN BRACE), forge_test +
  screens_test (craft rows 14, leave row 13, craft pages 3, weapon slot 13).

## Verification

- `make gen-check` PASS (two-pass gen converged; `make gen` is a no-op now).
- `make test` 6877/0; `make test-tools` 390 OK.
- `FXTEST_ONLY="test_cards test_forge test_screens test_screens_smithy"` PASS.
- Full 19-suite device gate + size re-run once before the commit.
- Review composites: `build/scratch/variant_{buckler,kite,tower,brace}.png`
  (8 facings x idle/stance/shot, body+head composed).

## Still open (second bead)

The forge node `sheet` symbol is metadata only: the render still draws
`mh_weapon_gun` for the whole class. Wire node -> sheet kind -> Game cache ->
`weaponSheet(g)` so the equipped branch draws its own shield.
