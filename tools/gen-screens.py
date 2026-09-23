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

The runtime (src/screens.hpp) reads records through core/fxmem.hpp during the
render/scan window; the host suite uses plain row structs. Action/condition/
flag enum values are emitted here so the packer and the runtime cannot drift.

Usage:
    python3 tools/gen-screens.py [--root DIR] [--dump]

    --root DIR  pipeline root holding data/ and src/generated (default: repo)
    --dump      validate + list the compiled screens on stdout; writes nothing
"""
import argparse
import json
import os
import re
import struct
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = "data/screens"
BLOB_REL = "fxdata/tables/screens.bin"
META_REL = "src/generated/screen_meta.hpp"

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

# Append-only: new names go on the end so the emitted ACTION_*/COND_* ids keep
# matching the historical records (gs.1 added equip_armor/crafted).
ACTION_NAMES = ("leave", "buy_upgrade", "take_quest", "turn_in_quest",
                "none", "hunt", "open_quests", "open_smith", "craft_armor",
                "equip_weapon", "open_gear", "equip_armor")
COND_NAMES = ("always", "zenny", "flag", "tier", "quest", "upgrade", "armor",
              "crafted")
# hide_locked: reserved. zenny: draw the live save.zenny balance in the cost
# column instead of the row cost (dynamic value token, qs.4 hub display).
# skill: draw the cached live skill points (ScreenState::skillPoints) in the cost
# column plus an S/M tier letter next to it (gs.2 GEAR readout); `param` = the
# armor::SKILL_* index.
ROW_FLAGS = {"hide_locked": 0x01, "zenny": 0x02, "skill": 0x04}
# COND_UPGRADE param packs (unlockFlag << 4) | (weaponIdx << 2) | tier (see
# screen_state.hpp): unlock 0 = always, else 1-based quest whose done bit gates
# the tier; weapon 0..TIER_COUNT-1; tier 1..SCREEN_MAX_TIER (2 in data).
UPGRADE_TIER_MAX = 3

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
    if cond == COND_NAMES.index("armor") and param is not None:
        # param packs (slot << 5) | pieceIdx (see screen_state.hpp COND_ARMOR):
        # piece 0..31 into armor::ARMOR_<ID>, slot 0..2 (head/body/charm).
        slot = (param >> 5) & 3
        if action != ACTION_NAMES.index("craft_armor"):
            errors.add(ctx, "condition 'armor' needs a craft_armor action")
        if slot >= 3:
            errors.add(ctx, "param: armor slot must be 0..2, got %d" % slot)
    if cond == COND_NAMES.index("crafted") and param is not None:
        # param packs (slot << 5) | pieceIdx (see screen_state.hpp COND_CRAFTED
        # / ACTION_EQUIP_ARMOR): the GEAR row is live once the piece's crafted
        # bit is set, and A toggles it into that slot.
        slot = (param >> 5) & 3
        if action != ACTION_NAMES.index("equip_armor"):
            errors.add(ctx, "condition 'crafted' needs an equip_armor action")
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


def normalize_screen(errors, rel, name, obj, seen_ids):
    ctx = rel
    check_keys(errors, ctx, obj, {"id", "title", "rows"})
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
    raw_rows = obj.get("rows")
    if not isinstance(raw_rows, list) or not raw_rows:
        errors.add(ctx, "rows: expected a non-empty array")
        raw_rows = []
    if len(raw_rows) > SCREEN_MAX:
        errors.add(ctx, "rows: %d exceed the %d row limit" % (len(raw_rows), SCREEN_MAX))
    rows = []
    for i, row in enumerate(raw_rows):
        normalized = normalize_row(errors, "%s.rows[%d]" % (ctx, i), row)
        if normalized is not None:
            rows.append(normalized)
    if None in (screen_id, title):
        return None
    return {"name": stem, "id": screen_id, "title": title, "rows": rows}


def compile_model(errors, root):
    data_dir = os.path.join(root, DATA_DIR)
    if not os.path.isdir(data_dir):
        errors.add(DATA_DIR, "missing screens directory")
        return None
    names = sorted(name for name in os.listdir(data_dir) if name.endswith(".json"))
    if not names:
        errors.add(DATA_DIR, "no screen JSON files found")
        return None
    screens = []
    seen_ids = set()
    for name in names:
        rel = "%s/%s" % (DATA_DIR, name)
        obj = load_json(errors, os.path.join(data_dir, name), rel)
        if obj is None:
            continue
        screen = normalize_screen(errors, rel, name, obj, seen_ids)
        if screen is not None:
            screens.append(screen)
    if errors.items:
        return None
    screens.sort(key=lambda screen: (screen["id"], screen["name"]))
    return {"screens": screens}


def pack_blob(errors, screens):
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
    if len(blob) >= 65536:
        errors.add("data", "size limit: blob is %d B, offsets are u16" % len(blob))
        return None
    return {"blob": bytes(blob), "def_offsets": def_offsets, "rows_start": rows_start}


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
    app("constexpr uint8_t TIER_COUNT = %d;   // must match core/save.hpp SAVE_TIER_COUNT" % TIER_COUNT)
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
    for i, screen in enumerate(screens):
        name = screen["name"].upper()
        app("constexpr uint8_t SCREEN_%s = %d;" % (name, i))
        app("constexpr uint16_t SCREEN_%s_OFF = %d;" % (name, packed["def_offsets"][i]))
        app("constexpr uint8_t SCREEN_%s_ROWS = %d;" % (name, len(screen["rows"])))
        app("constexpr uint8_t SCREEN_%s_TITLE_LEN = %d;" % (name, len(screen["title"])))
        app("constexpr uint16_t SCREEN_%s_FIRST_ROW = %d;"
            % (name, packed["rows_start"] + screen["firstRowRel"]))
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


def run(root, dump):
    errors = Errors()
    model = compile_model(errors, root)
    if model is None or errors.items:
        for item in errors.items:
            print("gen-screens: error: %s" % item, file=sys.stderr)
        print("gen-screens: FAIL (%d error%s)"
              % (len(errors.items), "" if len(errors.items) == 1 else "s"), file=sys.stderr)
        return 1
    packed = pack_blob(errors, model["screens"])
    if packed is None or errors.items:
        for item in errors.items:
            print("gen-screens: error: %s" % item, file=sys.stderr)
        print("gen-screens: FAIL", file=sys.stderr)
        return 1
    screens = model["screens"]

    if dump:
        for screen in screens:
            print("screen %s: id %d title %r rows %d off %d"
                  % (screen["name"], screen["id"], screen["title"], len(screen["rows"]),
                     packed["def_offsets"][screens.index(screen)]))
            for row in screen["rows"]:
                print("    row %r cost %d action %s flags 0x%02X cond %s param %d"
                      % (row["label"], row["cost"], ACTION_NAMES[row["action"]],
                         row["flags"], COND_NAMES[row["cond"]], row["param"]))
        print("gen-screens: %d screens, %d rows, %d B blob"
              % (len(screens), sum(len(s["rows"]) for s in screens), len(packed["blob"])))
        return 0

    wrote = set()
    if write_if_changed(os.path.join(root, BLOB_REL), packed["blob"]):
        wrote.add(BLOB_REL)
    if write_if_changed(os.path.join(root, META_REL), emit_meta_header(model, packed)):
        wrote.add(META_REL)
    print("gen-screens: %d screens, %d rows, %d B blob (magic 0x%04X version %d)"
          % (len(screens), sum(len(s["rows"]) for s in screens), len(packed["blob"]), MAGIC, VERSION))
    for rel in (BLOB_REL, META_REL):
        print("gen-screens: %s%s" % (rel, "" if rel in wrote else " (unchanged)"))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=None, help="pipeline root holding data/ (default: repository root)")
    parser.add_argument("--dump", action="store_true", help="validate + list the screens; write nothing")
    args = parser.parse_args(argv)
    root = os.path.abspath(args.root) if args.root else REPO_ROOT
    return run(root, args.dump)


if __name__ == "__main__":
    sys.exit(main())
