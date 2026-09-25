#!/usr/bin/env python3
"""Derive every creature box from artist-painted sprite-overlay masks.

Epic monhun-ardu-ryh (phase 1 spike, bead ryh.3): the source of truth for a
creature's geometry becomes `images/masks/<art-sheet>_<cellW>x<cellH>.png`
instead of the hand-authored `box` / `collide` / `windows[].box` keys in
`data/creatures/*.json`.

MASK FORMAT
-----------
One facing only (east); the packer already mirrors each column into the west
twin (bead ryh.2). A mask image is the sprite sheet's cell grid stacked as three
horizontal bands, top-to-bottom:

    band 0  collision   yellow  (255,255,0)                     = solid
    band 1  hitbox      orange  (255,128,0) violet (128,0,255)
                        cyan    (0,255,255) magenta (255,0,255) = window 1..N
    band 2  hurtbox     red     (255,0,0)  = head
                        blue    (0,0,255)  = appendage
                        green   (0,255,0)  = body (painted underneath)

Dimensions: W == (cellW + 2*MARGIN) * source_columns, H == cellH * 3. The
sprite/zone cell sits at a MARGIN px inset inside each column; the margin exists
because an attack window is body-centre relative and reaches behind the sprite
cell (the chicken wing_beat box is 26 wide at ox -8, so its left edge is 5 px
left of the cell). Every visible pixel must be exactly one palette colour; a
region that is not a single solid rectangle is a hard failure.

DERIVED DATA (`build/hitboxes.json`, consumed by gen-combat.py + gen-art.py)
---------------------------------------------------------------------------
    zone box   bbox of the red (head) / blue (appendage) region; its origin is
               the part-art anchor (gen-art crops the part sheet to the bbox).
    body box   bbox of the green body region (creature stats w/h).
    collide    bbox of the yellow region.
    windows    hitbox regions of a column (cell-relative, the monster top-left
               is the cell origin), mapped onto the attack whose `art.frame`
               selects that column (east ordinal = art.frame // 2), then
               converted back to the body-centre-relative `windows[].box` form
               the packed record uses.

Behaviour keys (dmgMul, hp, bodyShare, breakTypes, ...) stay hand-authored in
the creature JSON; only geometry moves here. Creatures without a mask keep
their hand boxes (the ryh.4 migration moves the rest).

VALIDATION (all hard failures)
------------------------------
1. mask dims == cell x source columns (x3 bands); every mask symbol resolves;
2. every region is one solid rect (head/appendage/collide/window; the green body
   is the fallback painted underneath and may be overpainted by the zones);
3. zones do not overlap each other (they may sit on the body rect);
4. collision/hurtbox regions are identical on every column of a sheet;
5. hitbox columns match the creature's attacks by `art.frame` (single-window
   attacks only; the multi-window heavy/ravager/sweep kits stay hand-authored
   until phase 2 decides their encoding);
6. round trip: bootstrap masks from the shipped JSON -> the geometry reproduces
   those records bit-for-bit (proves the pipeline before a region is authored).

Usage:
    python3 tools/gen-hitboxes.py [--root DIR]
        validate images/masks/*.png and write build/hitboxes.json
    python3 tools/gen-hitboxes.py --render [--root DIR]
        write bootstrap masks for every in-cell creature (JSON boxes -> PNG) into
        images/masks/ when absent, and a composite review PNG (sprite row + the
        three mask rows + the boxes on the art) into build/scratch/.
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

# The per-column inset that makes body-centre-relative windows reachable: the
# most negative chicken window edge is -5 (wing_beat ox -8, w 26), the widest
# positive edge is 36 (leap ox 12 w 18 -> centre 28 + 9). 8 px both sides is
# enough with headroom; the converter fails if a region paints past it.
MARGIN = 8

# Exact palette from the epic. Anything else (including near-colours) fails.
HURT_COLORS = {(255, 0, 0): "head", (0, 0, 255): "appendage", (0, 255, 0): "body"}
COLLIDE_COLORS = {(255, 255, 0): "solid"}
# Window colours are window 1..N in order (the packed order the attack reads).
WINDOW_COLORS = [(255, 128, 0), (128, 0, 255), (0, 255, 255), (255, 0, 255)]
BAND_PALETTE = {"collision": COLLIDE_COLORS, "hitbox": dict(zip(WINDOW_COLORS, [1, 2, 3, 4])),
                "hurtbox": HURT_COLORS}

# The one-facing beast sources carry BEAST_POSES east frames (7); attack sources
# carry one column per authored east ordinal (the packer doubles them).
BEAST_SOURCE_COLUMNS = 7

# Palette reverse maps for --render bootstrap images.
REV = {
    "collision": {"solid": (255, 255, 0, 255)},
    "hitbox": {1: (255, 128, 0, 255), 2: (128, 0, 255, 255), 3: (0, 255, 255, 255), 4: (255, 0, 255, 255)},
    "hurtbox": {"head": (255, 0, 0, 255), "appendage": (0, 0, 255, 255), "body": (0, 255, 0, 255)},
}


class MaskError(Exception):
    pass


def band_width(cell_w):
    return cell_w + 2 * MARGIN


def cell_to_pixel(col, cell_w, x):
    return col * band_width(cell_w) + MARGIN + x


def bbox_of(points):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    x0, y0, x1, y1 = min(xs), min(ys), max(xs) + 1, max(ys) + 1
    return {"ox": x0, "oy": y0, "w": x1 - x0, "h": y1 - y0}


def box_tuple(b):
    return (b["ox"], b["oy"], b["w"], b["h"])


def boxes_to_tuple(box):
    return (box["ox"], box["oy"], box["w"], box["h"])


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
    """stem -> path for images/masks/<symbol>_<W>x<H>.png."""
    found = {}
    for path in sorted(glob.glob(os.path.join(root, MASKS_REL, "*.png"))):
        name = os.path.basename(path)
        stem, _, dims = name[:-4].rpartition("_")
        if not stem or "x" not in dims:
            raise MaskError("%s: want <symbol>_<W>x<H>.png" % name)
        found[stem] = path
    return found


def resolve_symbol(creatures, symbol):
    """Return (creature_id, role, cell_w, cell_h) for a mask symbol."""
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
    cid, role = hits[0]
    stats = creatures[cid]["stats"]
    return cid, role, stats["w"], stats["h"]


def source_columns(creature, role, symbol):
    if role == "beast":
        return BEAST_SOURCE_COLUMNS
    frames = {atk["art"]["frame"] // 2 for atk in creature.get("attacks", [])
              if atk.get("art", {}).get("sheet") == symbol}
    return (max(frames) + 1) if frames else 0


# ---------------------------------------------------------------- mask parsing


def _parse_mask_pixels(img, label, cell_w, cell_h, ncols):
    """Pixel scan + palette + solid-rect validation. Regions are cell-relative
    (the sprite cell origin is (0,0); margin pixels give negative coordinates)."""
    want = (band_width(cell_w) * ncols, cell_h * BANDS_COUNT)
    if img.size != want:
        raise MaskError("%s: dims %dx%d, want %dx%d (cell %dx%d, margin %d, %d cols x %d bands)"
                        % (label, img.size[0], img.size[1], want[0], want[1],
                           cell_w, cell_h, MARGIN, ncols, BANDS_COUNT))
    px = img.load()
    parsed = {"cell": (cell_w, cell_h), "ncols": ncols, "bands": {}}
    for bi, band in enumerate(BANDS):
        palette = BAND_PALETTE[band]
        cols = []
        for col in range(ncols):
            found = {color: [] for color in palette}
            x0 = col * band_width(cell_w)
            for y in range(cell_h):
                for x in range(band_width(cell_w)):
                    r, g, b, a = px[x0 + x, bi * cell_h + y]
                    if a == 0:
                        continue
                    if a != 255 or (r, g, b) not in palette:
                        raise MaskError("%s: %s band col %d pixel (%d,%d) colour %s not in palette"
                                        % (label, band, col, x - MARGIN, y, (r, g, b, a)))
                    found[(r, g, b)].append((x - MARGIN, y))
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
    for name, box in zones.items():
        if box["ox"] < 0 or box["oy"] < 0 or box["ox"] + box["w"] > parsed["cell"][0] \
                or box["oy"] + box["h"] > parsed["cell"][1]:
            raise MaskError("%s: %s region %s leaves the sprite cell" % (label, name, box_tuple(box)))
    out = {"body": body, "zones": {n: zones[n] for n in ("head", "appendage") if n in zones}}
    out["collide"] = collide if collide is not None else {"ox": 0, "oy": 0, "w": body["w"], "h": body["h"]}
    return out


def windows_for_attacks(parsed, creature, symbol, label):
    """Map hitbox columns back onto the attacks whose art.frame selects them."""
    out = {}
    mapped = 0
    for atk in creature.get("attacks", []):
        art = atk.get("art")
        if not art or art.get("sheet") != symbol:
            continue
        col = art["frame"] // 2
        if col >= parsed["ncols"]:
            raise MaskError("%s: attack %s frame %d maps column %d past the mask"
                            % (label, atk["id"], art["frame"], col))
        regions = window_regions(parsed, col, label)
        want = len(atk.get("windows", []))
        if want > 1:
            raise MaskError("%s: attack %s has %d windows; multi-window kits stay hand-authored "
                            "until phase 2" % (label, atk["id"], want))
        if len(regions) != want:
            raise MaskError("%s: attack %s wants %d window(s), mask column %d paints %d"
                            % (label, atk["id"], want, col, len(regions)))
        out[atk["id"]] = [cell_window_to_record(b, parsed["cell"][0], parsed["cell"][1]) for b in regions]
        mapped += 1
    for col in range(parsed["ncols"]):
        if window_regions(parsed, col, label) and not any(
                a.get("art", {}).get("sheet") == symbol and a["art"]["frame"] // 2 == col
                for a in creature.get("attacks", [])):
            raise MaskError("%s: hitbox column %d maps to no attack" % (label, col))
    if mapped == 0:
        raise MaskError("%s: mask carries hitbox columns but no attack uses the sheet" % label)
    return out


def _require_blank_attack_bands(label, parsed):
    for band in ("collision", "hurtbox"):
        for col in range(parsed["ncols"]):
            if any(parsed["bands"][band][col].values()):
                raise MaskError("%s: an attack mask must not paint %s regions" % (label, band))


# ------------------------------------------------------------------- bootstrap


def bootstrap_mask(creature, role, symbol, cell_w, cell_h, ncols):
    """Render the shipped JSON geometry as a mask image (--render + round trip)."""
    img = Image.new("RGBA", (band_width(cell_w) * ncols, cell_h * BANDS_COUNT), (0, 0, 0, 0))
    px = img.load()
    stats = creature["stats"]
    body = {"ox": 0, "oy": 0, "w": stats["w"], "h": stats["h"]}
    zones = {name: creature["zones"][name]["box"] for name in creature.get("zones", {})}
    collide = creature.get("collide") or body

    def paint(band, col, kind, box):
        color = REV[band][kind]
        for y in range(box["oy"], box["oy"] + box["h"]):
            if not 0 <= y < cell_h:
                continue
            for x in range(box["ox"], box["ox"] + box["w"]):
                if not -MARGIN <= x < cell_w + MARGIN:
                    continue
                px[cell_to_pixel(col, cell_w, x), BANDS.index(band) * cell_h + y] = color

    if role == "beast":
        for col in range(ncols):
            paint("hurtbox", col, "body", body)
            for name in ("head", "appendage"):
                if name in zones:
                    paint("hurtbox", col, name, zones[name])
            paint("collision", col, "solid", collide)
    else:
        for atk in creature.get("attacks", []):
            art = atk.get("art")
            if not art or art.get("sheet") != symbol:
                continue
            col = art["frame"] // 2
            for i, win in enumerate(atk.get("windows", [])):
                cell = record_window_to_cell(win["box"], cell_w, cell_h)
                paint("hitbox", col, i + 1, cell)
    return img


def round_trip(creature, role, symbol, cell_w, cell_h, ncols):
    """Bootstrap masks from JSON and assert the derived geometry is identical."""
    img = bootstrap_mask(creature, role, symbol, cell_w, cell_h, ncols)
    parsed = _parse_mask_pixels(img, "%s (%s round trip)" % (creature["id"], symbol),
                                cell_w, cell_h, ncols)
    if role == "attack":
        got = windows_for_attacks(parsed, creature, symbol, creature["id"])
        for atk in creature.get("attacks", []):
            if atk.get("art", {}).get("sheet") == symbol:
                want = [box_tuple(w["box"]) for w in atk["windows"]]
                if [boxes_to_tuple(b) for b in got[atk["id"]]] != want:
                    raise MaskError("%s: round trip %s windows differ" % (creature["id"], atk["id"]))
        return
    derived = derive(parsed, creature, "%s (round trip)" % creature["id"])
    stats = creature["stats"]
    if box_tuple(derived["body"]) != (0, 0, stats["w"], stats["h"]):
        raise MaskError("%s: round trip body %s != shipped %dx%d"
                        % (creature["id"], box_tuple(derived["body"]), stats["w"], stats["h"]))
    for name in creature.get("zones", {}):
        if box_tuple(derived["zones"][name]) != box_tuple(creature["zones"][name]["box"]):
            raise MaskError("%s: round trip %s box %s != shipped %s"
                            % (creature["id"], name, box_tuple(derived["zones"][name]),
                               box_tuple(creature["zones"][name]["box"])))
    want_col = creature.get("collide") or {"ox": 0, "oy": 0, "w": stats["w"], "h": stats["h"]}
    if box_tuple(derived["collide"]) != box_tuple(want_col):
        raise MaskError("%s: round trip collide %s != shipped %s"
                        % (creature["id"], box_tuple(derived["collide"]), box_tuple(want_col)))


# ----------------------------------------------------------------------- main


def process(root):
    creatures = load_creatures(root)
    masks = discover_masks(root)
    hitboxes = {}
    masked = set()

    for symbol in sorted(masks):
        path = masks[symbol]
        cid, role, cell_w, cell_h = resolve_symbol(creatures, symbol)
        creature = creatures[cid]
        ncols = source_columns(creature, role, symbol)
        if ncols <= 0:
            raise MaskError("%s: could not derive a source column count" % path)
        parsed = parse_mask(path, symbol, cell_w, cell_h, ncols)
        entry = hitboxes.setdefault(cid, {})
        if role == "beast":
            derived = derive(parsed, creature, path)
            entry["body"] = derived["body"]
            entry["zones"] = derived["zones"]
            entry["collide"] = derived["collide"]
            masked.add(cid)
        else:
            _require_blank_attack_bands(path, parsed)
            entry.setdefault("windows", {}).update(windows_for_attacks(parsed, creature, symbol, path))

    for cid, entry in hitboxes.items():
        if "body" not in entry:
            raise MaskError("%s: attack mask without a beast mask" % cid)

    # Round trip every committed mask against the shipped JSON (only geometry
    # that fits the cell can be bootstrapped; out-of-cell / multi-window kits
    # stay hand-authored; the chicken is the spike target).
    for symbol in sorted(masks):
        cid, role, cell_w, cell_h = resolve_symbol(creatures, symbol)
        creature = creatures[cid]
        ncols = source_columns(creature, role, symbol)
        if role == "beast":
            if _creature_in_cell(creature, cell_w, cell_h):
                round_trip(creature, role, symbol, cell_w, cell_h, ncols)
        elif all(len(a.get("windows", [])) == 1 for a in creature.get("attacks", [])
                 if a.get("art", {}).get("sheet") == symbol):
            round_trip(creature, role, symbol, cell_w, cell_h, ncols)

    out = {"version": 1, "masks": BANDS, "margin": MARGIN,
           "creatures": {cid: hitboxes[cid] for cid in sorted(hitboxes)}}
    path = os.path.join(root, OUT_REL)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(json.dumps(out, indent=2, sort_keys=True) + "\n")
    print("gen-hitboxes: %d masked creature(s) [%s] -> %s"
          % (len(hitboxes), ", ".join(sorted(masked)) or "-", OUT_REL))
    return out


def _collide_in_cell(creature, cell_w, cell_h):
    box = creature.get("collide")
    if box is None:
        return True
    return box["ox"] >= 0 and box["oy"] >= 0 and box["ox"] + box["w"] <= cell_w and box["oy"] + box["h"] <= cell_h


def _creature_in_cell(creature, cell_w, cell_h):
    for name in creature.get("zones", {}):
        b = creature["zones"][name]["box"]
        if b["ox"] < 0 or b["oy"] < 0 or b["ox"] + b["w"] > cell_w or b["oy"] + b["h"] > cell_h:
            return False
    return _collide_in_cell(creature, cell_w, cell_h)


def _attack_fits(creature, symbol, cell_w, cell_h):
    for atk in creature.get("attacks", []):
        if atk.get("art", {}).get("sheet") != symbol:
            continue
        if len(atk.get("windows", [])) != 1:
            return False
        cell = record_window_to_cell(atk["windows"][0]["box"], cell_w, cell_h)
        if cell["ox"] < -MARGIN or cell["oy"] < 0 or cell["ox"] + cell["w"] > cell_w + MARGIN \
                or cell["oy"] + cell["h"] > cell_h:
            return False
    return True


def render_bootstrap(root, creatures, masks_dir):
    """Write bootstrap masks (JSON boxes -> PNG) for every in-cell creature into
    `masks_dir` (a scratch review area; the committed images/masks sources are
    hand-authored and never regenerated by make gen)."""
    os.makedirs(masks_dir, exist_ok=True)
    written = set()
    for cid, creature in sorted(creatures.items()):
        sheet = creature.get("art", {}).get("sheet")
        if not sheet:
            continue
        cell_w, cell_h = creature["stats"]["w"], creature["stats"]["h"]
        if not _creature_in_cell(creature, cell_w, cell_h):
            continue
        path = os.path.join(masks_dir, "%s_%dx%d.png" % (sheet, cell_w, cell_h))
        bootstrap_mask(creature, "beast", sheet, cell_w, cell_h, BEAST_SOURCE_COLUMNS).save(path)
        written.add(path)
        sheets = {atk["art"]["sheet"] for atk in creature.get("attacks", [])
                  if atk.get("art") and _attack_fits(creature, atk["art"]["sheet"], cell_w, cell_h)}
        for asheet in sorted(sheets):
            apath = os.path.join(masks_dir, "%s_%dx%d.png" % (asheet, cell_w, cell_h))
            bootstrap_mask(creature, "attack", asheet, cell_w, cell_h,
                           source_columns(creature, "attack", asheet)).save(apath)
            written.add(apath)
    for p in sorted(written):
        print("gen-hitboxes: bootstrapped %s" % os.path.relpath(p, root))


def _compose_review(root, creatures, out_dir):
    """Sprite row + three mask rows + the boxes drawn on the art, for each
    creature (beast mask: zones/collide) and each attack sheet (windows)."""
    panels = []
    hitboxes_path = os.path.join(root, OUT_REL)
    entries = {}
    if os.path.isfile(hitboxes_path):
        with open(hitboxes_path, encoding="utf-8") as handle:
            entries = json.load(handle).get("creatures", {})
    for cid, creature in sorted(creatures.items()):
        if cid not in entries:
            continue  # only review the creatures a mask has migrated
        beast = creature.get("art", {}).get("sheet")
        if not beast:
            continue
        cell_w, cell_h = creature["stats"]["w"], creature["stats"]["h"]
        panels.append(_review_panel(root, cid, beast, cell_h, "beast"))
        for asheet in sorted({atk["art"]["sheet"] for atk in creature.get("attacks", [])
                              if atk.get("art")}):
            panels.append(_review_panel(root, cid, asheet, cell_h, "attack"))
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
    path = os.path.join(out_dir, "hitbox_review.png")
    review.save(path)
    print("gen-hitboxes: review image -> %s" % os.path.relpath(path, root))


def _review_panel(root, cid, sheet, cell_h, role):
    cell_w = None
    sprite_path = None
    for stats_w in (32, 20, 16):
        candidate = os.path.join(root, BLOCKS_REL, "%s_%dx%d.png" % (sheet, stats_w, cell_h))
        if os.path.isfile(candidate):
            sprite_path, cell_w = candidate, stats_w
            break
    if sprite_path is None:
        return None
    sprite = Image.open(sprite_path).convert("RGBA")
    panel = Image.new("RGBA", (sprite.size[0], cell_h * 4), (16, 16, 24, 255))
    panel.paste(sprite, (0, 0))
    mask_path = os.path.join(root, MASKS_REL, "%s_%dx%d.png" % (sheet, cell_w, cell_h))
    if os.path.isfile(mask_path):
        mask = Image.open(mask_path).convert("RGBA")
        bw = band_width(cell_w)
        cropped = Image.new("RGBA", (sprite.size[0], cell_h * 3), (0, 0, 0, 0))
        for col in range(sprite.size[0] // cell_w):
            cropped.paste(mask.crop((col * bw, 0, col * bw + cell_w, cell_h * 3)), (col * cell_w, 0))
        panel.paste(cropped, (0, cell_h))
    draw = ImageDraw.Draw(panel)
    hitboxes_path = os.path.join(root, OUT_REL)
    if not os.path.isfile(hitboxes_path):
        return panel
    with open(hitboxes_path, encoding="utf-8") as handle:
        entry = json.load(handle)["creatures"].get(cid)
    if not entry:
        return panel

    def outline(b, color):
        draw.rectangle([b["ox"], b["oy"], b["ox"] + b["w"] - 1, b["oy"] + b["h"] - 1], outline=color)

    if role == "beast":
        for name, b in entry.get("zones", {}).items():
            outline(b, (255, 0, 0, 255) if name == "head" else (0, 0, 255, 255))
        outline(entry["collide"], (255, 255, 0, 255))
    else:
        for aid, boxes in entry.get("windows", {}).items():
            for b in boxes:
                cell = record_window_to_cell(b, cell_w, cell_h)
                outline(cell, (255, 128, 0, 255))
    return panel


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=ROOT, help="pipeline root (default: repo)")
    parser.add_argument("--render", action="store_true",
                        help="bootstrap missing masks and write the review PNG")
    args = parser.parse_args(argv)
    root = os.path.abspath(args.root)
    try:
        if args.render:
            scratch = os.path.join(root, "build", "scratch")
            render_bootstrap(root, load_creatures(root), os.path.join(scratch, "masks"))
        process(root)
        if args.render:
            _compose_review(root, load_creatures(root), os.path.join(root, "build", "scratch"))
    except MaskError as exc:
        print("gen-hitboxes: FAIL: %s" % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
