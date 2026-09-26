#!/usr/bin/env python3
"""Compile quest-def JSON into the packed FX blob + generated header.

    data/quests/*.json
        -> fxdata/tables/quests.bin        (packed blob; build intermediate)
        -> src/generated/quest_meta.hpp    (quest indices, offsets, enums)

Design: docs/quests-shops.md "Quests (content model)" (bead monhun-ardu-dlp.1,
hql.1: record v2). One QuestDef record per quest is read on device through
core/fxmem.hpp during the screen scan/render window (src/quest.hpp); the host
suite and the goal-accounting logic use the plain QuestDef struct
(src/quest_state.hpp).

Record v2 (9 B, little-endian, explicit u8/u16, no padding, fixed order):

    id u8, goalKind u8 (0 kill / 1 gather), target u8 (MonsterKind for kill /
    item idx for gather), need u8, rewardZenny u16, rewardItem u8
    (itemIdx + 1, 0 = none), rewardCount u8, unlockFlag u8

The kill target values mirror MonsterKind (MON_LUNGE..MON_POLE) so Game's
kill accounting can compare them directly. A gather target is validated against
the id list in data/items.json (source order == item index, so the packed
target is the item idx the runtime uses); a material reward is validated the
same way and packed as item idx + 1 (0 = none).

Optional `roomHint` (bead monhun-ardu-imx, docs/ui-design.md MAP screen): one of
the room ids in data/map.json. It is NOT packed into the blob; the generator
emits `QUEST_ROOM_HINT[QUEST_COUNT]` (u8 room index, sorted by id like
zone::ROOM_* in gen-zones, so it can index the MAP panel table on device) plus
`QUEST_ROOM_HINT_NONE` (0xFF). An absent key emits 0xFF; an invalid value is an
error.

Usage:
    python3 tools/gen-quests.py [--root DIR] [--dump]

    --root DIR  pipeline root holding data/ and src/generated (default: repo)
    --dump      validate + list the compiled quests on stdout; writes nothing
"""
import argparse
import json
import os
import re
import struct
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = "data/quests"
ITEMS_REL = "data/items.json"
MAP_REL = "data/map.json"
BLOB_REL = "fxdata/tables/quests.bin"
META_REL = "src/generated/quest_meta.hpp"

MAGIC = 0x5153   # 'S','Q' little-endian
VERSION = 2
FLAGS = 0
HEADER_SIZE = 8
RECORD_SIZE = 9
QUEST_MAX = 16

# `roomHint` sentinel (monhun-ardu-imx): no MAP marker for this quest.
ROOM_HINT_NONE = 0xFF

# Card copy bounds (ui.3): the pre-wrapped desc lines the card baker draws.
DESC_MAX_LINES = 4
DESC_MAX_LEN = 22

# Goal kinds; values mirror the packed goalKind byte + quests::GOAL_*.
GOAL_NAMES = ("kill", "gather")
GOAL_KILL = 0
GOAL_GATHER = 1

# Kill targets, index == MonsterKind (src/core/game.hpp). Keep in sync.
TARGET_NAMES = ("lunge", "sweep", "heavy", "ravager", "pole")

NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")

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


def read_int(errors, ctx, obj, key, lo, hi):
    value = obj.get(key) if isinstance(obj, dict) else None
    if not is_int(value):
        errors.add(ctx, "%s: expected an integer, got %r" % (key, value))
        return None
    if not lo <= value <= hi:
        errors.add(ctx, "%s: out of range %d..%d: %d" % (key, lo, hi, value))
        return None
    return value


def read_goal(errors, ctx, obj):
    value = obj.get("goalKind") if isinstance(obj, dict) else None
    if not isinstance(value, str) or value not in GOAL_NAMES:
        errors.add(ctx, "goalKind: unknown value %r (want one of %s)"
                   % (value, ", ".join(GOAL_NAMES)))
        return None
    return GOAL_NAMES.index(value)


def read_target(errors, ctx, obj, goal, item_ids):
    value = obj.get("target") if isinstance(obj, dict) else None
    if goal == GOAL_GATHER:
        if not isinstance(value, str) or value not in item_ids:
            errors.add(ctx, "target: unknown gather item %r (want an id from %s)"
                       % (value, ITEMS_REL))
            return None
        return item_ids.index(value)
    if not isinstance(value, str) or value not in TARGET_NAMES:
        errors.add(ctx, "target: unknown kill value %r (want one of %s)"
                   % (value, ", ".join(TARGET_NAMES)))
        return None
    return TARGET_NAMES.index(value)


def load_item_ids(errors, root):
    """The item id list from data/items.json, in source order (== item index).

    Validates gather targets and material rewards against the same table
    src/core/items.hpp uses; no second list is hardcoded here.
    """
    path = os.path.join(root, ITEMS_REL)
    doc = load_json(errors, path, ITEMS_REL)
    if doc is None:
        return None
    raw = doc.get("items") if isinstance(doc, dict) else None
    if not isinstance(raw, list) or not raw:
        errors.add(ITEMS_REL, "items: expected a non-empty array")
        return None
    ids = []
    for i, obj in enumerate(raw):
        item_id = obj.get("id") if isinstance(obj, dict) else None
        if not isinstance(item_id, str):
            errors.add("%s: items[%d]" % (ITEMS_REL, i), "id: expected a string")
            return None
        ids.append(item_id)
    return ids


def load_room_ids(errors, root):
    """The room id list from data/map.json, sorted by id == the packed room
    index (gen-zones assigns zone::ROOM_* by sorted id order).

    Validates a quest's optional `roomHint` against the same table the room
    graph uses; no second list is hardcoded here.
    """
    path = os.path.join(root, MAP_REL)
    doc = load_json(errors, path, MAP_REL)
    if doc is None:
        return None
    raw = doc.get("rooms") if isinstance(doc, dict) else None
    if not isinstance(raw, list) or not raw:
        errors.add(MAP_REL, "rooms: expected a non-empty array")
        return None
    ids = []
    for i, obj in enumerate(raw):
        room_id = obj.get("id") if isinstance(obj, dict) else None
        if not isinstance(room_id, str):
            errors.add("%s: rooms[%d]" % (MAP_REL, i), "id: expected a string")
            return None
        ids.append(room_id)
    return sorted(ids)


def read_room_hint(errors, ctx, obj, room_ids):
    """Optional `roomHint`: absent -> ROOM_HINT_NONE; present must be a known
    room id (data/map.json). Present-but-invalid is an error."""
    if not isinstance(obj, dict) or "roomHint" not in obj:
        return ROOM_HINT_NONE
    value = obj["roomHint"]
    if not isinstance(value, str) or value not in room_ids:
        errors.add(ctx, "roomHint: unknown room %r (want an id from %s)" % (value, MAP_REL))
        return None
    return room_ids.index(value)


def load_json(errors, path, rel):
    try:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    except OSError as exc:
        errors.add(rel, "cannot read file: %s" % exc)
    except ValueError as exc:
        errors.add(rel, "invalid JSON: %s" % exc)
    return None


def normalize_quest(errors, rel, name, obj, seen_ids, item_ids, room_ids):
    ctx = rel
    check_keys(errors, ctx, obj,
               {"id", "goalKind", "target", "need", "rewardZenny", "unlockFlag"},
               optional={"rewardItem", "rewardCount", "desc", "roomHint"})
    if not isinstance(obj, dict):
        return None
    stem = os.path.splitext(name)[0]
    if not NAME_RE.match(stem):
        errors.add(ctx, "file name: expected [a-z][a-z0-9_]*.json, got %r" % name)
    quest_id = read_int(errors, ctx, obj, "id", 0, QUEST_MAX - 1)
    if quest_id is not None:
        if quest_id in seen_ids:
            errors.add(ctx, "duplicate quest id %d" % quest_id)
        seen_ids.add(quest_id)
    goal = read_goal(errors, ctx, obj)
    if goal is not None:
        target = read_target(errors, ctx, obj, goal, item_ids)
    else:
        target = None
    need = read_int(errors, ctx, obj, "need", 1, 255)
    reward_zenny = read_int(errors, ctx, obj, "rewardZenny", 0, 65535)
    unlock = read_int(errors, ctx, obj, "unlockFlag", 0, 255)
    # rewardItem is optional. When present it names an item id (packed as
    # itemIdx + 1, 0 = none) and requires rewardCount 1..255; when absent
    # rewardCount must not be present either.
    has_item = "rewardItem" in obj if isinstance(obj, dict) else False
    has_count = "rewardCount" in obj if isinstance(obj, dict) else False
    reward_item = 0
    reward_count = 0
    if has_item:
        item_id = obj.get("rewardItem")
        if not isinstance(item_id, str) or item_id not in item_ids:
            errors.add(ctx, "rewardItem: unknown item %r (want an id from %s)"
                       % (item_id, ITEMS_REL))
        else:
            reward_item = item_ids.index(item_id) + 1
        if not has_count:
            errors.add(ctx, "missing key 'rewardCount'")
        else:
            reward_count = read_int(errors, ctx, obj, "rewardCount", 1, 255)
    elif has_count:
        errors.add(ctx, "rewardCount: requires rewardItem")
    room_hint = read_room_hint(errors, ctx, obj, room_ids)
    if None in (quest_id, goal, target, need, reward_zenny, unlock, room_hint):
        return None
    if has_item and (reward_item == 0 or reward_count is None):
        return None
    # `desc` is the pre-wrapped card copy (ui.3 card baker reads the JSON
    # directly); validated here, never packed into the mhQuests blob.
    desc = normalize_desc(errors, ctx, obj)
    return {"name": stem, "id": quest_id, "goalKind": goal, "target": target,
            "need": need, "rewardZenny": reward_zenny, "rewardItem": reward_item,
            "rewardCount": reward_count, "unlockFlag": unlock, "roomHint": room_hint,
            "desc": desc}


def normalize_desc(errors, ctx, obj):
    raw = obj.get("desc") if isinstance(obj, dict) else None
    if raw is None:
        return []
    if not isinstance(raw, list) or not raw or len(raw) > DESC_MAX_LINES:
        errors.add(ctx, "desc: expected 1..%d lines" % DESC_MAX_LINES)
        return []
    lines = []
    for i, line in enumerate(raw):
        if (not isinstance(line, str) or not line or len(line) > DESC_MAX_LEN
                or any(ord(ch) < 32 or ord(ch) > 126 for ch in line)):
            errors.add(ctx, "desc[%d]: expected 1..%d printable ASCII chars, got %r"
                       % (i, DESC_MAX_LEN, line))
            return []
        lines.append(line)
    return lines


def compile_model(errors, root):
    data_dir = os.path.join(root, DATA_DIR)
    if not os.path.isdir(data_dir):
        errors.add(DATA_DIR, "missing quests directory")
        return None
    item_ids = load_item_ids(errors, root)
    if item_ids is None:
        return None
    room_ids = load_room_ids(errors, root)
    if room_ids is None:
        return None
    names = sorted(name for name in os.listdir(data_dir) if name.endswith(".json"))
    if not names:
        errors.add(DATA_DIR, "no quest JSON files found")
        return None
    quests = []
    seen_ids = set()
    for name in names:
        rel = "%s/%s" % (DATA_DIR, name)
        obj = load_json(errors, os.path.join(data_dir, name), rel)
        if obj is None:
            continue
        quest = normalize_quest(errors, rel, name, obj, seen_ids, item_ids, room_ids)
        if quest is not None:
            quests.append(quest)
    if errors.items:
        return None
    quests.sort(key=lambda quest: (quest["id"], quest["name"]))
    return {"quests": quests, "roomIds": room_ids}


def pack_blob(errors, quests):
    count = len(quests)
    if count == 0 or count > QUEST_MAX:
        errors.add("data", "quest count %d outside 1..%d" % (count, QUEST_MAX))
        return None
    blob = bytearray(struct.pack("<HBBBBH", MAGIC, VERSION, FLAGS, count, 0, 0))
    for quest in quests:
        blob += struct.pack("<BBBBHBBB", quest["id"], quest["goalKind"],
                            quest["target"], quest["need"], quest["rewardZenny"],
                            quest["rewardItem"], quest["rewardCount"], quest["unlockFlag"])
    expected = HEADER_SIZE + RECORD_SIZE * count
    if len(blob) != expected:
        errors.add("data", "internal: blob is %d B, want %d" % (len(blob), expected))
        return None
    return bytes(blob)


def emit_meta_header(quests, blob, room_count):
    lines = []
    app = lines.append
    app("#pragma once")
    app("// Generated by tools/gen-quests.py -- do not edit.")
    app("//")
    app("// Quest data ABI (docs/quests-shops.md): header then one fixed 9 B")
    app("// QuestDef record per quest, ordered by id. src/quest.hpp reads this")
    app("// blob through core/fxmem.hpp during the screen scan/render window;")
    app("// src/quest_state.hpp holds the host-testable logic + struct.")
    app("")
    app("#include <stdint.h>")
    app("#include \"../core/progmem.hpp\"   // MH_PROGMEM: QUEST_ROOM_HINT lives in flash on AVR")
    app("")
    app("namespace quests {")
    app("")
    app("constexpr uint16_t MAGIC = 0x%04X;" % MAGIC)
    app("constexpr uint8_t VERSION = %d;" % VERSION)
    app("constexpr uint8_t FLAGS = 0x%02X;" % FLAGS)
    app("constexpr uint16_t SIZE = %d;" % len(blob))
    app("constexpr uint8_t HEADER_SIZE = %d;" % HEADER_SIZE)
    app("constexpr uint8_t RECORD_SIZE = %d;" % RECORD_SIZE)
    app("constexpr uint8_t QUEST_COUNT = %d;" % len(quests))
    app("")
    app("// Record field offsets (QuestDef: id, goalKind, target, need, rewardZenny,")
    app("// rewardItem, rewardCount, unlockFlag).")
    app("constexpr uint8_t DEF_ID_OFF = 0;")
    app("constexpr uint8_t DEF_GOAL_OFF = 1;")
    app("constexpr uint8_t DEF_TARGET_OFF = 2;")
    app("constexpr uint8_t DEF_NEED_OFF = 3;")
    app("constexpr uint8_t DEF_REWARD_ZENNY_OFF = 4;   // u16")
    app("constexpr uint8_t DEF_REWARD_ITEM_OFF = 6;    // itemIdx + 1, 0 = none")
    app("constexpr uint8_t DEF_REWARD_COUNT_OFF = 7;")
    app("constexpr uint8_t DEF_UNLOCK_OFF = 8;")
    app("")
    app("// Goal kinds; values mirror the packed goalKind byte.")
    for i, name in enumerate(GOAL_NAMES):
        app("constexpr uint8_t GOAL_%s = %d;" % (name.upper(), i))
    app("")
    app("// Kill target kinds; values mirror MonsterKind in src/core/game.hpp.")
    app("// A gather quest's target is instead an item index (data/items.json).")
    for i, name in enumerate(TARGET_NAMES):
        app("constexpr uint8_t TARGET_%s = %d;" % (name.upper(), i))
    app("")
    app("// MAP screen quest marker (monhun-ardu-imx): the room each quest points")
    app("// at, in quest index order (sorted by id). Values index the room order")
    app("// data/map.json is packed in (sorted by id == zone::ROOM_*, see")
    app("// generated/zone_meta.hpp); QUEST_ROOM_HINT_NONE = no marker. The hint")
    app("// is authored per quest (data/quests/*.json `roomHint`) and never packed")
    app("// into the mhQuests blob. Read it with mhPgmReadU8 (flash on AVR).")
    app("constexpr uint8_t QUEST_ROOM_HINT_NONE = 0x%02X;" % ROOM_HINT_NONE)
    app("constexpr uint8_t MAP_ROOM_COUNT = %d;" % room_count)
    hints = ", ".join("0x%02X" % quest["roomHint"] for quest in quests)
    app("MH_PROGMEM constexpr uint8_t QUEST_ROOM_HINT[QUEST_COUNT] = {%s};" % hints)
    app("")
    app("// Quest indices, sorted by id, with the cart record offsets the runtime uses.")
    for i, quest in enumerate(quests):
        name = quest["name"].upper()
        app("constexpr uint8_t QUEST_%s = %d;" % (name, i))
        app("constexpr uint16_t QUEST_%s_OFF = %d;" % (name, HEADER_SIZE + i * RECORD_SIZE))
    app("")
    app("}   // namespace quests")
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
            print("gen-quests: error: %s" % item, file=sys.stderr)
        print("gen-quests: FAIL (%d error%s)"
              % (len(errors.items), "" if len(errors.items) == 1 else "s"), file=sys.stderr)
        return 1
    quests = model["quests"]
    room_ids = model["roomIds"]
    blob = pack_blob(errors, quests)
    if blob is None or errors.items:
        for item in errors.items:
            print("gen-quests: error: %s" % item, file=sys.stderr)
        print("gen-quests: FAIL", file=sys.stderr)
        return 1

    if dump:
        for quest in quests:
            print("quest %s: id %d goal %s target %d need %d zenny %d item %d count %d unlock %d room %s"
                  % (quest["name"], quest["id"], GOAL_NAMES[quest["goalKind"]],
                     quest["target"], quest["need"], quest["rewardZenny"],
                     quest["rewardItem"], quest["rewardCount"], quest["unlockFlag"],
                     "none" if quest["roomHint"] == ROOM_HINT_NONE else room_ids[quest["roomHint"]]))
        print("gen-quests: %d quests, %d B blob, %d rooms" % (len(quests), len(blob), len(room_ids)))
        return 0

    wrote = set()
    if write_if_changed(os.path.join(root, BLOB_REL), blob):
        wrote.add(BLOB_REL)
    if write_if_changed(os.path.join(root, META_REL), emit_meta_header(quests, blob, len(room_ids))):
        wrote.add(META_REL)
    print("gen-quests: %d quests, %d B blob (magic 0x%04X version %d)"
          % (len(quests), len(blob), MAGIC, VERSION))
    for rel in (BLOB_REL, META_REL):
        print("gen-quests: %s%s" % (rel, "" if rel in wrote else " (unchanged)"))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=None, help="pipeline root holding data/ (default: repository root)")
    parser.add_argument("--dump", action="store_true", help="validate + list the quests; write nothing")
    args = parser.parse_args(argv)
    root = os.path.abspath(args.root) if args.root else REPO_ROOT
    return run(root, args.dump)


if __name__ == "__main__":
    sys.exit(main())
