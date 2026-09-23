#!/usr/bin/env python3
"""Compile the weapon forge trees into the packed FX blob + generated header.

    data/forge/*.json
        -> fxdata/tables/forge.bin        (packed blob; build intermediate)
        -> src/generated/forge_meta.hpp   (node indices/offsets/enums/tier map)

Design: docs/ui-design.md "Weapon tree model" (bead monhun-ardu-5co.4). One
class file per weapon class holds a tree of nodes; a node is a weapon the
player can forge (direct, pricier) or upgrade into from an owned parent. The
runtime (src/forge_state.hpp / src/forge.hpp) reads the packed records through
core/fxmem.hpp during the screen/render window and resolves the equipped node's
damage/speed multipliers (replacing the old smith tier table).

Node fields: id, label, parent (null = root), direct (bool), cost, mats,
directCost, directMats, dmgMul, spdMul, desc (card copy), sheet (equip sheet
symbol, metadata only). A root node has no parent; every class must have one.
Branches are allowed: a node's parent is any earlier node of the same class.

The generator also exposes load_nodes()/load_model() for tools/gen-screens.py
(FORGE/GEAR rows) and tools/gen-cards.py (node DESC/PARTS/STATS pages), so the
tree is authored once and every consumer reads the same source.

Blob layout (little-endian, explicit u8/u16, no padding, fixed order):

    header     8 B  magic u16 0x4647, version u8, flags u8, nodeCount u8,
                    reserved u8, reserved u16
    records   17 B each, in data order (class files sorted sword/flail/gun,
                    nodes in file order): class u8, parent u8 (0xFF = none),
                    flags u8 (bit0 = direct), dmgMul u8, spdMul u8,
                    cost u16, directCost u16,
                    mats[2] x (itemIdx+1 u8, count u8),
                    directMats[2] x (itemIdx+1 u8, count u8)

Usage:
    python3 tools/gen-forge.py [--root DIR] [--dump]

    --root DIR  pipeline root holding data/ and src/generated (default: repo)
    --dump      validate + list the compiled nodes on stdout; writes nothing
"""
import argparse
import json
import os
import re
import struct
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = "data/forge"
ITEMS_REL = "data/items.json"
BLOB_REL = "fxdata/tables/forge.bin"
META_REL = "src/generated/forge_meta.hpp"

MAGIC = 0x4647   # 'G','F' little-endian
VERSION = 1
FLAGS = 0
HEADER_SIZE = 8
RECORD_SIZE = 17
NODE_MAX = 64     # the save's weapon-owned bitset has 64 slots
MAT_SLOTS = 2     # packed {itemIdx+1, count} pairs per bill
NODE_NONE = 0xFF
FLAG_DIRECT = 0x01

# Weapon classes, index == WeaponId in src/core/game.hpp. Keep in sync.
WEAPON_NAMES = ("sword", "flail", "gun")

NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")
SHEET_RE = re.compile(r"^[a-z][a-z0-9_]*$")

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


def normalize_bill(errors, ctx, obj, key, item_ids):
    """Optional bill: up to MAT_SLOTS {item, count} pairs (item names)."""
    raw = obj.get(key, []) if isinstance(obj, dict) else []
    if not isinstance(raw, list):
        errors.add(ctx, "%s: expected an array" % key)
        return None
    if len(raw) > MAT_SLOTS:
        errors.add(ctx, "%s: %d pairs exceed the %d packed slots" % (key, len(raw), MAT_SLOTS))
        return None
    mats = []
    seen = set()
    for i, entry in enumerate(raw):
        ec = "%s.%s[%d]" % (ctx, key, i)
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


def normalize_node(errors, ctx, obj, cls, item_ids, by_id):
    check_keys(errors, ctx, obj, {"id", "label", "parent", "direct", "cost", "mats",
                                  "directCost", "directMats", "dmgMul", "spdMul", "desc"},
               ("sheet",))
    if not isinstance(obj, dict):
        return None
    node_id = obj.get("id")
    if not isinstance(node_id, str) or not NAME_RE.match(node_id):
        errors.add(ctx, "id: expected a [a-z][a-z0-9_]* id")
        return None
    if node_id in by_id:
        errors.add(ctx, "duplicate node id '%s'" % node_id)
        return None
    label = read_text(errors, ctx, obj, "label", 12)
    parent = obj.get("parent")
    if parent is not None:
        if not isinstance(parent, str) or parent not in by_id:
            errors.add(ctx, "parent: unknown node %r (must be an earlier node in the class)" % (parent,))
            return None
        if by_id[parent]["class"] != cls:
            errors.add(ctx, "parent: node %r is a different class" % (parent,))
            return None
    direct = obj.get("direct")
    if not isinstance(direct, bool):
        errors.add(ctx, "direct: expected a boolean, got %r" % (direct,))
        return None
    cost = read_int(errors, ctx, obj, "cost", 0, 65535)
    direct_cost = read_int(errors, ctx, obj, "directCost", 0, 65535)
    dmg = read_int(errors, ctx, obj, "dmgMul", 1, 255)
    spd = read_int(errors, ctx, obj, "spdMul", 1, 255)
    mats = normalize_bill(errors, ctx, obj, "mats", item_ids)
    direct_mats = normalize_bill(errors, ctx, obj, "directMats", item_ids)
    desc = obj.get("desc")
    if not isinstance(desc, list) or not desc or not all(isinstance(line, str) for line in desc):
        errors.add(ctx, "desc: expected a non-empty array of strings")
        return None
    for i, line in enumerate(desc):
        if any(not 32 <= ord(ch) <= 126 for ch in line):
            errors.add(ctx, "desc[%d]: chars must be printable ASCII" % i)
            return None
    sheet = obj.get("sheet")
    if sheet is not None and (not isinstance(sheet, str) or not SHEET_RE.match(sheet)):
        errors.add(ctx, "sheet: expected a [a-z][a-z0-9_]* symbol")
        return None
    if None in (label, cost, direct_cost, dmg, spd, mats, direct_mats):
        return None
    return {"id": node_id, "label": label, "parent": parent, "direct": direct,
            "cost": cost, "mats": mats, "directCost": direct_cost, "directMats": direct_mats,
            "dmgMul": dmg, "spdMul": spd, "desc": list(desc), "sheet": sheet,
            "class": cls, "index": len(by_id)}


def compile_model(errors, root):
    item_ids = load_item_ids(errors, root)
    if item_ids is None:
        return None
    data_dir = os.path.join(root, DATA_DIR)
    if not os.path.isdir(data_dir):
        errors.add(DATA_DIR, "missing forge directory")
        return None
    names = sorted(name for name in os.listdir(data_dir) if name.endswith(".json"))
    if not names:
        errors.add(DATA_DIR, "no forge JSON files found")
        return None
    # Order class files by WeaponId (sword, flail, gun) rather than file name, so
    # node ids + the generated FORGE/GEAR rows follow the game's weapon order.
    order = {}
    for name in names:
        try:
            with open(os.path.join(data_dir, name), encoding="utf-8") as handle:
                cls = json.load(handle).get("class")
            order[name] = WEAPON_NAMES.index(cls) if cls in WEAPON_NAMES else len(WEAPON_NAMES)
        except (OSError, ValueError, AttributeError):
            order[name] = len(WEAPON_NAMES)
    names.sort(key=lambda name: (order[name], name))
    classes = []
    nodes = []
    by_id = {}
    for name in names:
        rel = "%s/%s" % (DATA_DIR, name)
        obj = load_json(errors, os.path.join(data_dir, name), rel)
        if obj is None:
            continue
        check_keys(errors, rel, obj, {"class", "nodes"}, ("header",))
        cls_name = obj.get("class") if isinstance(obj, dict) else None
        if cls_name not in WEAPON_NAMES:
            errors.add(rel, "class: unknown value %r (want one of %s)"
                       % (cls_name, ", ".join(WEAPON_NAMES)))
            continue
        cls = WEAPON_NAMES.index(cls_name)
        raw_nodes = obj.get("nodes")
        if not isinstance(raw_nodes, list) or not raw_nodes:
            errors.add(rel, "nodes: expected a non-empty array")
            continue
        header = obj.get("header", "-- %s --" % cls_name.upper()[:3])
        if not isinstance(header, str) or not 1 <= len(header) <= 16:
            errors.add(rel, "header: expected a 1..16 char string")
            continue
        classes.append({"name": cls_name, "class": cls, "header": header,
                        "firstNode": len(nodes), "count": 0})
        for i, raw in enumerate(raw_nodes):
            ctx = "%s.nodes[%d]" % (rel, i)
            node = normalize_node(errors, ctx, raw, cls, item_ids, by_id)
            if node is None:
                continue
            node["index"] = len(nodes)
            by_id[node["id"]] = node
            nodes.append(node)
            classes[-1]["count"] += 1
        roots = [n for n in nodes[classes[-1]["firstNode"]:] if n["parent"] is None]
        if len(roots) != 1:
            errors.add(rel, "class %s must have exactly one root node (found %d)"
                       % (cls_name, len(roots)))
    if errors.items:
        return None
    if len(nodes) > NODE_MAX:
        errors.add(DATA_DIR, "size limit: %d nodes exceed the %d slot cap" % (len(nodes), NODE_MAX))
        return None
    for node in nodes:
        node["parentIdx"] = NODE_NONE if node["parent"] is None else by_id[node["parent"]]["index"]
        for mat in node["mats"] + node["directMats"]:
            mat["itemIdx"] = item_ids.index(mat["item"])
    # Tree shape (emitted as NODE_DEPTH/NODE_BRANCH for tooling + the FORGE row
    # prefixes): depth is the root distance, branch the sibling ordinal (0-based
    # in data order). Both are pure functions of the parent chain.
    for node in nodes:
        node["depth"] = 0 if node["parent"] is None else by_id[node["parent"]]["depth"] + 1
    # Sibling ordinals are per (class, parent): roots of different classes are
    # not siblings of one another.
    sib = {}
    for node in nodes:
        key = (node["class"], node["parentIdx"])
        node["branch"] = sib.get(key, 0)
        sib[key] = node["branch"] + 1
    sibs = {}
    for node in nodes:
        key = (node["class"], node["parentIdx"])
        sibs[key] = sibs.get(key, 0) + 1
    for node in nodes:
        key = (node["class"], node["parentIdx"])
        node["hasLaterSibling"] = node["branch"] + 1 < sibs[key]
    return {"nodes": nodes, "classes": classes, "itemIds": item_ids}


def row_label(model, node):
    """FORGE/GEAR row label: the tree prefix (design ASCII style) + node label."""
    if node["depth"] == 0:
        return node["label"]
    by_index = model["nodes"]
    parts = []
    cur = by_index[node["parentIdx"]]
    while cur["depth"] > 0:
        parts.append("|  " if cur["hasLaterSibling"] else "   ")
        cur = by_index[cur["parentIdx"]]
    parts.reverse()
    return "".join(parts) + "+- " + node["label"]


def _mat_slots(mats):
    slots = []
    for i in range(MAT_SLOTS):
        if i < len(mats):
            slots += [mats[i]["itemIdx"] + 1, mats[i]["count"]]
        else:
            slots += [0, 0]
    return slots


def pack_blob(errors, nodes):
    count = len(nodes)
    if count == 0 or count > NODE_MAX:
        errors.add("data", "node count %d outside 1..%d" % (count, NODE_MAX))
        return None
    blob = bytearray(struct.pack("<HBBBBH", MAGIC, VERSION, FLAGS, count, 0, 0))
    for node in nodes:
        flags = FLAG_DIRECT if node["direct"] else 0
        slots = _mat_slots(node["mats"])
        dslots = _mat_slots(node["directMats"])
        blob += struct.pack("<BBBBBH H", node["class"], node["parentIdx"], flags,
                            node["dmgMul"], node["spdMul"], node["cost"], node["directCost"])
        blob += struct.pack("<BBBB", slots[0], slots[1], slots[2], slots[3])
        blob += struct.pack("<BBBB", dslots[0], dslots[1], dslots[2], dslots[3])
    expected = HEADER_SIZE + RECORD_SIZE * count
    if len(blob) != expected:
        errors.add("data", "internal: blob is %d B, want %d" % (len(blob), expected))
        return None
    return bytes(blob)


def emit_meta_header(model, blob):
    nodes = model["nodes"]
    lines = []
    app = lines.append
    app("#pragma once")
    app("// Generated by tools/gen-forge.py -- do not edit.")
    app("//")
    app("// Weapon forge-tree ABI (bead monhun-ardu-5co.4, docs/ui-design.md): 8 B")
    app("// header + one fixed 17 B node record, little-endian, no padding. src/forge.hpp")
    app("// reads the records through core/fxmem.hpp during the screen/render window;")
    app("// src/forge_state.hpp holds the host-testable node struct + forge/upgrade")
    app("// semantics.")
    app("")
    app("#include <stdint.h>")
    app("")
    app("namespace forge {")
    app("")
    app("constexpr uint16_t MAGIC = 0x%04X;" % MAGIC)
    app("constexpr uint8_t VERSION = %d;" % VERSION)
    app("constexpr uint8_t FLAGS = 0x%02X;" % FLAGS)
    app("constexpr uint16_t SIZE = %d;" % len(blob))
    app("constexpr uint8_t HEADER_SIZE = %d;" % HEADER_SIZE)
    app("constexpr uint8_t RECORD_SIZE = %d;" % RECORD_SIZE)
    app("constexpr uint8_t NODE_COUNT = %d;" % len(nodes))
    app("constexpr uint8_t WEAPON_COUNT = %d;" % len(WEAPON_NAMES))
    app("constexpr uint8_t MAT_SLOTS = %d;" % MAT_SLOTS)
    app("constexpr uint8_t NODE_NONE = 0x%02X;" % NODE_NONE)
    app("constexpr uint8_t FLAG_DIRECT = 0x%02X;" % FLAG_DIRECT)
    app("")
    app("// Node record field offsets.")
    app("constexpr uint8_t N_CLASS_OFF = 0;")
    app("constexpr uint8_t N_PARENT_OFF = 1;    // u8 node id, NODE_NONE = root")
    app("constexpr uint8_t N_FLAGS_OFF = 2;     // FLAG_DIRECT")
    app("constexpr uint8_t N_DMG_OFF = 3;")
    app("constexpr uint8_t N_SPD_OFF = 4;")
    app("constexpr uint8_t N_COST_OFF = 5;      // u16 upgrade cost")
    app("constexpr uint8_t N_DIRECTCOST_OFF = 7;  // u16 direct-forge cost")
    app("constexpr uint8_t N_MAT_OFF = 9;       // MAT_SLOTS x (itemIdx+1 u8, count u8)")
    app("constexpr uint8_t N_DIRECTMAT_OFF = 13;  // MAT_SLOTS x (itemIdx+1 u8, count u8)")
    app("constexpr uint8_t N_MAT_STRIDE = 2;")
    app("")
    app("// Weapon class ids; values mirror WeaponId in src/core/game.hpp.")
    for i, name in enumerate(WEAPON_NAMES):
        app("constexpr uint8_t WEAPON_%s = %d;" % (name.upper(), i))
    app("")
    app("// Node indices, in data order, with the cart record offsets.")
    for node in nodes:
        tag = node["id"].upper()
        app("constexpr uint8_t NODE_%s = %d;" % (tag, node["index"]))
        app("constexpr uint16_t NODE_%s_OFF = %d;" % (tag, HEADER_SIZE + node["index"] * RECORD_SIZE))
    app("")
    app("// Tree shape per node index: depth (root 0) and sibling ordinal. The FORGE")
    app("// row labels bake the prefixes from these at gen time; host tests read them.")
    app("constexpr uint8_t NODE_DEPTH[NODE_COUNT] = {%s};"
        % ", ".join(str(n["depth"]) for n in nodes))
    app("constexpr uint8_t NODE_BRANCH[NODE_COUNT] = {%s};"
        % ", ".join(str(n["branch"]) for n in nodes))
    app("")
    app("// Class first-node index: NODE_<CLASS>_FIRST + n walks the class nodes in")
    app("// data order (the FORGE/GEAR generated row blocks).")
    for entry in model["classes"]:
        app("constexpr uint8_t NODE_%s_FIRST = %d;"
            % (entry["name"].upper(), entry["firstNode"]))
    app("")
    app("}   // namespace forge")
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


def load_model(root):
    """Convenience for the other generators: strict compile or None."""
    errors = Errors()
    model = compile_model(errors, root)
    if model is None or errors.items:
        return None
    return model


def load_nodes(root):
    model = load_model(root)
    return None if model is None else model["nodes"]


def run(root, dump):
    errors = Errors()
    model = compile_model(errors, root)
    if model is None or errors.items:
        for item in errors.items:
            print("gen-forge: error: %s" % item, file=sys.stderr)
        print("gen-forge: FAIL (%d error%s)"
              % (len(errors.items), "" if len(errors.items) == 1 else "s"), file=sys.stderr)
        return 1
    nodes = model["nodes"]
    blob = pack_blob(errors, nodes)
    if blob is None or errors.items:
        for item in errors.items:
            print("gen-forge: error: %s" % item, file=sys.stderr)
        print("gen-forge: FAIL", file=sys.stderr)
        return 1

    if dump:
        for node in nodes:
            mats = " ".join("%s x%d" % (m["item"], m["count"]) for m in node["mats"])
            dmats = " ".join("%s x%d" % (m["item"], m["count"]) for m in node["directMats"])
            print("node %s: class %s parent %s direct %s cost %d [%s] directCost %d [%s] dmg %d spd %d"
                  % (node["id"], WEAPON_NAMES[node["class"]], node["parent"], node["direct"],
                     node["cost"], mats if mats else "-", node["directCost"], dmats if dmats else "-",
                     node["dmgMul"], node["spdMul"]))
        print("gen-forge: %d nodes, %d B blob" % (len(nodes), len(blob)))
        return 0

    wrote = set()
    if write_if_changed(os.path.join(root, BLOB_REL), blob):
        wrote.add(BLOB_REL)
    if write_if_changed(os.path.join(root, META_REL), emit_meta_header(model, blob)):
        wrote.add(META_REL)
    print("gen-forge: %d nodes, %d B blob (magic 0x%04X version %d)"
          % (len(nodes), len(blob), MAGIC, VERSION))
    for rel in (BLOB_REL, META_REL):
        print("gen-forge: %s%s" % (rel, "" if rel in wrote else " (unchanged)"))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=None, help="pipeline root holding data/ (default: repository root)")
    parser.add_argument("--dump", action="store_true", help="validate + list the compiled nodes; writes nothing")
    args = parser.parse_args(argv)
    root = os.path.abspath(args.root) if args.root else REPO_ROOT
    return run(root, args.dump)


if __name__ == "__main__":
    sys.exit(main())
