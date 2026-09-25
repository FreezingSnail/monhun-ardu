#!/usr/bin/env python3
"""Author the equipment placeholder sheets + packed catalog (bead monhun-ardu-3o9).

    data/equipment/*.json
        -> images/equip/<sheet>_<cellW>x<cellH>.png   (4-shade placeholder art)
        -> src/generated/equip_meta.hpp               (catalog ABI + tables)
        -> fxdata/tables/equip.bin                    (packed catalog blob)

Schema and layout live in docs/equipment-framework.md. Validation is strict:
unknown/missing keys, id == file name, `sheet` == "mh_<id>", per-slot cell size,
cell height multiple of 8, poseMap keys from the pose set with `idle` present,
in-range pose rows, and integer-only fields.

Two record forms exist:

  * authored sheet (default): the tool writes a 4-shade placeholder PNG under
    images/equip/ and the converter packs it. `player_base` matches
    docs/art/player_base_16x16.png pixel-for-pixel (all 8 angle cells
    prefilled); shadow/body/head sheets are the layered paper-doll art (body in
    a WHITE idle row + LIGHT dodge row with the ground-shadow bar baked into
    every frame, heads with the per-facing eye slot); weapon/offhand
    placeholders stay blank.
  * `"source": "gen-art"` ref: the record reuses an existing gen-art sprite
    symbol (`sheet`, validated and resolved against fxdata/fxdata.h); no PNG is
    authored. A 19 B part record (sheet offset, anchor, order/frames, per-pose
    frame, optional variants) is packed into the same blob for the render path
    -- see tst/fxdatatest/player_art_test.hpp.

Layered slots (`shadow`/`body`/`head`) always emit a part record too, even
when the sheet is authored: the render slot loop draws them through the same
cart part view as the gen-art overlays. `data/equipment/sets/default.json`
picks the default draw set; the generator emits its part ids as constexpr
constants (DEFAULT_BODY/DEFAULT_HEAD, ...), so changing the default head or body
is a JSON edit + `make gen` and never touches render code. A slot omitted from
the set is simply not drawn (the default omits `shadow`: its row is baked into
the body sheet).

The placeholder art is authored from the same 4-shade primitives as
tools/gen-art.py / tools/gen-base-sheet.py (palette copied here on purpose:
1:1 with the L4 triplane levels).

Blob layout (little-endian, explicit u8/i8, no padding, fixed order), following
the tools/gen-fxtables.cpp serializer pattern:

    header   8 B  magic u16 (0x4551), version u8, flags u8, itemCount u16,
                   reserved u16
    item    19 B  slot, order, frames, cellW, cellH, anchorX i8, anchorY i8,
                   poseRow[12]
    part    19 B  player part records in PART_* order: sheet u24 (fx offset),
                   anchorX i8, anchorY i8, order u8, frames u8, frame[12] u8
                   (gen-art overlays + authored shadow/body/head layers)
    varoff 2*(n+1) B  u16 variant-data index per part, then the variant bytes

The part view is read on device from the mhEquip blob during the render pass
(tools emit only the offsets into equip_meta.hpp; see src/render.hpp partDraw).
`order`/`frames` let the render pick a per-facing frame from the pose row
(order `facing`: frame = facing; `facing*pose`: row*FACINGS + facing; `pose`:
the stored frame), so 8-way layers are pure data.

Two-pass note: the baked sheet offsets come from the *previous* run's
fxdata/fxdata.h, so adding or renaming a gen-art sheet shifts the FX image and
`make gen` must run twice (the second pass re-bakes from the new header). The
generated equip_meta.hpp pins every baked offset with an AVR static_assert
against the live symbol, so a skipped pass fails the device build instead of
shipping stale addresses.

Usage:
    python3 tools/gen-equipment.py [--root DIR] [--dump]

    --root DIR  pipeline root holding data/, images/ and src/generated
    --dump      validate + list the catalog on stdout; writes nothing
"""
import argparse
import io
import json
import os
import re
import struct
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

DATA_DIR = "data/equipment"
SETS_DIR = "data/equipment/sets"
DEFAULT_SET_REL = "data/equipment/sets/default.json"
IMAGES_REL = "images/equip"
BLOB_REL = "fxdata/tables/equip.bin"
META_REL = "src/generated/equip_meta.hpp"
FX_HEADER_REL = "fxdata/fxdata.h"

MAGIC = 0x4551
VERSION = 1
FLAGS = 0
HEADER_SIZE = 8
ITEM_SIZE = 19
FACINGS = 8

# `source: "gen-art"` records reference an existing fx sprite symbol (declared in
# fxdata/fxdata.h) instead of authoring a placeholder sheet: no images/equip PNG,
# the sheet is drawn with its current pixels. See the player part view emitted in
# equip_meta.hpp.
# `source` declares where a sheet's pixels come from: absent = placeholder/
# layered art authored by this tool, "gen-art" = an existing fx sprite symbol,
# "mirror" = a weapon sheet this tool authors in the one-facing + packer-mirror
# format (docs/weapon-art.md).
ART_SOURCES = ("gen-art", "mirror")

SLOTS = ("player", "shadow", "body", "head", "weapon", "offhand")
# Equipment layers drawn through the cart part view: authored shadow/body/head
# sheets and every gen-art overlay (weapon/offhand). `player` is the combined
# draw-over base template and stays out of the part view.
LAYERED_SLOTS = ("shadow", "body", "head")
ORDERS = ("facing", "facing*pose", "pose")
POSES = ("idle", "attack_startup", "attack_active", "attack_recover", "parry",
         "whirl", "guard", "shove", "dodge", "deflect", "stun", "dead")
POSE_COUNT = len(POSES)

# Part-view record: sheet u24 + anchorX i8 + anchorY i8 + order u8 + frames u8
# + frame[12]. order/frames select the per-facing frame at draw time (see
# src/render.hpp partFrame).
PART_SIZE = 7 + POSE_COUNT

# docs/equipment-framework.md cart sheet layout.
EXPECTED_CELL = {
    "player": (16, 16),
    "shadow": (16, 16),
    "body": (16, 16),
    "head": (16, 16),
    "weapon": (32, 32),
    "offhand": (16, 16),
}
# docs/art/player_base_16x16.png anchors: player/body/head/shadow cell center,
# weapon/offhand grip.
EXPECTED_ANCHOR = {
    "player": (8, 8),
    "shadow": (8, 8),
    "body": (8, 8),
    "head": (8, 8),
    "weapon": (16, 16),
    "offhand": (16, 16),
}

ID_RE = re.compile(r"^[a-z][a-z0-9_]*$")
SYMBOL_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
# fxdata.h declares every sheet offset as `constexpr uint24_t <symbol> = <n>;`.
# The value is the sheet's absolute FX-image offset (also emitted in fxdata.h as
# uint24_t, so parse either hex or decimal); gen-art part records bake it in.
FX_SYMBOL_RE = re.compile(r"constexpr\s+uint24_t\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(0[xX][0-9A-Fa-f]+|\d+)")
MAX_ID = 31

# 4-shade RGBA palette, 1:1 with L4_Triplane (copied from tools/gen-base-sheet.py).
CLEAR = (0, 0, 0, 0)
BLACK = (0, 0, 0, 255)
DARK = (85, 85, 85, 255)
LIGHT = (170, 170, 170, 255)
WHITE = (255, 255, 255, 255)

# Helmet eye slot per facing (x, y, w, h) inside the head area; None = facing
# away (helmet back). Must stay identical to tools/gen-base-sheet.py SLIT_RECTS
# and docs/art/player_base_16x16.png: the slot is the facing read.
SLIT_RECTS = (
    (9, 3, 1, 2),    # E:  profile, slit edge-on at the right
    (9, 4, 2, 1),    # SE: three-quarter
    (7, 4, 3, 1),    # S:  full front, widest
    (5, 4, 2, 1),    # SW: three-quarter
    (6, 3, 1, 2),    # W:  profile, slit edge-on at the left
    None,            # NW
    None,            # N
    None,            # NE
)

# Placeholder head styles by item id. A new head item is a JSON record + one
# entry here (the authored-art step): render never names a head sheet.
HEAD_STYLES = {
    "head_base": "base",        # plain helmet
    "head_helm": "helm",        # wider light dome + dark rim
    "head_bandana": "bandana",  # white head + dark band row
}
# Body pose rows: idle in WHITE (matches the old fxplayer normal shade), dodge
# in LIGHT (the old fxplayer dodge shade).
BODY_ROW_SHADES = {0: WHITE, 1: LIGHT}

_MISSING = object()


class Errors:
    """Collects user-facing validation errors; never raises on schema issues."""

    def __init__(self):
        self.items = []

    def add(self, ctx, message):
        self.items.append("%s: %s" % (ctx, message))


def is_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


def is_part(item):
    """True when the item gets a cart part record (gen-art overlay, an authored
    shadow/body/head layer, or an authored weapon sheet — docs/weapon-art.md)."""
    return item["genArt"] or item["mirror"] or item["slot"] in LAYERED_SLOTS


def check_keys(errors, ctx, obj, required, optional=()):
    if not isinstance(obj, dict):
        errors.add(ctx, "expected an object")
        return False
    for key in sorted(obj):
        if key not in required and key not in optional:
            errors.add(ctx, "unknown key '%s'" % key)
    for key in required:
        if key not in obj:
            errors.add(ctx, "missing key '%s'" % key)
    return True


def read_int(errors, ctx, obj, key, lo, hi):
    if not isinstance(obj, dict) or key not in obj:
        return None
    value = obj[key]
    if not is_int(value):
        errors.add(ctx, "%s: expected an integer, got %r" % (key, value))
        return None
    if not lo <= value <= hi:
        errors.add(ctx, "%s: out of range %d..%d: %d" % (key, lo, hi, value))
        return None
    return value


def read_pair(errors, ctx, obj, key, lo, hi):
    value = obj.get(key) if isinstance(obj, dict) else None
    if not isinstance(value, list) or len(value) != 2 or not all(is_int(v) for v in value):
        errors.add(ctx, "%s: expected [x, y] integers, got %r" % (key, value))
        return None
    for v in value:
        if not lo <= v <= hi:
            errors.add(ctx, "%s: out of range %d..%d: %d" % (key, lo, hi, v))
            return None
    return value


def load_fxdata_symbols(root):
    """Sheet symbols declared by fxdata/fxdata.h -> absolute FX offsets
    (None if the header is absent)."""
    path = os.path.join(root, FX_HEADER_REL)
    try:
        with open(path, encoding="utf-8") as handle:
            return {name: int(value, 0) for name, value in FX_SYMBOL_RE.findall(handle.read())}
    except OSError:
        return None


def load_json(errors, path, rel):
    try:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    except OSError as exc:
        errors.add(rel, "cannot read file: %s" % exc)
    except ValueError as exc:
        errors.add(rel, "invalid JSON: %s" % exc)
    return None


def normalize_item(errors, rel, name, obj, seen_ids, fx_symbols):
    ctx = rel
    check_keys(errors, ctx, obj, {"id", "slot", "sheet", "cell", "anchor", "order", "frames", "poseMap"},
               ("flags", "source", "variants"))
    if not isinstance(obj, dict):
        return None

    item_id = obj.get("id")
    if not isinstance(item_id, str) or not ID_RE.match(item_id) or len(item_id) > MAX_ID:
        errors.add(ctx, "id: expected [a-z][a-z0-9_]* (<=%d), got %r" % (MAX_ID, item_id))
        item_id = None
    else:
        if item_id != os.path.splitext(name)[0]:
            errors.add(ctx, "id '%s' does not match file name '%s'" % (item_id, name))
        if item_id in seen_ids:
            errors.add(ctx, "duplicate item id '%s'" % item_id)
        seen_ids.add(item_id)

    slot = obj.get("slot")
    if slot not in SLOTS:
        errors.add(ctx, "slot: unknown value %r (want one of %s)" % (slot, ", ".join(SLOTS)))
        slot = None

    # `source: "gen-art"` records reuse an existing fx sprite: the sheet is the
    # symbol itself (no mh_ prefix, no placeholder PNG), and it must already be
    # declared in fxdata/fxdata.h. `source: "mirror"` records are weapon sheets
    # authored by this tool from the pose-row spec (docs/weapon-art.md), with the
    # packer plan emitted to images/equip/layout.json. Everything else keeps the
    # authored-sheet rules.
    source = obj.get("source")
    gen_art = source == "gen-art"
    mirror_art = source == "mirror"
    if source is not None and source not in ART_SOURCES:
        errors.add(ctx, "source: unknown value %r (want one of %s)" % (source, ", ".join(ART_SOURCES)))

    sheet = obj.get("sheet")
    if not isinstance(sheet, str) or not SYMBOL_RE.match(sheet):
        errors.add(ctx, "sheet: expected a C symbol, got %r" % (sheet,))
        sheet = None
    elif gen_art:
        if fx_symbols is None:
            errors.add(ctx, "source 'gen-art': %s not found (cannot validate sheet %r)" % (FX_HEADER_REL, sheet))
        elif sheet not in fx_symbols:
            errors.add(ctx, "sheet: %r is not declared in %s" % (sheet, FX_HEADER_REL))
    elif item_id is not None and sheet != "mh_" + item_id:
        errors.add(ctx, "sheet: expected 'mh_%s', got %r" % (item_id, sheet))

    cell = read_pair(errors, ctx, obj, "cell", 1, 255)
    anchor = None
    cw = ch = ax = ay = None
    if cell is not None:
        cw, ch = cell
        if not gen_art and ch % 8 != 0:
            # Converter/packer rule for authored sheets; gen-art reuse may have
            # any existing frame height (fxwhirl frames are 8x4).
            errors.add(ctx, "cell: height %d must be a multiple of 8" % ch)
        if not gen_art and slot is not None:
            want = EXPECTED_CELL[slot]
            if (cw, ch) != want:
                errors.add(ctx, "cell: %s cells are %dx%d, got %dx%d" % (slot, want[0], want[1], cw, ch))
        # The anchor is the frame-local pivot the reference point maps to, so a
        # pivot may sit outside the cell (reload/erase) and gen-art anchors are
        # signed; authored sheets keep the [0, cell] window and slot convention.
        anchor = read_pair(errors, ctx, obj, "anchor", -128 if gen_art else 0, 127 if gen_art else 255)
        if anchor is not None:
            ax, ay = anchor
            if gen_art:
                pass
            elif not (0 <= ax <= cw and 0 <= ay <= ch):
                errors.add(ctx, "anchor: (%d,%d) outside the %dx%d cell" % (ax, ay, cw, ch))
                anchor = None
            elif slot is not None and (ax, ay) != EXPECTED_ANCHOR[slot]:
                want = EXPECTED_ANCHOR[slot]
                errors.add(ctx, "anchor: %s anchor is %d,%d, got %d,%d" % (slot, want[0], want[1], ax, ay))
                anchor = None

    order = obj.get("order")
    if order not in ORDERS:
        errors.add(ctx, "order: unknown value %r (want one of %s)" % (order, ", ".join(ORDERS)))
        order = None

    frames = read_int(errors, ctx, obj, "frames", 1, 255)
    rows = None
    if order is not None and frames is not None:
        if order == "facing":
            if frames not in (1, FACINGS):
                errors.add(ctx, "order 'facing': frames must be 1 or %d, got %d" % (FACINGS, frames))
            else:
                rows = 1
        elif order == "pose":
            # One frame per pose row, no facing dimension: poseMap values are
            # sheet frame indices (gen-art strips are not facing-indexed).
            rows = frames
        else:
            if frames % FACINGS != 0:
                errors.add(ctx, "order 'facing*pose': frames must be a multiple of %d, got %d" % (FACINGS, frames))
            else:
                rows = frames // FACINGS

    if mirror_art:
        # The authored weapon sheets all carry the docs/weapon-art.md row table;
        # the tool draws them from WEAPON_ART, so an unknown id is a hard error
        # instead of a silently blank sheet.
        if item_id is not None and item_id not in WEAPON_ART:
            errors.add(ctx, "source 'mirror': no weapon art spec for %r" % item_id)
        if order is not None and order != "facing*pose":
            errors.add(ctx, "source 'mirror': order must be 'facing*pose', got %r" % order)
        if frames is not None and frames != WEAPON_ROWS * FACINGS:
            errors.add(ctx, "source 'mirror': frames must be %d, got %d" % (WEAPON_ROWS * FACINGS, frames))

    pose_map = obj.get("poseMap")
    pose_rows = None
    if not isinstance(pose_map, dict):
        errors.add(ctx, "poseMap: expected an object")
    elif rows is not None:
        if "idle" not in pose_map:
            errors.add(ctx, "poseMap: missing 'idle' (missing keys fall back to it)")
        idle_row = pose_map.get("idle", 0)
        if "idle" in pose_map and not is_int(idle_row):
            errors.add(ctx, "poseMap.idle: expected an integer, got %r" % (idle_row,))
            idle_row = 0
        if not 0 <= idle_row < rows:
            errors.add(ctx, "poseMap.idle: out of range 0..%d: %r" % (rows - 1, idle_row))
            idle_row = 0
        pose_rows = []
        for pose in POSES:
            if pose not in pose_map:
                pose_rows.append(idle_row)
                continue
            value = pose_map[pose]
            if not is_int(value):
                errors.add(ctx, "poseMap.%s: expected an integer, got %r" % (pose, value))
                value = idle_row
            elif not 0 <= value < rows:
                errors.add(ctx, "poseMap.%s: out of range 0..%d: %d" % (pose, rows - 1, value))
                value = idle_row
            pose_rows.append(value)
        for key in sorted(pose_map):
            if key not in POSES:
                errors.add(ctx, "poseMap: unknown pose '%s' (want one of %s)" % (key, ", ".join(POSES)))

    flags = obj.get("flags", [])
    if not isinstance(flags, list) or not all(isinstance(f, str) and ID_RE.match(f) for f in flags):
        errors.add(ctx, "flags: expected an array of [a-z][a-z0-9_]* strings")
        flags = []

    # Optional `variants`: a small per-item frame selector for poses that the
    # 12-name pose set cannot express (the sword slash frames 0..4 by attack).
    # The render path indexes VARIANT_<ID> with a compact attack slot.
    variants = obj.get("variants")
    var_list = []
    if variants is not None:
        if not isinstance(variants, list) or not variants:
            errors.add(ctx, "variants: expected a non-empty array of frame indices")
        elif frames is not None:
            for value in variants:
                if not is_int(value) or not 0 <= value < frames:
                    errors.add(ctx, "variants: %r out of range 0..%d" % (value, frames - 1))
                else:
                    var_list.append(value)

    if None in (item_id, slot, sheet, cell, anchor, order, frames) or pose_rows is None:
        return None
    return {"id": item_id, "slot": slot, "sheet": sheet, "cell": (cw, ch), "anchor": (ax, ay),
            "order": order, "frames": frames, "rows": rows, "poseRows": pose_rows,
            "genArt": gen_art, "mirror": mirror_art, "variants": var_list, "flags": list(flags)}


def load_default_set(errors, root, items):
    """`data/equipment/sets/default.json` -> {slot: item id} (None if absent).

    The set only names layered slots (shadow/body/head) and only items that
    already exist in the catalog with the matching slot, so a typo'd default is
    a compile error instead of a silently blank layer. A missing slot is allowed
    and means that layer is not drawn (bead monhun-ardu-3fh: the default omits
    `shadow`, whose row is baked into the body sheet, while the shadow catalog
    record stays available).
    """
    path = os.path.join(root, DEFAULT_SET_REL)
    if not os.path.isfile(path):
        return None
    obj = load_json(errors, path, DEFAULT_SET_REL)
    if not isinstance(obj, dict):
        errors.add(DEFAULT_SET_REL, "expected an object")
        return None
    for key in sorted(obj):
        if key not in LAYERED_SLOTS:
            errors.add(DEFAULT_SET_REL, "unknown key '%s' (want one of %s)" % (key, ", ".join(LAYERED_SLOTS)))
    by_id = {item["id"]: item for item in items}
    out = {}
    for slot in LAYERED_SLOTS:
        value = obj.get(slot)
        if value is None:
            continue   # slot omitted: that layer is simply not drawn
        if not isinstance(value, str):
            errors.add(DEFAULT_SET_REL, "%s: expected an item id string, got %r" % (slot, value))
            continue
        item = by_id.get(value)
        if item is None:
            errors.add(DEFAULT_SET_REL, "%s: unknown item id %r" % (slot, value))
        elif item["slot"] != slot:
            errors.add(DEFAULT_SET_REL, "%s: item %r has slot %r" % (slot, value, item["slot"]))
        elif not is_part(item):
            errors.add(DEFAULT_SET_REL, "%s: item %r is not a layered part" % (slot, value))
        else:
            out[slot] = value
    return out


def compile_model(errors, root):
    data_dir = os.path.join(root, DATA_DIR)
    if not os.path.isdir(data_dir):
        errors.add(DATA_DIR, "missing equipment directory")
        return None
    names = sorted(name for name in os.listdir(data_dir) if name.endswith(".json"))
    if not names:
        errors.add(DATA_DIR, "no equipment JSON files found")
        return None
    fx_symbols = load_fxdata_symbols(root)
    items = []
    seen_ids = set()
    for name in names:
        rel = "%s/%s" % (DATA_DIR, name)
        obj = load_json(errors, os.path.join(data_dir, name), rel)
        if obj is None:
            continue
        item = normalize_item(errors, rel, name, obj, seen_ids, fx_symbols)
        if item is not None:
            items.append(item)
    if errors.items:
        return None
    items.sort(key=lambda item: item["id"])
    default_set = load_default_set(errors, root, items)
    if errors.items:
        return None
    return {"items": items, "fxSymbols": fx_symbols, "defaultSet": default_set}


# ------------------------------------------------------------------ placeholder art


def rect(img, x, y, w, h, color):
    if w <= 0 or h <= 0:
        return
    if x < 0 or y < 0 or x + w > img.size[0] or y + h > img.size[1]:
        raise SystemExit("gen-equipment: block (%d,%d,%d,%d) outside %s canvas" % (x, y, w, h, img.size))
    px = img.load()
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            px[xx, yy] = color


def new(w, h):
    return Image.new("RGBA", (w, h), CLEAR)


def player_cell(facing):
    """docs/art/player_base_16x16.png cell for one facing: mock silhouette +
    the black head slit that marks the direction (must stay pixel-identical to
    tools/gen-base-sheet.py)."""
    img = new(16, 16)
    rect(img, 2, 15, 12, 1, DARK)   # shadow
    rect(img, 5, 1, 6, 6, WHITE)    # head
    rect(img, 4, 7, 8, 6, WHITE)    # torso
    rect(img, 5, 13, 2, 2, WHITE)   # legs
    rect(img, 9, 13, 2, 2, WHITE)
    slit = SLIT_RECTS[facing % 8]
    if slit is not None:
        rect(img, slit[0], slit[1], slit[2], slit[3], BLACK)
    return img


def body_cell(shade):
    """Torso + legs + the baked ground shadow in one shade: the mock drawPlayer
    rects, so body/head compose at the same (8,8) anchor. The shadow row (rect
    2,15,12,1 in DARK, identical to the shadow_base sheet) is painted into every
    body frame so the render drops its separate shadow blit (bead
    monhun-ardu-3fh); the bar never overlaps the torso/legs, so the composite is
    pixel-identical."""
    img = new(16, 16)
    rect(img, 2, 15, 12, 1, DARK)   # baked ground shadow
    rect(img, 4, 7, 8, 6, shade)    # torso
    rect(img, 5, 13, 2, 2, shade)   # legs
    rect(img, 9, 13, 2, 2, shade)
    return img


def head_cell(style, facing):
    """Head layer cell: the per-style helmet/head silhouette plus the black eye
    slot for this facing (only the 5 toward-viewer facings draw it)."""
    img = new(16, 16)
    if style == "base":
        rect(img, 5, 1, 6, 6, WHITE)    # plain helmet
    elif style == "helm":
        rect(img, 4, 1, 8, 5, LIGHT)    # wider dome
        rect(img, 4, 6, 8, 1, DARK)     # rim
    elif style == "bandana":
        rect(img, 5, 1, 6, 6, WHITE)    # head
        rect(img, 5, 1, 6, 1, DARK)     # dark band row
    else:
        raise SystemExit("gen-equipment: no placeholder art for head style %r" % style)
    slit = SLIT_RECTS[facing % 8]
    if slit is not None:
        rect(img, slit[0], slit[1], slit[2], slit[3], BLACK)
    return img


def shadow_cell():
    img = new(16, 16)
    rect(img, 2, 15, 12, 1, DARK)   # current shadow bar
    return img


def placeholder_cell(item, index):
    """Placeholder pixels for one frame; None leaves the cell transparent."""
    slot = item["slot"]
    if slot == "player":
        return player_cell(index)   # 8 angle cells prefilled (docs/art template)
    if slot == "shadow":
        return shadow_cell() if index == 0 else None
    if slot == "body":
        # order 'facing*pose': rows are poses (row 0 idle, row 1 dodge); a
        # single-row 'facing' body keeps the idle shade.
        row = index // FACINGS if item["order"] == "facing*pose" else 0
        return body_cell(BODY_ROW_SHADES.get(row, WHITE))
    if slot == "head":
        style = HEAD_STYLES.get(item["id"])
        if style is None:
            raise SystemExit("gen-equipment: add %r to HEAD_STYLES for placeholder art" % item["id"])
        return head_cell(style, index % FACINGS)
    return None  # weapon / offhand placeholders stay blank


# ------------------------------------------------------------------ weapon art
# Player weapon sheets (docs/weapon-art.md). Each row is a pose; the source
# authors 5 facings (E, SE, S, N, NE) and the packer mirrors the west twins via
# images/equip/layout.json, exactly like the creature sheets (ryh.2). Shipped
# frame index = row * FACINGS + facing; the row tables below match
# src/render.hpp (wpn::ROW_*).
WEAPON_SRC_FACINGS = (0, 1, 2, 6, 7)   # DIR8 order used in the source columns
# packed column -> (source column, mirror), per row
WEAPON_MIRROR_PLAN = ((0, False), (1, False), (2, False), (1, True), (0, True), (4, True), (3, False), (4, False))
WEAPON_ROW_MOVE0 = 2                   # slot s -> startup 2 + 2*s, active 3 + 2*s
WEAPON_SLOTS = 10                      # combo 0..2, special, branch a/b, finisher, alt, roll, charge
WEAPON_ROW_STANCE = 22
WEAPON_ROW_DODGE = 23
WEAPON_ROW_DEFENSE = 24                # flail deflect / gun shove
WEAPON_ROW_STUN = 25
WEAPON_ROW_RIM = 26
WEAPON_ROWS = 27

# DIR8 unit vectors (E, SE, S, SW, W, NW, N, NE): +x right, +y down.
DIR8 = ((16, 0), (11, 11), (0, 16), (-11, 11), (-16, 0), (-11, -11), (0, -16), (11, -11))

# (slot, startup angle deg, active angle deg): 0 deg = along the facing vector,
# negative = toward the hunter's lead side, positive = trail side.
SWORD_MOVES = (
    (0, -120, -35),   # combo 0: high lead cut
    (1, 150, 30),     # combo 1: return cut
    (2, -150, 80),    # combo 2: heavy down cut
    (3, -30, 0),      # special: riposte thrust
    (4, -100, -15),   # step-slash
    (5, -170, 10),    # spin-cut
    (6, -140, 65),    # branch 2: finisher slam
    (7, -60, 0),      # alt: lunge thrust
    (8, -180, -25),   # roll slash
)


def wpt(facing, u, v):
    """Weapon-space (u along facing, v lead/trail) -> cell pixel."""
    dx, dy = DIR8[facing]
    return (16 + (u * dx - v * dy + 8) // 16, 16 + (u * dy + v * dx + 8) // 16)


def put(img, x, y, color):
    if 0 <= x < img.size[0] and 0 <= y < img.size[1]:
        img.load()[x, y] = color


def dot(img, x, y, color, size=1):
    for dy in range(size):
        for dx in range(size):
            put(img, x + dx, y + dy, color)


def wline(img, facing, u0, v0, u1, v1, color, w=1):
    """Thick line between two weapon-space points (45 deg steps stay chunky)."""
    x0, y0 = wpt(facing, u0, v0)
    x1, y1 = wpt(facing, u1, v1)
    dx = abs(x1 - x0)
    dy = -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        dot(img, x0, y0, color, w)
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def warc(img, facing, cu, cv, radius, a0_deg, a1_deg, color, w=1, steps=8):
    """Arc in weapon space around (cu, cv); angles in degrees."""
    import math
    prev = None
    for i in range(steps + 1):
        a = math.radians(a0_deg + (a1_deg - a0_deg) * i / steps)
        u = cu + radius * math.cos(a)
        v = cv + radius * math.sin(a)
        if prev is not None:
            wline(img, facing, prev[0], prev[1], u, v, color, w)
        prev = (u, v)


def _cosdir(deg):
    import math
    return math.cos(math.radians(deg))


def _sindir(deg):
    import math
    return math.sin(math.radians(deg))


# Move records for the three weapons, loaded from build/fxdump.json (the host
# dump of src/core/game.hpp, written earlier in gen.sh): the art derives every
# pose from the move's own fields -- reach/hw/hh/lunge/effect/id -- never from
# copied literals (docs/weapon-art.md). AtkId names are parsed from game.hpp so
# the slot fold here cannot drift from src/render.hpp wpn::ATK_SLOT.
WEAPON_COUNT = 3
WEAPON_MOVES = None   # [weapon][slot] -> record dict, filled by run()
ATK_IDS = None        # {"ATK_NONE": 0, ...}, parsed from src/core/game.hpp


def load_atk_ids(root):
    path = os.path.join(root, "src", "core", "game.hpp")
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    match = re.search(r"enum AtkId\s*:\s*int8_t\s*\{(.*?)\};", text, re.S)
    if not match:
        raise SystemExit("gen-equipment: no AtkId enum in %s" % path)
    ids = {}
    value = 0
    body = re.sub(r"//[^\n]*", "", match.group(1))
    for token in body.split(","):
        token = token.strip()
        if not token:
            continue
        if "=" in token:
            name, raw = token.split("=")
            name, value = name.strip(), int(raw.strip())
        else:
            name = token
        ids[name] = value
        value += 1
    return ids


def load_weapon_moves(root, path=None):
    """build/fxdump.json -> [weapon][slot] move records (docs/weapon-art.md).

    Slot map: 0..2 combo, 3 special, 4 branch-a (STEPSLASH/TRIP/POINTBLANK),
    5 branch-b (SPINCUT/GUARDBASH), 6 finisher (BRANCH2), 7 alt, 8 roll,
    9 charge[0]. A zero-box branch (flail's whirl stance) never claims a slot.
    """
    if path is None:
        path = os.path.join(root, "build", "fxdump.json")
    with open(path, encoding="utf-8") as handle:
        doc = json.load(handle)
    if len(doc["weapons"]) != WEAPON_COUNT:
        raise SystemExit("gen-equipment: fxdump weapons: %d, want %d"
                         % (len(doc["weapons"]), WEAPON_COUNT))
    slot_ids = {
        4: (ATK_IDS["ATK_STEPSLASH"], ATK_IDS["ATK_TRIP"], ATK_IDS["ATK_POINTBLANK"]),
        5: (ATK_IDS["ATK_SPINCUT"], ATK_IDS["ATK_GUARDBASH"]),
        6: (ATK_IDS["ATK_BRANCH2"],),
    }
    moves = []
    for weapon in doc["weapons"]:
        recs = [None] * 10
        recs[0], recs[1], recs[2] = weapon["attacks"]
        recs[3] = weapon["special"]
        for slot, wanted in slot_ids.items():
            for branch in weapon["branches"]:
                if branch["id"] in wanted and branch["hw"]:
                    recs[slot] = branch
                    break
        recs[7], recs[8], recs[9] = weapon["alt"], weapon["roll"], weapon["charge"][0]
        moves.append(recs)
    return moves


def move_record(weapon, slot):
    """The move record for a slot, or None when the weapon has no such move
    (the flail has no branch-b; its slot row stays blank)."""
    return WEAPON_MOVES[weapon][slot]


def blade(img, facing, u0, v0, u1, v1, core=WHITE):
    """3 px dark blade with a 1 px core; two calls never overlap by accident."""
    wline(img, facing, u0, v0, u1, v1, DARK, 3)
    wline(img, facing, u0, v0, u1, v1, core, 1)
    dot(img, *wpt(facing, u1, v1), core, 1)


def ball(img, facing, u, v, r, core=WHITE):
    """Flail head: dark rim, bright core (per-pixel, no brush shift)."""
    inner = (r - 1) * (r - 1)
    for dv in range(-r, r + 1):
        for du in range(-r, r + 1):
            d2 = du * du + dv * dv
            if d2 <= r * r:
                put(img, *wpt(facing, u + du, v + dv), core if d2 <= inner else DARK)


def chain(img, facing, u0, v0, u1, v1):
    """Three chain dots between the hand and the ball (clamped to the cell)."""
    for i in (1, 2, 3):
        f = i / 4.0
        u = u0 + (u1 - u0) * f
        v = v0 + (v1 - v0) * f
        if -15 <= u <= 15:
            dot(img, *wpt(facing, u, v), DARK, 2)


def shield(img, facing, u, v, lit):
    """Gunshield plate: dark rim, light/white face, black slot at the centre."""
    w, h = 4, 12
    face = WHITE if lit else LIGHT
    for dv in range(-h // 2 - 1, h // 2 + 2):
        for du in range(-w // 2, w // 2 + 2):
            edge = du >= w // 2 or dv <= -h // 2 - 1 or dv >= h // 2 + 1
            put(img, *wpt(facing, u + du, v + dv), DARK if edge else face)
    put(img, *wpt(facing, u + w // 2, v), BLACK)


# Combo chain cut directions, in chain order (slot 0..2): the moveset's own
# order picks the alternation; the box aspect and id pick everything else.
SWORD_COMBO_ANGLES = ((-120, -35), (150, 30), (-150, 80))


def sword_move_art(record, slot):
    """(startup angle, active angle) for one sword move, from the record.

    Identity first (id says spin/step/roll/alt/finisher), geometry second
    (tall box = overhead, wide box = horizontal), chain order third.
    """
    atk_id = record["id"]
    if atk_id == ATK_IDS["ATK_SPINCUT"]:
        return -120, 0            # spin-cut: full turn
    if atk_id == ATK_IDS["ATK_STEPSLASH"]:
        return -70, 0             # step-slash: forward drive
    if atk_id == ATK_IDS["ATK_ROLL"]:
        return -100, 25           # roll slash: low cut
    if atk_id == ATK_IDS["ATK_ALT"]:
        return -35, 0             # thrust opener (lunge streak carries the drive)
    if atk_id == ATK_IDS["ATK_BRANCH2"]:
        return -140, 85           # finisher: overhead slam
    if atk_id == ATK_IDS["ATK_NONE"] and slot < 3:
        return SWORD_COMBO_ANGLES[slot]
    hw, hh = record["hw"], record["hh"]
    if hh >= hw + 6:
        return -130, 80           # tall box: overhead cut
    if hw >= hh + 6:
        return -35, 20            # wide box: horizontal cut
    return -90, 45                # diagonal


def sword_cell(row, facing):
    """One 32x32 sword pose cell (grip at the cell centre on hand rows, hitbox
    centre on active rows — docs/weapon-art.md reference table)."""
    img = new(32, 32)
    last_move_row = WEAPON_ROW_MOVE0 + 2 * WEAPON_SLOTS - 1
    if WEAPON_ROW_MOVE0 <= row <= last_move_row:
        slot = (row - WEAPON_ROW_MOVE0) // 2
        record = move_record(0, slot)
        if record is None or not record["hw"]:
            return None   # unused slot (sword has no charge): blank row
        a_start, a_active = sword_move_art(record, slot)
        reach, hw, hh = record["reach"], record["hw"], record["hh"]
        active = (row - WEAPON_ROW_MOVE0) % 2 == 1
        # The swing trail/frame count comes from the move's active window, so a
        # longer-lived hit reads as a wider sweep.
        steps = 4 + min(8, record["active"])
        if active:
            # Box-centred: the cell centre is the hitbox centre. The blade lies
            # in the box, rotated to the strike angle; the dark arc is the swing
            # trail from the hand (reach px behind the box).
            pu, pv = -reach, 0
            half = hw / 2.0
            wline(img, facing, pu, pv, pu + 2, pv, DARK, 3)          # wrist
            if abs(a_active - a_start) > 20:
                warc(img, facing, pu, pv, reach - 1, a_start, a_active, DARK, 1, steps)
            else:
                # pure thrust: straight trail from the hand to the box
                wline(img, facing, pu + 2, pv, -half, 0, DARK, 1)
            blade(img, facing,
                  -half * _cosdir(a_active), -half * _sindir(a_active),
                  half * _cosdir(a_active), half * _sindir(a_active))
            if record["lunge"]:
                # the move drives the hunter forward: dash streaks at the box's
                # near side (clamped inside the 32x32 cell)
                for v in (-3, 3):
                    wline(img, facing, max(-15.5, -half - 6), v, max(-13.5, -half - 2), v, DARK, 1)
        else:
            wline(img, facing, -2, 0, 1, 0, DARK, 3)             # grip
            blade(img, facing, 1, 0,
                  1 + 8 * _cosdir(a_start), 8 * _sindir(a_start))
            # wind-up arc stops halfway to the strike angle, so the startup row
            # never reads as the hit
            warc(img, facing, 0, 0, 9, a_start, a_start + (a_active - a_start) * 0.5, DARK, 1, steps)
        return img
    if row == 0:      # idle: blade resting forward-down
        wline(img, facing, -3, 0, 1, 0, DARK, 3)
        blade(img, facing, 1, 0, 12, 4)
    elif row == 1:    # recover: blade low
        wline(img, facing, -3, 0, 1, 0, DARK, 3)
        blade(img, facing, 1, 0, 9, 7)
    elif row == WEAPON_ROW_STANCE:   # parry: blade vertical in front
        wline(img, facing, -3, 0, 1, 0, DARK, 3)
        wline(img, facing, 5, -8, 5, 8, DARK, 3)
        wline(img, facing, 5, -7, 5, 7, WHITE, 1)
    elif row == WEAPON_ROW_DODGE:    # tucked
        wline(img, facing, -2, 0, 1, 0, DARK, 3)
        blade(img, facing, 1, 0, 4, 9)
    elif row == WEAPON_ROW_STUN:     # dropped
        wline(img, facing, -2, 0, 1, 0, DARK, 3)
        wline(img, facing, 1, 0, 5, 11, DARK, 3)
    elif row == WEAPON_ROW_RIM:      # riposte rim: white ring, box-centred
        warc(img, facing, 0, 0, 9, 0, 360, WHITE, 1, 12)
    else:
        return None
    return img


def flail_move_art(record, slot):
    """(startup angle, active angle) for one flail move, from the record."""
    atk_id = record["id"]
    if record["effect"]:
        return -40, 60            # trip: low sweep (effect 1)
    if atk_id == ATK_IDS["ATK_BRANCH2"]:
        return -140, 80           # finisher: overhead slam
    if atk_id == ATK_IDS["ATK_ROLL"]:
        return -110, 30           # roll sweep
    if atk_id == ATK_IDS["ATK_ALT"]:
        return -30, 20            # wide sweep opener
    if atk_id == ATK_IDS["ATK_CHARGE"]:
        return -150, 70           # charge slam
    if atk_id == ATK_IDS["ATK_NONE"] and slot == 3:
        return -90, -10           # special: big throw
    if atk_id == ATK_IDS["ATK_NONE"] and slot < 3:
        return ((-120, -30), (140, 25), (-160, 60))[slot]
    hw, hh = record["hw"], record["hh"]
    if hw >= hh + 8:
        return -50, 25            # wide box: horizontal sweep
    if hh >= hw + 8:
        return -140, 80           # tall box: overhead
    return -100, 40


def flail_cell(row, facing):
    """One 32x32 flail pose cell. The ball is the strike mass (at the hit box
    centre on active rows); the chain runs back to the hand."""
    img = new(32, 32)
    last_move_row = WEAPON_ROW_MOVE0 + 2 * WEAPON_SLOTS - 1
    if WEAPON_ROW_MOVE0 <= row <= last_move_row:
        slot = (row - WEAPON_ROW_MOVE0) // 2
        record = move_record(1, slot)
        if record is None or not record["hw"]:
            return None
        a_start, a_active = flail_move_art(record, slot)
        reach, hw, hh = record["reach"], record["hw"], record["hh"]
        radius = 2 + min(3, hh // 8)
        active = (row - WEAPON_ROW_MOVE0) % 2 == 1
        steps = 4 + min(8, record["active"])
        if active:
            pu, pv = -reach, 0
            wline(img, facing, pu, pv, pu + 2, pv, DARK, 3)              # wrist
            span = a_active - a_start
            if abs(span) > 20:
                warc(img, facing, pu, pv, reach - 1, a_start, a_active, DARK, 1, steps)
                # In-cell sweep echo + ball ghosts: the hand is off-cell for the
                # long reaches, so the arc alone would not read here.
                warc(img, facing, 0, 0, min(12, hw // 2 + 1), a_start, a_active, DARK, 1, steps)
                if abs(span) > 40:
                    for f in (0.33, 0.66):
                        ga = a_start + span * f
                        ball(img, facing, 9 * _cosdir(ga), 9 * _sindir(ga),
                             max(2, radius - 2), core=LIGHT)
            ball(img, facing, 0, 0, radius)
            chain(img, facing, pu + 2, pv, -radius, 0)
            if record["lunge"]:
                for v in (-3, 3):
                    wline(img, facing, max(-15.5, -hw / 2 - 6), v, max(-13.5, -hw / 2 - 2), v, DARK, 1)
        else:
            ball(img, facing, 1 + 8 * _cosdir(a_start), 8 * _sindir(a_start), radius)
            chain(img, facing, 0, 0, 1 + 8 * _cosdir(a_start), 8 * _sindir(a_start))
            warc(img, facing, 0, 0, 8, a_start, a_start + (a_active - a_start) * 0.5, DARK, 1, steps)
        return img
    if row == 0:      # idle: chain and ball resting forward-down
        ball(img, facing, 9, 5, 3)
        chain(img, facing, 0, 0, 9, 5)
    elif row == 1:    # recover: ball low
        ball(img, facing, 7, 8, 3)
        chain(img, facing, 0, 0, 7, 8)
    elif row == WEAPON_ROW_STANCE:   # whirl base: hand + slack chain (ring/ball are parts)
        wline(img, facing, -3, 0, 1, 0, DARK, 3)
        chain(img, facing, 1, 0, 8, -2)
    elif row == WEAPON_ROW_DEFENSE:  # deflect: two light bars in front
        wline(img, facing, -3, 0, 1, 0, DARK, 3)
        wline(img, facing, 4, -6, 4, 6, LIGHT, 1)
        wline(img, facing, 7, -7, 7, 7, LIGHT, 1)
    elif row == WEAPON_ROW_DODGE:    # tucked
        ball(img, facing, 4, 7, 3)
        chain(img, facing, 0, 0, 4, 7)
    elif row == WEAPON_ROW_STUN:     # slack chain, ball dropped
        ball(img, facing, 5, 10, 3)
        wline(img, facing, 1, 0, 5, 10, DARK, 1)
    else:
        return None
    return img


def gun_move_art(record, slot):
    """(startup angle, active angle, mode) for one gunshield move. Mode:
    'shot' (shell/muzzle), 'bash' (shield strike), 'thrust' (flat shield hit)."""
    if record["shell"] or record["reach"] >= 24:
        return -20, 0, "shot"
    if record["push"]:
        return -30, 0, "bash"
    return -40, 10, "thrust"


def gun_cell(row, facing):
    """One 32x32 gunshield pose cell: shield plate + barrel; muzzle flash on the
    shot actives, plate strike on the bashes."""
    img = new(32, 32)
    last_move_row = WEAPON_ROW_MOVE0 + 2 * WEAPON_SLOTS - 1
    if WEAPON_ROW_MOVE0 <= row <= last_move_row:
        slot = (row - WEAPON_ROW_MOVE0) // 2
        record = move_record(2, slot)
        if record is None or not record["hw"]:
            return None
        a_start, a_active, mode = gun_move_art(record, slot)
        reach = record["reach"]
        active = (row - WEAPON_ROW_MOVE0) % 2 == 1
        if active:
            wline(img, facing, -reach, 0, -reach + 2, 0, DARK, 3)        # wrist
            if mode == "shot":
                # rifle shot: barrel forward + flash at the muzzle. The render
                # references this row at the hand for the arrowshot special, so
                # the flash sits on the muzzle, not on the 44 px hitscan box.
                wline(img, facing, -3, 1, 4, 1, DARK, 3)                 # barrel
                for du, dv in ((8, 1), (6, -1), (6, 3), (2, 1)):
                    wline(img, facing, 5, 1, du, dv, WHITE, 1)
                dot(img, *wpt(facing, 5, 1), WHITE, 1)
            else:
                shield(img, facing, 0, 0, lit=False)
                for v in (-3, 3):
                    wline(img, facing, max(-15.5, -reach - 4), v, max(-13.5, -reach - 1), v, DARK, 1)
        else:
            shield(img, facing, 4, 0, lit=False)
            wline(img, facing, -3, 1, 1, 1, DARK, 3)                     # barrel
            warc(img, facing, 0, 0, 7, a_start, a_start + (a_active - a_start) * 0.5, DARK, 1, 5)
        return img
    if row == 0:      # idle: plate up, barrel low
        shield(img, facing, 4, 0, lit=False)
        wline(img, facing, -4, 2, 1, 2, DARK, 3)
    elif row == 1:    # recover: plate low
        shield(img, facing, 3, 1, lit=False)
        wline(img, facing, -3, 2, 2, 2, DARK, 3)
    elif row == WEAPON_ROW_STANCE:   # guard: fully lit plate
        shield(img, facing, 4, 0, lit=True)
        wline(img, facing, -4, 2, 1, 2, DARK, 3)
    elif row == WEAPON_ROW_DEFENSE:  # shove: plate thrust (render adds the offset)
        shield(img, facing, 6, 0, lit=True)
        for v in (-3, 3):
            wline(img, facing, -6, v, -2, v, DARK, 1)
    elif row == WEAPON_ROW_DODGE:    # tucked
        shield(img, facing, 3, 3, lit=False)
    elif row == WEAPON_ROW_STUN:     # dropped
        wline(img, facing, -2, 3, 1, 3, DARK, 3)
        shield(img, facing, 4, 5, lit=False)
    else:
        return None
    return img


WEAPON_ART = {
    "weapon_sword": sword_cell,
    "weapon_flail": flail_cell,
    "weapon_gun": gun_cell,
}


def weapon_plan(item):
    """Packed frame plan for a mirrored weapon sheet (row-major, per row)."""
    rows = item["frames"] // FACINGS
    cols = len(WEAPON_SRC_FACINGS)
    plan = []
    for r in range(rows):
        for src_col, mirror in WEAPON_MIRROR_PLAN:
            plan.append([r * cols + src_col, mirror])
    return plan


def author_sheet(item):
    cw, ch = item["cell"]
    frames = item["frames"]
    art = WEAPON_ART.get(item["id"]) if item["mirror"] else None
    if art is not None:
        rows = frames // FACINGS
        sheet = new(cw * len(WEAPON_SRC_FACINGS), ch * rows)
        for row in range(rows):
            for col, facing in enumerate(WEAPON_SRC_FACINGS):
                cell = art(row, facing)
                if cell is not None:
                    if cell.size != (cw, ch):
                        raise SystemExit("gen-equipment: weapon %dx%d != cell %dx%d for %s"
                                         % (cell.size[0], cell.size[1], cw, ch, item["id"]))
                    sheet.paste(cell, (col * cw, row * ch))
        return sheet
    if item["order"] == "facing":
        cols, rows = frames, 1
    else:
        cols, rows = FACINGS, frames // FACINGS
    sheet = new(cw * cols, ch * rows)
    for index in range(frames):
        cell = placeholder_cell(item, index)
        if cell is not None:
            if cell.size != (cw, ch):
                raise SystemExit("gen-equipment: placeholder %dx%d != cell %dx%d for %s"
                                 % (cell.size[0], cell.size[1], cw, ch, item["id"]))
            sheet.paste(cell, ((index % cols) * cw, (index // cols) * ch))
    return sheet



def image_name(item):
    cw, ch = item["cell"]
    return "%s_%dx%d.png" % (item["sheet"], cw, ch)


def png_bytes(img):
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()


# ------------------------------------------------------------------------ catalog


def frame_table(item):
    """FRAME[pose][facing]; order 'facing' repeats frame 0 when frames == 1;
    order 'pose' has no facing dimension and repeats the resolved frame."""
    table = []
    for row in item["poseRows"]:
        if item["order"] == "facing":
            table.append([0] * FACINGS if item["frames"] == 1 else list(range(FACINGS)))
        elif item["order"] == "pose":
            table.append([row] * FACINGS)
        else:
            table.append([row * FACINGS + facing for facing in range(FACINGS)])
    return table


def part_section(items):
    """Layout of the cart part view section (None without any part records).

    Parts are the gen-art overlays plus the authored shadow/body/head layers
    (see is_part). Returns the part list and every offset the blob packer and
    the header emitter share, so the two cannot disagree.
    """
    parts = [item for item in items if is_part(item)]
    if not parts:
        return None
    off = HEADER_SIZE + ITEM_SIZE * len(items)
    variant_offsets = []
    variant_data = []
    for item in parts:
        variant_offsets.append(len(variant_data))
        variant_data.extend(item["variants"])
    variant_offsets.append(len(variant_data))
    if not variant_data:
        variant_data = [0]   # keep the table addressable when no part uses one
    return {
        "parts": parts,
        "parts_off": off,
        "variants_off": off + PART_SIZE * len(parts),
        "variant_data_off": off + PART_SIZE * len(parts) + 2 * len(variant_offsets),
        "variant_offsets": variant_offsets,
        "variant_data": variant_data,
    }


def pack_blob(errors, items, fx_symbols):
    blob = bytearray(struct.pack("<HBBHH", MAGIC, VERSION, FLAGS, len(items), 0))
    for item in items:
        cw, ch = item["cell"]
        ax, ay = item["anchor"]
        record = bytearray([SLOTS.index(item["slot"]), ORDERS.index(item["order"]),
                            item["frames"], cw, ch])
        record += struct.pack("<bb", ax, ay)
        record += bytes(item["poseRows"])
        if len(record) != ITEM_SIZE:
            errors.add(item["id"], "internal: item record is %d B, want %d" % (len(record), ITEM_SIZE))
            return None
        blob += record
    expected = HEADER_SIZE + ITEM_SIZE * len(items)
    layout = part_section(items)
    if layout is not None:
        for item in layout["parts"]:
            value = (fx_symbols or {}).get(item["sheet"])
            if value is None:
                if item["genArt"]:
                    errors.add(item["id"], "internal: no fx offset for sheet %r" % item["sheet"])
                    return None
                # Authored layer: the sheet symbol only exists after the same
                # run's convert-sprite, so the first `make gen` bakes 0; the
                # AVR static_assert forces the second pass (two-pass note).
                value = 0
            if not 0 <= value <= 0xFFFFFF:
                errors.add(item["id"], "internal: sheet offset out of range: %d" % value)
                return None
            ax, ay = item["anchor"]
            blob += int(value).to_bytes(3, "little")
            blob += struct.pack("<bb", ax, ay)
            blob += bytes([ORDERS.index(item["order"]), item["frames"]])
            blob += bytes(item["poseRows"])
        for v_off in layout["variant_offsets"]:
            blob += struct.pack("<H", v_off)
        blob += bytes(layout["variant_data"])
        expected = layout["variant_data_off"] + len(layout["variant_data"])
    if len(blob) != expected:
        errors.add(BLOB_REL, "internal: blob is %d B, want %d" % (len(blob), expected))
        return None
    return bytes(blob)


def _const_name(order):
    return order.upper().replace("*", "_")


def _emit_table(lines, decl, values, per_line):
    lines.append(decl + " = {")
    for i in range(0, len(values), per_line):
        lines.append("    " + ", ".join(str(v) for v in values[i:i + per_line]) + ",")
    lines.append("};")


def emit_meta_header(items, blob, fx_symbols, default_set):
    lines = []
    app = lines.append
    app("#pragma once")
    app("// Generated by tools/gen-equipment.py -- do not edit.")
    app("//")
    app("// Equipment catalog ABI: header (magic u16, version u8, flags u8, itemCount")
    app("// u16, reserved u16), fixed-size %d-byte item records, then the gen-art part" % ITEM_SIZE)
    app("// view, little-endian, explicit u8/i8, no padding. The blob is the mhEquip")
    app("// raw_t section (fxdata/fxdata.txt); render reads the part view on device")
    app("// through core/fxmem.hpp (see src/render.hpp partDraw).")
    app("")
    app("#include <stdint.h>")
    if any(item["genArt"] for item in items):
        app("#if defined(__AVR__)")
        app("#include \"../fxdata.h\"   // live sheet symbols for the stale-blob static_assert")
        app("#endif")
    app("")
    app("namespace equip {")
    app("")
    app("constexpr uint16_t MAGIC = 0x%04X;" % MAGIC)
    app("constexpr uint8_t VERSION = %d;" % VERSION)
    app("constexpr uint8_t FLAGS = 0x%02X;" % FLAGS)
    app("constexpr uint16_t SIZE = %d;" % len(blob))
    app("constexpr uint8_t HEADER_SIZE = %d;" % HEADER_SIZE)
    app("constexpr uint8_t ITEM_SIZE = %d;" % ITEM_SIZE)
    app("constexpr uint8_t FACINGS = %d;" % FACINGS)
    app("constexpr uint8_t SLOT_COUNT = %d;" % len(SLOTS))
    app("constexpr uint8_t POSE_COUNT = %d;" % POSE_COUNT)
    app("constexpr uint8_t ITEM_COUNT = %d;" % len(items))
    app("")
    app("// Slots, in draw order (docs/equipment-framework.md).")
    for i, slot in enumerate(SLOTS):
        app("constexpr uint8_t SLOT_%s = %d;" % (slot.upper(), i))
    app("")
    app("// Poses; missing poseMap keys resolve to idle at gen time.")
    for i, pose in enumerate(POSES):
        app("constexpr uint8_t POSE_%s = %d;" % (pose.upper(), i))
    app("")
    app("// Sheet frame order ('facing' = one row of facings, 'facing*pose' =")
    app("// FACINGS columns x rows, 'pose' = one row per pose, no facing).")
    for i, order in enumerate(ORDERS):
        app("constexpr uint8_t ORDER_%s = %d;" % (_const_name(order), i))
    app("")
    app("// Item indices, sorted by id.")
    for i, item in enumerate(items):
        app("constexpr uint8_t ITEM_%s = %d;" % (item["id"].upper(), i))
    app("")
    app("// Sheet symbol names (fxdata/equip/Sprites.txt -> fxdata.h uint24_t offsets).")
    for item in items:
        app('constexpr const char *SHEET_%s = "%s";' % (item["id"].upper(), item["sheet"]))
    app("")
    app("// Per-item catalog, indexed by the ITEM_* constants above.")
    _emit_table(lines, "constexpr uint8_t ITEM_SLOT[ITEM_COUNT]", [SLOTS.index(i["slot"]) for i in items], 8)
    _emit_table(lines, "constexpr uint8_t ITEM_ORDER[ITEM_COUNT]", [ORDERS.index(i["order"]) for i in items], 8)
    _emit_table(lines, "constexpr uint8_t ITEM_FRAMES[ITEM_COUNT]", [i["frames"] for i in items], 8)
    _emit_table(lines, "constexpr uint8_t ITEM_CELL_W[ITEM_COUNT]", [i["cell"][0] for i in items], 8)
    _emit_table(lines, "constexpr uint8_t ITEM_CELL_H[ITEM_COUNT]", [i["cell"][1] for i in items], 8)
    _emit_table(lines, "constexpr int8_t ITEM_ANCHOR_X[ITEM_COUNT]", [i["anchor"][0] for i in items], 8)
    _emit_table(lines, "constexpr int8_t ITEM_ANCHOR_Y[ITEM_COUNT]", [i["anchor"][1] for i in items], 8)
    app("")
    app("// Pose -> phase row, resolved. All rows are 0 for single-row sheets.")
    for item in items:
        _emit_table(lines, "constexpr uint8_t POSE_ROW_%s[POSE_COUNT]" % item["id"].upper(),
                    item["poseRows"], POSE_COUNT)
    app("")
    app("// Frame index tables: FRAME_<ID>[pose][facing]. 'facing' sheets index the")
    app("// facing directly (single-frame sheets repeat frame 0); 'facing*pose' sheets")
    app("// use row * FACINGS + facing.")
    for item in items:
        app("constexpr uint8_t FRAME_%s[POSE_COUNT][FACINGS] = {" % item["id"].upper())
        for row in frame_table(item):
            app("    {" + ", ".join(str(v) for v in row) + "},")
        app("};")
    app("")
    emit_part_view(lines, items, fx_symbols, default_set)
    app("}   // namespace equip")
    app("")
    return "\n".join(lines)


def emit_part_view(lines, items, fx_symbols, default_set):
    """Player part view offsets for the cart part records (gen-art overlays +
    authored shadow/body/head layers).

    The records themselves live in the mhEquip cart blob (pack_blob's part
    section); only the byte offsets are generated here, so the render path
    (src/render.hpp partDraw) reads sheet + anchor + order/frames + frame from
    the cart during the render pass. See docs/equipment-framework.md and
    tst/fxdatatest/player_art_test.hpp for the pixel oracle."""
    layout = part_section(items)
    if layout is None:
        return
    parts = layout["parts"]
    app = lines.append
    app("// ---- player part view (records live in the mhEquip blob) ----------")
    app("// One PART_SIZE record per part at PARTS_OFF: sheet u24 (fx offset),")
    app("// anchorX i8, anchorY i8, order u8, frames u8, frame[POSE_COUNT] u8;")
    app("// then the u16 variant index table and the variant frame bytes.")
    app("// Little-endian; read on device through core/fxmem.hpp during the")
    app("// render pass (src/render.hpp partDraw/partFrame).")
    app("constexpr uint8_t PART_COUNT = %d;" % len(parts))
    for i, item in enumerate(parts):
        app("constexpr uint8_t PART_%s = %d;" % (item["id"].upper(), i))
    app("")
    app("constexpr uint16_t PARTS_OFF = %d;" % layout["parts_off"])
    app("constexpr uint8_t PART_SIZE = %d;" % PART_SIZE)
    app("constexpr uint8_t PART_SHEET_OFF = 0;             // u24")
    app("constexpr uint8_t PART_ANCHOR_X_OFF = 3;          // i8")
    app("constexpr uint8_t PART_ANCHOR_Y_OFF = 4;          // i8")
    app("constexpr uint8_t PART_ORDER_OFF = 5;             // u8 ORDER_*")
    app("constexpr uint8_t PART_FRAMES_OFF = 6;            // u8")
    app("constexpr uint8_t PART_FRAME_OFF = 7;             // u8[POSE_COUNT]")
    app("constexpr uint16_t PART_VARIANT_OFFSETS_OFF = %d;" % layout["variants_off"])
    app("constexpr uint16_t PART_VARIANT_DATA_OFF = %d;" % layout["variant_data_off"])
    app("constexpr uint8_t PART_VARIANT_COUNT = %d;" % len(layout["variant_data"]))
    app("")

    if default_set:
        # data/equipment/sets/default.json -> the part ids the slot loop draws.
        # Switching the default head/body is a JSON edit + make gen. Omitted
        # slots emit no constant (the layer is not drawn).
        app("// Default draw set (data/equipment/sets/default.json): the render")
        app("// slot loop draws these part ids, so re-skinning the player is a")
        app("// data edit + make gen, never a render edit.")
        for slot in LAYERED_SLOTS:
            if slot in default_set:
                app("constexpr uint8_t DEFAULT_%s = PART_%s;" % (slot.upper(), default_set[slot].upper()))
        app("")

    # Pin the baked absolute sheet offsets against the live fxdata.h symbols.
    # The values came from the previous fxdata.h, so adding/renaming a sheet
    # needs a second `make gen` to re-bake (authored layers included); this
    # AVR-only assert fails the build if that pass was skipped (stale equip.bin).
    # Zero flash cost.
    sheets = []
    for item in parts:
        if item["sheet"] not in sheets:
            sheets.append(item["sheet"])
    app("// Baked absolute sheet offsets (one per referenced part symbol).")
    app("// The blob stores these; a static_assert pins each against fxdata.h so a")
    app("// stale equip.bin (one gen pass behind) cannot ship on AVR.")
    for sheet in sheets:
        # uint32_t (not uint16_t): authored equip sheets live after the tables
        # and can sit above 64 KB in the FX image (SpritesU seeks with uint24_t).
        app("constexpr uint32_t SHEET_OFF_%s = %d;" % (sheet.upper(), (fx_symbols or {}).get(sheet, 0)))
    app("#if defined(__AVR__)")
    for sheet in sheets:
        app("static_assert(SHEET_OFF_%s == static_cast<uint32_t>(%s), \"equip blob stale: re-run make gen\");"
            % (sheet.upper(), sheet))
    app("#endif")
    app("")


# --------------------------------------------------------------------------- main


def write_if_changed(path, data):
    if isinstance(data, str):
        data = data.encode("utf-8")
    if os.path.isfile(path):
        with open(path, "rb") as handle:
            if handle.read() == data:
                return False
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(data)
    return True


def clean_stale(directory, expected):
    for name in sorted(os.listdir(directory)):
        if name in expected or not name.endswith(".png"):
            continue
        os.remove(os.path.join(directory, name))
        print("gen-equipment: removed stale %s" % os.path.join(IMAGES_REL, name))


def run(root, dump):
    errors = Errors()
    model = compile_model(errors, root)
    if model is None or errors.items:
        for item in errors.items:
            print("gen-equipment: error: %s" % item, file=sys.stderr)
        print("gen-equipment: FAIL (%d error%s)"
              % (len(errors.items), "" if len(errors.items) == 1 else "s"), file=sys.stderr)
        return 1
    items = model["items"]
    blob = pack_blob(errors, items, model["fxSymbols"])
    if blob is None or errors.items:
        for item in errors.items:
            print("gen-equipment: error: %s" % item, file=sys.stderr)
        print("gen-equipment: FAIL", file=sys.stderr)
        return 1

    if dump:
        for item in items:
            cw, ch = item["cell"]
            print("item %s: slot %s sheet %s cell %dx%d anchor %d,%d order %s frames %d rows %d"
                  % (item["id"], item["slot"], item["sheet"], cw, ch,
                     item["anchor"][0], item["anchor"][1], item["order"], item["frames"], item["rows"]))
            print("    pose rows: " + ", ".join("%s=%d" % (pose, row) for pose, row in zip(POSES, item["poseRows"])))
        if model["defaultSet"]:
            print("default set: " + ", ".join("%s=%s" % (slot, model["defaultSet"][slot])
                                               for slot in LAYERED_SLOTS if slot in model["defaultSet"]))
        print("gen-equipment: %d items, %d B blob" % (len(items), len(blob)))
        return 0

    global ATK_IDS, WEAPON_MOVES
    if any(item["mirror"] for item in items):
        # Authored weapon sheets derive their poses from the move records; the
        # AtkId names come from game.hpp so the slot fold cannot drift from the
        # render. Trees without either (unit fixtures) never reach this.
        ATK_IDS = load_atk_ids(root)
        WEAPON_MOVES = load_weapon_moves(root)
    images_dir = os.path.join(root, IMAGES_REL)
    os.makedirs(images_dir, exist_ok=True)
    wrote = set()
    expected = set()
    sizes = {}
    for item in items:
        if item["genArt"]:
            continue   # reuses an existing fx sprite; nothing to author
        name = image_name(item)
        expected.add(name)
        sheet = author_sheet(item)
        sizes[item["id"]] = sheet.size
        rel = "%s/%s" % (IMAGES_REL, name)
        if write_if_changed(os.path.join(images_dir, name), png_bytes(sheet)):
            wrote.add(rel)
    clean_stale(images_dir, expected)

    # Mirror-authored sheets: the packer (tools/convert-sprite.py) reads the
    # plan so the 5-col source ships as the 8-col sheet (docs/weapon-art.md).
    plans = {item["sheet"]: weapon_plan(item) for item in items if item["mirror"]}
    layout_path = os.path.join(images_dir, "layout.json")
    if plans:
        if write_if_changed(layout_path, json.dumps({"version": 1, "sheets": plans}, indent=2) + "\n"):
            wrote.add("%s/layout.json" % IMAGES_REL)
    elif os.path.isfile(layout_path):
        os.remove(layout_path)
        print("gen-equipment: removed stale %s/layout.json" % IMAGES_REL)

    if write_if_changed(os.path.join(root, BLOB_REL), blob):
        wrote.add(BLOB_REL)
    header = emit_meta_header(items, blob, model["fxSymbols"], model["defaultSet"])
    if write_if_changed(os.path.join(root, META_REL), header):
        wrote.add(META_REL)

    print("gen-equipment: %d items, %d B blob (magic 0x%04X version %d)"
          % (len(items), len(blob), MAGIC, VERSION))
    for item in items:
        if item["genArt"]:
            print("gen-equipment: %s (gen-art %s, no PNG)" % (item["sheet"], item["id"]))
            continue
        rel = "%s/%s" % (IMAGES_REL, image_name(item))
        width, height = sizes[item["id"]]
        print("gen-equipment: %s (%dx%d, %s)" % (rel, width, height,
                                                 "wrote" if rel in wrote else "unchanged"))
    for rel in (BLOB_REL, META_REL):
        print("gen-equipment: %s%s" % (rel, "" if rel in wrote else " (unchanged)"))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=None, help="pipeline root holding data/ (default: repository root)")
    parser.add_argument("--dump", action="store_true", help="validate + list the catalog; write nothing")
    args = parser.parse_args(argv)
    root = os.path.abspath(args.root) if args.root else ROOT
    return run(root, args.dump)


if __name__ == "__main__":
    sys.exit(main())
