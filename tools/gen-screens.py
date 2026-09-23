#!/usr/bin/env python3
"""Compile data-driven screen JSON into the packed FX blob + generated header.

    data/screens/*.json
        -> fxdata/tables/screens.bin        (packed blob; build intermediate)
        -> src/generated/screen_meta.hpp    (screen indices, offsets, enums)

Design: docs/quests-shops.md "Screen data" (beads qs.1). One generic list
renderer walks these records; adding a screen or a row is a JSON edit + `make
gen` and never touches render code.

Blob layout (little-endian, explicit u8/u16, no padding, fixed order):

    header     8 B  magic u16 0x5343, version u8, flags u8, screenCount u8,
                    reserved u8, rowCount u16
    defOff     2*screenCount B  u16 absolute byte offset of each ScreenDef,
                    ordered by the screen index in screen_meta.hpp
    defs       variable  ScreenDef: id u8, titleLen u8, title[titleLen],
                    rowCount u8, firstRow u16 (absolute blob offset)
    rows       variable  ScreenRow: labelLen u8, label[labelLen], cost u16,
                    actionId u8, flags u8, condId u8, param u8
    pageTable  variable  per screen (index order): pageCount u8, then pageCount
                    x u24 absolute FX addresses of the baked mh_screen_<name>_
                    <page> layer arrays (0 = not prebaked / unresolved)

The runtime (src/screens.hpp) reads records through core/fxmem.hpp during the
render/scan window; the host suite uses plain row structs. Action/condition/
flag enum values are emitted here so the packer and the runtime cannot drift.

Screen prebake v2 (epic hbk, docs/ui-design.md): a screen with "prebake": true
also bakes one 128x64 4-shade image per 6-row page (the runtime scroll window),
written to images/screens/<symbol>_128x64.png and declared in
fxdata/screens/Sprites.txt as 3x 1bpp page-major layer arrays; the page table
above carries their absolute FX addresses so the device can blit them.

Usage:
    python3 tools/gen-screens.py [--root DIR] [--dump] [--sheet FILE]

    --root DIR  pipeline root holding data/ and src/generated (default: repo)
    --dump      validate + list the compiled screens on stdout; writes nothing
    --sheet FILE  also render every baked page to a review PNG (never committed)
"""
import argparse
import importlib.util
import json
import os
import re
import struct
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
DATA_DIR = "data/screens"
BLOB_REL = "fxdata/tables/screens.bin"
META_REL = "src/generated/screen_meta.hpp"
SPRITES_REL = "fxdata/screens/Sprites.txt"
IMAGE_DIR_REL = "images/screens"
FX_HEADER_REL = "fxdata/fxdata.h"
SKILLS_REL = "data/skills.json"

MAGIC = 0x5343
VERSION = 1
FLAGS = 0
HEADER_SIZE = 8
DEF_OFF_OFF = HEADER_SIZE

NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")
TITLE_MAX = 16
LABEL_MAX = 16
SCREEN_MAX = 255
TIER_COUNT = 3   # N_WEAPONS (W_SWORD/W_FLAIL/W_GUN); must match core/save.hpp

# ui.3.1 (5co.6) dropped open_smith/craft_armor and the armor condition: the
# smith screen is gone (FORGE replaces it in ui.4) and armor crafting moved onto
# the detail card (the bill bakes into mhCards). Ids are regenerated wholesale,
# so the runtime reads them from the generated constants, never literals.
ACTION_NAMES = ("leave", "buy_upgrade", "take_quest", "turn_in_quest",
                "none", "hunt", "open_quests",
                "equip_weapon", "open_gear", "equip_armor",
                "forge_node", "open_forge")
COND_NAMES = ("always", "zenny", "flag", "tier", "quest", "upgrade")
# hide_locked: reserved. skill: draw the cached live skill points
# (ScreenState::skillPoints) in the cost column plus an S/M tier letter next to
# it (gs.2 GEAR readout); `param` = the armor::SKILL_* index. The qs.4 zenny
# dynamic-value token is retired (ui.5): the live balance moved to the header.
ROW_FLAGS = {"hide_locked": 0x01, "skill": 0x04, "forge": 0x08}
# Per-screen generated weapon block (ui.4, 5co.4): "weapons": "forge" emits the
# class headers + forge_node rows (cost column = the upgrade cost), "equip"
# emits the same tree with equip_weapon rows (no cost; the GEAR list). The rows
# are built from data/forge/*.json so the tree is authored once.
WEAPONS_MODES = ("forge", "equip")
# COND_UPGRADE param packs (unlockFlag << 4) | (weaponIdx << 2) | tier (see
# screen_state.hpp): unlock 0 = always, else 1-based quest whose done bit gates
# the tier; weapon 0..TIER_COUNT-1; tier 1..SCREEN_MAX_TIER (2 in data).
UPGRADE_TIER_MAX = 3

# ---- screen prebake v2 (epic hbk, docs/ui-design.md) ------------------------
# A screen with "prebake": true bakes one 128x64 4-shade page per 6-row block;
# the device blits the page (cardBlit, the same 3x 1bpp page-major layer family
# as the detail cards) and draws only the live chrome on top. The layout below
# is frozen in docs/ui-design.md and must mirror src/screens.hpp:
# SCREEN_ROW_Y0=11, SCREEN_ROW_H=9, SCREEN_LABEL_X=10.
PREBAKE_LAYOUT = {
    "title_band_h": 8,    # dark band behind the title + live zenny
    "rule_y": 8,          # light rule under the band
    "row_y0": 11,         # first row's glyph lane (src/screens.hpp)
    "row_h": 9,
    "rows_per_page": 6,
    "label_x": 10,        # past the live cursor chip
    "cost_right": 112,    # cost digits end here (marker column 116..123 stays clear)
    "prefix_chars": " +-|",   # tree prefix drawn dark, the name light
    "header_prefix": "--",    # section header rows (class/armor bands)
    "cost_max": 999,      # 3 digits fit left of the marker column
    # Hub bottom strip (ui.5.2/hbk.3): the five skill abbreviations bake at
    # fixed slots on the hub page; the device draws only the live weapon class
    # abbr + tier and each active skill's points (slot + 12).
    "strip_y": 56,
    "strip_slot_x0": 20,
    "strip_slot_w": 20,
}
PAGE_MAX = 8              # baked pages per screen (u8 count; 8 x 6 = 48 rows)

# 4-shade palette (1:1 with L4_Triplane; same values as gen-cards/gen-zones).
CLEAR = (0, 0, 0, 0)
BLACK = (0, 0, 0, 255)
DARK = (85, 85, 85, 255)
LIGHT = (170, 170, 170, 255)
WHITE = (255, 255, 255, 255)
SHADE_STEP = (254 + 4) // 4   # 64, mirrors convert-sprite/gen-zones/gen-cards

PAGE_W = 128
PAGE_H = 64
PAGE_LAYERS = 3
PAGE_LAYER_BYTES = PAGE_W * (PAGE_H // 8)   # 1024
PAGE_BYTES = PAGE_LAYER_BYTES * PAGE_LAYERS  # 3072

_MISSING = object()


class Errors:
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


def read_int(errors, ctx, obj, key, lo, hi, default=_MISSING):
    if not isinstance(obj, dict) or key not in obj:
        if default is _MISSING:
            return None
        return default
    value = obj[key]
    if not is_int(value):
        errors.add(ctx, "%s: expected an integer, got %r" % (key, value))
        return None
    if not lo <= value <= hi:
        errors.add(ctx, "%s: out of range %d..%d: %d" % (key, lo, hi, value))
        return None
    return value


def read_enum(errors, ctx, obj, key, table, default=_MISSING):
    if not isinstance(obj, dict) or key not in obj:
        if default is _MISSING:
            return None
        return default
    value = obj[key]
    if not isinstance(value, str) or value not in table:
        errors.add(ctx, "%s: unknown value %r (want one of %s)" % (key, value, ", ".join(table)))
        return None
    return table.index(value) if isinstance(table, tuple) else table[value]


def read_text(errors, ctx, obj, key, max_len):
    value = obj.get(key) if isinstance(obj, dict) else None
    if not isinstance(value, str):
        errors.add(ctx, "%s: expected a string, got %r" % (key, value))
        return None
    if not 1 <= len(value) <= max_len:
        errors.add(ctx, "%s: length %d outside 1..%d" % (key, len(value), max_len))
        return None
    if any(not 32 <= ord(ch) <= 126 for ch in value):
        errors.add(ctx, "%s: chars must be printable ASCII (32..126)" % key)
        return None
    return value


def read_flags(errors, ctx, obj):
    value = obj.get("flags", []) if isinstance(obj, dict) else []
    if not isinstance(value, list):
        errors.add(ctx, "flags: expected an array of names")
        return None
    mask = 0
    seen = set()
    for i, name in enumerate(value):
        if not isinstance(name, str) or name not in ROW_FLAGS:
            errors.add(ctx, "flags[%d]: unknown flag %r (want one of %s)"
                       % (i, name, ", ".join(sorted(ROW_FLAGS))))
            continue
        if name in seen:
            errors.add(ctx, "flags[%d]: duplicate flag '%s'" % (i, name))
            continue
        seen.add(name)
        mask |= ROW_FLAGS[name]
    return mask


def normalize_row(errors, ctx, obj):
    check_keys(errors, ctx, obj, {"label", "cost", "action"}, ("flags", "condition", "param"))
    label = read_text(errors, ctx, obj, "label", LABEL_MAX)
    cost = read_int(errors, ctx, obj, "cost", 0, 65535)
    action = read_enum(errors, ctx, obj, "action", ACTION_NAMES)
    cond = read_enum(errors, ctx, obj, "condition", COND_NAMES, default=0)
    param = read_int(errors, ctx, obj, "param", 0, 255, default=0)
    mask = read_flags(errors, ctx, obj)
    if cond == COND_NAMES.index("flag") and param is not None and param > 31:
        errors.add(ctx, "param: save flag bit must be 0..31, got %d" % param)
    if cond == COND_NAMES.index("tier") and param is not None and param >= TIER_COUNT:
        errors.add(ctx, "param: tier index must be 0..%d, got %d" % (TIER_COUNT - 1, param))
    if cond == COND_NAMES.index("quest") and param is not None:
        # param packs (need << 4) | quest id (see screen_state.hpp COND_QUEST):
        # take rows leave the need nibble 0, turn-in rows carry their need there.
        need = (param >> 4) & 15
        if action == ACTION_NAMES.index("take_quest") and need != 0:
            errors.add(ctx, "param: take_quest need nibble must be 0, got %d" % need)
        if action == ACTION_NAMES.index("turn_in_quest") and need == 0:
            errors.add(ctx, "param: turn_in_quest need nibble must be 1..15, got %d" % need)
        if action not in (ACTION_NAMES.index("take_quest"), ACTION_NAMES.index("turn_in_quest")):
            errors.add(ctx, "condition 'quest' needs a take_quest/turn_in_quest action")
    if action == ACTION_NAMES.index("equip_armor") and param is not None:
        # param packs (slot << 5) | pieceIdx (see src/card_state.hpp): the GEAR
        # row opens the armor card, whose A crafts/equips into that slot.
        slot = (param >> 5) & 3
        if slot >= 3:
            errors.add(ctx, "param: armor slot must be 0..2, got %d" % slot)
    if cond == COND_NAMES.index("upgrade") and param is not None:
        # param packs (unlock << 4) | (weapon << 2) | tier (see screen_state.hpp).
        weapon = (param >> 2) & 3
        tier = param & 3
        if action != ACTION_NAMES.index("buy_upgrade"):
            errors.add(ctx, "condition 'upgrade' needs a buy_upgrade action")
        if weapon >= TIER_COUNT:
            errors.add(ctx, "param: upgrade weapon index must be 0..%d, got %d" % (TIER_COUNT - 1, weapon))
        if tier < 1 or tier > UPGRADE_TIER_MAX:
            errors.add(ctx, "param: upgrade tier must be 1..%d, got %d" % (UPGRADE_TIER_MAX, tier))
    if None in (label, cost, action, cond, param, mask):
        return None
    return {"label": label, "cost": cost, "action": action, "flags": mask, "cond": cond, "param": param}


def load_json(errors, path, rel):
    try:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    except OSError as exc:
        errors.add(rel, "cannot read file: %s" % exc)
    except ValueError as exc:
        errors.add(rel, "invalid JSON: %s" % exc)
    return None


def load_forge_module():
    spec = importlib.util.spec_from_file_location("gen_forge", os.path.join(HERE, "gen-forge.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_glyphs():
    """The GLYPHS table from tools/gen-art.py (single source for the 4x8 lane)."""
    spec = importlib.util.spec_from_file_location("gen_art", os.path.join(HERE, "gen-art.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.GLYPHS


GLYPHS = load_glyphs()


def load_fxdata_symbols(root):
    path = os.path.join(root, FX_HEADER_REL)
    try:
        with open(path, encoding="utf-8") as handle:
            return {name: int(value, 0) for name, value in
                    re.findall(r"constexpr\s+uint24_t\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(0[xX][0-9A-Fa-f]+|\d+)",
                               handle.read())}
    except OSError:
        return {}


def read_skill_abbrs(errors, root):
    """3-char skill abbreviations (armor::SKILL_* order) for the baked hub strip."""
    doc = load_json(errors, os.path.join(root, SKILLS_REL), SKILLS_REL)
    if doc is None:
        return None
    raw = doc.get("skills") if isinstance(doc, dict) else None
    if not isinstance(raw, list) or not raw:
        errors.add(SKILLS_REL, "skills: expected a non-empty array")
        return None
    abbrs = []
    for i, obj in enumerate(raw):
        abbr = obj.get("abbr") if isinstance(obj, dict) else None
        if not isinstance(abbr, str) or not 2 <= len(abbr) <= 4:
            errors.add("%s: skills[%d].abbr" % (SKILLS_REL, i), "expected a 2..4 char abbr")
            return None
        abbrs.append(abbr[:3])
    return abbrs


def weapon_rows(errors, ctx, mode, forge_model):
    """Class headers + one row per node, generated from the forge tree. The
    `mode` picks the row action (forge_node vs equip_weapon) and whether the
    cost column carries the upgrade cost (FORGE) or stays 0 (GEAR)."""
    gen_forge = load_forge_module()
    rows = []
    action = ACTION_NAMES.index("forge_node" if mode == "forge" else "equip_weapon")
    for entry in forge_model["classes"]:
        rows.append({"label": entry["header"], "cost": 0, "action": ACTION_NAMES.index("none"),
                     "flags": 0, "cond": COND_NAMES.index("always"), "param": 0})
        for node in forge_model["nodes"]:
            if node["class"] != entry["class"]:
                continue
            label = gen_forge.row_label(forge_model, node)
            if len(label) > LABEL_MAX:
                errors.add(ctx, "weapon row %r exceeds %d chars" % (label, LABEL_MAX))
                continue
            rows.append({"label": label,
                         "cost": node["cost"] if mode == "forge" else 0,
                         "action": action, "flags": ROW_FLAGS["forge"],
                         "cond": COND_NAMES.index("always"), "param": node["index"]})
    if not rows:
        errors.add(ctx, "weapons: no forge nodes to generate rows from")
    return rows


# ------------------------------------------------------- prebaked page art


def new_page():
    return Image.new("RGBA", (PAGE_W, PAGE_H), CLEAR)


def fill_rect(img, x, y, w, h, color):
    px = img.load()
    for yy in range(max(0, y), min(PAGE_H, y + h)):
        for xx in range(max(0, x), min(PAGE_W, x + w)):
            px[xx, yy] = color


def draw_text(img, x, y, text, color):
    """Plot `text` on the 4 px glyph lane (3 px glyph + 1 px gap, rows 0..4).

    Must match the device textPut() lane glyph-for-glyph: the runtime re-draws
    the selected row's label in white exactly over its baked copy."""
    px = img.load()
    for i, ch in enumerate(text.upper()):
        glyph = GLYPHS.get(ch)
        if glyph is None:
            continue
        for row, bits in enumerate(glyph):
            for col in range(3):
                if bits & (4 >> col):
                    xx = x + i * 4 + col
                    yy = y + row
                    if 0 <= xx < PAGE_W and 0 <= yy < PAGE_H:
                        px[xx, yy] = color


def text_w(text):
    return len(text) * 4


def split_prefix(label):
    """(tree prefix, name): the leading run of tree-drawing chars, drawn dark."""
    i = 0
    while i < len(label) and label[i] in PREBAKE_LAYOUT["prefix_chars"]:
        i += 1
    return label[:i], label[i:]


def header_text(label):
    """Section header text without the ASCII frame: the band already groups the
    section, so "-- SWD --" bakes as "SWD". Falls back to the raw label when
    stripping leaves nothing."""
    text = label.strip().strip("-").strip()
    return text if text else label


def screen_pages(screen, skill_abbrs):
    """Bake the screen's pages: [(page index, PIL image)], one per 6-row block
    -- exactly the runtime scroll window (page = scroll / 6)."""
    layout = PREBAKE_LAYOUT
    rows = screen["rows"]
    pages = []
    page_count = (len(rows) + layout["rows_per_page"] - 1) // layout["rows_per_page"]
    for page in range(page_count):
        img = new_page()
        fill_rect(img, 0, 0, PAGE_W, layout["title_band_h"], DARK)
        draw_text(img, 2, 0, screen["title"], WHITE)
        # Page indicator (n/m): static per baked page, so it is baked here and
        # the runtime no longer draws it (title width is known at bake time).
        if page_count > 1:
            draw_text(img, 2 + text_w(screen["title"]) + 4, 0, "%d/%d" % (page + 1, page_count), LIGHT)
        for x in range(PAGE_W):
            img.load()[x, layout["rule_y"]] = LIGHT
        # Hub bottom strip: the five skill labels bake at fixed slots (the live
        # weapon marker + points are drawn by the device on the same line).
        if screen["strip"] and page == 0:
            for i, abbr in enumerate(skill_abbrs):
                draw_text(img, layout["strip_slot_x0"] + i * layout["strip_slot_w"],
                          layout["strip_y"], abbr, LIGHT)
        for i in range(layout["rows_per_page"]):
            index = page * layout["rows_per_page"] + i
            if index >= len(rows):
                break
            row = rows[index]
            y = layout["row_y0"] + i * layout["row_h"]
            label = row["label"]
            if label.startswith(layout["header_prefix"]):
                fill_rect(img, 0, y - 1, PAGE_W, layout["title_band_h"], DARK)
                text = header_text(label)
                draw_text(img, (PAGE_W - text_w(text)) // 2, y, text, WHITE)
                continue
            prefix, name = split_prefix(label)
            draw_text(img, layout["label_x"], y, prefix, LIGHT)
            draw_text(img, layout["label_x"] + text_w(prefix), y, name, LIGHT)
            if row["cost"] > 0:
                digits = str(row["cost"])
                draw_text(img, layout["cost_right"] - text_w(digits), y, digits, WHITE)
        pages.append((page, img))
    return pages


def page_symbol(screen, page):
    return "mh_screen_%s_%d" % (screen["name"], page)


def page_image_rel(screen, page):
    return "%s/%s_%dx%d.png" % (IMAGE_DIR_REL, page_symbol(screen, page), PAGE_W, PAGE_H)


def shade_on(red, layer):
    """True when a pixel lights layer `layer`; mirrors convert-sprite SHADES=4."""
    return 1 if red >= (layer + 1) * SHADE_STEP else 0


def page_layers(img):
    """128x64 RGBA -> 3x 1bpp page-major layers (plane p at p*PAGE_LAYER_BYTES)."""
    out = bytearray(PAGE_BYTES)
    px = img.load()
    for y in range(PAGE_H):
        base = (y // 8) * PAGE_W
        bit = 1 << (y & 7)
        for x in range(PAGE_W):
            r, g, b, a = px[x, y]
            if a < 128:
                continue
            for p in range(PAGE_LAYERS):
                if shade_on(r, p):
                    out[p * PAGE_LAYER_BYTES + base + x] |= bit
    return bytes(out)


def emit_page_sprites(screens):
    lines = ["// Generated by tools/gen-screens.py -- do not edit.",
             "// Prebaked list-screen layer arrays (3x 1bpp page-major, 128x64); see docs/ui-design.md."]
    for screen in screens:
        for page, img in screen["pages"]:
            data = page_layers(img)
            lines.append("uint8_t %s[] =" % page_symbol(screen, page))
            lines.append("{")
            for i in range(0, len(data), 16):
                lines.append("    " + ", ".join(str(b) for b in data[i:i + 16]) + ",")
            lines.append("};")
    lines.append("")
    return "\n".join(lines)


def clean_stale_images(screens, root):
    directory = os.path.join(root, IMAGE_DIR_REL)
    if not os.path.isdir(directory):
        return
    expected = set()
    for screen in screens:
        for page, _img in screen["pages"]:
            expected.add(os.path.basename(page_image_rel(screen, page)))
    for name in sorted(os.listdir(directory)):
        if name in expected or not name.endswith(".png"):
            continue
        os.remove(os.path.join(directory, name))
        print("gen-screens: removed stale %s/%s" % (IMAGE_DIR_REL, name))


def normalize_screen(errors, rel, name, obj, seen_ids, forge_model):
    ctx = rel
    check_keys(errors, ctx, obj, {"id", "title", "rows"}, ("weapons", "prebake", "strip"))
    if not isinstance(obj, dict):
        return None
    stem = os.path.splitext(name)[0]
    if not NAME_RE.match(stem):
        errors.add(ctx, "file name: expected [a-z][a-z0-9_]*.json, got %r" % name)
    screen_id = read_int(errors, ctx, obj, "id", 0, 255)
    if screen_id is not None:
        if screen_id in seen_ids:
            errors.add(ctx, "duplicate screen id %d" % screen_id)
        seen_ids.add(screen_id)
    title = read_text(errors, ctx, obj, "title", TITLE_MAX)
    prebake = obj.get("prebake", False)
    if not isinstance(prebake, bool):
        errors.add(ctx, "prebake: expected a boolean, got %r" % (prebake,))
        prebake = False
    strip = obj.get("strip", False)
    if not isinstance(strip, bool):
        errors.add(ctx, "strip: expected a boolean, got %r" % (strip,))
        strip = False
    mode = obj.get("weapons")
    if mode is not None and mode not in WEAPONS_MODES:
        errors.add(ctx, "weapons: unknown mode %r (want one of %s)" % (mode, ", ".join(WEAPONS_MODES)))
        mode = None
    raw_rows = obj.get("rows")
    if not isinstance(raw_rows, list) or not raw_rows:
        errors.add(ctx, "rows: expected a non-empty array")
        raw_rows = []
    rows = []
    if mode is not None:
        if forge_model is None:
            errors.add(ctx, "weapons: forge tree unavailable (data/forge/*.json)")
        else:
            rows += weapon_rows(errors, ctx, mode, forge_model)
    for i, row in enumerate(raw_rows):
        normalized = normalize_row(errors, "%s.rows[%d]" % (ctx, i), row)
        if normalized is not None:
            rows.append(normalized)
    if len(rows) > SCREEN_MAX:
        errors.add(ctx, "rows: %d exceed the %d row limit" % (len(rows), SCREEN_MAX))
    if prebake and len(rows) > PAGE_MAX * PREBAKE_LAYOUT["rows_per_page"]:
        errors.add(ctx, "prebake: %d rows exceed the %d baked pages (%d rows)"
                   % (len(rows), PAGE_MAX, PAGE_MAX * PREBAKE_LAYOUT["rows_per_page"]))
    if prebake:
        for row in rows:
            if row["cost"] > PREBAKE_LAYOUT["cost_max"]:
                errors.add(ctx, "prebake: row %r cost %d exceeds the 3-digit bake cap %d"
                           % (row["label"], row["cost"], PREBAKE_LAYOUT["cost_max"]))
    if None in (screen_id, title):
        return None
    return {"name": stem, "id": screen_id, "title": title, "rows": rows, "prebake": prebake, "strip": strip}


def compile_model(errors, root):
    data_dir = os.path.join(root, DATA_DIR)
    if not os.path.isdir(data_dir):
        errors.add(DATA_DIR, "missing screens directory")
        return None
    names = sorted(name for name in os.listdir(data_dir) if name.endswith(".json"))
    if not names:
        errors.add(DATA_DIR, "no screen JSON files found")
        return None
    # The forge tree backs the generated FORGE/GEAR weapon row blocks; only load
    # it when a screen asks (so gen-screens stays independent of data/forge).
    forge_model = None
    want_skills = False
    for name in names:
        obj = load_json(errors, os.path.join(data_dir, name), "%s/%s" % (DATA_DIR, name))
        if isinstance(obj, dict) and obj.get("weapons") is not None:
            forge_model = load_forge_module().load_model(root)
        if isinstance(obj, dict) and obj.get("strip"):
            want_skills = True
    skill_abbrs = read_skill_abbrs(errors, root) if want_skills else None
    if want_skills and skill_abbrs is None:
        errors.add(DATA_DIR, "strip: skill abbreviations unavailable (data/skills.json)")
    screens = []
    seen_ids = set()
    for name in names:
        rel = "%s/%s" % (DATA_DIR, name)
        obj = load_json(errors, os.path.join(data_dir, name), rel)
        if obj is None:
            continue
        screen = normalize_screen(errors, rel, name, obj, seen_ids, forge_model)
        if screen is not None:
            screens.append(screen)
    if errors.items:
        return None
    screens.sort(key=lambda screen: (screen["id"], screen["name"]))
    # Screen prebake v2 (hbk.2): bake the pages of every opted-in screen. The
    # page images are authored here (deterministic RGBA -> PNG -> layer arrays),
    # so a data-only screen edit re-bakes its pages.
    for screen in screens:
        screen["pages"] = screen_pages(screen, skill_abbrs) if screen["prebake"] else []
    return {"screens": screens}


def pack_blob(errors, screens, fx_symbols, unresolved):
    count = len(screens)
    if count == 0 or count > SCREEN_MAX:
        errors.add("data", "screen count %d outside 1..%d" % (count, SCREEN_MAX))
        return None

    # Pass 1: rows, each screen's firstRow relative to the row section start.
    row_bytes = bytearray()
    for screen in screens:
        screen["firstRowRel"] = len(row_bytes)
        for row in screen["rows"]:
            label = row["label"].encode("ascii")
            row_bytes += bytes([len(label)]) + label
            row_bytes += struct.pack("<HBBBB", row["cost"], row["action"], row["flags"],
                                     row["cond"], row["param"])

    defs_len = sum(5 + len(screen["title"]) for screen in screens)
    rows_start = HEADER_SIZE + 2 * count + defs_len

    # Pass 2: defs with absolute firstRow offsets, then the defOff table.
    def_off = HEADER_SIZE + 2 * count
    def_offsets = []
    def_bytes = bytearray()
    for screen in screens:
        def_offsets.append(def_off)
        title = screen["title"].encode("ascii")
        record = bytes([screen["id"], len(title)]) + title + bytes([len(screen["rows"])])
        record += struct.pack("<H", rows_start + screen["firstRowRel"])
        def_bytes += record
        def_off += len(record)

    blob = bytearray(struct.pack("<HBBBBH", MAGIC, VERSION, FLAGS, count, 0,
                                 sum(len(screen["rows"]) for screen in screens)))
    for off in def_offsets:
        blob += struct.pack("<H", off)
    blob += def_bytes
    blob += row_bytes
    # Page table (hbk.2): per screen, u8 pageCount + pageCount x u24 absolute FX
    # addresses of the baked page layer arrays (0 = not prebaked, or unresolved
    # on a first generation pass before fxdata-build.py writes fxdata.h).
    page_table_off = len(blob)
    for screen in screens:
        blob += bytes([len(screen["pages"])])
        for page, _img in screen["pages"]:
            symbol = page_symbol(screen, page)
            value = fx_symbols.get(symbol)
            if value is None:
                unresolved.append(symbol)
                value = 0
            blob += struct.pack("<I", value)[:3]
    if len(blob) >= 65536:
        errors.add("data", "size limit: blob is %d B, offsets are u16" % len(blob))
        return None
    return {"blob": bytes(blob), "def_offsets": def_offsets, "rows_start": rows_start,
            "page_table_off": page_table_off}


def emit_meta_header(model, packed):
    screens = model["screens"]
    counts = [len(screen["rows"]) for screen in screens]
    lines = []
    app = lines.append
    app("#pragma once")
    app("// Generated by tools/gen-screens.py -- do not edit.")
    app("//")
    app("// Screen data ABI (docs/quests-shops.md): header, a u16 ScreenDef offset")
    app("// per screen index, the variable ScreenDef records then the variable")
    app("// ScreenRow records. src/screens.hpp reads this blob through")
    app("// core/fxmem.hpp during the scan/render window.")
    app("")
    app("#include <stdint.h>")
    app("")
    app("namespace screens {")
    app("")
    app("constexpr uint16_t MAGIC = 0x%04X;" % MAGIC)
    app("constexpr uint8_t VERSION = %d;" % VERSION)
    app("constexpr uint8_t FLAGS = 0x%02X;" % FLAGS)
    app("constexpr uint16_t SIZE = %d;" % len(packed["blob"]))
    app("constexpr uint8_t HEADER_SIZE = %d;" % HEADER_SIZE)
    app("constexpr uint8_t DEF_OFF_OFF = %d;" % DEF_OFF_OFF)
    app("constexpr uint16_t ROWS_OFF = %d;" % packed["rows_start"])
    app("constexpr uint8_t SCREEN_COUNT = %d;" % len(screens))
    app("constexpr uint16_t ROW_COUNT = %d;" % sum(counts))
    app("")
    app("// Prebaked screen pages (hbk.2): per screen, a u8 page count then that")
    app("// many u24 absolute FX addresses of the mh_screen_<name>_<page> layer")
    app("// arrays. pageCount == 0 = the screen renders through the legacy text")
    app("// path; src/screens.hpp walks the table from PAGE_TABLE_OFF.")
    app("constexpr uint16_t PAGE_TABLE_OFF = %d;" % packed["page_table_off"])
    app("constexpr uint8_t SCREEN_PAGE_MAX = %d;" % PAGE_MAX)
    app("")
    app("// Row action ids; src/screen_state.hpp switches on these.")
    for i, name in enumerate(ACTION_NAMES):
        app("constexpr uint8_t ACTION_%s = %d;" % (name.upper(), i))
    app("")
    app("// Row condition ids; 0 = always, else the save query in screenCondOk().")
    for i, name in enumerate(COND_NAMES):
        app("constexpr uint8_t COND_%s = %d;" % (name.upper(), i))
    app("")
    app("// ScreenRow flags.")
    for name in sorted(ROW_FLAGS):
        app("constexpr uint8_t ROW_F_%s = 0x%02X;" % (name.upper(), ROW_FLAGS[name]))
    app("")
    app("// Screen indices, sorted by id, with the cart offsets the runtime uses.")
    page_off = packed["page_table_off"]
    for i, screen in enumerate(screens):
        name = screen["name"].upper()
        app("constexpr uint8_t SCREEN_%s = %d;" % (name, i))
        app("constexpr uint16_t SCREEN_%s_OFF = %d;" % (name, packed["def_offsets"][i]))
        app("constexpr uint8_t SCREEN_%s_ROWS = %d;" % (name, len(screen["rows"])))
        app("constexpr uint8_t SCREEN_%s_TITLE_LEN = %d;" % (name, len(screen["title"])))
        app("constexpr uint16_t SCREEN_%s_FIRST_ROW = %d;"
            % (name, packed["rows_start"] + screen["firstRowRel"]))
        app("constexpr uint8_t SCREEN_%s_PAGES = %d;" % (name, len(screen["pages"])))
        app("constexpr uint16_t SCREEN_%s_PAGE_TABLE = %d;" % (name, page_off))
        page_off += 1 + 3 * len(screen["pages"])
    app("")
    app("}   // namespace screens")
    app("")
    return "\n".join(lines)


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


def render_sheet(screens):
    """Compose every baked page into one review grid (never committed)."""
    cols = 4
    label_h = 10
    pages = []
    for screen in screens:
        for page, img in screen["pages"]:
            pages.append((page_symbol(screen, page), img))
    rows = (len(pages) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * PAGE_W, max(1, rows) * (PAGE_H + label_h)), (0, 0, 0))
    for i, (symbol, img) in enumerate(pages):
        cx = (i % cols) * PAGE_W
        cy = (i // cols) * (PAGE_H + label_h)
        sheet.paste(img.convert("RGB"), (cx, cy))
        draw_text(sheet, cx + 2, cy + PAGE_H + 1, symbol.replace("mh_screen_", ""), WHITE)
    return sheet


def run(root, dump, sheet=None):
    errors = Errors()
    model = compile_model(errors, root)
    if model is None or errors.items:
        for item in errors.items:
            print("gen-screens: error: %s" % item, file=sys.stderr)
        print("gen-screens: FAIL (%d error%s)"
              % (len(errors.items), "" if len(errors.items) == 1 else "s"), file=sys.stderr)
        return 1
    unresolved = []
    packed = pack_blob(errors, model["screens"], load_fxdata_symbols(root), unresolved)
    if packed is None or errors.items:
        for item in errors.items:
            print("gen-screens: error: %s" % item, file=sys.stderr)
        print("gen-screens: FAIL", file=sys.stderr)
        return 1
    screens = model["screens"]
    page_total = sum(len(s["pages"]) for s in screens)

    if dump:
        for screen in screens:
            print("screen %s: id %d title %r rows %d off %d pages %d"
                  % (screen["name"], screen["id"], screen["title"], len(screen["rows"]),
                     packed["def_offsets"][screens.index(screen)], len(screen["pages"])))
            for page, _img in screen["pages"]:
                print("    page %d -> %s" % (page, page_symbol(screen, page)))
            for row in screen["rows"]:
                print("    row %r cost %d action %s flags 0x%02X cond %s param %d"
                      % (row["label"], row["cost"], ACTION_NAMES[row["action"]],
                         row["flags"], COND_NAMES[row["cond"]], row["param"]))
        print("gen-screens: %d screens, %d rows, %d pages, %d B blob"
              % (len(screens), sum(len(s["rows"]) for s in screens), page_total, len(packed["blob"])))
        if sheet:
            path = sheet if os.path.isabs(sheet) else os.path.join(root, sheet)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            render_sheet(screens).save(path, format="PNG")
            print("gen-screens: contact sheet %s" % path)
        return 0

    wrote = set()
    # Author the page PNGs first (gen-cards pattern: the screen art is fully
    # generated, so it is always rewritten; stale pages are removed).
    for screen in screens:
        for page, img in screen["pages"]:
            rel = page_image_rel(screen, page)
            path = os.path.join(root, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            img.save(path, format="PNG")
            wrote.add(rel)
    clean_stale_images(screens, root)

    if write_if_changed(os.path.join(root, SPRITES_REL), emit_page_sprites(screens)):
        wrote.add(SPRITES_REL)
    if write_if_changed(os.path.join(root, BLOB_REL), packed["blob"]):
        wrote.add(BLOB_REL)
    if write_if_changed(os.path.join(root, META_REL), emit_meta_header(model, packed)):
        wrote.add(META_REL)
    print("gen-screens: %d screens, %d rows, %d pages, %d B blob (magic 0x%04X version %d)"
          % (len(screens), sum(len(s["rows"]) for s in screens), page_total,
             len(packed["blob"]), MAGIC, VERSION))
    for rel in (SPRITES_REL, BLOB_REL, META_REL):
        print("gen-screens: %s%s" % (rel, "" if rel in wrote else " (unchanged)"))
    if unresolved:
        print("gen-screens: %d page symbols unresolved on this pass (first gen before fxdata-build): %s"
              % (len(unresolved), ", ".join(sorted(set(unresolved))[:3])))
    if sheet:
        path = sheet if os.path.isabs(sheet) else os.path.join(root, sheet)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        render_sheet(screens).save(path, format="PNG")
        print("gen-screens: contact sheet %s" % path)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=None, help="pipeline root holding data/ (default: repository root)")
    parser.add_argument("--dump", action="store_true", help="validate + list the screens; write nothing")
    parser.add_argument("--sheet", default=None,
                        help="also render every baked page to this review PNG (e.g. build/screens_contact_sheet.png; never committed)")
    args = parser.parse_args(argv)
    root = os.path.abspath(args.root) if args.root else REPO_ROOT
    return run(root, args.dump, args.sheet)


if __name__ == "__main__":
    sys.exit(main())
