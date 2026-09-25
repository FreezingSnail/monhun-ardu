#!/usr/bin/env python3
"""Derive every creature box from artist-painted sprite-overlay masks.

Epic monhun-ardu-ryh: the source of truth for a creature's geometry is
`images/masks/<art-sheet>_<cellW>x<cellH>.png` instead of the hand-authored
`box` / `collide` / `windows[].box` keys in `data/creatures/*.json`.

MASK FORMAT
-----------
One facing only (east); the packer already mirrors each column into the west
twin (bead ryh.2). A mask image is the sprite sheet's cell grid stacked as three
horizontal bands, top-to-bottom:

    band 0  collision   yellow  (255,255,0)                     = solid
    band 1  hitbox      orange  (255,128,0) = this column's window
                        (violet/cyan/magenta are reserved for a future
                        multi-rect column; a column paints exactly one rect)
    band 2  hurtbox     red     (255,0,0)  = head
                        blue    (0,0,255)  = appendage
                        green   (0,255,0)  = body (painted underneath)

Dimensions: W == (cellW + 2*marginX) * columns, H == (cellH + 2*marginY) * 3.
The cell is the mask's coordinate frame (named in the file); the body cell
origin is the cell origin (0,0). The per-side margins are derived from the image
dims vs the cell, and exist because regions reach outside the cell: the chicken
wing_beat window reaches 5 px left of the cell, the longtail tail sits 24 px
left of the body, and the bull stomp window reaches 2 px above/below the 22-tall
body. Every visible pixel must be exactly one palette colour; a region that is
not a single solid rectangle is a hard failure.

`columns` is the number of hitbox windows the mask owns (bead ryh.6): the hitbox
band has ONE COLUMN PER WINDOW, in the creature's packed window order, and the
whole mask is that many columns wide. A mask that owns no window (a beast sheet
whose attacks all draw their own art) still carries one column so its
collision/hurtbox bands have a frame; its hitbox band stays blank. The column
count comes from the creature JSON's attacks, not from the art sheet's pose
count -- the hurtbox/collision bands are column-invariant, so the old
art-pose tie is gone.

DERIVED DATA (`build/hitboxes.json`, consumed by gen-combat.py + gen-art.py)
--------------------------------------------------------------------------
    zone box   bbox of the red (head) / blue (appendage) region; its origin is
               the part-art anchor (gen-art crops the part sheet to the bbox).
    body box   bbox of the green body region; must equal the creature's
               stats.w/h (the packed body record + the window centre).
    collide    bbox of the yellow region (falls back to the body box).
    windows    hitbox region of each column (cell-relative, the body top-left
               is the cell origin), mapped onto the attack window it owns:
               column i = the creature's i-th packed window assigned to this
               mask (its attack's art sheet, or the beast sheet when the attack
               authors no art), then converted back to the body-centre-relative
               `windows[].box` form the packed record uses.

Behaviour keys (dmgMul, hp, bodyShare, breakTypes, ...) stay hand-authored in
the creature JSON; only geometry moves here. Multi-window kits (sweep gore,
ravager tail_sweep, heavy tail_spin) author one mask column per window too, so
no attack window is hand-authored any more.

PLAYER MASK (bead ryh.5)
------------------------
`images/masks/mh_player_base_16x16.png` has no creature JSON: its cell is the
16x16 player body cell, the collision/hurtbox bands are column-invariant (one
hurt region, the green body), and the hitbox band carries one column per weapon
attack entry, in table order (attacks 0..2, special, roll, alt, charge 0..1,
branches 0..2) for the three weapons -- 33 columns. The converter writes the
player body/collide box and a per-attack (reach, hw, hh) table to
`src/generated/player_boxes.hpp`, which game.hpp's WEAPON_DEFS and the player's
body literals read. A blank column is an all-zero attack (no box).

VALIDATION (all hard failures)
------------------------------
1. mask width == (cell + 2*margin) x the windows the mask owns (at least one
   column); height == (cell + 2*margin) x 3 bands; every mask symbol resolves;
   the margin on each side is a non-negative whole number of pixels;
2. every region is one solid rect (head/appendage/collide/window; the green body
   is the fallback painted underneath and may be overpainted by the zones);
3. zones do not overlap each other (they may sit on the body rect);
4. collision/hurtbox regions are identical on every column of a sheet;
5. hitbox columns: column i paints the i-th window assigned to the mask, every
   assigned column paints exactly one rect, no column past the assigned count
   paints anything, and a creature's columns across its masks total its window
   count;
6. the body region reproduces the creature's stats.w/h at the origin (so the
   packed body box and the window centre do not drift).

Usage:
    python3 tools/gen-hitboxes.py [--root DIR]
        validate images/masks/*.png and write build/hitboxes.json
    python3 tools/gen-hitboxes.py --render [--root DIR]
        write the composite review PNG (sprite row + the three mask rows + the
        boxes on the art) into build/scratch/hitbox_review.png.
"""
import argparse
import glob
import json
import os
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CREATURES_REL = "data/creatures"
MASKS_REL = "images/masks"
BLOCKS_REL = "images/blocks"
OUT_REL = "build/hitboxes.json"

BANDS = ("collision", "hitbox", "hurtbox")
BANDS_COUNT = len(BANDS)

# Exact palette from the epic. Anything else (including near-colours) fails.
HURT_COLORS = {(255, 0, 0): "head", (0, 0, 255): "appendage", (0, 255, 0): "body"}
COLLIDE_COLORS = {(255, 255, 0): "solid"}
# Window colours are window 1..N in order (the packed order the attack reads).
WINDOW_COLORS = [(255, 128, 0), (128, 0, 255), (0, 255, 255), (255, 0, 255)]
BAND_PALETTE = {"collision": COLLIDE_COLORS, "hitbox": dict(zip(WINDOW_COLORS, [1, 2, 3, 4])),
                "hurtbox": HURT_COLORS}

# Player mask (epic monhun-ardu-ryh, bead ryh.5). The player has no creature
# JSON, so its mask is resolved by symbol: cell 16x16 (the body cell), the
# collision/hurtbox bands column-invariant, and the hitbox columns one per
# WeaponDef attack entry so every weapon arc is paintable. The generated header
# (src/generated/player_boxes.hpp) is what game.hpp's WEAPON_DEFS and
# Player::init read; the column order below is the ABI the two sides share.
PLAYER_SYMBOL = "mh_player_base"
PLAYER_HEADER_REL = "src/generated/player_boxes.hpp"
# One entry per weapon, in mask hitbox-column order: attacks[0..2], special,
# roll, alt, charge[0..1], branches[0..2].
PLAYER_ENTRY_COLUMNS = (
    ("attacks", 0), ("attacks", 1), ("attacks", 2), ("special", 0),
    ("roll", 0), ("alt", 0), ("charge", 0), ("charge", 1),
    ("branch", 0), ("branch", 1), ("branch", 2),
)
PLAYER_WEAPON_COUNT = 3
PLAYER_NCOLS = PLAYER_WEAPON_COUNT * len(PLAYER_ENTRY_COLUMNS)

# Palette reverse maps for review images.
REV = {
    "collision": {"solid": (255, 255, 0, 255)},
    "hitbox": {1: (255, 128, 0, 255), 2: (128, 0, 255, 255), 3: (0, 255, 255, 255), 4: (255, 0, 255, 255)},
    "hurtbox": {"head": (255, 0, 0, 255), "appendage": (0, 0, 255, 255), "body": (0, 255, 0, 255)},
}


class MaskError(Exception):
    pass


def palette_reverse(band, kind):
    return REV[band][kind]


# ------------------------------------------------------------------ art sheets


def _find_block(root, sheet):
    for path in sorted(glob.glob(os.path.join(root, BLOCKS_REL, "%s_*.png" % sheet))):
        name = os.path.basename(path)[:-4]
        _, _, dims = name.rpartition("_")
        fw, _, fh = dims.partition("x")
        if fw.isdigit() and fh.isdigit():
            return path, int(fw), int(fh)
    return None, 0, 0


# ------------------------------------------------------------------- creatures


def load_creatures(root):
    creatures = {}
    for path in sorted(glob.glob(os.path.join(root, CREATURES_REL, "*.json"))):
        with open(path, encoding="utf-8") as handle:
            obj = json.load(handle)
        creatures[obj["id"]] = obj
    if not creatures:
        raise MaskError("no creature JSON under %s" % CREATURES_REL)
    return creatures


def discover_masks(root):
    """symbol -> (path, cell_w, cell_h) from images/masks/<symbol>_<W>x<H>.png."""
    found = {}
    for path in sorted(glob.glob(os.path.join(root, MASKS_REL, "*.png"))):
        name = os.path.basename(path)[:-4]
        stem, _, dims = name.rpartition("_")
        if not stem or "x" not in dims:
            raise MaskError("%s: want <symbol>_<W>x<H>.png" % name)
        cw, _, ch = dims.partition("x")
        if not cw.isdigit() or not ch.isdigit():
            raise MaskError("%s: want integer <symbol>_<W>x<H>.png dims" % name)
        found[stem] = (path, int(cw), int(ch))
    return found


def resolve_symbol(creatures, symbol):
    """Return (creature_id, role) for a mask symbol."""
    hits = []
    for cid, c in creatures.items():
        if c.get("art", {}).get("sheet") == symbol:
            hits.append((cid, "beast"))
        for atk in c.get("attacks", []):
            if atk.get("art", {}).get("sheet") == symbol:
                hits.append((cid, "attack"))
    if not hits:
        raise MaskError("%s: no creature or attack uses this art sheet" % symbol)
    if len({cid for cid, _ in hits}) > 1:
        raise MaskError("%s: art sheet shared by creatures %s" % (symbol, sorted({cid for cid, _ in hits})))
    return hits[0]


# ---------------------------------------------------------------- mask parsing


def _band_geometry(img, label, cell_w, cell_h, ncols):
    """(margin_x, margin_y, band_w, band_h) from the image dims vs the cell."""
    img_w, img_h = img.size
    if ncols <= 0 or img_w % ncols:
        raise MaskError("%s: width %d is not %d equal columns" % (label, img_w, ncols))
    band_w = img_w // ncols
    if (band_w - cell_w) < 0 or (band_w - cell_w) % 2:
        raise MaskError("%s: band width %d must be cell %d + an even margin" % (label, band_w, cell_w))
    if img_h % BANDS_COUNT:
        raise MaskError("%s: height %d is not %d stacked bands" % (label, img_h, BANDS_COUNT))
    band_h = img_h // BANDS_COUNT
    if (band_h - cell_h) < 0 or (band_h - cell_h) % 2:
        raise MaskError("%s: band height %d must be cell %d + an even margin" % (label, band_h, cell_h))
    return (band_w - cell_w) // 2, (band_h - cell_h) // 2, band_w, band_h


def _parse_mask_pixels(img, label, cell_w, cell_h, ncols):
    """Pixel scan + palette + solid-rect validation. Regions are cell-relative
    (the sprite cell origin is (0,0); margin pixels give negative coordinates)."""
    mx, my, band_w, band_h = _band_geometry(img, label, cell_w, cell_h, ncols)
    px = img.load()
    parsed = {"cell": (cell_w, cell_h), "ncols": ncols, "margin": (mx, my), "bands": {}}
    for bi, band in enumerate(BANDS):
        palette = BAND_PALETTE[band]
        cols = []
        for col in range(ncols):
            found = {color: [] for color in palette}
            x0 = col * band_w
            for y in range(band_h):
                for x in range(band_w):
                    r, g, b, a = px[x0 + x, bi * band_h + y]
                    if a == 0:
                        continue
                    if a != 255 or (r, g, b) not in palette:
                        raise MaskError("%s: %s band col %d pixel (%d,%d) colour %s not in palette"
                                        % (label, band, col, x - mx, y - my, (r, g, b, a)))
                    found[(r, g, b)].append((x - mx, y - my))
            cols.append(found)
        parsed["bands"][band] = cols
    _validate_columns_identical(label, parsed, "collision")
    _validate_columns_identical(label, parsed, "hurtbox")
    return parsed


def parse_mask(path, symbol, cell_w, cell_h, ncols):
    return _parse_mask_pixels(Image.open(path).convert("RGBA"), path, cell_w, cell_h, ncols)


def _region_stats(path, band, col, color, points, solid=True):
    box = bbox_of(points)
    if solid and len(points) != box["w"] * box["h"]:
        raise MaskError("%s: %s band col %d colour %s is not a solid rect (bbox %s, %d px)"
                        % (path, band, col, color, box_tuple(box), len(points)))
    return box


def bbox_of(points):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    x0, y0, x1, y1 = min(xs), min(ys), max(xs) + 1, max(ys) + 1
    return {"ox": x0, "oy": y0, "w": x1 - x0, "h": y1 - y0}


def box_tuple(b):
    return (b["ox"], b["oy"], b["w"], b["h"])


def boxes_to_tuple(box):
    return (box["ox"], box["oy"], box["w"], box["h"])


def _column_regions(path, band, col, found):
    out = {}
    for color, points in found.items():
        if points:
            # Body (green) may be overpainted; other hurt/collision regions are
            # single solid rects.
            solid = not (band == "hurtbox" and HURT_COLORS.get(color) == "body")
            out[color] = box_tuple(_region_stats(path, band, col, color, points, solid=solid))
    return out


def _validate_columns_identical(path, parsed, band):
    cols = parsed["bands"][band]
    ref = _column_regions(path, band, 0, cols[0])
    for i in range(1, len(cols)):
        if not ref == _column_regions(path, band, i, cols[i]):
            raise MaskError("%s: %s band col %d differs from col 0 (zones/collision must be "
                            "column-invariant)" % (path, band, i))


def hurt_regions(parsed, label):
    found = parsed["bands"]["hurtbox"][0]
    zones = {}
    for color, points in found.items():
        if points:
            # The body (green) is the fallback painted underneath the zones, so
            # it is explicitly allowed to be overpainted; the head/appendage
            # regions must each be one solid rect.
            zones[HURT_COLORS[color]] = _region_stats(
                label, "hurtbox", 0, color, points, solid=HURT_COLORS[color] != "body")
    return zones


def collide_region(parsed, label):
    found = parsed["bands"]["collision"][0]
    for color, points in found.items():
        if points:
            return _region_stats(label, "collision", 0, color, points)
    return None


def window_regions(parsed, col, label):
    found = parsed["bands"]["hitbox"][col]
    out = []
    gap = False
    for color in WINDOW_COLORS:
        points = found[color]
        if points:
            if gap:
                raise MaskError("%s: hitbox col %d window colours must be contiguous from 1" % (label, col))
            out.append(_region_stats(label, "hitbox", col, color, points))
        else:
            gap = True
    return out


def boxes_overlap(a, b):
    return (a["ox"] < b["ox"] + b["w"] and b["ox"] < a["ox"] + a["w"]
            and a["oy"] < b["oy"] + b["h"] and b["oy"] < a["oy"] + a["h"])


def _leaves_frame(label, name, box, parsed):
    cell_w, cell_h = parsed["cell"]
    mx, my = parsed["margin"]
    if box["ox"] < -mx or box["oy"] < -my or box["ox"] + box["w"] > cell_w + mx \
            or box["oy"] + box["h"] > cell_h + my:
        raise MaskError("%s: %s region %s leaves the mask cell + margin"
                        % (label, name, box_tuple(box)))


# ---------------------------------------------------------------- conversions


def cell_window_to_record(box, cell_w, cell_h):
    """Cell-relative hit rect -> the packed, body-centre-relative window box."""
    return {"ox": box["ox"] - cell_w // 2 + box["w"] // 2,
            "oy": box["oy"] - cell_h // 2 + box["h"] // 2,
            "w": box["w"], "h": box["h"]}


def record_window_to_cell(box, cell_w, cell_h):
    return {"ox": box["ox"] + cell_w // 2 - box["w"] // 2,
            "oy": box["oy"] + cell_h // 2 - box["h"] // 2,
            "w": box["w"], "h": box["h"]}


# ------------------------------------------------------------------- deriving


def derive(parsed, creature, label):
    """Validate + derive the beast-mask geometry."""
    stats = creature["stats"]
    zones = hurt_regions(parsed, label)
    collide = collide_region(parsed, label)
    names = set(creature.get("zones", {}))
    for name in zones:
        if name == "body":
            continue
        if name not in names:
            raise MaskError("%s: mask paints a %s region but the creature declares no such zone"
                            % (label, name))
    if "head" in names and "head" not in zones:
        raise MaskError("%s: creature declares a head zone but the mask paints none" % label)
    if "appendage" in names and "appendage" not in zones:
        raise MaskError("%s: creature declares an appendage zone but the mask paints none" % label)
    if "head" in zones and "appendage" in zones and boxes_overlap(zones["head"], zones["appendage"]):
        raise MaskError("%s: head and appendage hurtboxes overlap" % label)
    body = zones.get("body")
    if body is None:
        raise MaskError("%s: hurtbox band paints no body region" % label)
    if box_tuple(body) != (0, 0, stats["w"], stats["h"]):
        raise MaskError("%s: body region %s must equal the creature stats %dx%d at the origin"
                        % (label, box_tuple(body), stats["w"], stats["h"]))
    for name, box in zones.items():
        _leaves_frame(label, name, box, parsed)
    out = {"body": body, "zones": {n: zones[n] for n in ("head", "appendage") if n in zones}}
    collisions = collide if collide is not None else {"ox": 0, "oy": 0, "w": body["w"], "h": body["h"]}
    _leaves_frame(label, "collide", collisions, parsed)
    out["collide"] = collisions
    return out


def window_sheet(creature, attack):
    """The mask symbol a window belongs to: the attack's art sheet, or the
    creature's beast sheet when the attack authors no art (ravager bite /
    tail_sweep ride the fxmonster beast mask)."""
    art = attack.get("art") or {}
    sheet = art.get("sheet")
    if sheet:
        return sheet
    return creature.get("art", {}).get("sheet")


def window_assignment(creature):
    """{symbol: [(attack, window_index), ...]} in the creature's packed window
    order (attack source order, then window order).

    Every attack window is owned by exactly one mask; a mask's hitbox band
    paints one column per owned window, in this order (bead ryh.6)."""
    out = {}
    for atk in creature.get("attacks", []):
        windows = atk.get("windows") or []
        if not windows:
            continue
        symbol = window_sheet(creature, atk)
        if not symbol:
            raise MaskError("%s: attack %s has windows but no art sheet and no beast sheet"
                            % (creature.get("id"), atk.get("id")))
        owned = out.setdefault(symbol, [])
        for wi in range(len(windows)):
            owned.append((atk, wi))
    return out


def windows_for_symbol(parsed, assigned, label):
    """Validate the mask's hitbox columns and derive the owned window boxes.

    `assigned` is the packed (attack, window_index) list this mask owns: column
    i paints the i-th assigned window, each assigned column paints exactly one
    rect, and no column past the assigned count paints anything."""
    cell_w, cell_h = parsed["cell"]
    k = len(assigned)
    for col in range(parsed["ncols"]):
        regions = window_regions(parsed, col, label)
        if col < k:
            if len(regions) != 1:
                raise MaskError("%s: hitbox col %d must paint exactly one window rect (got %d)"
                                % (label, col, len(regions)))
        elif regions:
            raise MaskError("%s: hitbox col %d paints a window but only %d are assigned"
                            % (label, col, k))
    out = []
    for col, (atk, wi) in enumerate(assigned):
        region = window_regions(parsed, col, label)[0]
        out.append((atk["id"], wi, cell_window_to_record(region, cell_w, cell_h)))
    return out


def _require_blank_attack_bands(label, parsed):
    for band in ("collision", "hurtbox"):
        for col in range(parsed["ncols"]):
            if any(parsed["bands"][band][col].values()):
                raise MaskError("%s: an attack mask must not paint %s regions" % (label, band))


def _attack_cell_matches_stats(parsed, creature, label):
    stats = creature["stats"]
    if parsed["cell"] != (stats["w"], stats["h"]):
        raise MaskError("%s: attack mask cell %s must equal the creature body %dx%d (the window "
                        "centre is body-relative)" % (label, parsed["cell"], stats["w"], stats["h"]))


# ----------------------------------------------------------------- player mask


def derive_player(parsed, label):
    """Validate + derive the player body/collide box and one rect (or none) per
    mask hitbox column. The player has no hurt zones: one solid hurt region (the
    body painted green) is the whole contract."""
    found = parsed["bands"]["hurtbox"][0]
    zones = {}
    for color, points in found.items():
        if points:
            zones[HURT_COLORS[color]] = _region_stats(label, "hurtbox", 0, color, points, solid=True)
    extra = sorted(n for n in zones if n != "body")
    if extra:
        raise MaskError("%s: player mask paints %s (the player has no hurt zones)"
                        % (label, ", ".join(extra)))
    body = zones.get("body")
    if body is None:
        raise MaskError("%s: player hurtbox band paints no body region" % label)
    if body["w"] <= 0 or body["h"] <= 0:
        raise MaskError("%s: player body box %s is empty" % (label, box_tuple(body)))
    _leaves_frame(label, "body", body, parsed)
    collide = collide_region(parsed, label)
    if collide is None:
        collide = {"ox": body["ox"], "oy": body["oy"], "w": body["w"], "h": body["h"]}
    _leaves_frame(label, "collide", collide, parsed)
    columns = []
    for col in range(parsed["ncols"]):
        regions = window_regions(parsed, col, label)
        if len(regions) > 1:
            raise MaskError("%s: hitbox col %d paints %d windows (player attacks are single-window)"
                            % (label, col, len(regions)))
        columns.append(regions[0] if regions else None)
    if len(columns) != PLAYER_NCOLS:
        raise MaskError("%s: player mask has %d hitbox columns, want %d (one per attack entry)"
                        % (label, len(columns), PLAYER_NCOLS))
    return body, collide, columns


def cell_rect_to_attack(box, body, label, col):
    """Mask hit rect -> the packed attack box (reach, hw, hh). The rect is the
    sim's meleeHitbox() rect for an east-facing hunter: centred vertically on the
    body centre, offset `reach` along x. A rect that is not vertically centred
    cannot be represented (meleeHitbox has no vertical offset field)."""
    cx = body["ox"] + body["w"] // 2
    cy = body["oy"] + body["h"] // 2
    hw, hh = box["w"], box["h"]
    want_oy = cy - (hh >> 1)
    if box["oy"] != want_oy:
        raise MaskError("%s: hitbox col %d rect %s is not centred on the body y (want oy %d)"
                        % (label, col, box_tuple(box), want_oy))
    return [box["ox"] - cx + (hw >> 1), hw, hh]


def _player_header_text(body, collide, columns):
    lines = []
    lines.append("// Generated by tools/gen-hitboxes.py from")
    lines.append("// images/masks/mh_player_base_16x16.png (make gen). Do not hand-edit:")
    lines.append("// paint the mask and regenerate. The player body/collide box and")
    lines.append("// every weapon attack box (reach/hw/hh) come from this header;")
    lines.append("// game.hpp WEAPON_DEFS and Player::init read it.")
    lines.append("#pragma once")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("namespace mh {")
    lines.append("namespace playerboxes {")
    lines.append("")
    lines.append("// Player body box (the mask's green hurtbox region; its origin is the")
    lines.append("// art anchor, same contract as the creature zone boxes). collide is")
    lines.append("// the yellow region, the box syncMonsterTarget/pushApart resolve")
    lines.append("// against.")
    lines.append("constexpr int16_t BODY_OX = %d;" % body["ox"])
    lines.append("constexpr int16_t BODY_OY = %d;" % body["oy"])
    lines.append("constexpr int16_t BODY_W = %d;" % body["w"])
    lines.append("constexpr int16_t BODY_H = %d;" % body["h"])
    lines.append("constexpr int16_t COLLIDE_OX = %d;" % collide["ox"])
    lines.append("constexpr int16_t COLLIDE_OY = %d;" % collide["oy"])
    lines.append("constexpr int16_t COLLIDE_W = %d;" % collide["w"])
    lines.append("constexpr int16_t COLLIDE_H = %d;" % collide["h"])
    lines.append("")
    lines.append("// One attack: reach (centre offset along the facing vector), hw/hh (the")
    lines.append("// melee box size). A zero box is an entry the mask leaves blank.")
    lines.append("struct Box {")
    lines.append("    int16_t reach, hw, hh;")
    lines.append("};")
    lines.append("")
    lines.append("// Per weapon, mask hitbox-column order: attacks[0..2], special, roll,")
    lines.append("// alt, charge[0..1], branches[0..2].")
    lines.append("constexpr Box ATTACKS[%d][%d] = {" % (PLAYER_WEAPON_COUNT, len(PLAYER_ENTRY_COLUMNS)))
    for w in range(PLAYER_WEAPON_COUNT):
        base = w * len(PLAYER_ENTRY_COLUMNS)
        rows = []
        for i in range(len(PLAYER_ENTRY_COLUMNS)):
            box = columns[base + i]
            if box is None:
                rows.append("{0, 0, 0}")
            else:
                rows.append("{%d, %d, %d}" % (box[0], box[1], box[2]))
        lines.append("    {" + ", ".join(rows) + "},")
    lines.append("};")
    lines.append("")
    lines.append("}  // namespace playerboxes")
    lines.append("}  // namespace mh")
    return "\n".join(lines) + "\n"


# ----------------------------------------------------------------------- main


def process(root):
    creatures = load_creatures(root)
    masks = discover_masks(root)
    hitboxes = {}
    masked = set()
    player = None

    # Map every creature's windows onto their owning mask symbols first, so a
    # mask's hitbox column count is known before it is parsed (bead ryh.6).
    assignments = {}
    for cid, creature in creatures.items():
        assignments[cid] = window_assignment(creature)
    for cid, assign in assignments.items():
        for symbol in assign:
            if symbol not in masks:
                raise MaskError("%s: attack windows ride %s but no images/masks/%s_*.png exists"
                                % (cid, symbol, symbol))

    for symbol in sorted(masks):
        path, cell_w, cell_h = masks[symbol]
        if symbol.startswith("mh_map_"):
            # Room masks (bead ryh.7) share images/masks/ with the creature
            # masks but are compiled by tools/gen-zones.py, not here.
            continue
        if symbol == PLAYER_SYMBOL:
            parsed = parse_mask(path, symbol, cell_w, cell_h, PLAYER_NCOLS)
            body, collide, columns = derive_player(parsed, path)
            attacks = [cell_rect_to_attack(b, body, path, i) if b is not None else [0, 0, 0]
                       for i, b in enumerate(columns)]
            player = {"body": body, "collide": collide, "attacks": attacks}
            header = os.path.join(root, PLAYER_HEADER_REL)
            os.makedirs(os.path.dirname(header), exist_ok=True)
            with open(header, "w", encoding="utf-8", newline="\n") as handle:
                handle.write(_player_header_text(body, collide, attacks))
            print("gen-hitboxes: player boxes -> %s" % PLAYER_HEADER_REL)
            continue
        cid, role = resolve_symbol(creatures, symbol)
        creature = creatures[cid]
        assigned = assignments.get(cid, {}).get(symbol, [])
        # One hitbox column per owned window; a mask with no windows still needs
        # one column so its collision/hurtbox bands have a frame.
        ncols = max(1, len(assigned))
        parsed = parse_mask(path, symbol, cell_w, cell_h, ncols)
        entry = hitboxes.setdefault(cid, {})
        if role == "beast":
            if cell_w < creature["stats"]["w"] or cell_h < creature["stats"]["h"]:
                raise MaskError("%s: beast mask cell %dx%d is smaller than the body %dx%d"
                                % (path, cell_w, cell_h, creature["stats"]["w"], creature["stats"]["h"]))
            derived = derive(parsed, creature, path)
            entry["body"] = derived["body"]
            entry["zones"] = derived["zones"]
            entry["collide"] = derived["collide"]
            masked.add(cid)
        else:
            _attack_cell_matches_stats(parsed, creature, path)
            _require_blank_attack_bands(path, parsed)
        if assigned:
            for aid, wi, box in windows_for_symbol(parsed, assigned, path):
                entry.setdefault("windows", {}).setdefault(aid, {})[wi] = box

    for cid, entry in hitboxes.items():
        if "body" not in entry:
            raise MaskError("%s: attack mask without a beast mask" % cid)
        windows = entry.get("windows")
        if windows:
            entry["windows"] = {aid: [boxes[i] for i in sorted(boxes)]
                                for aid, boxes in windows.items()}

    # Column count == window count (bead ryh.6): every authored window is owned
    # by exactly one mask column, and no mask paints a column it does not own.
    for cid, creature in creatures.items():
        total = sum(len(atk.get("windows", [])) for atk in creature.get("attacks", []))
        got = sum(len(boxes) for boxes in hitboxes.get(cid, {}).get("windows", {}).values())
        if got != total:
            raise MaskError("%s: masks derive %d window(s), the creature authors %d"
                            % (cid, got, total))

    out = {"version": 2, "bands": list(BANDS),
           "creatures": {cid: hitboxes[cid] for cid in sorted(hitboxes)}}
    if player is not None:
        out["player"] = player
    path = os.path.join(root, OUT_REL)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(json.dumps(out, indent=2, sort_keys=True) + "\n")
    print("gen-hitboxes: %d masked creature(s) [%s] -> %s"
          % (len(hitboxes), ", ".join(sorted(masked)) or "-", OUT_REL))
    return out


# ------------------------------------------------------------------- review


def _review_panel(root, cid, sheet, role, entry, ncols):
    sprite_path, fw, fh = _find_block(root, sheet)
    if sprite_path is None:
        return None
    sprite = Image.open(sprite_path).convert("RGBA")
    masks = discover_masks(root)
    if sheet not in masks:
        return None
    mask_path, cell_w, cell_h = masks[sheet]
    mask = Image.open(mask_path).convert("RGBA")
    mx, my, band_w, band_h = _band_geometry(mask, mask_path, cell_w, cell_h, ncols)
    cols = ncols
    sprite_cols = sprite.width // fw

    width = max(sprite.width, band_w * cols)
    height = band_h * 4
    panel = Image.new("RGBA", (width, height), (16, 16, 24, 255))
    for col in range(cols):
        base = col * band_w
        if col < sprite_cols:
            panel.paste(sprite.crop((col * fw, 0, col * fw + fw, fh)), (base + mx, my))
        band = mask.crop((base, 0, base + band_w, band_h * 3))
        panel.paste(band, (base, band_h))

    draw = ImageDraw.Draw(panel)

    def outline(b, color, off_x, off_y):
        draw.rectangle([off_x + mx + b["ox"], off_y + my + b["oy"],
                        off_x + mx + b["ox"] + b["w"] - 1, off_y + my + b["oy"] + b["h"] - 1], outline=color)

    if role == "beast":
        for col in range(cols):
            base = col * band_w
            for name, b in entry.get("zones", {}).items():
                outline(b, (255, 0, 0, 255) if name == "head" else (0, 0, 255, 255), base, 0)
            outline(entry["collide"], (255, 255, 0, 255), base, 0)
    else:
        for aid, boxes in entry.get("windows", {}).items():
            for b in boxes:
                cell = record_window_to_cell(b, cell_w, cell_h)
                for col in range(cols):
                    outline(cell, (255, 128, 0, 255), col * band_w, 0)
    return panel


def _compose_review(root):
    panels = []
    hitboxes_path = os.path.join(root, OUT_REL)
    entries = {}
    if os.path.isfile(hitboxes_path):
        with open(hitboxes_path, encoding="utf-8") as handle:
            entries = json.load(handle).get("creatures", {})
    creatures = load_creatures(root)
    for cid, entry in sorted(entries.items()):
        creature = creatures.get(cid)
        if not creature:
            continue
        assign = window_assignment(creature)
        beast = creature.get("art", {}).get("sheet")
        if not beast:
            continue
        panels.append(_review_panel(root, cid, beast, "beast", entry, max(1, len(assign.get(beast, [])))))
        for asheet in sorted({atk["art"]["sheet"] for atk in creature.get("attacks", [])
                              if atk.get("art")}):
            panels.append(_review_panel(root, cid, asheet, "attack",
                                        {"windows": entry.get("windows", {})},
                                        max(1, len(assign.get(asheet, [])))))
    panels = [p for p in panels if p is not None]
    if not panels:
        return
    width = max(p.size[0] for p in panels)
    height = sum(p.size[1] + 4 for p in panels)
    review = Image.new("RGBA", (width, height), (16, 16, 24, 255))
    y = 0
    for panel in panels:
        review.paste(panel, (0, y))
        y += panel.size[1] + 4
    review = review.resize((width * 2, height * 2), Image.NEAREST)
    out_dir = os.path.join(root, "build", "scratch")
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "hitbox_review.png")
    review.save(path)
    print("gen-hitboxes: review image -> %s" % os.path.relpath(path, root))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=ROOT, help="pipeline root (default: repo)")
    parser.add_argument("--render", action="store_true", help="write the review PNG")
    args = parser.parse_args(argv)
    root = os.path.abspath(args.root)
    try:
        process(root)
        if args.render:
            _compose_review(root)
    except MaskError as exc:
        print("gen-hitboxes: FAIL: %s" % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
