#!/usr/bin/env python3
"""Compile smith upgrade-def JSON into the packed FX blob + generated header.

    data/smith/*.json
        -> fxdata/tables/smith.bin        (packed blob; build intermediate)
        -> src/generated/smith_meta.hpp   (def indices, offsets, enums)

Design: docs/quests-shops.md "Smith (content model)" (bead monhun-ardu-4ug,
qs.3). One UpgradeDef record per tier is read on device through
core/fxmem.hpp during the screen scan/render window (src/smith.hpp); the host
suite and the screen logic use the plain UpgradeDef struct
(src/upgrade_state.hpp). Two tiers per weapon, three weapons.

Blob layout (little-endian, explicit u8/u16, no padding, fixed order):

    header     8 B  magic u16 0x534D, version u8, flags u8, defCount u8,
                    reserved u8, reserved u16
    records    7 B each, ordered by (weaponIdx, tier): weaponIdx u8, tier u8,
                    cost u16, dmgMul u8, spdMul u8, unlockFlag u8

Usage:
    python3 tools/gen-smith.py [--root DIR] [--dump]

    --root DIR  pipeline root holding data/ and src/generated (default: repo)
    --dump      validate + list the compiled upgrades on stdout; writes nothing
"""
import argparse
import json
import os
import re
import struct
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = "data/smith"
BLOB_REL = "fxdata/tables/smith.bin"
META_REL = "src/generated/smith_meta.hpp"

MAGIC = 0x534D   # 'M','S' little-endian
VERSION = 1
FLAGS = 0
HEADER_SIZE = 8
RECORD_SIZE = 7
UPGRADE_MAX = 32
TIER_MAX = 2   # two tiers per weapon (docs/quests-shops.md)

# Weapon indices, index == WeaponId in src/core/game.hpp. Keep in sync.
WEAPON_NAMES = ("sword", "flail", "gun")

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


def read_weapon(errors, ctx, obj):
    value = obj.get("weapon") if isinstance(obj, dict) else None
    if not isinstance(value, str) or value not in WEAPON_NAMES:
        errors.add(ctx, "weapon: unknown value %r (want one of %s)"
                   % (value, ", ".join(WEAPON_NAMES)))
        return None
    return WEAPON_NAMES.index(value)


def load_json(errors, path, rel):
    try:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    except OSError as exc:
        errors.add(rel, "cannot read file: %s" % exc)
    except ValueError as exc:
        errors.add(rel, "invalid JSON: %s" % exc)
    return None


def normalize_upgrade(errors, rel, name, obj, seen_keys):
    ctx = rel
    check_keys(errors, ctx, obj, {"weapon", "tier", "cost", "dmgMul", "spdMul", "unlockFlag"})
    if not isinstance(obj, dict):
        return None
    stem = os.path.splitext(name)[0]
    if not NAME_RE.match(stem):
        errors.add(ctx, "file name: expected [a-z][a-z0-9_]*.json, got %r" % name)
    weapon = read_weapon(errors, ctx, obj)
    tier = read_int(errors, ctx, obj, "tier", 1, TIER_MAX)
    cost = read_int(errors, ctx, obj, "cost", 0, 65535)
    dmg = read_int(errors, ctx, obj, "dmgMul", 1, 255)
    spd = read_int(errors, ctx, obj, "spdMul", 1, 255)
    unlock = read_int(errors, ctx, obj, "unlockFlag", 0, 255)
    if weapon is not None and tier is not None:
        key = (weapon, tier)
        if key in seen_keys:
            errors.add(ctx, "duplicate upgrade for %s tier %d" % (WEAPON_NAMES[weapon], tier))
        seen_keys.add(key)
    if None in (weapon, tier, cost, dmg, spd, unlock):
        return None
    return {"name": stem, "weapon": weapon, "tier": tier, "cost": cost,
            "dmgMul": dmg, "spdMul": spd, "unlockFlag": unlock}


def compile_model(errors, root):
    data_dir = os.path.join(root, DATA_DIR)
    if not os.path.isdir(data_dir):
        errors.add(DATA_DIR, "missing smith directory")
        return None
    names = sorted(name for name in os.listdir(data_dir) if name.endswith(".json"))
    if not names:
        errors.add(DATA_DIR, "no smith JSON files found")
        return None
    upgrades = []
    seen_keys = set()
    for name in names:
        rel = "%s/%s" % (DATA_DIR, name)
        obj = load_json(errors, os.path.join(data_dir, name), rel)
        if obj is None:
            continue
        up = normalize_upgrade(errors, rel, name, obj, seen_keys)
        if up is not None:
            upgrades.append(up)
    if errors.items:
        return None

    # Two tiers per weapon: every weapon present must supply tiers 1..TIER_MAX.
    for weapon in sorted({up["weapon"] for up in upgrades}):
        tiers = sorted(up["tier"] for up in upgrades if up["weapon"] == weapon)
        want = list(range(1, TIER_MAX + 1))
        if tiers != want:
            errors.add(DATA_DIR, "weapon %s: tiers %s, want %s"
                       % (WEAPON_NAMES[weapon], tiers, want))
    if errors.items:
        return None
    upgrades.sort(key=lambda up: (up["weapon"], up["tier"], up["name"]))
    return {"upgrades": upgrades}


def pack_blob(errors, upgrades):
    count = len(upgrades)
    if count == 0 or count > UPGRADE_MAX:
        errors.add("data", "upgrade count %d outside 1..%d" % (count, UPGRADE_MAX))
        return None
    blob = bytearray(struct.pack("<HBBBBH", MAGIC, VERSION, FLAGS, count, 0, 0))
    for up in upgrades:
        blob += struct.pack("<BBHBBB", up["weapon"], up["tier"], up["cost"],
                            up["dmgMul"], up["spdMul"], up["unlockFlag"])
    expected = HEADER_SIZE + RECORD_SIZE * count
    if len(blob) != expected:
        errors.add("data", "internal: blob is %d B, want %d" % (len(blob), expected))
        return None
    return bytes(blob)


def emit_meta_header(upgrades, blob):
    lines = []
    app = lines.append
    app("#pragma once")
    app("// Generated by tools/gen-smith.py -- do not edit.")
    app("//")
    app("// Smith upgrade data ABI (docs/quests-shops.md): header then one fixed")
    app("// 7 B UpgradeDef record per tier, ordered by (weaponIdx, tier).")
    app("// src/smith.hpp reads this blob through core/fxmem.hpp during the")
    app("// screen scan/render window; src/upgrade_state.hpp holds the")
    app("// host-testable struct + integer-percent multiplier math.")
    app("")
    app("#include <stdint.h>")
    app("")
    app("namespace smith {")
    app("")
    app("constexpr uint16_t MAGIC = 0x%04X;" % MAGIC)
    app("constexpr uint8_t VERSION = %d;" % VERSION)
    app("constexpr uint8_t FLAGS = 0x%02X;" % FLAGS)
    app("constexpr uint16_t SIZE = %d;" % len(blob))
    app("constexpr uint8_t HEADER_SIZE = %d;" % HEADER_SIZE)
    app("constexpr uint8_t RECORD_SIZE = %d;" % RECORD_SIZE)
    app("constexpr uint8_t UPGRADE_COUNT = %d;" % len(upgrades))
    app("constexpr uint8_t TIER_COUNT = %d;" % TIER_MAX)
    app("constexpr uint8_t WEAPON_COUNT = %d;" % len(WEAPON_NAMES))
    app("")
    app("// Record field offsets (UpgradeDef: weaponIdx, tier, cost, dmgMul, spdMul, unlockFlag).")
    app("constexpr uint8_t UPG_WEAPON_OFF = 0;")
    app("constexpr uint8_t UPG_TIER_OFF = 1;")
    app("constexpr uint8_t UPG_COST_OFF = 2;   // u16")
    app("constexpr uint8_t UPG_DMG_OFF = 4;")
    app("constexpr uint8_t UPG_SPD_OFF = 5;")
    app("constexpr uint8_t UPG_UNLOCK_OFF = 6;")
    app("")
    app("// Weapon indices; values mirror WeaponId in src/core/game.hpp.")
    for i, name in enumerate(WEAPON_NAMES):
        app("constexpr uint8_t WEAPON_%s = %d;" % (name.upper(), i))
    app("")
    app("// Upgrade indices, sorted by (weapon, tier), with the cart record offsets.")
    for i, up in enumerate(upgrades):
        name = up["name"].upper()
        app("constexpr uint8_t UPG_%s = %d;" % (name, i))
        app("constexpr uint16_t UPG_%s_OFF = %d;" % (name, HEADER_SIZE + i * RECORD_SIZE))
    app("")
    app("}   // namespace smith")
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
            print("gen-smith: error: %s" % item, file=sys.stderr)
        print("gen-smith: FAIL (%d error%s)"
              % (len(errors.items), "" if len(errors.items) == 1 else "s"), file=sys.stderr)
        return 1
    upgrades = model["upgrades"]
    blob = pack_blob(errors, upgrades)
    if blob is None or errors.items:
        for item in errors.items:
            print("gen-smith: error: %s" % item, file=sys.stderr)
        print("gen-smith: FAIL", file=sys.stderr)
        return 1

    if dump:
        for up in upgrades:
            print("upgrade %s: weapon %s tier %d cost %d dmg %d spd %d unlock %d"
                  % (up["name"], WEAPON_NAMES[up["weapon"]], up["tier"], up["cost"],
                     up["dmgMul"], up["spdMul"], up["unlockFlag"]))
        print("gen-smith: %d upgrades, %d B blob" % (len(upgrades), len(blob)))
        return 0

    wrote = set()
    if write_if_changed(os.path.join(root, BLOB_REL), blob):
        wrote.add(BLOB_REL)
    if write_if_changed(os.path.join(root, META_REL), emit_meta_header(upgrades, blob)):
        wrote.add(META_REL)
    print("gen-smith: %d upgrades, %d B blob (magic 0x%04X version %d)"
          % (len(upgrades), len(blob), MAGIC, VERSION))
    for rel in (BLOB_REL, META_REL):
        print("gen-smith: %s%s" % (rel, "" if rel in wrote else " (unchanged)"))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=None, help="pipeline root holding data/ (default: repository root)")
    parser.add_argument("--dump", action="store_true", help="validate + list the upgrades; write nothing")
    args = parser.parse_args(argv)
    root = os.path.abspath(args.root) if args.root else REPO_ROOT
    return run(root, args.dump)


if __name__ == "__main__":
    sys.exit(main())
