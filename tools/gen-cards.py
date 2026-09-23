#!/usr/bin/env python3
"""Bake the prebaked detail-card pipeline (bead monhun-ardu-5co.3, docs/ui-design.md).

    data/armor.json + data/skills.json + data/items.json + data/quests/*.json
        -> images/cards/mh_card_<kind>_<name>_<page>_128x64.png   (authored art)
        -> fxdata/cards/Sprites.txt   (3x 1bpp page-major layer arrays)
        -> fxdata/tables/cards.bin    (packed card ABI; build intermediate)
        -> src/generated/card_meta.hpp (constants, item indices, page symbols)

One 128x64 image per item page. Armor pages are DESC / PARTS / STATS / SKILL;
quest pages are GOAL / PROG / REWARD. A page with no data is not generated (a
zenny-only armor piece has no PARTS page; a quest with no reward has no REWARD
page). The image format is exactly the fie.5 room-image layer family: three
1 bpp page-major layers (plane p at p*1024), so src/cards.hpp reuses the
room-image streaming copy reader for the blit.

Everything static is baked: title, body copy, required counts, stats, skills,
costs. Only the dynamic bits are overlay slots (live have-counts on PARTS, the
quest progress bar + number) and the device-drawn hint line.

Text is drawn from the same GLYPHS table tools/gen-art.py authors the FX font
sheets from, so card copy matches the list screens glyph-for-glyph.

Usage:
    python3 tools/gen-cards.py [--root DIR] [--dump]

    --root DIR  pipeline root holding data/, images/ and src/generated (default: repo)
    --dump      validate + list the compiled pages on stdout; writes nothing
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

SKILLS_REL = "data/skills.json"
ARMOR_REL = "data/armor.json"
ITEMS_REL = "data/items.json"
QUESTS_DIR_REL = "data/quests"
BLOB_REL = "fxdata/tables/cards.bin"
META_HPP_REL = "src/generated/card_meta.hpp"
SPRITES_REL = "fxdata/cards/Sprites.txt"
IMAGE_DIR_REL = "images/cards"
FX_HEADER_REL = "fxdata/fxdata.h"

MAGIC = 0x4341   # 'A','C' little-endian (card)
VERSION = 1
FLAGS = 0
HEADER_SIZE = 8
ITEM_SIZE = 27   # kind, pageMask, overlayCount + 4 u24 pages + 2x6 overlay
PAGE_MAX = 4
OVERLAY_MAX = 2
ITEM_MAX = 16

# Card kinds (packed record byte 0).
KIND_ARMOR = 0
KIND_QUEST = 1

# Page ids. Armor uses DESC..SKILL, quests GOAL..REWARD; the ids share the
# 0..PAGE_MAX-1 space so one mask works for both.
PAGE_DESC = 0
PAGE_PARTS = 1
PAGE_STATS = 2
PAGE_SKILL = 3
PAGE_GOAL = 0
PAGE_PROG = 1
PAGE_REWARD = 2

# Overlay kinds (packed record overlay byte 0).
OVERLAY_HAVE = 0    # live save.items[arg0] count
OVERLAY_PROG = 1    # progress bar fill = save.progress * arg1 / arg0
OVERLAY_PROGNUM = 2  # live save.progress number

# Armor slot labels (mirror armor::SLOT_*).
SLOTS = ("head", "body", "charm")
# Kill targets (mirror quests::TARGET_*).
TARGET_NAMES = ("lunge", "sweep", "heavy", "ravager")
GOAL_KILL = 0

CARD_W = 128
CARD_H = 64
LAYERS = 3
LAYER_BYTES = CARD_W * (CARD_H // 8)   # 1024
CARD_BYTES = LAYER_BYTES * LAYERS      # 3072

# 4-shade palette (1:1 with L4_Triplane; same values as gen-zones/gen-art).
CLEAR = (0, 0, 0, 0)
BLACK = (0, 0, 0, 255)
DARK = (85, 85, 85, 255)
LIGHT = (170, 170, 170, 255)
WHITE = (255, 255, 255, 255)
SHADE_STEP = (254 + 4) // 4   # 64, mirrors convert-sprite/gen-zones

# Page layout (4x8 glyph lane, 4 px advance).
TITLE_Y = 0
RULE_Y = 9
BODY_Y = 13
LINE_H = 8
HINT_Y = 56
RIGHT = 124

NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")

_MISSING = object()


def load_glyphs():
    """The GLYPHS table from tools/gen-art.py (single source for the 4x8 lane)."""
    spec = importlib.util.spec_from_file_location("gen_art", os.path.join(HERE, "gen-art.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.GLYPHS


GLYPHS = load_glyphs()


class Errors:
    def __init__(self):
        self.items = []

    def add(self, ctx, message):
        self.items.append("%s: %s" % (ctx, message))


def load_json(errors, path, rel):
    try:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    except OSError as exc:
        errors.add(rel, "cannot read file: %s" % exc)
    except ValueError as exc:
        errors.add(rel, "invalid JSON: %s" % exc)
    return None


def read_item_ids(errors, root):
    doc = load_json(errors, os.path.join(root, ITEMS_REL), ITEMS_REL)
    if doc is None:
        return None
    raw = doc.get("items") if isinstance(doc, dict) else None
    if not isinstance(raw, list) or not raw:
        errors.add(ITEMS_REL, "items: expected a non-empty array")
        return None
    ids = []
    for i, obj in enumerate(raw):
        name = obj.get("id") if isinstance(obj, dict) else None
        if not isinstance(name, str) or not NAME_RE.match(name):
            errors.add("%s: items[%d]" % (ITEMS_REL, i), "id: expected a [a-z][a-z0-9_]* id")
            return None
        ids.append(name)
    return ids


def read_skills(errors, root):
    """id -> abbr from data/skills.json (validated by gen-armor)."""
    doc = load_json(errors, os.path.join(root, SKILLS_REL), SKILLS_REL)
    if doc is None:
        return None
    raw = doc.get("skills") if isinstance(doc, dict) else None
    if not isinstance(raw, list) or not raw:
        errors.add(SKILLS_REL, "skills: expected a non-empty array")
        return None
    abbrs = {}
    for i, obj in enumerate(raw):
        if not isinstance(obj, dict):
            errors.add("%s: skills[%d]" % (SKILLS_REL, i), "expected an object")
            return None
        abbrs[obj.get("id")] = obj.get("abbr", "")
    return abbrs


def read_armor(errors, root, item_ids):
    doc = load_json(errors, os.path.join(root, ARMOR_REL), ARMOR_REL)
    if doc is None:
        return None
    raw = doc.get("pieces") if isinstance(doc, dict) else None
    if not isinstance(raw, list) or not raw:
        errors.add(ARMOR_REL, "pieces: expected a non-empty array")
        return None
    pieces = []
    for i, obj in enumerate(raw):
        if not isinstance(obj, dict):
            errors.add("%s: pieces[%d]" % (ARMOR_REL, i), "expected an object")
            return None
        recipe = obj.get("recipe") or {}
        mats = []
        for entry in recipe.get("materials", []):
            item = entry.get("item") if isinstance(entry, dict) else None
            if item not in item_ids:
                errors.add("%s: pieces[%d].recipe" % (ARMOR_REL, i), "unknown material %r" % (item,))
                return None
            mats.append({"item": item_ids.index(item), "name": item.replace("_", " ").upper(),
                         "count": int(entry["count"])})
        pieces.append({
            "id": obj["id"],
            "slot": SLOTS.index(obj["slot"]) if obj.get("slot") in SLOTS else 0,
            "defense": int(obj.get("defense", 0)),
            "resist": obj.get("resist", {}),
            "desc": obj.get("desc", []),
            "skills": [{"id": e["id"], "points": int(e["points"])} for e in obj.get("skills", [])],
            "materials": mats,
            "zenny": int(recipe.get("zenny", 0)),
        })
    return pieces


def read_quests(errors, root, item_ids):
    directory = os.path.join(root, QUESTS_DIR_REL)
    if not os.path.isdir(directory):
        errors.add(QUESTS_DIR_REL, "missing quests directory")
        return None
    quests = []
    for name in sorted(n for n in os.listdir(directory) if n.endswith(".json")):
        rel = "%s/%s" % (QUESTS_DIR_REL, name)
        obj = load_json(errors, os.path.join(directory, name), rel)
        if obj is None:
            continue
        reward_item = None
        if obj.get("rewardItem") is not None:
            rid = obj["rewardItem"]
            if rid not in item_ids:
                errors.add(rel, "rewardItem: unknown item %r" % (rid,))
                return None
            reward_item = {"index": item_ids.index(rid), "name": rid.replace("_", " ").upper(),
                           "count": int(obj.get("rewardCount", 1))}
        target = obj.get("target")
        if obj.get("goalKind") == "gather" and target in item_ids:
            goal_name = target.replace("_", " ").upper()
        else:
            goal_name = target.upper() if isinstance(target, str) else "?"
        quests.append({
            "id": int(obj["id"]),
            "name": os.path.splitext(name)[0],
            "goalKind": 0 if obj.get("goalKind") == "kill" else 1,
            "need": int(obj.get("need", 1)),
            "goalName": goal_name,
            "desc": obj.get("desc", []),
            "rewardZenny": int(obj.get("rewardZenny", 0)),
            "rewardItem": reward_item,
        })
    if not quests:
        errors.add(QUESTS_DIR_REL, "no quest JSON files found")
        return None
    quests.sort(key=lambda q: (q["id"], q["name"]))
    # The runtime card index is the quest id (src/card_state.hpp cardRowIndex:
    # the row param packs (need << 4) | quest id), and the card table stores the
    # quests in sorted order. That only lines up when the ids are dense 0..N-1.
    for rank, quest in enumerate(quests):
        if quest["id"] != rank:
            errors.add(QUESTS_DIR_REL, "quest ids must be dense 0..N-1 (the card "
                       "index is the quest id): %s has id %d at rank %d"
                       % (quest["name"], quest["id"], rank))
    return quests


# ------------------------------------------------------------------- drawing


def new_card():
    return Image.new("RGBA", (CARD_W, CARD_H), CLEAR)


def draw_text(img, x, y, text, color):
    """Plot `text` on the 4 px glyph lane (3 px glyph + 1 px gap, rows 0..4)."""
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
                    if 0 <= xx < CARD_W and 0 <= yy < CARD_H:
                        px[xx, yy] = color


def text_w(text):
    return len(text) * 4


def draw_rule(img):
    px = img.load()
    for x in range(CARD_W):
        px[x, RULE_Y] = LIGHT


def draw_title(img, name, tag):
    draw_text(img, 2, TITLE_Y, name, WHITE)
    draw_text(img, RIGHT - text_w(tag), TITLE_Y, tag, LIGHT)
    draw_rule(img)


def draw_body(img, lines):
    y = BODY_Y
    for line in lines:
        draw_text(img, 4, y, line, LIGHT)
        y += LINE_H


def signed(v):
    return "%+d" % v if v != 0 else "0"


def draw_have_line(img, y, label, need, overlay_x):
    draw_text(img, 4, y, label, LIGHT)
    draw_text(img, 4 + text_w(label) + 8, y, "NEED %d" % need, LIGHT)
    draw_text(img, overlay_x - 20, y, "HAVE", LIGHT)


def armor_pages(piece, abbrs):
    """Return (pages, overlays) for one armor piece. pages = [(id, PIL image)]."""
    name = piece["id"].replace("_", " ").upper()
    pages = []
    overlays = []

    img = new_card()
    draw_title(img, name, "DESC")
    draw_body(img, piece["desc"] if piece["desc"] else [name])
    pages.append((PAGE_DESC, img))

    if piece["materials"] or piece["zenny"] > 0:
        img = new_card()
        draw_title(img, name, "PARTS")
        y = BODY_Y
        for i, mat in enumerate(piece["materials"]):
            draw_have_line(img, y, mat["name"], mat["count"], 112)
            overlays.append({"kind": OVERLAY_HAVE, "page": PAGE_PARTS, "x": 112, "y": y,
                             "arg0": mat["item"], "arg1": 0})
            y += LINE_H
        if piece["zenny"] > 0:
            draw_text(img, 4, y, "ZENNY %d" % piece["zenny"], LIGHT)
        pages.append((PAGE_PARTS, img))

    img = new_card()
    draw_title(img, name, "STATS")
    r = piece["resist"]
    lines = ["DEF %d" % piece["defense"],
             "FIRE %s  WATER %s" % (signed(r.get("fire", 0)), signed(r.get("water", 0))),
             "ICE %s  THUNDER %s" % (signed(r.get("ice", 0)), signed(r.get("thunder", 0)))]
    draw_body(img, lines)
    pages.append((PAGE_STATS, img))

    if piece["skills"]:
        img = new_card()
        draw_title(img, name, "SKILL")
        lines = []
        for entry in piece["skills"]:
            abbr = abbrs.get(entry["id"]) or entry["id"].replace("_", " ").upper()[:4]
            lines.append("%s %d" % (abbr, entry["points"]))
        draw_body(img, lines)
        pages.append((PAGE_SKILL, img))

    return pages, overlays


def quest_pages(quest):
    name = quest["name"].replace("_", " ").upper()
    pages = []
    overlays = []

    img = new_card()
    draw_title(img, name, "GOAL")
    lines = list(quest["desc"])
    if quest["goalKind"] == GOAL_KILL:
        lines.append("SLAY %d %s" % (quest["need"], quest["goalName"]))
    else:
        lines.append("GATHER %d %s" % (quest["need"], quest["goalName"]))
    draw_body(img, lines)
    pages.append((PAGE_GOAL, img))

    if quest["need"] > 0:
        img = new_card()
        draw_title(img, name, "PROG")
        draw_text(img, 4, BODY_Y, "TARGET %d" % quest["need"], LIGHT)
        # Bar outline at (8, 27, 112, 6); the device fills it live.
        px = img.load()
        for x in range(8, 120):
            px[x, 27] = LIGHT
            px[x, 32] = LIGHT
        for y in range(27, 33):
            px[8, y] = LIGHT
            px[119, y] = LIGHT
        draw_text(img, 4, 42, "HAVE", LIGHT)
        overlays.append({"kind": OVERLAY_PROG, "page": PAGE_PROG, "x": 8, "y": 27,
                         "arg0": quest["need"], "arg1": 112})
        pages.append((PAGE_PROG, img))

    if quest["rewardZenny"] > 0 or quest["rewardItem"] is not None:
        img = new_card()
        draw_title(img, name, "REWARD")
        lines = []
        if quest["rewardZenny"] > 0:
            lines.append("ZENNY %d" % quest["rewardZenny"])
        if quest["rewardItem"] is not None:
            lines.append("%s X %d" % (quest["rewardItem"]["name"], quest["rewardItem"]["count"]))
        draw_body(img, lines)
        pages.append((PAGE_REWARD, img))

    return pages, overlays


# ------------------------------------------------------------------ pipeline


def compile_model(errors, root):
    item_ids = read_item_ids(errors, root)
    if item_ids is None:
        return None
    abbrs = read_skills(errors, root)
    if abbrs is None:
        return None
    pieces = read_armor(errors, root, item_ids)
    if pieces is None:
        return None
    quests = read_quests(errors, root, item_ids)
    if quests is None:
        return None

    items = []
    for i, piece in enumerate(pieces):
        pages, overlays = armor_pages(piece, abbrs)
        mask = 0
        for page_id, _img in pages:
            mask |= 1 << page_id
        items.append({"kind": KIND_ARMOR, "index": i, "name": piece["id"],
                      "pages": pages, "overlays": overlays, "mask": mask})
    for i, quest in enumerate(quests):
        pages, overlays = quest_pages(quest)
        mask = 0
        for page_id, _img in pages:
            mask |= 1 << page_id
        items.append({"kind": KIND_QUEST, "index": i, "name": quest["name"],
                      "pages": pages, "overlays": overlays, "mask": mask})

    if len(items) > ITEM_MAX:
        errors.add("data", "size limit: %d card items exceed the %d item cap" % (len(items), ITEM_MAX))
    for item in items:
        if len(item["overlays"]) > OVERLAY_MAX:
            errors.add("data", "%s: %d overlays exceed the %d slot cap"
                       % (item["name"], len(item["overlays"]), OVERLAY_MAX))
    if errors.items:
        return None
    return items


def page_symbol(item, page_id):
    return "mh_card_%s_%s_%d" % ("armor" if item["kind"] == KIND_ARMOR else "quest",
                                 item["name"], page_id)


def page_image_rel(item, page_id):
    return "%s/%s_%dx%d.png" % (IMAGE_DIR_REL, page_symbol(item, page_id), CARD_W, CARD_H)


def shade_on(red, layer):
    """True when a pixel lights layer `layer`; mirrors convert-sprite SHADES=4."""
    return 1 if red >= (layer + 1) * SHADE_STEP else 0


def card_layers(img):
    """128x64 RGBA -> 3x 1bpp page-major layers (plane p at p*LAYER_BYTES)."""
    out = bytearray(CARD_BYTES)
    px = img.load()
    for y in range(CARD_H):
        base = (y // 8) * CARD_W
        bit = 1 << (y & 7)
        for x in range(CARD_W):
            r, g, b, a = px[x, y]
            if a < 128:
                continue
            for p in range(LAYERS):
                if shade_on(r, p):
                    out[p * LAYER_BYTES + base + x] |= bit
    return bytes(out)


def load_fxdata_symbols(root):
    path = os.path.join(root, FX_HEADER_REL)
    try:
        with open(path, encoding="utf-8") as handle:
            return {name: int(value, 0) for name, value in
                    re.findall(r"constexpr\s+uint24_t\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(0[xX][0-9A-Fa-f]+|\d+)",
                               handle.read())}
    except OSError:
        return {}


def pack_blob(items, fx_symbols):
    blob = bytearray(struct.pack("<HBBBBH", MAGIC, VERSION, FLAGS, len(items), 0, 0))
    unresolved = []
    for item in items:
        by_page = {page_id: img for page_id, img in item["pages"]}
        blob += struct.pack("<BBB", item["kind"], item["mask"], len(item["overlays"]))
        for page_id in range(PAGE_MAX):
            if page_id in by_page:
                symbol = page_symbol(item, page_id)
                value = fx_symbols.get(symbol)
                if value is None:
                    unresolved.append(symbol)
                    value = 0
                blob += struct.pack("<I", value)[:3]
            else:
                blob += b"\x00\x00\x00"
        for i in range(OVERLAY_MAX):
            if i < len(item["overlays"]):
                ov = item["overlays"][i]
                blob += struct.pack("<BBBBBB", ov["kind"], ov["page"], ov["x"], ov["y"], ov["arg0"], ov["arg1"])
            else:
                blob += b"\x00" * 6
    expected = HEADER_SIZE + ITEM_SIZE * len(items)
    assert len(blob) == expected, (len(blob), expected)
    return bytes(blob), unresolved


def emit_sprites(items):
    lines = ["// Generated by tools/gen-cards.py -- do not edit.",
             "// Detail-card layer arrays (3x 1bpp page-major, 128x64); see docs/ui-design.md."]
    for item in items:
        for page_id, img in item["pages"]:
            data = card_layers(img)
            lines.append("uint8_t %s[] =" % page_symbol(item, page_id))
            lines.append("{")
            for i in range(0, len(data), 16):
                lines.append("    " + ", ".join(str(b) for b in data[i:i + 16]) + ",")
            lines.append("};")
    lines.append("")
    return "\n".join(lines)


def emit_meta_header(items, fx_symbols):
    lines = []
    app = lines.append
    quest_base = sum(1 for item in items if item["kind"] == KIND_ARMOR)
    app("#pragma once")
    app("// Generated by tools/gen-cards.py -- do not edit.")
    app("//")
    app("// Prebaked detail-card ABI (bead monhun-ardu-5co.3, docs/ui-design.md):")
    app("// 8 B header + one fixed 27 B item record, little-endian, no padding.")
    app("// Offsets are absolute byte offsets into the mhCards raw_t section")
    app("// (fxdata/fxdata.txt); on AVR the loader reads mhCards + off through")
    app("// core/fxmem.hpp (src/cards.hpp). Page image symbols are the FX-image")
    app("// addresses the blit seeks; a page absent from the mask has offset 0.")
    app("")
    app("#include <stdint.h>")
    app("")
    app("namespace cards {")
    app("")
    app("constexpr uint16_t MAGIC = 0x%04X;" % MAGIC)
    app("constexpr uint8_t VERSION = %d;" % VERSION)
    app("constexpr uint8_t FLAGS = 0x%02X;" % FLAGS)
    app("constexpr uint16_t SIZE = %d;" % (HEADER_SIZE + ITEM_SIZE * len(items)))
    app("constexpr uint8_t HEADER_SIZE = %d;" % HEADER_SIZE)
    app("constexpr uint8_t ITEM_SIZE = %d;" % ITEM_SIZE)
    app("constexpr uint8_t ITEM_COUNT = %d;" % len(items))
    app("constexpr uint8_t PAGE_MAX = %d;" % PAGE_MAX)
    app("constexpr uint8_t OVERLAY_MAX = %d;" % OVERLAY_MAX)
    app("")
    app("// Card kinds.")
    app("constexpr uint8_t KIND_ARMOR = %d;" % KIND_ARMOR)
    app("constexpr uint8_t KIND_QUEST = %d;" % KIND_QUEST)
    app("")
    app("// Page ids (armor DESC..SKILL, quest GOAL..REWARD; shared mask space).")
    app("constexpr uint8_t PAGE_DESC = %d;" % PAGE_DESC)
    app("constexpr uint8_t PAGE_PARTS = %d;" % PAGE_PARTS)
    app("constexpr uint8_t PAGE_STATS = %d;" % PAGE_STATS)
    app("constexpr uint8_t PAGE_SKILL = %d;" % PAGE_SKILL)
    app("constexpr uint8_t PAGE_GOAL = %d;" % PAGE_GOAL)
    app("constexpr uint8_t PAGE_PROG = %d;" % PAGE_PROG)
    app("constexpr uint8_t PAGE_REWARD = %d;" % PAGE_REWARD)
    app("")
    app("// Overlay kinds.")
    app("constexpr uint8_t OVERLAY_HAVE = %d;" % OVERLAY_HAVE)
    app("constexpr uint8_t OVERLAY_PROG = %d;" % OVERLAY_PROG)
    app("constexpr uint8_t OVERLAY_PROGNUM = %d;" % OVERLAY_PROGNUM)
    app("")
    app("// Item record field offsets.")
    app("constexpr uint8_t ITEM_KIND_OFF = 0;")
    app("constexpr uint8_t ITEM_MASK_OFF = 1;")
    app("constexpr uint8_t ITEM_OVERLAY_COUNT_OFF = 2;")
    app("constexpr uint8_t ITEM_PAGES_OFF = 3;      // PAGE_MAX x u24 (0 = absent)")
    app("constexpr uint8_t ITEM_PAGE_STRIDE = 3;")
    app("constexpr uint8_t ITEM_OVERLAYS_OFF = 15;  // OVERLAY_MAX x 6 B")
    app("constexpr uint8_t ITEM_OVERLAY_STRIDE = 6;")
    app("constexpr uint8_t OVERLAY_KIND_OFF = 0;")
    app("constexpr uint8_t OVERLAY_PAGE_OFF = 1;")
    app("constexpr uint8_t OVERLAY_X_OFF = 2;")
    app("constexpr uint8_t OVERLAY_Y_OFF = 3;")
    app("constexpr uint8_t OVERLAY_ARG0_OFF = 4;")
    app("constexpr uint8_t OVERLAY_ARG1_OFF = 5;")
    app("")
    app("// Item indices + record offsets (armor in data/armor.json order, then")
    app("// quests sorted by (id, name) -- the generated quest_meta order).")
    app("constexpr uint8_t QUEST_BASE = %d;   // first quest card index" % quest_base)
    for i, item in enumerate(items):
        tag = item["name"].upper()
        prefix = "CARD_ARMOR_" if item["kind"] == KIND_ARMOR else "CARD_QUEST_"
        app("constexpr uint8_t %s%s = %d;" % (prefix, tag, i))
        app("constexpr uint16_t %s%s_OFF = %d;" % (prefix, tag, HEADER_SIZE + i * ITEM_SIZE))
    app("")
    app("// Page image symbols live in fxdata/fxdata.h (mh_card_<kind>_<name>_<page>);")
    app("// the runtime seeks them from the record's baked u24 offsets, so no")
    app("// string table is emitted here (it would cost flash and is test-only).")
    app("")
    app("}   // namespace cards")
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


def clean_stale_images(items, root):
    directory = os.path.join(root, IMAGE_DIR_REL)
    if not os.path.isdir(directory):
        return
    expected = set()
    for item in items:
        for page_id, _img in item["pages"]:
            expected.add(os.path.basename(page_image_rel(item, page_id)))
    for name in sorted(os.listdir(directory)):
        if name in expected or not name.endswith(".png"):
            continue
        os.remove(os.path.join(directory, name))
        print("gen-cards: removed stale %s/%s" % (IMAGE_DIR_REL, name))


def dump_model(items):
    for item in items:
        pages = " ".join(str(page_id) for page_id, _img in item["pages"])
        overlays = " ".join("%d@%d(%d,%d)" % (ov["kind"], ov["page"], ov["x"], ov["y"])
                            for ov in item["overlays"])
        print("card %s %s: mask 0x%02X pages [%s] overlays [%s]"
              % ("armor" if item["kind"] == KIND_ARMOR else "quest", item["name"],
                 item["mask"], pages, overlays))
    print("gen-cards: %d card items" % len(items))


# --------------------------------------------------------------- review sheet


SHEET_COLS = 4
SHEET_LABEL_H = 10


def render_sheet(items):
    """Compose every compiled page into one review grid (never committed).

    Rows of SHEET_COLS pages, each captioned with its FX symbol (minus the
    mh_card_ prefix) using the same glyph lane the cards use, so the sheet is
    deterministic and font-dependency-free (tools/contact_sheet.py pattern).
    """
    pages = []
    for item in items:
        for page_id, img in item["pages"]:
            pages.append((page_symbol(item, page_id), img))
    rows = (len(pages) + SHEET_COLS - 1) // SHEET_COLS
    sheet = Image.new("RGB", (SHEET_COLS * CARD_W, rows * (CARD_H + SHEET_LABEL_H)), (0, 0, 0))
    for i, (symbol, img) in enumerate(pages):
        cx = (i % SHEET_COLS) * CARD_W
        cy = (i // SHEET_COLS) * (CARD_H + SHEET_LABEL_H)
        sheet.paste(img.convert("RGB"), (cx, cy))
        draw_text(sheet, cx + 2, cy + CARD_H + 1, symbol.replace("mh_card_", ""), WHITE)
    return sheet


def run(root, dump, sheet=None):
    errors = Errors()
    items = compile_model(errors, root)
    if items is None or errors.items:
        for item in errors.items:
            print("gen-cards: error: %s" % item, file=sys.stderr)
        print("gen-cards: FAIL (%d error%s)"
              % (len(errors.items), "" if len(errors.items) == 1 else "s"), file=sys.stderr)
        return 1

    if dump:
        dump_model(items)
        return 0

    wrote = set()
    # Author the PNGs first (gen-zones-style: an existing refined PNG survives).
    for item in items:
        for page_id, img in item["pages"]:
            rel = page_image_rel(item, page_id)
            path = os.path.join(root, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            # Always rewrite: the card art is fully generated, not hand-refined.
            img.save(path, format="PNG")
            wrote.add(rel)
    clean_stale_images(items, root)

    fx_symbols = load_fxdata_symbols(root)
    blob, unresolved = pack_blob(items, fx_symbols)

    if write_if_changed(os.path.join(root, SPRITES_REL), emit_sprites(items)):
        wrote.add(SPRITES_REL)
    if write_if_changed(os.path.join(root, BLOB_REL), blob):
        wrote.add(BLOB_REL)
    if write_if_changed(os.path.join(root, META_HPP_REL), emit_meta_header(items, fx_symbols)):
        wrote.add(META_HPP_REL)

    page_total = sum(len(item["pages"]) for item in items)
    print("gen-cards: %d items, %d pages, %d B blob (magic 0x%04X version %d)"
          % (len(items), page_total, len(blob), MAGIC, VERSION))
    for rel in (SPRITES_REL, BLOB_REL, META_HPP_REL):
        print("gen-cards: %s%s" % (rel, "" if rel in wrote else " (unchanged)"))
    if unresolved:
        print("gen-cards: %d page symbols unresolved on this pass (first gen before fxdata-build): %s"
              % (len(unresolved), ", ".join(sorted(set(unresolved))[:3])))

    if sheet:
        path = sheet if os.path.isabs(sheet) else os.path.join(root, sheet)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        render_sheet(items).save(path, format="PNG")
        print("gen-cards: contact sheet %s" % path)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=None, help="pipeline root holding data/ (default: repository root)")
    parser.add_argument("--dump", action="store_true", help="validate + list the compiled pages; writes nothing")
    parser.add_argument("--sheet", default=None,
                        help="also render every page to this review PNG (e.g. build/cards_contact_sheet.png; never committed)")
    args = parser.parse_args(argv)
    root = os.path.abspath(args.root) if args.root else REPO_ROOT
    return run(root, args.dump, args.sheet)


if __name__ == "__main__":
    sys.exit(main())
