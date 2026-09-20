#!/usr/bin/env python3
"""Unit tests for tools/gen-zones.py (run: make test-tools).

The clean fixture under fixtures/gen_zones/clean is a minimal room graph (two
rooms, a monster, a heal, a prop, a `"menu"` door). Every failure case copies it
to build/tests/gen_zones/ and mutates the copy, so the tests stay read-only on
the repository fixtures and never write into /tmp.
"""
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import unittest

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
TOOL = os.path.join(TOOLS, "gen-zones.py")
FIXTURE = os.path.join(HERE, "fixtures", "gen_zones", "clean")
SCRATCH = os.path.join(ROOT, "build", "tests", "gen_zones")

MAP_REL = "data/map.json"
ITEMS_REL = "data/items.json"
BLOB_REL = "fxdata/tables/zones.bin"
SPRITES_REL = "fxdata/maps/Sprites.txt"
DATA_REL = "src/generated/zone_data.hpp"
META_REL = "src/generated/zone_meta.hpp"

MAGIC = 0x5A52
HEADER = struct.Struct("<HBBHHHHHH")   # 16 B
ROOM = struct.Struct("<HHHBHBHBHBBB")  # 18 B
DOOR = struct.Struct("<HHHHBB")        # 10 B
SPAWN = struct.Struct("<HH")           # 4 B
PROP = struct.Struct("<BHHBBBBBB")     # 11 B
HEAL = struct.Struct("<HHBB")          # 6 B
ARRAY_RE = re.compile(r"uint8_t\s+(\w+)\[\]\s*=\s*\n\{\n(.*?)\n\};", re.S)


def run_tool(*args):
    return subprocess.run([sys.executable, TOOL, *args], capture_output=True, text=True)


def parse_blob(blob):
    magic, version, flags, rooms, doors, spawns, props, heals, _ = HEADER.unpack_from(blob, 0)
    off = {}
    off["rooms"] = HEADER.size
    off["doors"] = off["rooms"] + ROOM.size * rooms
    off["spawns"] = off["doors"] + DOOR.size * doors
    off["props"] = off["spawns"] + SPAWN.size * spawns
    off["heals"] = off["props"] + PROP.size * props
    return {"magic": magic, "version": version, "flags": flags,
            "counts": (rooms, doors, spawns, props, heals), "off": off}


def parse_arrays(text):
    """symbol -> list[int] for the emitted uint8_t room layer arrays."""
    out = {}
    for symbol, body in ARRAY_RE.findall(text):
        out[symbol] = [int(piece) for piece in body.replace("\n", "").split(",") if piece.strip()]
    return out


class GenZonesTests(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        case = os.path.join(SCRATCH, self._testMethodName)
        shutil.rmtree(case, ignore_errors=True)
        shutil.copytree(FIXTURE, case)
        self.case = case

    def path(self, *parts):
        return os.path.join(self.case, *parts)

    def read(self, *parts):
        with open(self.path(*parts), encoding="utf-8") as handle:
            return handle.read()

    def read_bytes(self, *parts):
        with open(self.path(*parts), "rb") as handle:
            return handle.read()

    def compile(self, *extra):
        return run_tool("--root", self.case, *extra)

    def write_map(self, doc):
        with open(self.path(MAP_REL), "w", encoding="utf-8", newline="\n") as handle:
            json.dump(doc, handle, indent=2)
            handle.write("\n")

    def mutate(self, fn):
        with open(self.path(MAP_REL), encoding="utf-8") as handle:
            doc = json.load(handle)
        fn(doc)
        self.write_map(doc)

    def mutate_items(self, fn):
        with open(self.path(ITEMS_REL), encoding="utf-8") as handle:
            doc = json.load(handle)
        fn(doc)
        with open(self.path(ITEMS_REL), "w", encoding="utf-8", newline="\n") as handle:
            json.dump(doc, handle, indent=2)
            handle.write("\n")

    def assert_succeeds(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def assert_fails(self, result, *needles):
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        for needle in needles:
            self.assertIn(needle, result.stderr)

    # ---------------------------------------------------------------- clean

    def test_clean_compile_is_deterministic(self):
        self.assert_succeeds(self.compile())
        for rel in (BLOB_REL, SPRITES_REL, DATA_REL, META_REL):
            self.assertTrue(os.path.isfile(self.path(rel)), rel)
        for rel in ("images/maps/mh_map_camp_16x8.png", "images/maps/mh_map_area_16x8.png"):
            self.assertTrue(os.path.isfile(self.path(rel)), rel)
        first = {rel: self.read_bytes(rel) for rel in (BLOB_REL, SPRITES_REL, DATA_REL, META_REL)}

        second = self.compile()
        self.assert_succeeds(second)
        for rel in (BLOB_REL, SPRITES_REL, DATA_REL, META_REL):
            self.assertIn("%s (unchanged)" % rel, second.stdout)
            self.assertEqual(first[rel], self.read_bytes(rel), rel)

    def test_clean_blob_layout_and_sections(self):
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        parsed = parse_blob(blob)
        self.assertEqual(parsed["magic"], MAGIC)
        self.assertEqual(parsed["version"], 1)
        self.assertEqual(parsed["counts"], (2, 3, 4, 1, 1))
        self.assertEqual(len(blob), HEADER.size + ROOM.size * 2 + DOOR.size * 3
                         + SPAWN.size * 4 + PROP.size + HEAL.size)

        off = parsed["off"]
        area = ROOM.unpack_from(blob, off["rooms"])
        camp = ROOM.unpack_from(blob, off["rooms"] + ROOM.size)
        # w, h, firstDoor, doorCount, firstSpawn, spawnCount, firstProp, propCount,
        # firstHeal, healCount, monsterKind, monsterSpawn
        self.assertEqual(area, (16, 8, 0, 1, 0, 2, 0, 0, 0, 0, 0, 1))       # lunge@start
        self.assertEqual(camp, (16, 8, 1, 2, 2, 2, 0, 1, 0, 1, 0xFF, 0xFF))  # no monster

        doors = [DOOR.unpack_from(blob, off["doors"] + i * DOOR.size) for i in range(3)]
        self.assertEqual(doors[0], (0, 0, 4, 8, 1, 3))    # area -> camp.from_area
        self.assertEqual(doors[1], (12, 0, 4, 8, 0, 0))   # camp -> area.from_camp
        self.assertEqual(doors[2], (0, 0, 4, 8, 0xFF, 0xFF))   # camp -> menu (reserved)

        spawns = [SPAWN.unpack_from(blob, off["spawns"] + i * SPAWN.size) for i in range(4)]
        # area spawns sort by name inside the room: from_camp, start.
        self.assertEqual(spawns, [(1, 6), (12, 4), (1, 6), (14, 6)])

        # prop is type u8, x u16, y u16, sheet u8, frame u8, w u8, h u8,
        # gatherItem u8, gatherYield u8.
        self.assertEqual(PROP.unpack_from(blob, off["props"]), (0, 2, 0, 0, 0, 4, 4, 0, 0))
        self.assertEqual(HEAL.unpack_from(blob, off["heals"]), (2, 0, 4, 4))

    def test_clean_meta_header_constants(self):
        self.assert_succeeds(self.compile())
        text = self.read(META_REL)
        for needle in (
            "constexpr uint16_t MAGIC = 0x5A52;",
            "constexpr uint8_t VERSION = 1;",
            "constexpr uint8_t HEADER_SIZE = 16;",
            "constexpr uint8_t ROOM_SIZE = 18;",
            "constexpr uint8_t DOOR_SIZE = 10;",
            "constexpr uint8_t PROP_SIZE = 11;",
            "constexpr uint8_t PROP_GATHER_ITEM_OFF = 9;",
            "constexpr uint8_t PROP_GATHER_YIELD_OFF = 10;",
            "constexpr uint8_t GATHER_NONE = 0;",
            "constexpr uint8_t GATHER_HERB = 1;",
            "constexpr uint8_t GATHER_BLUE_MUSHROOM = 2;",
            "constexpr uint8_t GATHER_ORE = 3;",
            "constexpr uint8_t GATHER_BUG = 4;",
            "constexpr uint16_t ROOMS_COUNT = 2;",
            "constexpr uint8_t MONSTER_LUNGE = 0;",
            "constexpr uint8_t MONSTER_NONE = 0xFF;",
            "constexpr uint8_t DOOR_MENU = 0xFF;",
            "constexpr uint8_t ROOM_AREA = 0;",
            "constexpr uint8_t ROOM_CAMP = 1;",
            "constexpr uint16_t ROOM_CAMP_W = 16;",     # u16: rooms can exceed 255 px
            "constexpr uint16_t ROOM_CAMP_H = 8;",
            "constexpr uint16_t ROOM_CAMP_IMAGE_LAYER_BYTES = 16;",
            "constexpr uint8_t SPAWN_AREA_START = 1;",
            "constexpr uint8_t DOOR_CAMP_1 = 2;",
            "constexpr uint8_t DOOR_CAMP_1_TO_ROOM = DOOR_MENU;",
            "constexpr uint8_t DOOR_CAMP_0_TO_ROOM = 0;",
            "constexpr uint8_t SHEET_MH_MAP_TENT = 0;",
            'constexpr const char *ROOM_CAMP_IMAGE = "mh_map_camp";',
            'constexpr bool ROOM_CAMP_IMAGE_RESOLVED = false;',
        ):
            self.assertIn(needle, text)
        data = self.read(DATA_REL)
        self.assertIn("constexpr uint8_t ROOM_CAMP = 1;", data)
        self.assertIn("inline constexpr std::array<Door, 3> DOORS", data)

    def test_layer_arrays_emit_three_planes(self):
        self.assert_succeeds(self.compile())
        arrays = parse_arrays(self.read(SPRITES_REL))
        self.assertEqual(set(arrays), {"mh_map_camp", "mh_map_area"})
        # 16x8 -> layer bytes 16, 3 planes.
        self.assertEqual(len(arrays["mh_map_camp"]), 16 * 3)
        self.assertEqual(len(arrays["mh_map_area"]), 16 * 3)

    def test_dump_lists_graph_and_writes_nothing(self):
        result = self.compile("--dump")
        self.assert_succeeds(result)
        self.assertIn("room area: 16x8 image mh_map_area doors 1 spawns 2 props 0 heals 0 monster lunge@start",
                      result.stdout)
        self.assertIn("door 1: rect(0,0,4,8) -> menu", result.stdout)
        self.assertIn("gen-zones: 2 rooms, 3 doors, 4 spawns, 1 props, 1 heals, 115 B blob", result.stdout)
        for rel in (BLOB_REL, SPRITES_REL, DATA_REL, META_REL):
            self.assertFalse(os.path.exists(self.path(rel)), rel)
        self.assertFalse(os.path.exists(self.path("images", "maps", "mh_map_camp_16x8.png")))

    # ------------------------------------------------------- layer round-trip

    def test_layer_pixel_round_trip(self):
        # One 2x8 room with a known column and a fully transparent column.
        doc = {"version": 1, "rooms": [{
            "id": "camp", "w": 2, "h": 8,
            "image": "images/maps/mh_map_camp_2x8.png",
            "spawns": {"entry": {"x": 0, "y": 0}}, "monster": None}]}
        self.write_map(doc)
        os.makedirs(self.path("images", "maps"), exist_ok=True)
        img = Image.new("RGBA", (2, 8), (0, 0, 0, 0))
        shade = [(0, 0, 0, 255),   # BLACK  -> no plane
                 (85, 85, 85, 255),    # DARK   -> plane 0
                 (170, 170, 170, 255),  # LIGHT  -> planes 0+1
                 (255, 255, 255, 255)]  # WHITE  -> planes 0+1+2
        for y in range(4):
            img.putpixel((0, y), shade[y])
        img.save(self.path("images", "maps", "mh_map_camp_2x8.png"), format="PNG")

        self.assert_succeeds(self.compile())
        arrays = parse_arrays(self.read(SPRITES_REL))
        # layer0: x0 y1..y3 = 0b00001110, x1 transparent = 0
        # layer1: x0 y2..y3 = 0b00001100, x1 = 0
        # layer2: x0 y3      = 0b00001000, x1 = 0
        self.assertEqual(arrays["mh_map_camp"], [0x0E, 0x00, 0x0C, 0x00, 0x08, 0x00])
        self.assertIn("constexpr uint16_t ROOM_CAMP_IMAGE_LAYER_BYTES = 2;", self.read(META_REL))

    # ------------------------------------------------------------- gather props

    def test_gather_prop_packs_item_and_yield(self):
        self.mutate(lambda doc: doc["rooms"][0]["props"][0].__setitem__(
            "gather", {"item": "herb", "yield": 3}))
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        parsed = parse_blob(blob)
        prop = PROP.unpack_from(blob, parsed["off"]["props"])
        # type, x, y, sheet, frame, w, h, gatherItem, gatherYield
        self.assertEqual(prop, (0, 2, 0, 0, 0, 4, 4, 1, 3))
        text = self.read(META_REL)
        self.assertIn("constexpr uint8_t GATHER_NONE = 0;", text)
        self.assertIn("constexpr uint8_t GATHER_HERB = 1;", text)
        data = self.read(DATA_REL)
        self.assertIn("uint8_t gatherItem;", data)
        self.assertIn("uint8_t gatherYield;", data)
        # Determinism: a second compile writes nothing and keeps the bytes.
        first = {rel: self.read_bytes(rel) for rel in (BLOB_REL, DATA_REL, META_REL)}
        second = self.compile()
        self.assert_succeeds(second)
        for rel in (BLOB_REL, DATA_REL, META_REL):
            self.assertIn("%s (unchanged)" % rel, second.stdout)
            self.assertEqual(first[rel], self.read_bytes(rel), rel)

    def test_gather_unknown_item_rejected(self):
        self.mutate(lambda doc: doc["rooms"][0]["props"][0].__setitem__(
            "gather", {"item": "rock", "yield": 1}))
        self.assert_fails(self.compile(), "item: unknown value 'rock'")

    def test_gather_zero_yield_rejected(self):
        self.mutate(lambda doc: doc["rooms"][0]["props"][0].__setitem__(
            "gather", {"item": "herb", "yield": 0}))
        self.assert_fails(self.compile(), "yield: out of range 1..9: 0")

    def test_gather_yield_above_max_rejected(self):
        self.mutate(lambda doc: doc["rooms"][0]["props"][0].__setitem__(
            "gather", {"item": "herb", "yield": 10}))
        self.assert_fails(self.compile(), "yield: out of range 1..9: 10")

    def test_gather_unknown_key_rejected(self):
        self.mutate(lambda doc: doc["rooms"][0]["props"][0].__setitem__(
            "gather", {"item": "herb", "yield": 1, "respawn": 30}))
        self.assert_fails(self.compile(), "unknown key 'respawn'")

    def test_gather_missing_item_rejected(self):
        self.mutate(lambda doc: doc["rooms"][0]["props"][0].__setitem__(
            "gather", {"yield": 1}))
        self.assert_fails(self.compile(), "missing key 'item'")

    def test_gather_rect_out_of_room_rejected(self):
        def add_gather_and_shift(doc):
            prop = doc["rooms"][0]["props"][0]
            prop["gather"] = {"item": "herb", "yield": 1}
            prop["x"] = 14   # x + w (4) = 18 > room w 16
        self.mutate(add_gather_and_shift)
        self.assert_fails(self.compile(), "leaves the 16x8 room")

    # ------------------------------------------------- gather <-> item table

    def test_gather_item_maps_to_item_table_index(self):
        # blue_mushroom is item index 1 in the fixture items.json, so the packed
        # gather code is index+1 == 2 (herb stays 1).
        self.mutate(lambda doc: doc["rooms"][0]["props"][0].__setitem__(
            "gather", {"item": "blue_mushroom", "yield": 2}))
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        parsed = parse_blob(blob)
        prop = PROP.unpack_from(blob, parsed["off"]["props"])
        self.assertEqual(prop[7], 2, "gather code is item index + 1")
        text = self.read(META_REL)
        self.assertIn("constexpr uint8_t GATHER_HERB = 1;", text)
        self.assertIn("constexpr uint8_t GATHER_BLUE_MUSHROOM = 2;", text)

    def test_gather_every_item_packs_index_plus_one(self):
        # herb=1, blue_mushroom=2, ore=3, bug=4 (item index + 1) in the fixture.
        for name, code in (("herb", 1), ("blue_mushroom", 2), ("ore", 3), ("bug", 4)):
            self.mutate(lambda doc, name=name: doc["rooms"][0]["props"][0].__setitem__(
                "gather", {"item": name, "yield": 1}))
            self.assert_succeeds(self.compile())
            blob = self.read_bytes(BLOB_REL)
            parsed = parse_blob(blob)
            prop = PROP.unpack_from(blob, parsed["off"]["props"])
            self.assertEqual(prop[7], code, "gather code for %s" % name)
            self.assertIn("constexpr uint8_t GATHER_%s = %d;" % (name.upper(), code),
                          self.read(META_REL))

    def test_missing_items_file_rejected(self):
        os.remove(self.path(ITEMS_REL))
        self.assert_fails(self.compile(), "missing item file")

    def test_gather_item_missing_from_table_rejected(self):
        # Drop bug from the item table: the gather vocabulary still names it, so
        # the zone blob could point at a slot the inventory does not have.
        self.mutate_items(lambda doc: doc["items"].pop(3))
        self.assert_fails(self.compile(), "gather item 'bug' is not in the item table")

    # ---------------------------------------------------------- schema errors

    def test_unknown_root_key_rejected(self):
        self.mutate(lambda doc: doc.__setitem__("theme", "dark"))
        self.assert_fails(self.compile(), "unknown key 'theme'")

    def test_unknown_room_key_rejected(self):
        self.mutate(lambda doc: doc["rooms"][0].__setitem__("music", "x"))
        self.assert_fails(self.compile(), "unknown key 'music'")

    def test_missing_spawns_rejected(self):
        self.mutate(lambda doc: doc["rooms"][0].pop("spawns"))
        self.assert_fails(self.compile(), "spawns: expected a non-empty object")

    def test_float_rejected(self):
        self.mutate(lambda doc: doc["rooms"][0].__setitem__("w", 8.0))
        self.assert_fails(self.compile(), "w: expected an integer")

    def test_bool_rejected(self):
        self.mutate(lambda doc: doc["rooms"][0]["spawns"]["entry"].__setitem__("x", True))
        self.assert_fails(self.compile(), "x: expected an integer")

    def test_height_not_multiple_of_eight_rejected(self):
        self.mutate(lambda doc: doc["rooms"][0].__setitem__("h", 12))
        self.assert_fails(self.compile(), "h: 12 must be a multiple of 8")

    def test_image_symbol_mismatch_rejected(self):
        self.mutate(lambda doc: doc["rooms"][0].__setitem__(
            "image", "images/maps/mh_map_wrong_16x8.png"))
        self.assert_fails(self.compile(), "symbol 'mh_map_wrong' must be 'mh_map_camp'")

    def test_image_size_mismatch_rejected(self):
        self.mutate(lambda doc: doc["rooms"][0].__setitem__(
            "image", "images/maps/mh_map_camp_8x8.png"))
        self.assert_fails(self.compile(), "8x8 does not match room 16x8")

    def test_rect_leaving_room_rejected(self):
        self.mutate(lambda doc: doc["rooms"][0]["doors"][0].__setitem__("x", 14))
        self.assert_fails(self.compile(), "leaves the 16x8 room")

    def test_unknown_prop_type_rejected(self):
        self.mutate(lambda doc: doc["rooms"][0]["props"][0].__setitem__("type", "rock"))
        self.assert_fails(self.compile(), "type: unknown value 'rock'")

    def test_unknown_monster_kind_rejected(self):
        self.mutate(lambda doc: doc["rooms"][1]["monster"].__setitem__("kind", "dragon"))
        self.assert_fails(self.compile(), "kind: unknown value 'dragon'")

    # ------------------------------------------------------- id / cross-refs

    def test_duplicate_room_id_rejected(self):
        self.mutate(lambda doc: doc["rooms"][1].__setitem__("id", "camp"))
        self.assert_fails(self.compile(), "duplicate id 'camp'")

    def test_unknown_door_target_rejected(self):
        self.mutate(lambda doc: doc["rooms"][0]["doors"][0].__setitem__("to", "cave"))
        self.assert_fails(self.compile(), "to: unknown room id 'cave'")

    def test_unknown_to_spawn_rejected(self):
        self.mutate(lambda doc: doc["rooms"][0]["doors"][0].__setitem__("toSpawn", "nope"))
        self.assert_fails(self.compile(), "toSpawn: unknown spawn 'nope' in room 'area'")

    def test_missing_to_spawn_rejected(self):
        self.mutate(lambda doc: doc["rooms"][0]["doors"][0].pop("toSpawn"))
        self.assert_fails(self.compile(), "toSpawn: required for a door to room 'area'")

    def test_menu_to_spawn_rejected(self):
        self.mutate(lambda doc: doc["rooms"][0]["doors"][1].__setitem__("toSpawn", "entry"))
        self.assert_fails(self.compile(), "toSpawn: not allowed for a door to 'menu'")

    def test_unknown_monster_spawn_rejected(self):
        self.mutate(lambda doc: doc["rooms"][1]["monster"].__setitem__("spawn", "nope"))
        self.assert_fails(self.compile(), "spawn: unknown spawn 'nope'")

    # --------------------------------------------------------- reserved menu

    def test_menu_door_encodes_sentinel(self):
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        parsed = parse_blob(blob)
        # Global door 2 is camp's local door 1 (the reserved menu exit).
        self.assertEqual(DOOR.unpack_from(blob, parsed["off"]["doors"] + 2 * DOOR.size),
                         (0, 0, 4, 8, 0xFF, 0xFF))
        text = self.read(META_REL)
        self.assertIn("constexpr uint8_t DOOR_CAMP_1_TO_ROOM = DOOR_MENU;", text)
        self.assertIn("constexpr uint8_t DOOR_MENU = 0xFF;", text)

    # ------------------------------------------------------------ size limits

    def test_room_count_limit_rejected(self):
        rooms = []
        for i in range(255):
            rid = "r%03d" % i
            rooms.append({"id": rid, "w": 8, "h": 8,
                          "image": "images/maps/mh_map_%s_8x8.png" % rid,
                          "spawns": {"s": {"x": 0, "y": 0}}, "monster": None})
        self.write_map({"version": 1, "rooms": rooms})
        self.assert_fails(self.compile("--dump"),
                          "size limit: 255 rooms exceed the 254 room limit")

    def test_spawn_count_limit_rejected(self):
        rooms = []
        for rid in ("a", "b"):
            spawns = {"s%03d" % i: {"x": 0, "y": 0} for i in range(128)}
            rooms.append({"id": rid, "w": 8, "h": 8,
                          "image": "images/maps/mh_map_%s_8x8.png" % rid,
                          "spawns": spawns, "monster": None})
        self.write_map({"version": 1, "rooms": rooms})
        self.assert_fails(self.compile("--dump"),
                          "size limit: 256 spawns exceed the 255 spawn index limit")


if __name__ == "__main__":
    unittest.main()
