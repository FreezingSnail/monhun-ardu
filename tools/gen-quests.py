#!/usr/bin/env python3
"""Compile quest-def JSON into the packed FX blob + generated header.

    data/quests/*.json
        -> fxdata/tables/quests.bin        (packed blob; build intermediate)
        -> src/generated/quest_meta.hpp    (quest indices, offsets, enums)

Design: docs/quests-shops.md "Quests (content model)" (bead monhun-ardu-me6,
qs.2). One QuestDef record per quest is read on device through core/fxmem.hpp
during the screen scan/render window (src/quest.hpp); the host suite and the
kill-accounting logic use the plain QuestDef struct (src/quest_state.hpp). The
targetKind values mirror MonsterKind (MON_LUNGE..MON_RAVAGER) so Game's kill
accounting can compare them directly.

Blob layout (little-endian, explicit u8/u16, no padding, fixed order):

    header     8 B  magic u16 0x5153, version u8, flags u8, questCount u8,
                    reserved u8, reserved u16
    records    6 B each, ordered by quest id: id u8, targetKind u8, need u8,
                    reward u16, unlockFlag u8

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
BLOB_REL = "fxdata/tables/quests.bin"
META_REL = "src/generated/quest_meta.hpp"

MAGIC = 0x5153   # 'S','Q' little-endian
VERSION = 1
FLAGS = 0
HEADER_SIZE = 8
RECORD_SIZE = 6
QUEST_MAX = 16

# Target kinds, index == MonsterKind (src/core/game.hpp). Keep in sync.
TARGET_NAMES = ("lunge", "sweep", "heavy", "ravager")

NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")

_MISSING = object()


class Errors:
    def __init__(self):
        self.items = []

    def add(self, ctx, message):
        self.items.append("%s: %s" % (ctx, message))


def is_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


def check_keys(errors, ctx, obj, required):
    if not isinstance(obj, dict):
        errors.add(ctx, "expected an object")
        return False
    for key in sorted(obj):
        if key not in required:
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


def read_target(errors, ctx, obj):
    value = obj.get("targetKind") if isinstance(obj, dict) else None
    if not isinstance(value, str) or value not in TARGET_NAMES:
        errors.add(ctx, "targetKind: unknown value %r (want one of %s)"
                   % (value, ", ".join(TARGET_NAMES)))
        return None
    return TARGET_NAMES.index(value)


def load_json(errors, path, rel):
    try:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    except OSError as exc:
        errors.add(rel, "cannot read file: %s" % exc)
    except ValueError as exc:
        errors.add(rel, "invalid JSON: %s" % exc)
    return None


def normalize_quest(errors, rel, name, obj, seen_ids):
    ctx = rel
    check_keys(errors, ctx, obj, {"id", "targetKind", "need", "reward", "unlockFlag"})
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
    target = read_target(errors, ctx, obj)
    need = read_int(errors, ctx, obj, "need", 1, 255)
    reward = read_int(errors, ctx, obj, "reward", 0, 65535)
    unlock = read_int(errors, ctx, obj, "unlockFlag", 0, 255)
    if None in (quest_id, target, need, reward, unlock):
        return None
    return {"name": stem, "id": quest_id, "targetKind": target, "need": need,
            "reward": reward, "unlockFlag": unlock}


def compile_model(errors, root):
    data_dir = os.path.join(root, DATA_DIR)
    if not os.path.isdir(data_dir):
        errors.add(DATA_DIR, "missing quests directory")
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
        quest = normalize_quest(errors, rel, name, obj, seen_ids)
        if quest is not None:
            quests.append(quest)
    if errors.items:
        return None
    quests.sort(key=lambda quest: (quest["id"], quest["name"]))
    return {"quests": quests}


def pack_blob(errors, quests):
    count = len(quests)
    if count == 0 or count > QUEST_MAX:
        errors.add("data", "quest count %d outside 1..%d" % (count, QUEST_MAX))
        return None
    blob = bytearray(struct.pack("<HBBBBH", MAGIC, VERSION, FLAGS, count, 0, 0))
    for quest in quests:
        blob += struct.pack("<BBBH B", quest["id"], quest["targetKind"],
                            quest["need"], quest["reward"], quest["unlockFlag"])
    expected = HEADER_SIZE + RECORD_SIZE * count
    if len(blob) != expected:
        errors.add("data", "internal: blob is %d B, want %d" % (len(blob), expected))
        return None
    return bytes(blob)


def emit_meta_header(quests, blob):
    lines = []
    app = lines.append
    app("#pragma once")
    app("// Generated by tools/gen-quests.py -- do not edit.")
    app("//")
    app("// Quest data ABI (docs/quests-shops.md): header then one fixed 6 B")
    app("// QuestDef record per quest, ordered by id. src/quest.hpp reads this")
    app("// blob through core/fxmem.hpp during the screen scan/render window;")
    app("// src/quest_state.hpp holds the host-testable logic + struct.")
    app("")
    app("#include <stdint.h>")
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
    app("// Record field offsets (QuestDef: id, targetKind, need, reward, unlockFlag).")
    app("constexpr uint8_t DEF_ID_OFF = 0;")
    app("constexpr uint8_t DEF_TARGET_OFF = 1;")
    app("constexpr uint8_t DEF_NEED_OFF = 2;")
    app("constexpr uint8_t DEF_REWARD_OFF = 3;   // u16")
    app("constexpr uint8_t DEF_UNLOCK_OFF = 5;")
    app("")
    app("// Target kinds; values mirror MonsterKind in src/core/game.hpp.")
    for i, name in enumerate(TARGET_NAMES):
        app("constexpr uint8_t TARGET_%s = %d;" % (name.upper(), i))
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
    blob = pack_blob(errors, quests)
    if blob is None or errors.items:
        for item in errors.items:
            print("gen-quests: error: %s" % item, file=sys.stderr)
        print("gen-quests: FAIL", file=sys.stderr)
        return 1

    if dump:
        for quest in quests:
            print("quest %s: id %d target %s need %d reward %d unlock %d"
                  % (quest["name"], quest["id"], TARGET_NAMES[quest["targetKind"]],
                     quest["need"], quest["reward"], quest["unlockFlag"]))
        print("gen-quests: %d quests, %d B blob" % (len(quests), len(blob)))
        return 0

    wrote = set()
    if write_if_changed(os.path.join(root, BLOB_REL), blob):
        wrote.add(BLOB_REL)
    if write_if_changed(os.path.join(root, META_REL), emit_meta_header(quests, blob)):
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
