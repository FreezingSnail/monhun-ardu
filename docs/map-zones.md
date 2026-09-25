# Map / room-graph data (epic monhun-ardu-fie)

`data/map.json` is the source of truth for the demo overworld: a graph of rooms
with per-room art, named spawns, door rects, heal rects, props and an optional
monster. `tools/gen-zones.py` compiles it into the packed `mhZones` FX section
plus host/`device` headers and the room layer arrays the render path blits.
Edges are implicit in doors; there is no separate edge table.

The runtime loader (fie.4) reads the `mhZones` blob through `core/fxmem.hpp`;
the render path (fie.5) blits the room layers with `seekData`.

## JSON schema (data/map.json)

```
{
  "version": 1,
  "rooms": [
    {
      "id": "camp",                       // [a-z][a-z0-9_]*, <= 31 chars, unique
      "w": 128, "h": 56,                  // px; h must be a multiple of 8
      "image": "images/maps/mh_map_camp_128x56.png",
      "props":  [ { "type": "tent", "x": 40, "y": 8,
                    "sheet": "mh_map_tent", "frame": 0, "w": 32, "h": 24 },
                  { "type": "post", "x": 8, "y": 8,
                    "sheet": "mh_map_tent", "frame": 0, "w": 8, "h": 8,
                    "gather": { "item": "herb", "yield": 1 } } ],
      "spawns": { "entry": { "x": 20, "y": 44 } },   // non-empty
      "doors":  [ { "x": 120, "y": 24, "w": 8, "h": 24,
                    "to": "area", "toSpawn": "from_camp" } ],
      "heal":   [ { "x": 40, "y": 8, "w": 32, "h": 24 } ],
      "monster": { "kind": "lunge", "spawn": "start" }   // or null
    }
  ]
}
```

- `props[]`: `type` is one of `tent`, `door`, `pole`, `post` (`pole` is a
  legacy type token only — prg.8 removed the pole room); `sheet` is a C
  symbol that must resolve in `fxdata/fxdata.h` once its art is authored
  (fie.5). `frame`, `w`, `h` are u8, `x`/`y` u16.
- `props[].gather` (optional, bead monhun-ardu-feel.21; item table prg.2): a
  gather node. `item` names one of the gatherable item ids (`herb`,
  `blue_mushroom`, `ore`, `bug`) and must exist in `data/items.json`; `yield` is
  an integer 1..9. The prop's own `x`/`y`/`w`/`h` is the gather rect (it must
  stay inside the room like any prop rect). Unknown keys are errors. The packed
  record stores the item index+1 so a plain prop reads `GATHER_NONE` (0); the
  symbolic `GATHER_*` values live in `src/generated/zone_meta.hpp` and equal the
  `item::ITEM_<NAME>` index + 1. Gather nodes reuse an existing prop sheet for
  now — the render side draws a small procedural shape keyed off `gatherItem`
  (herb plant / mushroom / ore / bug silhouettes, prg.4), so no new art sheet
  is required.
- `doors[]`: `to` is another room id, or the reserved `"menu"` (exit to the
  opening menu). A door to a room **requires** `toSpawn` naming a spawn in the
  target room; a door to `"menu"` must **not** carry `toSpawn`.
- `heal[]`: heal rects (u16 x/y, u8 w/h).
- `monster`: optional `{kind, spawn}`; `kind` is one of `lunge`, `sweep`,
  `heavy`, `ravager` (values mirror `MonsterKind` in `src/core/game.hpp`), and
  `spawn` must name a spawn in the same room.
- All coordinates/rects must stay inside the room's own `w`/`h`. Every field
  is integer-only (floats/bools are rejected); unknown keys are errors.

### Authored gather nodes (bead prg.4)

| room | prop | item | yield | rect |
|---|---|---|---|---|
| camp | `PROP_CAMP_1` | herb | 1 | 8,8,8,8 |
| camp | `PROP_CAMP_2` | herb | 2 | 72,40,8,8 |
| camp | `PROP_CAMP_3` | blue_mushroom | 1 | 24,8,8,8 |
| area | `PROP_AREA_0` | herb | 1 | 40,16,8,8 |
| area | `PROP_AREA_1` | herb | 2 | 160,40,8,8 |
| area | `PROP_AREA_2` | herb | 3 | 280,88,8,8 |
| area | `PROP_AREA_3` | blue_mushroom | 1 | 96,80,8,8 |
| area | `PROP_AREA_4` | blue_mushroom | 2 | 216,16,8,8 |
| area | `PROP_AREA_5` | ore | 1 | 120,88,8,8 |
| area | `PROP_AREA_6` | ore | 2 | 352,16,8,8 |
| area | `PROP_AREA_7` | bug | 1 | 200,96,8,8 |

Nodes stay clear of spawns, doors, heal rects and the monster start; every
node is picked once per hunt (`Game::gatherMask`, reset only by `newGame`).

`image` must be `images/maps/<room symbol>_<W>x<H>.png` with `<room symbol>` =
`mh_map_<id>` and exactly the room's `W`x`H`. The generator authors a
deterministic flat-shade placeholder only when that PNG is missing, so refined
art is never overwritten. Sprites `images/maps/*.png` are declared by the
generated `fxdata/maps/Sprites.txt`.

## Packed blob — `fxdata/tables/zones.bin` (`mhZones` raw_t)

Little-endian, explicit u8/u16, no padding, fixed section order. Rooms sort by
id; spawns sort by name inside a room; doors/props/heals keep source order
inside a room.

| section | size | fields |
|---|---|---|
| header | 16 B | magic u16 `0x5A52`, version u8, flags u8, rooms u16, doors u16, spawns u16, props u16, heals u16, reserved u16 |
| room | 18 B | w u16, h u16, firstDoor u16, doorCount u8, firstSpawn u16, spawnCount u8, firstProp u16, propCount u8, firstHeal u16, healCount u8, monsterKind u8, monsterSpawn u8 |
| door | 10 B | x u16, y u16, w u16, h u16, toRoom u8, toSpawn u8 |
| spawn | 4 B | x u16, y u16 |
| prop | 11 B | type u8, x u16, y u16, sheet u8, frame u8, w u8, h u8, gatherItem u8, gatherYield u8 |
| heal | 6 B | x u16, y u16, w u8, h u8 |

- `first*` are global section indices; per-room counts are u8 (<= 255).
- `monsterKind` is `MONSTER_NONE` (`0xFF`) when the room has no monster, else
  the `MONSTER_*` index into `MONSTER_KINDS`
  (`lunge=0, sweep=1, heavy=2, ravager=3`).
- `monsterSpawn` and `door.toSpawn` are global spawn indices; a door to
  `"menu"` stores `toRoom` = `DOOR_MENU` (`0xFF`) and `toSpawn` = `0xFF`.
- `prop.sheet` is an index into the prop sheet list (the distinct `sheet`
  symbols in first-seen room order), not a raw fxdata address. The generated
  `SHEET_<SYMBOL>_OFF` constants carry the FX-image address.
- `prop.gatherItem` is `GATHER_NONE` (0) for a plain prop or the item index+1
  (`GATHER_HERB` = 1, `GATHER_BLUE_MUSHROOM` = 2, `GATHER_ORE` = 3,
  `GATHER_BUG` = 4, resolved against `data/items.json`); `prop.gatherYield` is
  the authored yield (1..9) or 0 when `gatherItem` is `GATHER_NONE`.

Size limits (validated): <= 254 rooms, <= 65535 records per section, <= 255
records per room, <= 255 spawns total (spawn index is u8), blob < 65536 B.

### Symbolic ids

`src/generated/zone_meta.hpp` emits `ROOM_*`, `SPAWN_<ROOM>_<NAME>`,
`DOOR_<ROOM>_<local>`, `PROP_<ROOM>_<local>`, `HEAL_<ROOM>_<local>` indices
plus `*_OFF` blob offsets, `PROP_*`/`MONSTER_*`/`DOOR_MENU` value tables, and
the per-room room-image base + layer-size constants. Host/test code references
these symbolic constants only — never a literal record index.
`src/generated/zone_data.hpp` mirrors the blob as plain host structs/arrays for
the host suites.

## Room layer format (the fie.5 blit source)

Each room image is converted to 3 nested 1bpp planes (4-shade `L4_Triplane`) in
SSD1306 page-major order:

```
layer p byte at   p * (w * h/8)  +  (y/8) * w  +  x     // one byte == one 8-px column
bit (y & 7) set    <=>  pixel (x,y) lights plane p
bytes per layer    = w * (h/8),  total = 3 * w * (h/8)
```

A pixel lights plane `p` (0..2) when its red channel reaches the `(p+1)`-th
step of the `254/4` ramp (thresholds 64/128/192) — the same conversion as
`tools/convert-sprite.py get_shade`, so DARK/LIGHT/WHITE nest on planes
1/2/3. A fully transparent pixel (alpha < 128) lights no plane (shade 0).

The three layers concatenate in plane order into the room's `uint8_t
mh_map_<id>[]` array in `fxdata/maps/Sprites.txt` (an `include` section of the
one FX image). fie.5 samples:

```
seekData(mh_map_<id> + plane * ROOM_<ID>_IMAGE_LAYER_BYTES
                     + (y/8) * W + x)
```

`ROOM_<ID>_IMAGE_OFF` (the array's FX address) is baked from `fxdata/fxdata.h`;
an AVR-only `static_assert` pins it against the live symbol, so a stale blob
(fxdata.h changed without a regen) fails the device build.

Placeholder art is a deterministic dark field with a light border and a white
centre marker — enough to prove clipping/shift; fie.5 refines the real art.

## Room masks (bead monhun-ardu-ryh.7)

A room's geometry is sourced from `images/masks/mh_map_<room>_<W>x<H>.png`
instead of the hand `x`/`y`/`w`/`h` in `data/map.json`. The mask is the room
grid (`W`x`H`) stacked as four horizontal bands, same solids-only idea as the
creature masks (`tools/gen-hitboxes.py`):

| band | colour | feeds |
|---|---|---|
| props | one colour per prop type (`tent`/`door`/`pole`/`post`/`smithy`) | the non-gather prop rects |
| gather | one colour per gather item (`herb`/`blue_mushroom`/`ore`/`bug`) | the gather-node prop rects |
| doors | one colour per door (room-local order) | the door rects |
| heal | one colour per heal rect | the heal rects |

Every painted connected region must be one solid rectangle; prop/gather rects
are consumed in canonical order (left-to-right, then top-to-bottom) and matched
to the JSON entries in source order, while door/heal colours are per entry (so
their pairing is order-independent). Validations (hard fail): every rect inside
the room bounds, gather rects non-empty, each door touches the room edge it
leaves through, prop/gather rect counts match the JSON, and unknown/hand
geometry keys (`x`/`y`/`w`/`h`, `heal`, `smithy`) are rejected on a masked room.
The `heal` section comes from the mask's heal band and the `smithy` section from
the `type=smithy` prop rect.

A masked room keeps behaviour only in `data/map.json`: `props[]` `type`/`sheet`/
`frame`/`gather`, `doors[]` `to`/`toSpawn`, `spawns`, `monster`. Rects are the
same after the migration (the packed blob is byte-identical). A room without a
mask (the unit-test fixtures) keeps the legacy hand-geometry path;
`python3 tools/gen-zones.py --bootstrap` authors a starting mask from the current
geometry and `--render` writes `build/scratch/roommask_review.png` (room art +
the four mask bands + the derived rects outlined).

## Generation / workflow

```
python3 tools/gen-zones.py [--root DIR] [--dump]
```

`--dump` validates and prints the compiled graph (rooms, sizes, doors, props,
spawns, heals, blob size) and writes nothing. `make gen` runs the tool (before
`fxdata-build.py`) via `tools/gen.sh`.

Two-pass note: the baked `ROOM_*_IMAGE_OFF` / `SHEET_*_OFF` addresses come from
the *previous* run's `fxdata/fxdata.h`, so adding/renaming a room symbol (or
any change that shifts FX addresses ahead of the maps section) needs a second
`make gen` to re-bake. `make gen-check` runs from the committed (stable) state
and fails if any generated artifact drifts.

Generated artifacts (never hand-edit): `images/maps/*.png` (placeholders only),
`fxdata/maps/Sprites.txt`, `fxdata/tables/zones.bin`, `src/generated/zone_data.hpp`,
`src/generated/zone_meta.hpp`, and `fxdata/manifest.json`. The room masks under
`images/masks/` are hand-authored sources (`fxdata_manifest` tracks them as
inputs; a mask edit without a regen fails `make gen-check`).

## Tests

`tools/tests/test_gen_zones.py` (native `unittest`, run `make test-tools`)
covers schema/id/cross-ref/integer errors, the reserved `"menu"` door, gather
nodes (item/yield round-trip, unknown item, bad yield, out-of-room rect),
determinism, `--dump` smoke, size limits and a layer pixel round-trip against a
hand-authored PNG.
