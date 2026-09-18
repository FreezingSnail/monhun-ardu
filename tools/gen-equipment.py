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
    prefilled); weapon/offhand placeholders stay blank.
  * `"source": "gen-art"` ref: the record reuses an existing gen-art sprite
    symbol (`sheet`, validated and resolved against fxdata/fxdata.h); no PNG is
    authored. A 17 B part record (sheet offset, anchor, per-pose frame,
    optional variants) is packed into the same blob for the render path --
    see tst/fxdatatest/player_art_test.hpp.

The placeholder art is authored from the same 4-shade primitives as
tools/gen-art.py / tools/gen-base-sheet.py (palette copied here on purpose:
1:1 with the L4 triplane levels).

Blob layout (little-endian, explicit u8/i8, no padding, fixed order), following
the tools/gen-fxtables.cpp serializer pattern:

    header   8 B  magic u16 (0x4551), version u8, flags u8, itemCount u16,
                   reserved u16
    item    19 B  slot, order, frames, cellW, cellH, anchorX i8, anchorY i8,
                   poseRow[12]
    part    17 B  gen-art part records in PART_* order: sheet u24 (fx offset),
                   anchorX i8, anchorY i8, frame[12] u8  (only when the
                   catalog has `source: "gen-art"` records)
    varoff 2*(n+1) B  u16 variant-data index per part, then the variant bytes

The part view is read on device from the mhEquip blob during the render pass
(tools emit only the offsets into equip_meta.hpp; see src/render.hpp partDraw).

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
ART_SOURCES = ("gen-art",)

SLOTS = ("player", "shadow", "body", "head", "weapon", "offhand")
ORDERS = ("facing", "facing*pose", "pose")
POSES = ("idle", "attack_startup", "attack_active", "attack_recover", "parry",
         "whirl", "guard", "shove", "dodge", "deflect", "stun", "dead")
POSE_COUNT = len(POSES)

# Part-view record: sheet u24 + anchorX i8 + anchorY i8 + frame[12].
PART_SIZE = 5 + POSE_COUNT

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

_MISSING = object()


class Errors:
    """Collects user-facing validation errors; never raises on schema issues."""

    def __init__(self):
        self.items = []

    def add(self, ctx, message):
        self.items.append("%s: %s" % (ctx, message))


def is_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


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
    # declared in fxdata/fxdata.h. Everything else keeps the authored-sheet rules.
    source = obj.get("source")
    gen_art = source == "gen-art"
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
            "genArt": gen_art, "variants": var_list, "flags": list(flags)}


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
    return {"items": items, "fxSymbols": fx_symbols}


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
    # Helmet eye slot per facing (x, y, w, h); None = facing away, hidden.
    slits = (
        (9, 3, 1, 2),    # E profile
        (9, 4, 2, 1),    # SE
        (7, 4, 3, 1),    # S front
        (5, 4, 2, 1),    # SW
        (6, 3, 1, 2),    # W profile
        None,            # NW
        None,            # N
        None,            # NE
    )
    slit = slits[facing % 8]
    if slit is not None:
        rect(img, slit[0], slit[1], slit[2], slit[3], BLACK)
    return img


def body_cell():
    """Current fxplayer body minus head and shadow (mock drawPlayer rects)."""
    img = new(16, 16)
    rect(img, 4, 7, 8, 6, WHITE)    # torso
    rect(img, 5, 13, 2, 2, WHITE)   # legs
    rect(img, 9, 13, 2, 2, WHITE)
    return img


def head_cell():
    img = new(16, 16)
    rect(img, 5, 1, 6, 6, WHITE)    # mock head rect
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
        return body_cell() if index == 0 else None
    if slot == "head":
        return head_cell() if index == 0 else None
    return None  # weapon / offhand placeholders stay blank


def author_sheet(item):
    cw, ch = item["cell"]
    frames = item["frames"]
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
    """Layout of the gen-art part view section (None without gen-art records).

    Returns the part list and every offset the blob packer and the header
    emitter share, so the two cannot disagree.
    """
    parts = [item for item in items if item["genArt"]]
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
                errors.add(item["id"], "internal: no fx offset for sheet %r" % item["sheet"])
                return None
            if not 0 <= value <= 0xFFFFFF:
                errors.add(item["id"], "internal: sheet offset out of range: %d" % value)
                return None
            ax, ay = item["anchor"]
            blob += int(value).to_bytes(3, "little")
            blob += struct.pack("<bb", ax, ay)
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


def emit_meta_header(items, blob, fx_symbols):
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
    emit_part_view(lines, items, fx_symbols)
    app("}   // namespace equip")
    app("")
    return "\n".join(lines)


def emit_part_view(lines, items, fx_symbols):
    """Player part view offsets for `source: "gen-art"` records.

    The records themselves live in the mhEquip cart blob (pack_blob's part
    section); only the byte offsets are generated here, so the render path
    (src/render.hpp partDraw) reads sheet + anchor + frame from the cart during
    the render pass. See docs/equipment-framework.md and
    tst/fxdatatest/player_art_test.hpp for the pixel oracle."""
    layout = part_section(items)
    if layout is None:
        return
    parts = layout["parts"]
    app = lines.append
    app("// ---- gen-art player part view (records live in the mhEquip blob) --")
    app("// One PART_SIZE record per part at PARTS_OFF: sheet u24 (fx offset),")
    app("// anchorX i8, anchorY i8, frame[POSE_COUNT] u8; then the u16")
    app("// variant index table and the variant frame bytes. Little-endian; read")
    app("// on device through core/fxmem.hpp during the render pass.")
    app("constexpr uint8_t PART_COUNT = %d;" % len(parts))
    for i, item in enumerate(parts):
        app("constexpr uint8_t PART_%s = %d;" % (item["id"].upper(), i))
    app("")
    app("constexpr uint16_t PARTS_OFF = %d;" % layout["parts_off"])
    app("constexpr uint8_t PART_SIZE = %d;" % PART_SIZE)
    app("constexpr uint8_t PART_SHEET_OFF = 0;             // u24")
    app("constexpr uint8_t PART_ANCHOR_X_OFF = 3;          // i8")
    app("constexpr uint8_t PART_ANCHOR_Y_OFF = 4;          // i8")
    app("constexpr uint8_t PART_FRAME_OFF = 5;             // u8[POSE_COUNT]")
    app("constexpr uint16_t PART_VARIANT_OFFSETS_OFF = %d;" % layout["variants_off"])
    app("constexpr uint16_t PART_VARIANT_DATA_OFF = %d;" % layout["variant_data_off"])
    app("constexpr uint8_t PART_VARIANT_COUNT = %d;" % len(layout["variant_data"]))
    app("")

    # Pin the baked absolute sheet offsets against the live fxdata.h symbols.
    # The values came from the previous fxdata.h, so adding/renaming a gen-art
    # sheet needs a second `make gen` to re-bake; this AVR-only assert fails the
    # build if that pass was skipped (stale equip.bin). Zero flash cost.
    sheets = []
    for item in parts:
        if item["sheet"] not in sheets:
            sheets.append(item["sheet"])
    app("// Baked absolute sheet offsets (one per referenced gen-art symbol).")
    app("// The blob stores these; a static_assert pins each against fxdata.h so a")
    app("// stale equip.bin (one gen pass behind) cannot ship on AVR.")
    for sheet in sheets:
        app("constexpr uint16_t SHEET_OFF_%s = %d;" % (sheet.upper(), fx_symbols[sheet]))
    app("#if defined(__AVR__)")
    for sheet in sheets:
        app("static_assert(SHEET_OFF_%s == static_cast<uint16_t>(%s), \"equip blob stale: re-run make gen\");"
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
        print("gen-equipment: %d items, %d B blob" % (len(items), len(blob)))
        return 0

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

    if write_if_changed(os.path.join(root, BLOB_REL), blob):
        wrote.add(BLOB_REL)
    header = emit_meta_header(items, blob, model["fxSymbols"])
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
