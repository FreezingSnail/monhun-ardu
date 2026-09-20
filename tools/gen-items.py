#!/usr/bin/env python3
"""Compile the item table JSON into the packed FX blob + generated headers.

    data/items.json
        -> fxdata/tables/items.bin          (packed blob; build intermediate)
        -> src/generated/items_data.hpp     (host plain struct + array)
        -> src/generated/items_meta.hpp     (ABI: VERSION/SIZE, offsets, ids)
        -> src/generated/items_expect.hpp   (record sizes, spot values, sha256)

Design: bead monhun-ardu-prg.2 (epic monhun-ardu-prg). One record per item:
`id` (a [a-z][a-z0-9_]* symbol, also the inventory index), `kind`
(consumable/material), `heal`, `stam`, `sell`. The runtime (src/core/items.hpp)
reads the blob through core/fxmem.hpp on AVR and the host array off it; the
inventory is a fixed Game::items[ITEM_COUNT] indexed by the generated ITEM_*
constants (cap ITEM_MAX). Herb is the first authored item and keeps item index
0, so the feel.22 gather/eat path is unchanged.

The gather vocabulary (tools/gen-zones.py GATHER_ITEMS) is a subset of these
ids; gen-zones reads this same file so `zone::GATHER_<NAME>` stays the item
index + 1 (0 == not a gather node).

Blob layout (little-endian, explicit u8/u16, no padding, fixed order):

    header   8 B  magic u16 0x5449, version u8, flags u8, count u8,
                   reserved u8, reserved u16
    item     5 B  kind u8, heal u8, stam u8, sell u16

Records keep data/items.json source order (item index == record index).

Usage:
    python3 tools/gen-items.py [--root DIR] [--dump]

    --root DIR  pipeline root holding data/ and src/generated (default: repo)
    --dump      validate + list the compiled items on stdout; writes nothing
"""
import argparse
import hashlib
import json
import os
import re
import struct
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_REL = "data/items.json"
BLOB_REL = "fxdata/tables/items.bin"
DATA_HPP_REL = "src/generated/items_data.hpp"
META_HPP_REL = "src/generated/items_meta.hpp"
EXPECT_HPP_REL = "src/generated/items_expect.hpp"

MAGIC = 0x5449   # 'I','T' little-endian
VERSION = 1
FLAGS = 0
HEADER_SIZE = 8
ITEM_SIZE = 5
ITEM_MAX = 16    # Game::items[] cap (one u8 per item id)

# Item kinds; values mirror the item::KIND_* constants and the packed u8.
KINDS = ("consumable", "material")

ID_RE = re.compile(r"^[a-z][a-z0-9_]*$")
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
    if not ID_RE.match(value):
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


def read_enum(errors, ctx, obj, key, table, default=_MISSING):
    if not isinstance(obj, dict) or key not in obj:
        if default is _MISSING:
            return None
        return default
    value = obj[key]
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


def normalize_item(errors, ctx, obj, seen_ids):
    check_keys(errors, ctx, obj, {"id", "kind", "heal", "stam", "sell"})
    if not isinstance(obj, dict):
        return None
    item_id = read_id(errors, ctx, obj, "id", seen_ids)
    kind = read_enum(errors, ctx, obj, "kind", KINDS)
    heal = read_int(errors, ctx, obj, "heal", 0, 255)
    stam = read_int(errors, ctx, obj, "stam", 0, 255)
    sell = read_int(errors, ctx, obj, "sell", 0, 65535)
    if None in (item_id, kind, heal, stam, sell):
        return None
    return {"id": item_id, "kind": kind, "heal": heal, "stam": stam, "sell": sell}


def compile_model(errors, root):
    path = os.path.join(root, DATA_REL)
    if not os.path.isfile(path):
        errors.add(DATA_REL, "missing item file")
        return None
    doc = load_json(errors, path, DATA_REL)
    if doc is None:
        return None
    check_keys(errors, DATA_REL, doc, {"version", "items"})
    read_int(errors, DATA_REL, doc, "version", 1, 1)
    raw_items = doc.get("items")
    if not isinstance(raw_items, list) or not raw_items:
        errors.add(DATA_REL, "items: expected a non-empty array")
        return None
    if len(raw_items) > ITEM_MAX:
        errors.add(DATA_REL, "size limit: %d items exceed the %d item cap" % (len(raw_items), ITEM_MAX))
    items = []
    seen_ids = set()
    for i, obj in enumerate(raw_items):
        item = normalize_item(errors, "%s: items[%d]" % (DATA_REL, i), obj, seen_ids)
        if item is not None:
            items.append(item)
    if errors.items:
        return None
    return {"items": items}


def pack_blob(errors, items):
    count = len(items)
    if count == 0 or count > 255:
        errors.add("data", "item count %d outside 1..255" % count)
        return None
    blob = bytearray(struct.pack("<HBBBBH", MAGIC, VERSION, FLAGS, count, 0, 0))
    for item in items:
        blob += struct.pack("<BBBH", item["kind"], item["heal"], item["stam"], item["sell"])
    expected = HEADER_SIZE + ITEM_SIZE * count
    if len(blob) != expected:
        errors.add("data", "internal: blob is %d B, want %d" % (len(blob), expected))
        return None
    return bytes(blob)


def emit_data_header(items, blob):
    lines = []
    app = lines.append
    app("#pragma once")
    app("// Generated by tools/gen-items.py -- do not edit.")
    app("//")
    app("// Host-side plain struct + array mirroring the packed mhItems blob. Field")
    app("// order matches the blob byte order; the host reads members directly, so")
    app("// host struct padding is irrelevant. The device reads the blob with the")
    app("// offsets in items_meta.hpp instead.")
    app("")
    app("#include <array>")
    app("#include <stdint.h>")
    app("")
    app("namespace item_data {")
    app("")
    app("constexpr uint8_t VERSION = %d;" % VERSION)
    app("constexpr uint16_t BLOB_SIZE = %d;" % len(blob))
    app("")
    app("struct Item {")
    app("    uint8_t kind;   // item::KIND_CONSUMABLE / KIND_MATERIAL")
    app("    uint8_t heal;")
    app("    uint8_t stam;")
    app("    uint16_t sell;")
    app("};")
    app("")
    app("inline constexpr std::array<Item, %d> ITEMS = {{" % len(items))
    for item in items:
        app("    {%d, %d, %d, %d},   // %s" % (item["kind"], item["heal"], item["stam"],
                                               item["sell"], item["id"]))
    app("}};")
    app("")
    app("}   // namespace item_data")
    app("")
    return "\n".join(lines)


def emit_meta_header(items, blob):
    lines = []
    app = lines.append
    app("#pragma once")
    app("// Generated by tools/gen-items.py -- do not edit.")
    app("//")
    app("// Item table ABI (bead monhun-ardu-prg.2): 8 B header + one fixed 5 B")
    app("// record per item, little-endian, no padding. Offsets are absolute byte")
    app("// offsets into the mhItems raw_t section (fxdata/fxdata.txt); on AVR the")
    app("// loader reads mhItems + off through core/fxmem.hpp (src/core/items.hpp).")
    app("// Item index == record index == Game::items[] slot; ITEM_MAX caps the")
    app("// inventory.")
    app("")
    app("#include <stdint.h>")
    app("")
    app("namespace item {")
    app("")
    app("constexpr uint16_t MAGIC = 0x%04X;" % MAGIC)
    app("constexpr uint8_t VERSION = %d;" % VERSION)
    app("constexpr uint8_t FLAGS = 0x%02X;" % FLAGS)
    app("constexpr uint16_t SIZE = %d;" % len(blob))
    app("constexpr uint8_t HEADER_SIZE = %d;" % HEADER_SIZE)
    app("constexpr uint8_t ITEM_SIZE = %d;" % ITEM_SIZE)
    app("constexpr uint8_t ITEM_COUNT = %d;" % len(items))
    app("constexpr uint8_t ITEM_MAX = %d;" % ITEM_MAX)
    app("constexpr uint16_t ITEMS_OFF = %d;" % HEADER_SIZE)
    app("")
    app("// Item kinds; values mirror the packed kind byte.")
    for i, name in enumerate(KINDS):
        app("constexpr uint8_t KIND_%s = %d;" % (name.upper(), i))
    app("")
    app("// Record field offsets (kind, heal, stam, sell).")
    app("constexpr uint8_t ITEM_KIND_OFF = 0;")
    app("constexpr uint8_t ITEM_HEAL_OFF = 1;")
    app("constexpr uint8_t ITEM_STAM_OFF = 2;")
    app("constexpr uint8_t ITEM_SELL_OFF = 3;   // u16")
    app("")
    app("// Item indices + blob record offsets, in data/items.json source order.")
    for i, item in enumerate(items):
        name = item["id"].upper()
        app("constexpr uint8_t ITEM_%s = %d;" % (name, i))
        app("constexpr uint16_t ITEM_%s_OFF = %d;" % (name, HEADER_SIZE + i * ITEM_SIZE))
    app("")
    app("}   // namespace item")
    app("")
    return "\n".join(lines)


def emit_expect_header(items, blob):
    lines = []
    app = lines.append
    digest = hashlib.sha256(blob).hexdigest()
    app("#pragma once")
    app("// Generated by tools/gen-items.py -- do not edit.")
    app("//")
    app("// Byte-level expectations for the host table test and the Ardens loader")
    app("// test: record sizes, pinned spot values and the blob sha256.")
    app("")
    app("#include <stdint.h>")
    app("")
    app("namespace item_expect {")
    app("")
    app("constexpr uint16_t BLOB_SIZE = %d;" % len(blob))
    app("constexpr uint8_t ITEM_SIZE = %d;" % ITEM_SIZE)
    app("constexpr uint8_t ITEM_COUNT = %d;" % len(items))
    app("")
    for item in items:
        name = item["id"].upper()
        app("constexpr uint8_t ITEM_%s_KIND = %d;" % (name, item["kind"]))
        app("constexpr uint8_t ITEM_%s_HEAL = %d;" % (name, item["heal"]))
        app("constexpr uint8_t ITEM_%s_STAM = %d;" % (name, item["stam"]))
        app("constexpr uint16_t ITEM_%s_SELL = %d;" % (name, item["sell"]))
    app("")
    app("// sha256 of fxdata/tables/items.bin: %s" % digest)
    app("constexpr uint8_t BLOB_SHA256[32] = {")
    raw = hashlib.sha256(blob).digest()
    for offset in range(0, 32, 8):
        app("    " + ", ".join("0x%02X" % byte for byte in raw[offset:offset + 8]) + ",")
    app("};")
    app("")
    app("}   // namespace item_expect")
    app("")
    return "\n".join(lines)


def dump_model(items, blob):
    for item in items:
        print("item %s: kind %s heal %d stam %d sell %d" % (
            item["id"], KINDS[item["kind"]], item["heal"], item["stam"], item["sell"]))
    print("gen-items: %d items, %d B blob" % (len(items), len(blob)))


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
    items = model["items"] if model is not None and not errors.items else None
    blob = pack_blob(errors, items) if items is not None and not errors.items else None
    if errors.items:
        for item in errors.items:
            print("gen-items: error: %s" % item, file=sys.stderr)
        print("gen-items: FAIL (%d error%s)"
              % (len(errors.items), "" if len(errors.items) == 1 else "s"), file=sys.stderr)
        return 1
    if dump:
        dump_model(items, blob)
        return 0

    changed = []
    if write_if_changed(os.path.join(root, BLOB_REL), blob):
        changed.append(BLOB_REL)
    if write_if_changed(os.path.join(root, DATA_HPP_REL), emit_data_header(items, blob)):
        changed.append(DATA_HPP_REL)
    if write_if_changed(os.path.join(root, META_HPP_REL), emit_meta_header(items, blob)):
        changed.append(META_HPP_REL)
    if write_if_changed(os.path.join(root, EXPECT_HPP_REL), emit_expect_header(items, blob)):
        changed.append(EXPECT_HPP_REL)
    print("gen-items: %d items, %d B blob (magic 0x%04X version %d)"
          % (len(items), len(blob), MAGIC, VERSION))
    for rel in (BLOB_REL, DATA_HPP_REL, META_HPP_REL, EXPECT_HPP_REL):
        print("gen-items: %s%s" % (rel, "" if rel in changed else " (unchanged)"))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=None, help="pipeline root holding data/ (default: repository root)")
    parser.add_argument("--dump", action="store_true", help="validate + list the compiled items; write nothing")
    args = parser.parse_args(argv)
    root = os.path.abspath(args.root) if args.root else REPO_ROOT
    return run(root, args.dump)


if __name__ == "__main__":
    sys.exit(main())
