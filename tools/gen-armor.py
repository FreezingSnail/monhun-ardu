#!/usr/bin/env python3
"""Compile the armor + skill JSON into the packed FX blob + generated headers.

    data/skills.json + data/armor.json
        -> fxdata/tables/armor.bin          (packed blob; build intermediate)
        -> src/generated/armor_data.hpp     (host plain structs + arrays)
        -> src/generated/armor_meta.hpp     (ABI: VERSION/SIZE, offsets, ids)
        -> src/generated/armor_expect.hpp   (sizes, spot values, sha256)

Design: docs/equipment-framework.md ("Armor data") and bead monhun-ardu-arm.1
(epic monhun-ardu-arm). Armor is a flat list of pieces; each piece names a slot
(head/body/charm), a defense value, four elemental resists, its skill points and
its smith recipe (up to two material pairs + a zenny cost). Skills are a fixed
vocabulary with an effect kind and a per-point magnitude; the shared activation
thresholds live in the same file so the rule is data, not code.

The runtime (arm.2/arm.3) reads the blob through core/fxmem.hpp on AVR and the
host arrays off it. Nothing reads this blob yet -- the bead is data + tooling
only, so the shipping image is unchanged; the cart image grows by the blob.

Blob layout (little-endian, explicit u8/i8/u16, no padding, fixed order):

    header   8 B  magic u16 0x5241, version u8, flags u8, pieceCount u8,
                   skillCount u8, reserved u16
    piece   18 B  slot u8, defense u8, resist[4] i8 (fire,water,ice,thunder),
                   zenny u16, mat[ARMOR_MAT_SLOTS] x (itemIdx+1 u8, count u8),
                   sheet u8 (index+1, 0 = none), skillCount u8,
                   skills[ARMOR_SKILL_SLOTS] x (skillIdx+1 u8, points u8)
    skill    3 B  kind u8, maxPoints u8, perPoint u8

Records keep data source order (piece index == record index; the same for
skills). The sheet table is the sorted, de-duplicated set of declared sheets;
0 is reserved for "no sheet".

Usage:
    python3 tools/gen-armor.py [--root DIR] [--dump]

    --root DIR  pipeline root holding data/ and src/generated (default: repo)
    --dump      validate + list the compiled pieces/skills; writes nothing
"""
import argparse
import hashlib
import json
import os
import re
import struct
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKILLS_REL = "data/skills.json"
ARMOR_REL = "data/armor.json"
ITEMS_REL = "data/items.json"
BLOB_REL = "fxdata/tables/armor.bin"
DATA_HPP_REL = "src/generated/armor_data.hpp"
META_HPP_REL = "src/generated/armor_meta.hpp"
EXPECT_HPP_REL = "src/generated/armor_expect.hpp"

MAGIC = 0x5241   # 'A','R' little-endian
VERSION = 1
FLAGS = 0
HEADER_SIZE = 8
PIECE_SIZE = 18
SKILL_SIZE = 3
PIECE_MAX = 32
SKILL_MAX = 16
MAT_SLOTS = 2
SKILL_SLOTS = 2

# Piece slot indices; values mirror the packed slot byte (armor::SLOT_*).
SLOTS = ("head", "body", "charm")

# Skill effect kinds; values mirror the packed kind byte (armor::KIND_*).
KINDS = ("ATTACK_UP", "DEFENSE_UP", "HEALTH_UP", "STAMINA_UP", "EVADE_WINDOW")

# Elemental resist order; the packed i8 pair order and the meta offsets.
RESISTS = ("fire", "water", "ice", "thunder")

POINTS_MAX = 15    # per-piece skill points cap (and the maxPoints cap)
DEFENSE_MAX = 255
RESIST_MIN = -128
RESIST_MAX = 127

NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")
MAX_ID = 31

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


def read_id(errors, ctx, obj, key, seen=None):
    value = obj.get(key) if isinstance(obj, dict) else None
    if not isinstance(value, str):
        errors.add(ctx, "%s: expected a string id, got %r" % (key, value))
        return None
    if not NAME_RE.match(value):
        errors.add(ctx, "%s: id '%s' must match [a-z][a-z0-9_]*" % (key, value))
        return None
    if len(value) > MAX_ID:
        errors.add(ctx, "%s: id '%s' is longer than %d characters" % (key, value, MAX_ID))
        return None
    if seen is not None:
        if value in seen:
            errors.add(ctx, "duplicate id '%s'" % value)
        seen.add(value)
    return value


def read_enum(errors, ctx, obj, key, table):
    value = obj.get(key) if isinstance(obj, dict) else None
    if not isinstance(value, str) or value not in table:
        errors.add(ctx, "%s: unknown value %r (want one of %s)" % (key, value, ", ".join(table)))
        return None
    return table.index(value)


def load_json(errors, path, rel):
    try:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    except OSError as exc:
        errors.add(rel, "cannot read file: %s" % exc)
    except ValueError as exc:
        errors.add(rel, "invalid JSON: %s" % exc)
    return None


def load_item_ids(errors, root):
    """Ordered item ids from data/items.json (source order == item index)."""
    path = os.path.join(root, ITEMS_REL)
    if not os.path.isfile(path):
        errors.add(ITEMS_REL, "missing item file (recipe material ids resolve against it)")
        return None
    doc = load_json(errors, path, ITEMS_REL)
    if doc is None:
        return None
    raw = doc.get("items")
    if not isinstance(raw, list) or not raw:
        errors.add(ITEMS_REL, "items: expected a non-empty array")
        return None
    ids = []
    for i, obj in enumerate(raw):
        name = obj.get("id") if isinstance(obj, dict) else None
        if not isinstance(name, str) or not NAME_RE.match(name):
            errors.add("%s: items[%d].id must match [a-z][a-z0-9_]*" % (ITEMS_REL, i))
            continue
        ids.append(name)
    if errors.items:
        return None
    return ids


def normalize_skills(errors, doc):
    """Parse data/skills.json into {thresholds, skills}; skills keep source order."""
    if doc is None:
        return None
    check_keys(errors, SKILLS_REL, doc, {"version", "thresholds", "skills"})
    read_int(errors, SKILLS_REL, doc, "version", 1, 1)
    thresholds = doc.get("thresholds")
    thr = None
    if not isinstance(thresholds, dict):
        errors.add(SKILLS_REL, "thresholds: expected an object")
    else:
        check_keys(errors, SKILLS_REL + ": thresholds", thresholds, {"s", "m"})
        s = read_int(errors, SKILLS_REL + ": thresholds", thresholds, "s", 1, POINTS_MAX)
        m = read_int(errors, SKILLS_REL + ": thresholds", thresholds, "m", 1, POINTS_MAX)
        if s is not None and m is not None:
            if s >= m:
                errors.add(SKILLS_REL, "thresholds: s (%d) must be below m (%d)" % (s, m))
            else:
                thr = {"s": s, "m": m}
    raw = doc.get("skills")
    if not isinstance(raw, list) or not raw:
        errors.add(SKILLS_REL, "skills: expected a non-empty array")
        return None
    if len(raw) > SKILL_MAX:
        errors.add(SKILLS_REL, "size limit: %d skills exceed the %d skill cap" % (len(raw), SKILL_MAX))
    skills = []
    seen = set()
    for i, obj in enumerate(raw):
        ctx = "%s: skills[%d]" % (SKILLS_REL, i)
        check_keys(errors, ctx, obj, {"id", "kind", "maxPoints", "perPoint"})
        if not isinstance(obj, dict):
            continue
        skill_id = read_id(errors, ctx, obj, "id", seen)
        kind = read_enum(errors, ctx, obj, "kind", KINDS)
        max_points = read_int(errors, ctx, obj, "maxPoints", 1, POINTS_MAX)
        per_point = read_int(errors, ctx, obj, "perPoint", 0, 255)
        if None in (skill_id, kind, max_points, per_point):
            continue
        skills.append({"id": skill_id, "kind": kind, "maxPoints": max_points, "perPoint": per_point})
    if errors.items or thr is None:
        return None
    return {"thresholds": thr, "skills": skills}


def normalize_materials(errors, ctx, obj, item_ids):
    """Recipe bill: up to MAT_SLOTS {item, count} pairs. Absent/empty is a
    zenny-only recipe. Duplicate items are rejected: the packed slots are a set
    and the runtime debit walks them in order."""
    raw = obj.get("materials", []) if isinstance(obj, dict) else []
    if not isinstance(raw, list):
        errors.add(ctx, "materials: expected an array")
        return None
    if len(raw) > MAT_SLOTS:
        errors.add(ctx, "materials: %d pairs exceed the %d packed slots" % (len(raw), MAT_SLOTS))
        return None
    mats = []
    seen = set()
    for i, entry in enumerate(raw):
        ec = "%s.materials[%d]" % (ctx, i)
        if not isinstance(entry, dict):
            errors.add(ec, "expected an object")
            return None
        check_keys(errors, ec, entry, {"item", "count"})
        item = entry.get("item")
        if not isinstance(item, str) or item not in item_ids:
            errors.add(ec, "item: unknown item %r (not in %s)" % (item, ITEMS_REL))
            return None
        if item in seen:
            errors.add(ec, "duplicate material '%s'" % item)
            return None
        seen.add(item)
        count = read_int(errors, ec, entry, "count", 1, 255)
        if count is None:
            return None
        mats.append({"item": item, "count": count})
    return mats


def normalize_resist(errors, ctx, obj):
    resist = obj.get("resist") if isinstance(obj, dict) else None
    if not isinstance(resist, dict):
        errors.add(ctx, "resist: expected an object")
        return None
    check_keys(errors, ctx + ".resist", resist, set(RESISTS))
    out = []
    for elem in RESISTS:
        value = read_int(errors, ctx + ".resist", resist, elem, RESIST_MIN, RESIST_MAX)
        if value is None:
            return None
        out.append(value)
    return out


def normalize_piece_skills(errors, ctx, obj, skill_index):
    raw = obj.get("skills") if isinstance(obj, dict) else None
    if not isinstance(raw, list):
        errors.add(ctx, "skills: expected an array")
        return None
    if not raw:
        errors.add(ctx, "skills: expected at least one skill")
        return None
    if len(raw) > SKILL_SLOTS:
        errors.add(ctx, "skills: %d entries exceed the %d packed slots" % (len(raw), SKILL_SLOTS))
        return None
    skills = []
    seen = set()
    for i, entry in enumerate(raw):
        ec = "%s.skills[%d]" % (ctx, i)
        if not isinstance(entry, dict):
            errors.add(ec, "expected an object")
            return None
        check_keys(errors, ec, entry, {"id", "points"})
        skill_id = entry.get("id")
        if not isinstance(skill_id, str) or skill_id not in skill_index:
            errors.add(ec, "id: unknown skill %r (not in %s)" % (skill_id, SKILLS_REL))
            return None
        if skill_id in seen:
            errors.add(ec, "duplicate skill '%s'" % skill_id)
            return None
        seen.add(skill_id)
        points = read_int(errors, ec, entry, "points", 0, POINTS_MAX)
        if points is None:
            return None
        skills.append({"skill": skill_index[skill_id], "points": points})
    return skills


def normalize_piece(errors, ctx, obj, seen_ids, skill_index, item_ids):
    check_keys(errors, ctx, obj, {"id", "slot", "defense", "resist", "skills", "recipe"},
               ("sheet",))
    if not isinstance(obj, dict):
        return None
    piece_id = read_id(errors, ctx, obj, "id", seen_ids)
    slot = read_enum(errors, ctx, obj, "slot", SLOTS)
    defense = read_int(errors, ctx, obj, "defense", 0, DEFENSE_MAX)
    resist = normalize_resist(errors, ctx, obj)
    skills = normalize_piece_skills(errors, ctx, obj, skill_index)
    recipe = obj.get("recipe")
    if not isinstance(recipe, dict):
        errors.add(ctx, "recipe: expected an object")
        zenny = None
        mats = None
    else:
        check_keys(errors, ctx + ".recipe", recipe, {"materials", "zenny"})
        zenny = read_int(errors, ctx + ".recipe", recipe, "zenny", 0, 65535)
        mats = normalize_materials(errors, ctx + ".recipe", recipe, item_ids)
    sheet = obj.get("sheet")
    if sheet is not None and (not isinstance(sheet, str) or not NAME_RE.match(sheet)):
        errors.add(ctx, "sheet: expected a [a-z][a-z0-9_]* symbol, got %r" % (sheet,))
        sheet = None
    if None in (piece_id, slot, defense, resist, skills, zenny, mats):
        return None
    return {"id": piece_id, "slot": slot, "defense": defense, "resist": resist,
            "skills": skills, "materials": mats, "zenny": zenny, "sheet": sheet}


def compile_model(errors, root):
    item_ids = load_item_ids(errors, root)
    if item_ids is None:
        return None

    skills_path = os.path.join(root, SKILLS_REL)
    if not os.path.isfile(skills_path):
        errors.add(SKILLS_REL, "missing skill file")
        return None
    skill_model = normalize_skills(errors, load_json(errors, skills_path, SKILLS_REL))
    if skill_model is None:
        return None
    skill_index = {s["id"]: i for i, s in enumerate(skill_model["skills"])}

    armor_path = os.path.join(root, ARMOR_REL)
    if not os.path.isfile(armor_path):
        errors.add(ARMOR_REL, "missing armor file")
        return None
    doc = load_json(errors, armor_path, ARMOR_REL)
    if doc is None:
        return None
    check_keys(errors, ARMOR_REL, doc, {"version", "pieces"})
    read_int(errors, ARMOR_REL, doc, "version", 1, 1)
    raw = doc.get("pieces")
    if not isinstance(raw, list) or not raw:
        errors.add(ARMOR_REL, "pieces: expected a non-empty array")
        return None
    if len(raw) > PIECE_MAX:
        errors.add(ARMOR_REL, "size limit: %d pieces exceed the %d piece cap" % (len(raw), PIECE_MAX))
    pieces = []
    seen_ids = set()
    for i, obj in enumerate(raw):
        piece = normalize_piece(errors, "%s: pieces[%d]" % (ARMOR_REL, i), obj, seen_ids, skill_index, item_ids)
        if piece is not None:
            pieces.append(piece)
    if errors.items:
        return None

    # Cross-check: the points a skill can reach across all pieces must fit the
    # authored cap, otherwise the game could grant more than maxPoints.
    totals = {}
    for piece in pieces:
        for entry in piece["skills"]:
            totals[entry["skill"]] = totals.get(entry["skill"], 0) + entry["points"]
    for idx, total in sorted(totals.items()):
        cap = skill_model["skills"][idx]["maxPoints"]
        if total > cap:
            errors.add(ARMOR_REL, "skill %s: %d total points exceed maxPoints %d"
                       % (skill_model["skills"][idx]["id"], total, cap))
    if errors.items:
        return None

    # Sheet table: sorted unique names; index 0 is reserved for "no sheet".
    sheets = sorted({piece["sheet"] for piece in pieces if piece["sheet"] is not None})
    sheet_index = {name: i + 1 for i, name in enumerate(sheets)}

    skill_model["itemIds"] = item_ids
    return {"pieces": pieces, "skills": skill_model["skills"], "thresholds": skill_model["thresholds"],
            "sheets": sheets, "sheetIndex": sheet_index, "itemIds": item_ids}


def pack_blob(errors, model):
    pieces = model["pieces"]
    skills = model["skills"]
    if not 1 <= len(pieces) <= 255:
        errors.add("data", "piece count %d outside 1..255" % len(pieces))
        return None
    if not 1 <= len(skills) <= 255:
        errors.add("data", "skill count %d outside 1..255" % len(skills))
        return None
    blob = bytearray(struct.pack("<HBBBBH", MAGIC, VERSION, FLAGS, len(pieces), len(skills), 0))
    sheet_index = model["sheetIndex"]
    for piece in pieces:
        mats = piece["materials"]
        mat_bytes = bytearray()
        for i in range(MAT_SLOTS):
            if i < len(mats):
                mat_bytes += bytes((model["itemIds"].index(mats[i]["item"]) + 1, mats[i]["count"]))
            else:
                mat_bytes += b"\x00\x00"
        skill_bytes = bytearray()
        for i in range(SKILL_SLOTS):
            if i < len(piece["skills"]):
                entry = piece["skills"][i]
                skill_bytes += bytes((entry["skill"] + 1, entry["points"]))
            else:
                skill_bytes += b"\x00\x00"
        sheet_code = 0
        if piece["sheet"] is not None:
            sheet_code = sheet_index[piece["sheet"]]
        blob += struct.pack("<BBbbbbH", piece["slot"], piece["defense"],
                            piece["resist"][0], piece["resist"][1], piece["resist"][2], piece["resist"][3],
                            piece["zenny"])
        blob += bytes(mat_bytes)
        blob += struct.pack("<BB", sheet_code, len(piece["skills"]))
        blob += bytes(skill_bytes)
    for skill in skills:
        blob += struct.pack("<BBB", skill["kind"], skill["maxPoints"], skill["perPoint"])
    expected = HEADER_SIZE + PIECE_SIZE * len(pieces) + SKILL_SIZE * len(skills)
    if len(blob) != expected:
        errors.add("data", "internal: blob is %d B, want %d" % (len(blob), expected))
        return None
    return bytes(blob)


def emit_data_header(model, blob):
    pieces = model["pieces"]
    skills = model["skills"]
    lines = []
    app = lines.append
    app("#pragma once")
    app("// Generated by tools/gen-armor.py -- do not edit.")
    app("//")
    app("// Host-side plain structs mirroring the packed mhArmor blob (data/skills.json")
    app("// + data/armor.json). Field order matches the blob byte order; the host")
    app("// reads members directly, so host padding is irrelevant. The device reads")
    app("// the blob with the offsets in armor_meta.hpp instead.")
    app("")
    app("#include <array>")
    app("#include <stdint.h>")
    app("")
    app("namespace armor_data {")
    app("")
    app("constexpr uint8_t VERSION = %d;" % VERSION)
    app("constexpr uint16_t BLOB_SIZE = %d;" % len(blob))
    app("")
    app("struct Skill {")
    app("    uint8_t kind;        // armor::KIND_*")
    app("    uint8_t maxPoints;")
    app("    uint8_t perPoint;    // effect magnitude per point")
    app("};")
    app("")
    app("struct MatSlot {")
    app("    uint8_t item;        // item index + 1, 0 = empty")
    app("    uint8_t count;")
    app("};")
    app("")
    app("struct SkillSlot {")
    app("    uint8_t skill;       // skill index + 1, 0 = empty")
    app("    uint8_t points;")
    app("};")
    app("")
    app("struct Piece {")
    app("    uint8_t slot;        // armor::SLOT_*")
    app("    uint8_t defense;")
    app("    int8_t resist[4];    // fire, water, ice, thunder")
    app("    uint16_t zenny;      // recipe zenny cost")
    app("    MatSlot mat[%d];" % MAT_SLOTS)
    app("    uint8_t sheet;       // sheet index + 1, 0 = none")
    app("    uint8_t skillCount;")
    app("    SkillSlot skills[%d];" % SKILL_SLOTS)
    app("};")
    app("")
    app("inline constexpr std::array<Skill, %d> SKILLS = {{" % len(skills))
    for skill in skills:
        app("    {%d, %d, %d},   // %s = %s" % (skill["kind"], skill["maxPoints"], skill["perPoint"],
                                               skill["id"], KINDS[skill["kind"]]))
    app("}};")
    app("")
    app("inline constexpr std::array<Piece, %d> PIECES = {{" % len(pieces))
    for piece in pieces:
        r = piece["resist"]
        mats = []
        for i in range(MAT_SLOTS):
            if i < len(piece["materials"]):
                mats.append((model["itemIds"].index(piece["materials"][i]["item"]) + 1,
                             piece["materials"][i]["count"]))
            else:
                mats.append((0, 0))
        sk = []
        for i in range(SKILL_SLOTS):
            if i < len(piece["skills"]):
                sk.append((piece["skills"][i]["skill"] + 1, piece["skills"][i]["points"]))
            else:
                sk.append((0, 0))
        sheet = model["sheetIndex"].get(piece["sheet"], 0) if piece["sheet"] is not None else 0
        app("    {%d, %d, {%d, %d, %d, %d}, %d, {{%d, %d}, {%d, %d}}, %d, %d, {{%d, %d}, {%d, %d}}},   // %s"
            % (piece["slot"], piece["defense"], r[0], r[1], r[2], r[3], piece["zenny"],
               mats[0][0], mats[0][1], mats[1][0], mats[1][1], sheet, len(piece["skills"]),
               sk[0][0], sk[0][1], sk[1][0], sk[1][1], piece["id"]))
    app("}};")
    app("")
    app("}   // namespace armor_data")
    app("")
    return "\n".join(lines)


def emit_meta_header(model, blob):
    pieces = model["pieces"]
    skills = model["skills"]
    sheets = model["sheets"]
    lines = []
    app = lines.append
    app("#pragma once")
    app("// Generated by tools/gen-armor.py -- do not edit.")
    app("//")
    app("// Armor table ABI (bead monhun-ardu-arm.1): 8 B header + one fixed 18 B")
    app("// piece record + one fixed 3 B skill record, little-endian, no padding.")
    app("// Offsets are absolute byte offsets into the mhArmor raw_t section")
    app("// (fxdata/fxdata.txt); on AVR the loader reads mhArmor + off through")
    app("// core/fxmem.hpp. Piece/skill index == record index; armor::mat::<ID>")
    app("// indexes the mhItems table (item index + 1 in a recipe slot).")
    app("")
    app("#include <stdint.h>")
    app("")
    app("namespace armor {")
    app("")
    app("constexpr uint16_t MAGIC = 0x%04X;" % MAGIC)
    app("constexpr uint8_t VERSION = %d;" % VERSION)
    app("constexpr uint8_t FLAGS = 0x%02X;" % FLAGS)
    app("constexpr uint16_t SIZE = %d;" % len(blob))
    app("constexpr uint8_t HEADER_SIZE = %d;" % HEADER_SIZE)
    app("constexpr uint8_t PIECE_SIZE = %d;" % PIECE_SIZE)
    app("constexpr uint8_t SKILL_SIZE = %d;" % SKILL_SIZE)
    app("constexpr uint8_t PIECE_COUNT = %d;" % len(pieces))
    app("constexpr uint8_t SKILL_COUNT = %d;" % len(skills))
    app("constexpr uint8_t PIECE_MAX = %d;" % PIECE_MAX)
    app("constexpr uint8_t MAT_SLOTS = %d;    // packed {item,count} pairs per piece" % MAT_SLOTS)
    app("constexpr uint8_t SKILL_SLOTS = %d;  // packed {skill,points} pairs per piece" % SKILL_SLOTS)
    app("constexpr uint16_t PIECES_OFF = %d;" % HEADER_SIZE)
    app("constexpr uint16_t SKILLS_OFF = %d;" % (HEADER_SIZE + PIECE_SIZE * len(pieces)))
    app("constexpr uint8_t SHEET_NONE = 0;")
    app("")
    app("// Piece slots; values mirror the packed slot byte.")
    for i, name in enumerate(SLOTS):
        app("constexpr uint8_t SLOT_%s = %d;" % (name.upper(), i))
    app("")
    app("// Skill effect kinds; values mirror the packed kind byte.")
    for i, name in enumerate(KINDS):
        app("constexpr uint8_t KIND_%s = %d;" % (name, i))
    app("")
    app("// Piece field offsets (slot, defense, resist, zenny, mat, sheet, skills).")
    app("constexpr uint8_t PIECE_SLOT_OFF = 0;")
    app("constexpr uint8_t PIECE_DEFENSE_OFF = 1;")
    app("constexpr uint8_t PIECE_RESIST_OFF = 2;    // 4 x i8: fire, water, ice, thunder")
    app("constexpr uint8_t PIECE_ZENNY_OFF = 6;     // u16")
    app("constexpr uint8_t PIECE_MAT_OFF = 8;       // MAT_SLOTS x (itemIdx+1 u8, count u8)")
    app("constexpr uint8_t PIECE_MAT_STRIDE = 2;")
    app("constexpr uint8_t PIECE_SHEET_OFF = 12;    // sheet index + 1, 0 = none")
    app("constexpr uint8_t PIECE_SKILL_COUNT_OFF = 13;")
    app("constexpr uint8_t PIECE_SKILLS_OFF = 14;   // SKILL_SLOTS x (skillIdx+1 u8, points u8)")
    app("constexpr uint8_t PIECE_SKILL_STRIDE = 2;")
    app("")
    app("// Skill field offsets (kind, maxPoints, perPoint).")
    app("constexpr uint8_t SKILL_KIND_OFF = 0;")
    app("constexpr uint8_t SKILL_MAX_OFF = 1;")
    app("constexpr uint8_t SKILL_PER_POINT_OFF = 2;")
    app("")
    app("// Shared activation thresholds (data/skills.json): total points across the")
    app("// equipped pieces below THRESHOLD_S are inert; >= THRESHOLD_S activates the")
    app("// skill; THRESHOLD_M is the max useful total (see docs/equipment-framework.md).")
    app("constexpr uint8_t THRESHOLD_S = %d;" % model["thresholds"]["s"])
    app("constexpr uint8_t THRESHOLD_M = %d;" % model["thresholds"]["m"])
    app("")
    app("// Armor piece indices + blob record offsets, in data/armor.json source order.")
    for i, piece in enumerate(pieces):
        name = piece["id"].upper()
        app("constexpr uint8_t ARMOR_%s = %d;" % (name, i))
        app("constexpr uint16_t ARMOR_%s_OFF = %d;" % (name, HEADER_SIZE + i * PIECE_SIZE))
    app("")
    app("// Skill indices + blob record offsets, in data/skills.json source order.")
    for i, skill in enumerate(skills):
        name = skill["id"].upper()
        app("constexpr uint8_t SKILL_%s = %d;" % (name, i))
        app("constexpr uint16_t SKILL_%s_OFF = %d;"
            % (name, HEADER_SIZE + PIECE_SIZE * len(pieces) + i * SKILL_SIZE))
    app("")
    if sheets:
        app("// Sheet symbols referenced by pieces (placeholder ids until the sprite")
        app("// epic lands); the packed sheet field is this index. 0 = no sheet.")
        app("namespace sheet {")
        app("constexpr uint8_t NONE = 0;")
        for i, name in enumerate(sheets):
            app("constexpr uint8_t %s = %d;" % (name.upper(), i + 1))
        app("}   // namespace sheet")
        app("")
    item_ids = model["itemIds"]
    app("// Recipe material item ids (index into Game::items[] / the mhItems table).")
    app("namespace mat {")
    for i, name in enumerate(item_ids):
        app("constexpr uint8_t %s = %d;" % (name.upper(), i))
    app("}   // namespace mat")
    app("")
    app("}   // namespace armor")
    app("")
    return "\n".join(lines)


def emit_expect_header(model, blob):
    pieces = model["pieces"]
    skills = model["skills"]
    lines = []
    app = lines.append
    digest = hashlib.sha256(blob).hexdigest()
    app("#pragma once")
    app("// Generated by tools/gen-armor.py -- do not edit.")
    app("//")
    app("// Byte-level expectations for the host table test and the Ardens loader")
    app("// test: record sizes, pinned spot values and the blob sha256.")
    app("")
    app("#include <stdint.h>")
    app("")
    app("namespace armor_expect {")
    app("")
    app("constexpr uint16_t BLOB_SIZE = %d;" % len(blob))
    app("constexpr uint8_t PIECE_SIZE = %d;" % PIECE_SIZE)
    app("constexpr uint8_t SKILL_SIZE = %d;" % SKILL_SIZE)
    app("constexpr uint8_t PIECE_COUNT = %d;" % len(pieces))
    app("constexpr uint8_t SKILL_COUNT = %d;" % len(skills))
    app("")
    for piece in pieces:
        name = piece["id"].upper()
        r = piece["resist"]
        app("constexpr uint8_t ARMOR_%s_SLOT = %d;" % (name, piece["slot"]))
        app("constexpr uint8_t ARMOR_%s_DEFENSE = %d;" % (name, piece["defense"]))
        app("constexpr int8_t ARMOR_%s_RESIST_FIRE = %d;" % (name, r[0]))
        app("constexpr int8_t ARMOR_%s_RESIST_WATER = %d;" % (name, r[1]))
        app("constexpr int8_t ARMOR_%s_RESIST_ICE = %d;" % (name, r[2]))
        app("constexpr int8_t ARMOR_%s_RESIST_THUNDER = %d;" % (name, r[3]))
        app("constexpr uint16_t ARMOR_%s_ZENNY = %d;" % (name, piece["zenny"]))
        sheet = model["sheetIndex"].get(piece["sheet"], 0) if piece["sheet"] is not None else 0
        app("constexpr uint8_t ARMOR_%s_SHEET = %d;" % (name, sheet))
        for i in range(SKILL_SLOTS):
            if i < len(piece["skills"]):
                app("constexpr uint8_t ARMOR_%s_SKILL%d = %d;"
                    % (name, i, piece["skills"][i]["skill"] + 1))
                app("constexpr uint8_t ARMOR_%s_SKILL%d_POINTS = %d;"
                    % (name, i, piece["skills"][i]["points"]))
            else:
                app("constexpr uint8_t ARMOR_%s_SKILL%d = 0;" % (name, i))
                app("constexpr uint8_t ARMOR_%s_SKILL%d_POINTS = 0;" % (name, i))
        for i in range(MAT_SLOTS):
            if i < len(piece["materials"]):
                code = model["itemIds"].index(piece["materials"][i]["item"]) + 1
                app("constexpr uint8_t ARMOR_%s_MAT%d = %d;" % (name, i, code))
                app("constexpr uint8_t ARMOR_%s_MAT%d_COUNT = %d;"
                    % (name, i, piece["materials"][i]["count"]))
            else:
                app("constexpr uint8_t ARMOR_%s_MAT%d = 0;" % (name, i))
                app("constexpr uint8_t ARMOR_%s_MAT%d_COUNT = 0;" % (name, i))
    app("")
    for skill in skills:
        name = skill["id"].upper()
        app("constexpr uint8_t SKILL_%s_KIND = %d;" % (name, skill["kind"]))
        app("constexpr uint8_t SKILL_%s_MAX_POINTS = %d;" % (name, skill["maxPoints"]))
        app("constexpr uint8_t SKILL_%s_PER_POINT = %d;" % (name, skill["perPoint"]))
    app("")
    app("// sha256 of fxdata/tables/armor.bin: %s" % digest)
    app("constexpr uint8_t BLOB_SHA256[32] = {")
    raw = hashlib.sha256(blob).digest()
    for offset in range(0, 32, 8):
        app("    " + ", ".join("0x%02X" % byte for byte in raw[offset:offset + 8]) + ",")
    app("};")
    app("")
    app("}   // namespace armor_expect")
    app("")
    return "\n".join(lines)


def dump_model(model, blob):
    for kind_skill in model["skills"]:
        print("skill %s: kind %s maxPoints %d perPoint %d" % (
            kind_skill["id"], KINDS[kind_skill["kind"]],
            kind_skill["maxPoints"], kind_skill["perPoint"]))
    for piece in model["pieces"]:
        r = piece["resist"]
        mats = " ".join("%s x%d" % (m["item"], m["count"]) for m in piece["materials"])
        sk = " ".join("%s %d" % (model["skills"][e["skill"]]["id"], e["points"]) for e in piece["skills"])
        print("piece %s: slot %s defense %d resist %d/%d/%d/%d skills %s recipe %s %d zenny"
              % (piece["id"], SLOTS[piece["slot"]], piece["defense"], r[0], r[1], r[2], r[3],
                 sk, mats if mats else "-", piece["zenny"]))
    print("gen-armor: thresholds s=%d m=%d, %d pieces, %d skills, %d B blob"
          % (model["thresholds"]["s"], model["thresholds"]["m"],
             len(model["pieces"]), len(model["skills"]), len(blob)))


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


def run(root, dump):
    errors = Errors()
    model = compile_model(errors, root)
    blob = pack_blob(errors, model) if model is not None and not errors.items else None
    if errors.items:
        for item in errors.items:
            print("gen-armor: error: %s" % item, file=sys.stderr)
        print("gen-armor: FAIL (%d error%s)"
              % (len(errors.items), "" if len(errors.items) == 1 else "s"), file=sys.stderr)
        return 1
    if dump:
        dump_model(model, blob)
        return 0

    changed = []
    if write_if_changed(os.path.join(root, BLOB_REL), blob):
        changed.append(BLOB_REL)
    if write_if_changed(os.path.join(root, DATA_HPP_REL), emit_data_header(model, blob)):
        changed.append(DATA_HPP_REL)
    if write_if_changed(os.path.join(root, META_HPP_REL), emit_meta_header(model, blob)):
        changed.append(META_HPP_REL)
    if write_if_changed(os.path.join(root, EXPECT_HPP_REL), emit_expect_header(model, blob)):
        changed.append(EXPECT_HPP_REL)
    print("gen-armor: %d pieces, %d skills, %d B blob (magic 0x%04X version %d)"
          % (len(model["pieces"]), len(model["skills"]), len(blob), MAGIC, VERSION))
    for rel in (BLOB_REL, DATA_HPP_REL, META_HPP_REL, EXPECT_HPP_REL):
        print("gen-armor: %s%s" % (rel, "" if rel in changed else " (unchanged)"))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=None, help="pipeline root holding data/ (default: repository root)")
    parser.add_argument("--dump", action="store_true", help="validate + list the compiled pieces/skills; write nothing")
    args = parser.parse_args(argv)
    root = os.path.abspath(args.root) if args.root else REPO_ROOT
    return run(root, args.dump)


if __name__ == "__main__":
    sys.exit(main())
