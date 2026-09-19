#!/usr/bin/env python3
"""Compile the room-graph JSON into the packed FX blob + generated headers.

    data/map.json
        -> images/maps/<room>_<W>x<H>.png      (placeholder art only; see note)
        -> fxdata/maps/Sprites.txt             (room layer arrays; FX image section)
        -> fxdata/tables/zones.bin             (packed graph blob; build intermediate)
        -> src/generated/zone_data.hpp         (host plain structs + arrays)
        -> src/generated/zone_meta.hpp         (VERSION/SIZE, offsets, ids)

Design: docs/map-zones.md (epic monhun-ardu-fie, bead .3). Handles room graphs
of any size: a room is a stored background image plus props, named spawns, door
rects, heal rects and an optional monster. Edges are implicit in doors; a door
target is either another room id or the reserved "menu" (exit to the opening
menu). The runtime (fie.4) reads the blob through core/fxmem.hpp; the render
path (fie.5) blits the room image layers using the baked per-room FX offset
emitted here.

Blob layout (little-endian, explicit u8/u16, no padding, fixed order):

    header   16 B  magic u16 0x5A52, version u8, flags u8,
                   rooms u16, doors u16, spawns u16, props u16, heals u16,
                   reserved u16
    room     18 B  w u16, h u16, firstDoor u16, doorCount u8,
                   firstSpawn u16, spawnCount u8, firstProp u16, propCount u8,
                   firstHeal u16, healCount u8, monsterKind u8, monsterSpawn u8
    door     10 B  x u16, y u16, w u16, h u16, toRoom u8, toSpawn u8
    spawn     4 B  x u16, y u16
    prop      9 B  type u8, x u16, y u16, sheet u8, frame u8, w u8, h u8
    heal      6 B  x u16, y u16, w u8, h u8

Rooms sort by id; spawns sort by name inside a room; doors/props/heals keep
source order inside a room. Room image height must be a multiple of 8 (the
SSD1306 page-major layer format stores one byte per 8-px column) and the image
symbol is `mh_map_<id>` (the converter derives the same symbol from the PNG
basename).

Placeholder art note: for a room whose PNG is missing this tool authors a
deterministic flat-shade placeholder (fie.5 refines the art). An existing PNG
is never overwritten, so refined art survives `make gen`.

Usage:
    python3 tools/gen-zones.py [--root DIR] [--dump]

    --root DIR  pipeline root holding data/, images/ and src/generated
    --dump      validate + list the compiled graph on stdout; writes nothing
"""
import argparse
import json
import os
import re
import struct
import sys

from PIL import Image

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_REL = "data/map.json"
BLOB_REL = "fxdata/tables/zones.bin"
DATA_HPP_REL = "src/generated/zone_data.hpp"
META_HPP_REL = "src/generated/zone_meta.hpp"
FX_HEADER_REL = "fxdata/fxdata.h"

MAGIC = 0x5A52   # 'R','Z' little-endian (room zones)
VERSION = 1
FLAGS = 0
HEADER_SIZE = 16
ROOM_SIZE = 18
DOOR_SIZE = 10
SPAWN_SIZE = 4
PROP_SIZE = 9
HEAL_SIZE = 6

ROOM_MAX = 254          # room index is u8; 0xFF is reserved
SECTION_MAX = 65535     # section first-indices are u16
PER_ROOM_MAX = 255      # per-room counts are u8
SPAWN_MAX = 255         # spawn index is u8; 0xFF is reserved (door to menu)
SHEET_MAX = 255
ID_RE = re.compile(r"^[a-z][a-z0-9_]*$")
SYMBOL_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
IMAGE_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)_(\d+)x(\d+)\.png$")
MAX_ID = 31

# Prop kinds. The runtime may switch on these; the sheet symbol names the FX
# sprite (fie.5 authors the prop art). MONSTER kinds mirror MonsterKind in
# src/core/game.hpp (data/creatures/<id>.json ids).
PROP_TYPES = ("tent", "door", "pole", "post")
MONSTER_KINDS = ("lunge", "sweep", "heavy", "ravager")
MONSTER_NONE = 0xFF
DOOR_MENU = 0xFF        # door.toRoom sentinel: exit to the opening menu
IMAGE_DIR_REL = "images/maps"
IMAGE_SYMBOL_PREFIX = "mh_map_"
MAPS_SPRITES_REL = "fxdata/maps/Sprites.txt"
# Room layers use the 4-shade L4_Triplane nesting (tools/convert-sprite.py
# get_shade): a pixel lights layer p when its red channel reaches the (p+1)-th
# step of the 254/(shades) ramp. 3 layers total (shades - 1).
SHADES = 4
SHADE_STEP = (254 + SHADES) // SHADES   # 64
LAYER_COUNT = SHADES - 1                # 3

# 4-shade placeholder palette (1:1 with L4_Triplane; see tools/gen-equipment.py).
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


def read_enum_name(errors, ctx, obj, key, table, default=_MISSING):
    """Like read_enum, but keeps the validated symbolic name. The normalized
    monster keeps the name so the packer/header emitter can index MONSTER_KINDS
    (the blob stores the index); prop `type` uses read_enum's index directly."""
    if not isinstance(obj, dict) or key not in obj:
        if default is _MISSING:
            return None
        return default
    value = obj[key]
    if not isinstance(value, str) or value not in table:
        errors.add(ctx, "%s: unknown value %r (want one of %s)" % (key, value, ", ".join(table)))
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


def expect_image_path(room):
    """The image path a room must declare: images/maps/mh_map_<id>_<w>x<h>.png."""
    return "%s/%s%s_%dx%d.png" % (IMAGE_DIR_REL, IMAGE_SYMBOL_PREFIX, room["id"],
                                  room["w"], room["h"])


def room_image_symbol(room):
    return "%s%s" % (IMAGE_SYMBOL_PREFIX, room["id"])


def normalize_prop(errors, ctx, obj):
    check_keys(errors, ctx, obj, {"type", "x", "y", "sheet", "frame", "w", "h"})
    if not isinstance(obj, dict):
        return None
    prop_type = read_enum(errors, ctx, obj, "type", PROP_TYPES)
    x = read_int(errors, ctx, obj, "x", 0, 65535)
    y = read_int(errors, ctx, obj, "y", 0, 65535)
    sheet = obj.get("sheet")
    if not isinstance(sheet, str) or not SYMBOL_RE.match(sheet):
        errors.add(ctx, "sheet: expected a C symbol, got %r" % (sheet,))
        sheet = None
    frame = read_int(errors, ctx, obj, "frame", 0, 255)
    w = read_int(errors, ctx, obj, "w", 1, 255)
    h = read_int(errors, ctx, obj, "h", 1, 255)
    if None in (prop_type, x, y, sheet, frame, w, h):
        return None
    return {"type": prop_type, "x": x, "y": y, "sheet": sheet, "frame": frame, "w": w, "h": h}


def normalize_door(errors, ctx, obj):
    check_keys(errors, ctx, obj, {"x", "y", "w", "h", "to"}, {"toSpawn"})
    if not isinstance(obj, dict):
        return None
    x = read_int(errors, ctx, obj, "x", 0, 65535)
    y = read_int(errors, ctx, obj, "y", 0, 65535)
    w = read_int(errors, ctx, obj, "w", 1, 255)
    h = read_int(errors, ctx, obj, "h", 1, 255)
    to = obj.get("to")
    if not isinstance(to, str) or (to != "menu" and not ID_RE.match(to)):
        errors.add(ctx, "to: expected a room id or 'menu', got %r" % (to,))
        to = None
    to_spawn = obj.get("toSpawn")
    if to == "menu":
        if to_spawn is not None:
            errors.add(ctx, "toSpawn: not allowed for a door to 'menu'")
        to_spawn = None
    elif to_spawn is not None and not isinstance(to_spawn, str):
        errors.add(ctx, "toSpawn: expected a spawn name, got %r" % (to_spawn,))
        to_spawn = None
    elif to is not None and to_spawn is None:
        errors.add(ctx, "toSpawn: required for a door to room '%s'" % to)
    if None in (x, y, w, h, to):
        return None
    return {"x": x, "y": y, "w": w, "h": h, "to": to, "toSpawn": to_spawn}


def normalize_heal(errors, ctx, obj):
    check_keys(errors, ctx, obj, {"x", "y", "w", "h"})
    if not isinstance(obj, dict):
        return None
    x = read_int(errors, ctx, obj, "x", 0, 65535)
    y = read_int(errors, ctx, obj, "y", 0, 65535)
    w = read_int(errors, ctx, obj, "w", 1, 255)
    h = read_int(errors, ctx, obj, "h", 1, 255)
    if None in (x, y, w, h):
        return None
    return {"x": x, "y": y, "w": w, "h": h}


def normalize_spawn(errors, ctx, obj):
    check_keys(errors, ctx, obj, {"x", "y"})
    if not isinstance(obj, dict):
        return None
    x = read_int(errors, ctx, obj, "x", 0, 65535)
    y = read_int(errors, ctx, obj, "y", 0, 65535)
    if None in (x, y):
        return None
    return {"x": x, "y": y}


def normalize_monster(errors, ctx, obj):
    check_keys(errors, ctx, obj, {"kind", "spawn"})
    if not isinstance(obj, dict):
        return None
    kind = read_enum_name(errors, ctx, obj, "kind", MONSTER_KINDS)
    spawn = obj.get("spawn")
    if not isinstance(spawn, str):
        errors.add(ctx, "spawn: expected a spawn name, got %r" % (spawn,))
        spawn = None
    if kind is None or spawn is None:
        return None
    return {"kind": kind, "spawn": spawn}


def normalize_room(errors, ctx, obj, seen_ids):
    check_keys(errors, ctx, obj, {"id", "w", "h", "image", "spawns"},
               {"props", "doors", "heal", "monster"})
    if not isinstance(obj, dict):
        return None
    rid = read_id(errors, ctx, obj, "id", seen_ids)
    w = read_int(errors, ctx, obj, "w", 1, 65535)
    h = read_int(errors, ctx, obj, "h", 8, 65535)
    if h is not None and h % 8 != 0:
        errors.add(ctx, "h: %d must be a multiple of 8 (page-major layer format)" % h)
        h = None
    image = obj.get("image")
    if not isinstance(image, str):
        errors.add(ctx, "image: expected a PNG path, got %r" % (image,))
        image = None
    elif rid is not None and w is not None and h is not None:
        # The converter derives the declaration symbol from the PNG basename;
        # the picture must be named for the room symbol + its exact size.
        match = IMAGE_RE.match(os.path.basename(image))
        if not match:
            errors.add(ctx, "image: '%s' must be <symbol>_<w>x<h>.png" % image)
        else:
            symbol, iw, ih = match.group(1), int(match.group(2)), int(match.group(3))
            if symbol != room_image_symbol({"id": rid}):
                errors.add(ctx, "image: symbol '%s' must be '%s'" % (symbol, room_image_symbol({"id": rid})))
            if (iw, ih) != (w, h):
                errors.add(ctx, "image: %dx%d does not match room %dx%d" % (iw, ih, w, h))

    raw_spawns = obj.get("spawns")
    spawns = []
    if not isinstance(raw_spawns, dict) or not raw_spawns:
        errors.add(ctx, "spawns: expected a non-empty object of name -> {x,y}")
    else:
        for name in sorted(raw_spawns):
            sc = "%s.spawns.%s" % (ctx, name)
            if not ID_RE.match(name) or len(name) > MAX_ID:
                errors.add(sc, "spawn name must match [a-z][a-z0-9_]* (<=%d)" % MAX_ID)
                continue
            spawn = normalize_spawn(errors, sc, raw_spawns[name])
            if spawn is not None:
                spawn["name"] = name
                spawns.append(spawn)

    raw_props = obj.get("props", [])
    props = []
    if not isinstance(raw_props, list):
        errors.add(ctx, "props: expected an array")
    else:
        for i, prop in enumerate(raw_props):
            normalized = normalize_prop(errors, "%s.props[%d]" % (ctx, i), prop)
            if normalized is not None:
                props.append(normalized)

    raw_doors = obj.get("doors", [])
    doors = []
    if not isinstance(raw_doors, list):
        errors.add(ctx, "doors: expected an array")
    else:
        for i, door in enumerate(raw_doors):
            normalized = normalize_door(errors, "%s.doors[%d]" % (ctx, i), door)
            if normalized is not None:
                doors.append(normalized)

    raw_heal = obj.get("heal", [])
    heals = []
    if not isinstance(raw_heal, list):
        errors.add(ctx, "heal: expected an array")
    else:
        for i, heal in enumerate(raw_heal):
            normalized = normalize_heal(errors, "%s.heal[%d]" % (ctx, i), heal)
            if normalized is not None:
                heals.append(normalized)

    monster = None
    if obj.get("monster") is not None:
        monster = normalize_monster(errors, ctx + ".monster", obj.get("monster"))

    if None in (rid, w, h, image):
        return None

    # Keep the room rects/spawns inside the room so the runtime clamps are sane.
    for i, prop in enumerate(props):
        if prop["x"] + prop["w"] > w or prop["y"] + prop["h"] > h:
            errors.add("%s.props[%d]" % (ctx, i), "rect (%d,%d,%d,%d) leaves the %dx%d room"
                       % (prop["x"], prop["y"], prop["w"], prop["h"], w, h))
    for i, door in enumerate(doors):
        if door["x"] + door["w"] > w or door["y"] + door["h"] > h:
            errors.add("%s.doors[%d]" % (ctx, i), "rect (%d,%d,%d,%d) leaves the %dx%d room"
                       % (door["x"], door["y"], door["w"], door["h"], w, h))
    for i, heal in enumerate(heals):
        if heal["x"] + heal["w"] > w or heal["y"] + heal["h"] > h:
            errors.add("%s.heal[%d]" % (ctx, i), "rect (%d,%d,%d,%d) leaves the %dx%d room"
                       % (heal["x"], heal["y"], heal["w"], heal["h"], w, h))
    for spawn in spawns:
        if spawn["x"] > w or spawn["y"] > h:
            errors.add("%s.spawns.%s" % (ctx, spawn["name"]), "(%d,%d) leaves the %dx%d room"
                       % (spawn["x"], spawn["y"], w, h))

    return {"id": rid, "w": w, "h": h, "image": image, "spawns": spawns,
            "props": props, "doors": doors, "heals": heals, "monster": monster}


def compile_model(errors, root):
    path = os.path.join(root, DATA_REL)
    if not os.path.isfile(path):
        errors.add(DATA_REL, "missing map file")
        return None
    doc = load_json(errors, path, DATA_REL)
    if doc is None:
        return None
    check_keys(errors, DATA_REL, doc, {"version", "rooms"})
    read_int(errors, DATA_REL, doc, "version", 1, 1)
    raw_rooms = doc.get("rooms")
    if not isinstance(raw_rooms, list) or not raw_rooms:
        errors.add(DATA_REL, "rooms: expected a non-empty array")
        return None
    if len(raw_rooms) > ROOM_MAX:
        errors.add(DATA_REL, "size limit: %d rooms exceed the %d room limit" % (len(raw_rooms), ROOM_MAX))
    rooms = []
    seen_ids = set()
    for i, obj in enumerate(raw_rooms):
        room = normalize_room(errors, "%s: rooms[%d]" % (DATA_REL, i), obj, seen_ids)
        if room is not None:
            rooms.append(room)
    if errors.items:
        return None
    rooms.sort(key=lambda room: room["id"])

    by_id = {room["id"]: room for room in rooms}
    for room in rooms:
        ctx = "%s: rooms.%s" % (DATA_REL, room["id"])
        names = {spawn["name"] for spawn in room["spawns"]}
        for i, door in enumerate(room["doors"]):
            dc = "%s.doors[%d]" % (ctx, i)
            if door["to"] == "menu":
                continue
            target = by_id.get(door["to"])
            if target is None:
                errors.add(dc, "to: unknown room id '%s'" % door["to"])
                continue
            if door["toSpawn"] is not None and door["toSpawn"] not in {s["name"] for s in target["spawns"]}:
                errors.add(dc, "toSpawn: unknown spawn '%s' in room '%s'" % (door["toSpawn"], door["to"]))
        if room["monster"] is not None and room["monster"]["spawn"] not in names:
            errors.add(ctx + ".monster", "spawn: unknown spawn '%s'" % room["monster"]["spawn"])
    if errors.items:
        return None
    return {"rooms": rooms}


def build_layout(errors, model):
    """Assign every global index deterministically (rooms and doors by order)."""
    rooms = model["rooms"]
    doors = []
    spawns = []
    props = []
    heals = []
    sheets = []
    for room in rooms:
        room["firstDoor"] = len(doors)
        for local, door in enumerate(room["doors"]):
            doors.append({"room": room, "door": door, "index": len(doors), "local": local})
        room["firstSpawn"] = len(spawns)
        for local, spawn in enumerate(room["spawns"]):
            spawns.append({"room": room, "spawn": spawn, "index": len(spawns), "local": local})
        room["firstProp"] = len(props)
        for local, prop in enumerate(room["props"]):
            if prop["sheet"] not in sheets:
                sheets.append(prop["sheet"])
            props.append({"room": room, "prop": prop, "index": len(props), "local": local})
        room["firstHeal"] = len(heals)
        for local, heal in enumerate(room["heals"]):
            heals.append({"room": room, "heal": heal, "index": len(heals), "local": local})

    if len(spawns) > SPAWN_MAX:
        errors.add("data", "size limit: %d spawns exceed the %d spawn index limit" % (len(spawns), SPAWN_MAX))
    if len(sheets) > SHEET_MAX:
        errors.add("data", "size limit: %d prop sheets exceed the %d sheet index limit" % (len(sheets), SHEET_MAX))
    for name, section, limit in (("doors", len(doors), SECTION_MAX), ("spawns", len(spawns), SECTION_MAX),
                                 ("props", len(props), SECTION_MAX), ("heals", len(heals), SECTION_MAX)):
        if section > limit:
            errors.add("data", "size limit: %d %s exceed the %d record limit" % (section, name, limit))
    for room in rooms:
        for key, count in (("doors", len(room["doors"])), ("spawns", len(room["spawns"])),
                           ("props", len(room["props"])), ("heals", len(room["heals"]))):
            if count > PER_ROOM_MAX:
                errors.add("data", "size limit: room '%s' has %d %s (per-room max %d)"
                           % (room["id"], count, key, PER_ROOM_MAX))
    if errors.items:
        return None

    # Global spawn indices so doors/monsters can point straight at a spawn.
    spawn_index = {(entry["room"]["id"], entry["spawn"]["name"]): entry["index"] for entry in spawns}
    room_index = {room["id"]: i for i, room in enumerate(rooms)}
    sheet_index = {name: i for i, name in enumerate(sheets)}
    return {"rooms": rooms, "doors": doors, "spawns": spawns, "props": props, "heals": heals,
            "sheets": sheets, "spawn_index": spawn_index, "room_index": room_index,
            "sheet_index": sheet_index}


def section_offsets(counts):
    off = {}
    off["ROOMS"] = HEADER_SIZE
    off["DOORS"] = off["ROOMS"] + ROOM_SIZE * counts["ROOMS"]
    off["SPAWNS"] = off["DOORS"] + DOOR_SIZE * counts["DOORS"]
    off["PROPS"] = off["SPAWNS"] + SPAWN_SIZE * counts["SPAWNS"]
    off["HEALS"] = off["PROPS"] + PROP_SIZE * counts["PROPS"]
    off["SIZE"] = off["HEALS"] + HEAL_SIZE * counts["HEALS"]
    return off


def pack_model(errors, layout):
    rooms = layout["rooms"]
    counts = {"ROOMS": len(rooms), "DOORS": len(layout["doors"]), "SPAWNS": len(layout["spawns"]),
              "PROPS": len(layout["props"]), "HEALS": len(layout["heals"])}
    off = section_offsets(counts)
    if off["SIZE"] >= 65536:
        errors.add("data", "size limit: blob is %d B, offsets are u16" % off["SIZE"])
        return None

    body = bytearray()
    room_offsets = {}
    for i, room in enumerate(rooms):
        room_offsets[room["id"]] = off["ROOMS"] + i * ROOM_SIZE
        monster = room["monster"]
        if monster is None:
            monster_kind = MONSTER_NONE
            monster_spawn = 0xFF
        else:
            monster_kind = MONSTER_KINDS.index(monster["kind"])
            monster_spawn = layout["spawn_index"][(room["id"], monster["spawn"])]
        body += struct.pack("<HHHBHBHBHB", room["w"], room["h"],
                            room["firstDoor"], len(room["doors"]),
                            room["firstSpawn"], len(room["spawns"]),
                            room["firstProp"], len(room["props"]),
                            room["firstHeal"], len(room["heals"]))
        body += struct.pack("<BB", monster_kind, monster_spawn)
        if len(body) % ROOM_SIZE != 0:
            errors.add("data", "internal: room record is %d B, want %d" % (len(body) % ROOM_SIZE, ROOM_SIZE))
            return None

    door_offsets = {}
    for entry in layout["doors"]:
        door = entry["door"]
        room = entry["room"]
        door_offsets[(room["id"], entry["index"])] = off["DOORS"] + entry["index"] * DOOR_SIZE
        if door["to"] == "menu":
            to_room = DOOR_MENU
            to_spawn = 0xFF
        else:
            to_room = layout["room_index"][door["to"]]
            to_spawn = layout["spawn_index"][(door["to"], door["toSpawn"])]
        body += struct.pack("<HHHHBB", door["x"], door["y"], door["w"], door["h"], to_room, to_spawn)

    spawn_offsets = {}
    for entry in layout["spawns"]:
        spawn_offsets[(entry["room"]["id"], entry["spawn"]["name"])] = off["SPAWNS"] + entry["index"] * SPAWN_SIZE
        body += struct.pack("<HH", entry["spawn"]["x"], entry["spawn"]["y"])

    prop_offsets = {}
    for entry in layout["props"]:
        prop = entry["prop"]
        prop_offsets[(entry["room"]["id"], entry["index"])] = off["PROPS"] + entry["index"] * PROP_SIZE
        body += struct.pack("<BHHBBBB", prop["type"], prop["x"], prop["y"],
                            layout["sheet_index"][prop["sheet"]], prop["frame"], prop["w"], prop["h"])

    heal_offsets = {}
    for entry in layout["heals"]:
        heal = entry["heal"]
        heal_offsets[(entry["room"]["id"], entry["index"])] = off["HEALS"] + entry["index"] * HEAL_SIZE
        body += struct.pack("<HHBB", heal["x"], heal["y"], heal["w"], heal["h"])

    header = struct.pack("<HBB", MAGIC, VERSION, FLAGS)
    header += struct.pack("<HHHHH", counts["ROOMS"], counts["DOORS"], counts["SPAWNS"],
                          counts["PROPS"], counts["HEALS"])
    header += struct.pack("<H", 0)
    assert len(header) == HEADER_SIZE, len(header)
    blob = header + bytes(body)
    if len(blob) != off["SIZE"]:
        errors.add("data", "internal: blob is %d B, want %d" % (len(blob), off["SIZE"]))
        return None
    return {"blob": blob, "counts": counts, "off": off, "room_offsets": room_offsets,
            "door_offsets": door_offsets, "spawn_offsets": spawn_offsets,
            "prop_offsets": prop_offsets, "heal_offsets": heal_offsets}


def load_fxdata_symbols(root):
    path = os.path.join(root, FX_HEADER_REL)
    try:
        with open(path, encoding="utf-8") as handle:
            return {name: int(value, 0) for name, value in
                    re.findall(r"constexpr\s+uint24_t\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(0[xX][0-9A-Fa-f]+|\d+)",
                               handle.read())}
    except OSError:
        return None


def emit_data_header(layout, packed):
    rooms = layout["rooms"]
    lines = []
    app = lines.append
    app("#pragma once")
    app("// Generated by tools/gen-zones.py -- do not edit.")
    app("//")
    app("// Host-side plain structs + arrays mirroring the packed mhZones blob")
    app("// (docs/map-zones.md). Field order matches the blob byte order; the host")
    app("// reads members directly, so host struct padding is irrelevant. The device")
    app("// reads the blob with the offsets in zone_meta.hpp instead.")
    app("")
    app("#include <array>")
    app("#include <stdint.h>")
    app("")
    app("namespace zone_data {")
    app("")
    app("constexpr uint8_t VERSION = %d;" % VERSION)
    app("constexpr uint16_t BLOB_SIZE = %d;" % len(packed["blob"]))
    app("constexpr uint8_t MONSTER_NONE = 0x%02X;" % MONSTER_NONE)
    app("")
    app("struct Room {")
    app("    uint16_t w, h;")
    app("    uint16_t firstDoor;")
    app("    uint8_t doorCount;")
    app("    uint16_t firstSpawn;")
    app("    uint8_t spawnCount;")
    app("    uint16_t firstProp;")
    app("    uint8_t propCount;")
    app("    uint16_t firstHeal;")
    app("    uint8_t healCount;")
    app("    uint8_t monsterKind;   // MONSTER_NONE or MONSTER_KINDS index")
    app("    uint8_t monsterSpawn;  // global spawn index, 0xFF when none")
    app("};")
    app("")
    app("struct Door {")
    app("    uint16_t x, y, w, h;")
    app("    uint8_t toRoom;    // room index or DOOR_MENU (0xFF)")
    app("    uint8_t toSpawn;   // global spawn index in toRoom")
    app("};")
    app("")
    app("struct Spawn {")
    app("    uint16_t x, y;")
    app("};")
    app("")
    app("struct Prop {")
    app("    uint8_t type;   // PROP_* kind")
    app("    uint16_t x, y;")
    app("    uint8_t sheet;  // prop sheet index (SHEET_* in zone_meta.hpp)")
    app("    uint8_t frame, w, h;")
    app("};")
    app("")
    app("struct Heal {")
    app("    uint16_t x, y;")
    app("    uint8_t w, h;")
    app("};")
    app("")
    app("// Room indices, sorted by id.")
    for i, room in enumerate(rooms):
        app("constexpr uint8_t ROOM_%s = %d;" % (room["id"].upper(), i))
    app("")
    app("// Spawn indices (global section order).")
    for entry in layout["spawns"]:
        app("constexpr uint8_t SPAWN_%s_%s = %d;"
            % (entry["room"]["id"].upper(), entry["spawn"]["name"].upper(), entry["index"]))
    app("")
    app("// Door indices (global section order).")
    for entry in layout["doors"]:
        app("constexpr uint8_t DOOR_%s_%d = %d;"
            % (entry["room"]["id"].upper(), entry["local"], entry["index"]))
    app("")
    app("// Prop/heal indices.")
    for entry in layout["props"]:
        app("constexpr uint8_t PROP_%s_%d = %d;"
            % (entry["room"]["id"].upper(), entry["local"], entry["index"]))
    for entry in layout["heals"]:
        app("constexpr uint8_t HEAL_%s_%d = %d;"
            % (entry["room"]["id"].upper(), entry["local"], entry["index"]))
    app("")
    app("// Name tables: host-side lookup for loadRoom(roomId, spawnName).")
    app("struct RoomName { const char *id; uint8_t index; };")
    app("inline constexpr std::array<RoomName, %d> ROOM_NAMES = {{" % len(rooms))
    for i, room in enumerate(rooms):
        app('    {"%s", %d},' % (room["id"], i))
    app("}};")
    app("")
    app("struct SpawnName { const char *name; uint8_t room; uint8_t index; };")
    app("inline constexpr std::array<SpawnName, %d> SPAWN_NAMES = {{" % len(layout["spawns"]))
    for entry in layout["spawns"]:
        app('    {"%s", %d, %d},' % (entry["spawn"]["name"], layout["room_index"][entry["room"]["id"]], entry["index"]))
    app("}};")
    app("")
    app("// Section arrays, in packed order.")
    app("inline constexpr std::array<Room, %d> ROOMS = {{" % len(rooms))
    for room in rooms:
        monster = room["monster"]
        kind = MONSTER_NONE if monster is None else MONSTER_KINDS.index(monster["kind"])
        spawn = 0xFF if monster is None else layout["spawn_index"][(room["id"], monster["spawn"])]
        app("    {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d}," % (
            room["w"], room["h"], room["firstDoor"], len(room["doors"]),
            room["firstSpawn"], len(room["spawns"]), room["firstProp"], len(room["props"]),
            room["firstHeal"], len(room["heals"]), kind, spawn))
    app("}};")
    app("")
    app("inline constexpr std::array<Door, %d> DOORS = {{" % len(layout["doors"]))
    for entry in layout["doors"]:
        door = entry["door"]
        if door["to"] == "menu":
            to_room, to_spawn = DOOR_MENU, 0xFF
        else:
            to_room = layout["room_index"][door["to"]]
            to_spawn = layout["spawn_index"][(door["to"], door["toSpawn"])]
        app("    {%d, %d, %d, %d, %d, %d}," % (door["x"], door["y"], door["w"], door["h"], to_room, to_spawn))
    app("}};")
    app("")
    app("inline constexpr std::array<Spawn, %d> SPAWNS = {{" % len(layout["spawns"]))
    for entry in layout["spawns"]:
        app("    {%d, %d}," % (entry["spawn"]["x"], entry["spawn"]["y"]))
    app("}};")
    app("")
    app("inline constexpr std::array<Prop, %d> PROPS = {{" % len(layout["props"]))
    for entry in layout["props"]:
        prop = entry["prop"]
        app("    {%d, %d, %d, %d, %d, %d, %d}," % (
            prop["type"], prop["x"], prop["y"], layout["sheet_index"][prop["sheet"]],
            prop["frame"], prop["w"], prop["h"]))
    app("}};")
    app("")
    app("inline constexpr std::array<Heal, %d> HEALS = {{" % len(layout["heals"]))
    for entry in layout["heals"]:
        heal = entry["heal"]
        app("    {%d, %d, %d, %d}," % (heal["x"], heal["y"], heal["w"], heal["h"]))
    app("}};")
    app("")
    app("}   // namespace zone_data")
    app("")
    return "\n".join(lines)


def emit_meta_header(layout, packed, fx_symbols):
    rooms = layout["rooms"]
    off = packed["off"]
    lines = []
    app = lines.append
    app("#pragma once")
    app("// Generated by tools/gen-zones.py -- do not edit.")
    app("//")
    app("// Room-graph ABI (docs/map-zones.md): header, fixed-size room/door/spawn/")
    app("// prop/heal records, little-endian, no padding. Offsets are absolute byte")
    app("// offsets into the mhZones raw_t section (fxdata/fxdata.txt); on AVR the")
    app("// loader reads mhZones + off through core/fxmem.hpp. Room image and prop")
    app("// sheet offsets are the FX-image addresses fie.5 blits from.")
    app("")
    app("#include <stdint.h>")
    resolved_any = any(layout["sheets"]) or rooms
    if resolved_any:
        app("#if defined(__AVR__)")
        app("#include \"../fxdata.h\"   // live sheet symbols for the stale-blob static_assert")
        app("#endif")
    app("")
    app("namespace zone {")
    app("")
    app("constexpr uint16_t MAGIC = 0x%04X;" % MAGIC)
    app("constexpr uint8_t VERSION = %d;" % VERSION)
    app("constexpr uint8_t FLAGS = 0x%02X;" % FLAGS)
    app("constexpr uint16_t SIZE = %d;" % len(packed["blob"]))
    app("constexpr uint8_t HEADER_SIZE = %d;" % HEADER_SIZE)
    app("")
    app("constexpr uint8_t ROOM_SIZE = %d;" % ROOM_SIZE)
    app("constexpr uint8_t DOOR_SIZE = %d;" % DOOR_SIZE)
    app("constexpr uint8_t SPAWN_SIZE = %d;" % SPAWN_SIZE)
    app("constexpr uint8_t PROP_SIZE = %d;" % PROP_SIZE)
    app("constexpr uint8_t HEAL_SIZE = %d;" % HEAL_SIZE)
    app("")
    app("constexpr uint16_t ROOMS_OFF = %d;" % off["ROOMS"])
    app("constexpr uint16_t DOORS_OFF = %d;" % off["DOORS"])
    app("constexpr uint16_t SPAWNS_OFF = %d;" % off["SPAWNS"])
    app("constexpr uint16_t PROPS_OFF = %d;" % off["PROPS"])
    app("constexpr uint16_t HEALS_OFF = %d;" % off["HEALS"])
    app("constexpr uint16_t ROOMS_COUNT = %d;" % packed["counts"]["ROOMS"])
    app("constexpr uint16_t DOORS_COUNT = %d;" % packed["counts"]["DOORS"])
    app("constexpr uint16_t SPAWNS_COUNT = %d;" % packed["counts"]["SPAWNS"])
    app("constexpr uint16_t PROPS_COUNT = %d;" % packed["counts"]["PROPS"])
    app("constexpr uint16_t HEALS_COUNT = %d;" % packed["counts"]["HEALS"])
    app("")
    app("// Record field offsets.")
    app("constexpr uint8_t ROOM_W_OFF = 0;          // u16")
    app("constexpr uint8_t ROOM_H_OFF = 2;          // u16")
    app("constexpr uint8_t ROOM_FIRST_DOOR_OFF = 4; // u16")
    app("constexpr uint8_t ROOM_DOOR_COUNT_OFF = 6; // u8")
    app("constexpr uint8_t ROOM_FIRST_SPAWN_OFF = 7;   // u16")
    app("constexpr uint8_t ROOM_SPAWN_COUNT_OFF = 9;   // u8")
    app("constexpr uint8_t ROOM_FIRST_PROP_OFF = 10;   // u16")
    app("constexpr uint8_t ROOM_PROP_COUNT_OFF = 12;   // u8")
    app("constexpr uint8_t ROOM_FIRST_HEAL_OFF = 13;   // u16")
    app("constexpr uint8_t ROOM_HEAL_COUNT_OFF = 15;   // u8")
    app("constexpr uint8_t ROOM_MONSTER_KIND_OFF = 16; // u8")
    app("constexpr uint8_t ROOM_MONSTER_SPAWN_OFF = 17;// u8")
    app("constexpr uint8_t DOOR_X_OFF = 0;   // u16")
    app("constexpr uint8_t DOOR_Y_OFF = 2;   // u16")
    app("constexpr uint8_t DOOR_W_OFF = 4;   // u16")
    app("constexpr uint8_t DOOR_H_OFF = 6;   // u16")
    app("constexpr uint8_t DOOR_TO_ROOM_OFF = 8;  // u8")
    app("constexpr uint8_t DOOR_TO_SPAWN_OFF = 9; // u8")
    app("constexpr uint8_t SPAWN_X_OFF = 0;  // u16")
    app("constexpr uint8_t SPAWN_Y_OFF = 2;  // u16")
    app("constexpr uint8_t PROP_TYPE_OFF = 0;   // u8")
    app("constexpr uint8_t PROP_X_OFF = 1;      // u16")
    app("constexpr uint8_t PROP_Y_OFF = 3;      // u16")
    app("constexpr uint8_t PROP_SHEET_OFF = 5;  // u8")
    app("constexpr uint8_t PROP_FRAME_OFF = 6;  // u8")
    app("constexpr uint8_t PROP_W_OFF = 7;      // u8")
    app("constexpr uint8_t PROP_H_OFF = 8;      // u8")
    app("constexpr uint8_t HEAL_X_OFF = 0;  // u16")
    app("constexpr uint8_t HEAL_Y_OFF = 2;  // u16")
    app("constexpr uint8_t HEAL_W_OFF = 4;  // u8")
    app("constexpr uint8_t HEAL_H_OFF = 5;  // u8")
    app("")
    app("// Prop kinds; src render switches on these for special behaviour.")
    for i, name in enumerate(PROP_TYPES):
        app("constexpr uint8_t PROP_%s = %d;" % (name.upper(), i))
    app("")
    app("// Monster kinds, values mirror MonsterKind in src/core/game.hpp.")
    app("constexpr uint8_t MONSTER_NONE = 0x%02X;   // room has no monster" % MONSTER_NONE)
    for i, name in enumerate(MONSTER_KINDS):
        app("constexpr uint8_t MONSTER_%s = %d;" % (name.upper(), i))
    app("constexpr uint8_t DOOR_MENU = 0x%02X;   // door.toRoom: exit to the opening menu" % DOOR_MENU)
    app("")
    app("// Room indices + blob offsets, sorted by id.")
    for i, room in enumerate(rooms):
        tag = room["id"].upper()
        app("constexpr uint8_t ROOM_%s = %d;" % (tag, i))
        app("constexpr uint16_t ROOM_%s_OFF = %d;" % (tag, packed["room_offsets"][room["id"]]))
        app("constexpr uint8_t ROOM_%s_DOORS = %d;" % (tag, len(room["doors"])))
        app("constexpr uint8_t ROOM_%s_SPAWNS = %d;" % (tag, len(room["spawns"])))
        app("constexpr uint16_t ROOM_%s_FIRST_SPAWN = %d;" % (tag, room["firstSpawn"]))
        app("constexpr uint16_t ROOM_%s_FIRST_DOOR = %d;" % (tag, room["firstDoor"]))
        app("constexpr uint16_t ROOM_%s_W = %d;" % (tag, room["w"]))
        app("constexpr uint16_t ROOM_%s_H = %d;" % (tag, room["h"]))
        app("constexpr uint16_t ROOM_%s_IMAGE_LAYER_BYTES = %d;" % (tag, room["w"] * (room["h"] // 8)))
        app("constexpr uint32_t ROOM_%s_IMAGE_SIZE = %d;" % (tag, room["w"] * (room["h"] // 8) * LAYER_COUNT))
    app("")
    app("// Spawn indices + blob offsets (global section order).")
    for entry in layout["spawns"]:
        name = "SPAWN_%s_%s" % (entry["room"]["id"].upper(), entry["spawn"]["name"].upper())
        app("constexpr uint8_t %s = %d;" % (name, entry["index"]))
        app("constexpr uint16_t %s_OFF = %d;" % (name, packed["spawn_offsets"][(entry["room"]["id"], entry["spawn"]["name"])]))
    app("")
    app("// Door indices + blob offsets (global section order; names use the")
    app("// room-local index so host/meta symbol names stay in lockstep).")
    for entry in layout["doors"]:
        name = "DOOR_%s_%d" % (entry["room"]["id"].upper(), entry["local"])
        app("constexpr uint8_t %s = %d;" % (name, entry["index"]))
        app("constexpr uint16_t %s_OFF = %d;" % (name, packed["door_offsets"][(entry["room"]["id"], entry["index"])]))
        if entry["door"]["to"] == "menu":
            app("constexpr uint8_t %s_TO_ROOM = DOOR_MENU;" % name)
        else:
            app("constexpr uint8_t %s_TO_ROOM = %d;" % (name, layout["room_index"][entry["door"]["to"]]))
    app("")
    app("// Prop/heal indices + blob offsets (names use the room-local index).")
    for entry in layout["props"]:
        name = "PROP_%s_%d" % (entry["room"]["id"].upper(), entry["local"])
        app("constexpr uint8_t %s = %d;" % (name, entry["index"]))
        app("constexpr uint16_t %s_OFF = %d;" % (name, packed["prop_offsets"][(entry["room"]["id"], entry["index"])]))
    for entry in layout["heals"]:
        name = "HEAL_%s_%d" % (entry["room"]["id"].upper(), entry["local"])
        app("constexpr uint8_t %s = %d;" % (name, entry["index"]))
        app("constexpr uint16_t %s_OFF = %d;" % (name, packed["heal_offsets"][(entry["room"]["id"], entry["index"])]))
    app("")
    app("// Prop sheet names + FX-image offsets (0 = not yet authored; fie.5 art).")
    for i, sheet in enumerate(layout["sheets"]):
        symbol = sheet
        value = (fx_symbols or {}).get(symbol)
        app("constexpr uint8_t SHEET_%s = %d;" % (symbol.upper(), i))
        if value is None:
            app("constexpr bool SHEET_%s_RESOLVED = false;   // '%s' not in fxdata.h yet" % (symbol.upper(), symbol))
            app("constexpr uint32_t SHEET_%s_OFF = 0;" % symbol.upper())
        else:
            app("constexpr bool SHEET_%s_RESOLVED = true;" % symbol.upper())
            app("constexpr uint32_t SHEET_%s_OFF = %d;" % (symbol.upper(), value))
    app("")
    app("// Room image symbols + baked FX offsets (the fie.5 blit base). A missing")
    app("// symbol means a first gen pass before fxdata-build emitted it.")
    for room in rooms:
        symbol = room_image_symbol(room)
        value = (fx_symbols or {}).get(symbol)
        tag = room["id"].upper()
        app('constexpr const char *ROOM_%s_IMAGE = "%s";' % (tag, symbol))
        if value is None:
            app("constexpr bool ROOM_%s_IMAGE_RESOLVED = false;" % tag)
            app("constexpr uint32_t ROOM_%s_IMAGE_OFF = 0;" % tag)
        else:
            app("constexpr bool ROOM_%s_IMAGE_RESOLVED = true;" % tag)
            app("constexpr uint32_t ROOM_%s_IMAGE_OFF = %d;" % (tag, value))
    app("#if defined(__AVR__)")
    for room in rooms:
        symbol = room_image_symbol(room)
        if (fx_symbols or {}).get(symbol) is not None:
            tag = room["id"].upper()
            app("static_assert(ROOM_%s_IMAGE_OFF == static_cast<uint32_t>(%s), \"zone blob stale: re-run make gen\");"
                % (tag, symbol))
    for sheet in layout["sheets"]:
        if (fx_symbols or {}).get(sheet) is not None:
            app("static_assert(SHEET_%s_OFF == static_cast<uint32_t>(%s), \"zone blob stale: re-run make gen\");"
                % (sheet.upper(), sheet))
    app("#endif")
    app("")
    app("}   // namespace zone")
    app("")
    return "\n".join(lines)


def dump_model(layout, packed):
    for room in layout["rooms"]:
        monster = "-"
        if room["monster"] is not None:
            monster = "%s@%s" % (room["monster"]["kind"], room["monster"]["spawn"])
        print("room %s: %dx%d image %s doors %d spawns %d props %d heals %d monster %s"
              % (room["id"], room["w"], room["h"], room_image_symbol(room), len(room["doors"]),
                 len(room["spawns"]), len(room["props"]), len(room["heals"]), monster))
        for spawn in room["spawns"]:
            print("  spawn %s: (%d,%d)" % (spawn["name"], spawn["x"], spawn["y"]))
        for i, door in enumerate(room["doors"]):
            target = door["to"] if door["to"] == "menu" else "%s.%s" % (door["to"], door["toSpawn"])
            print("  door %d: rect(%d,%d,%d,%d) -> %s" % (i, door["x"], door["y"], door["w"], door["h"], target))
        for i, prop in enumerate(room["props"]):
            print("  prop %d: %s rect(%d,%d,%d,%d) sheet %s frame %d"
                  % (i, PROP_TYPES[prop["type"]], prop["x"], prop["y"], prop["w"], prop["h"],
                     prop["sheet"], prop["frame"]))
        for i, heal in enumerate(room["heals"]):
            print("  heal %d: rect(%d,%d,%d,%d)" % (i, heal["x"], heal["y"], heal["w"], heal["h"]))
    print("gen-zones: %d rooms, %d doors, %d spawns, %d props, %d heals, %d B blob"
          % (packed["counts"]["ROOMS"], packed["counts"]["DOORS"], packed["counts"]["SPAWNS"],
             packed["counts"]["PROPS"], packed["counts"]["HEALS"], len(packed["blob"])))


# ----------------------------------------------------------------- placeholder art


def rect(img, x, y, w, h, color):
    if w <= 0 or h <= 0:
        return
    px = img.load()
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            px[xx, yy] = color


def placeholder_png(room):
    """Deterministic flat-shade placeholder (fie.5 refines the real art)."""
    w, h = room["w"], room["h"]
    img = Image.new("RGBA", (w, h), DARK)
    rect(img, 0, 0, w, 1, LIGHT)
    rect(img, 0, h - 1, w, 1, LIGHT)
    cx, cy = w // 2, h // 2
    rect(img, cx - 4, cy - 4, 8, 8, WHITE)
    rect(img, cx - 1, cy - 1, 2, 2, BLACK)
    return img


def author_missing_art(rooms, root, errors):
    wrote = set()
    for room in rooms:
        path = os.path.join(root, room["image"])
        if os.path.isfile(path):
            continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        placeholder_png(room).save(path, format="PNG")
        wrote.add(room["image"])
    return wrote


def validate_art(rooms, root, errors):
    by_room = {}
    for room in rooms:
        path = os.path.join(root, room["image"])
        if not os.path.isfile(path):
            errors.add(room["image"], "missing room image (run without --dump to author a placeholder)")
            continue
        try:
            with Image.open(path) as img:
                size = img.size
        except OSError as exc:
            errors.add(room["image"], "cannot read image: %s" % exc)
            continue
        if size != (room["w"], room["h"]):
            errors.add(room["image"], "image is %dx%d, want %dx%d" % (size[0], size[1], room["w"], room["h"]))
        by_room[room["id"]] = path
    return by_room


def shade_on(red, layer):
    """True when a pixel lights layer `layer` (0..2). Mirrors
    tools/convert-sprite.py get_shade for SHADES=4 (thresholds 64/128/192)."""
    return 1 if red >= (layer + 1) * SHADE_STEP else 0


def room_layers(path, w, h):
    """Convert one room PNG to the 3x 1bpp page-major layer blob.

    Layout (spike monhun-ardu-fie.2): layer p occupies
    [(y / 8) * w + x], bit (y & 7); bytes per layer = w * (h / 8); the layers
    concatenate in plane order. Fully transparent pixels (alpha < 128) light no
    layer (shade 0), so a masked room erases to background.
    """
    layer_bytes = w * (h // 8)
    out = bytearray(layer_bytes * LAYER_COUNT)
    with Image.open(path) as raw:
        px = raw.convert("RGBA").load()
        for y in range(h):
            base = (y // 8) * w
            bit = 1 << (y & 7)
            for x in range(w):
                r, g, b, a = px[x, y]
                if a < 128:
                    continue
                for p in range(LAYER_COUNT):
                    if shade_on(r, p):
                        out[p * layer_bytes + base + x] |= bit
    return bytes(out), layer_bytes


def emit_maps_sprites(rooms, layers):
    """`fxdata/maps/Sprites.txt`: one uint8_t array per room (sorted by id), the
    form fxdata-build.py packs and fxdata_manifest.py parses. Room layers are
    addressed by fie.5 via seekData(mh_map_<id> + layer * w * (h/8) + off)."""
    lines = ["// Generated by tools/gen-zones.py -- do not edit.",
             "// Room layer arrays (3x 1bpp page-major); see docs/map-zones.md."]
    for room in rooms:
        lines.append("uint8_t %s[] =" % room_image_symbol(room))
        lines.append("{")
        data = layers[room["id"]][0]
        for i in range(0, len(data), 16):
            lines.append("    " + ", ".join(str(b) for b in data[i:i + 16]) + ",")
        lines.append("};")
    lines.append("")
    return "\n".join(lines)


def clean_stale_images(rooms, root):
    """Drop placeholder/refined PNGs whose room no longer exists, so the manifest
    never sees an orphan image under images/maps."""
    directory = os.path.join(root, IMAGE_DIR_REL)
    if not os.path.isdir(directory):
        return
    expected = {os.path.basename(room["image"]) for room in rooms}
    for name in sorted(os.listdir(directory)):
        if name in expected or not name.endswith(".png"):
            continue
        os.remove(os.path.join(directory, name))
        print("gen-zones: removed stale %s/%s" % (IMAGE_DIR_REL, name))


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
    layout = build_layout(errors, model) if model is not None and not errors.items else None
    packed = pack_model(errors, layout) if layout is not None and not errors.items else None
    if errors.items:
        for item in errors.items:
            print("gen-zones: error: %s" % item, file=sys.stderr)
        print("gen-zones: FAIL (%d error%s)" % (len(errors.items), "" if len(errors.items) == 1 else "s"),
              file=sys.stderr)
        return 1
    if dump:
        dump_model(layout, packed)
        return 0

    wrote = author_missing_art(layout["rooms"], root, errors)
    art = validate_art(layout["rooms"], root, errors)
    if errors.items:
        for item in errors.items:
            print("gen-zones: error: %s" % item, file=sys.stderr)
        print("gen-zones: FAIL (%d error%s)" % (len(errors.items), "" if len(errors.items) == 1 else "s"),
              file=sys.stderr)
        return 1
    clean_stale_images(layout["rooms"], root)

    # Convert each room PNG to the 3x 1bpp page-major layer arrays the FX image
    # packs as the maps/Sprites.txt section (fie.5 blits them via seekData).
    layers = {}
    for room in layout["rooms"]:
        data, _layer_bytes = room_layers(art[room["id"]], room["w"], room["h"])
        layers[room["id"]] = (data, _layer_bytes)

    fx_symbols = load_fxdata_symbols(root)
    if write_if_changed(os.path.join(root, BLOB_REL), packed["blob"]):
        wrote.add(BLOB_REL)
    if write_if_changed(os.path.join(root, MAPS_SPRITES_REL), emit_maps_sprites(layout["rooms"], layers)):
        wrote.add(MAPS_SPRITES_REL)
    if write_if_changed(os.path.join(root, DATA_HPP_REL), emit_data_header(layout, packed)):
        wrote.add(DATA_HPP_REL)
    if write_if_changed(os.path.join(root, META_HPP_REL), emit_meta_header(layout, packed, fx_symbols)):
        wrote.add(META_HPP_REL)

    unresolved = [sheet for sheet in layout["sheets"] if (fx_symbols or {}).get(sheet) is None]
    print("gen-zones: %d rooms, %d doors, %d spawns, %d props, %d heals, %d B blob (magic 0x%04X version %d)"
          % (packed["counts"]["ROOMS"], packed["counts"]["DOORS"], packed["counts"]["SPAWNS"],
             packed["counts"]["PROPS"], packed["counts"]["HEALS"], len(packed["blob"]), MAGIC, VERSION))
    for room in layout["rooms"]:
        rel = room["image"]
        data, layer_bytes = layers[room["id"]]
        print("gen-zones: %s (%dx%d, %d B layers, %s)" % (rel, room["w"], room["h"],
                                                          len(data), "wrote" if rel in wrote else "present"))
        print("gen-zones: %s -> %s (3 x %d B, %s)"
              % (rel, room_image_symbol(room), layer_bytes,
                 "wrote" if MAPS_SPRITES_REL in wrote else "present"))
    for rel in (BLOB_REL, MAPS_SPRITES_REL, DATA_HPP_REL, META_HPP_REL):
        print("gen-zones: %s%s" % (rel, "" if rel in wrote else " (unchanged)"))
    if unresolved:
        print("gen-zones: prop sheets unresolved (author in fie.5): %s" % ", ".join(unresolved))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=None, help="pipeline root holding data/ (default: repository root)")
    parser.add_argument("--dump", action="store_true", help="validate + list the compiled graph; write nothing")
    args = parser.parse_args(argv)
    root = os.path.abspath(args.root) if args.root else REPO_ROOT
    return run(root, args.dump)


if __name__ == "__main__":
    sys.exit(main())
