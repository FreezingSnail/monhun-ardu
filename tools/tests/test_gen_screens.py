#!/usr/bin/env python3
"""Unit tests for tools/gen-screens.py (run: make test-tools).

The clean fixture under fixtures/gen_screens/clean is a minimal screen tree
(two screens, three rows). Every failure case copies it to
build/tests/gen_screens/ and mutates the copy, so the tests stay read-only on
the repository fixtures and never write into /tmp.
"""
import json
import os
import shutil
import struct
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
TOOL = os.path.join(TOOLS, "gen-screens.py")
FIXTURE = os.path.join(HERE, "fixtures", "gen_screens", "clean")
SCRATCH = os.path.join(ROOT, "build", "tests", "gen_screens")

BLOB_REL = "fxdata/tables/screens.bin"
META_REL = "src/generated/screen_meta.hpp"

HEADER = struct.Struct("<HBBBBH")
DEF_OFF = HEADER.size
ROW_FIXED = struct.Struct("<HBBBB")


def parse_def(blob, off):
    title_len = blob[off + 1]
    return {
        "id": blob[off],
        "titleLen": title_len,
        "title": blob[off + 2:off + 2 + title_len].decode("ascii"),
        "rowCount": blob[off + 2 + title_len],
        "firstRow": struct.unpack_from("<H", blob, off + 3 + title_len)[0],
    }


def parse_row(blob, off):
    label_len = blob[off]
    fields = off + 1 + label_len
    cost, action, flags, cond, param = ROW_FIXED.unpack_from(blob, fields)
    return {
        "label": blob[off + 1:off + 1 + label_len].decode("ascii"),
        "cost": cost,
        "action": action,
        "flags": flags,
        "cond": cond,
        "param": param,
        "size": fields + ROW_FIXED.size - off,
    }


def parse_layers(text, symbol):
    """Byte list of one layer array from a Sprites.txt (test-only parser)."""
    start = text.index("uint8_t %s[] =" % symbol)
    body = text[text.index("{", start) + 1:text.index("};", start)]
    return [int(value) for value in body.replace("\n", "").split(",") if value.strip()]


def run_tool(*args):
    return subprocess.run([sys.executable, TOOL, *args], capture_output=True, text=True)


class GenScreensTests(unittest.TestCase):
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

    def assert_succeeds(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def assert_fails(self, result, *needles):
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        for needle in needles:
            self.assertIn(needle, result.stderr)

    def mutate(self, rel_path, fn):
        with open(self.path(rel_path), encoding="utf-8") as handle:
            doc = json.load(handle)
        fn(doc)
        with open(self.path(rel_path), "w", encoding="utf-8", newline="\n") as handle:
            json.dump(doc, handle, indent=2)
            handle.write("\n")

    # ---------------------------------------------------------------- clean

    def test_clean_compile_is_deterministic(self):
        self.assert_succeeds(self.compile())
        for rel in (BLOB_REL, META_REL):
            self.assertTrue(os.path.isfile(self.path(rel)), rel)
        first_blob = self.read_bytes(BLOB_REL)
        first_meta = self.read(META_REL)
        second = self.compile()
        self.assert_succeeds(second)
        self.assertIn("%s (unchanged)" % BLOB_REL, second.stdout)
        self.assertIn("%s (unchanged)" % META_REL, second.stdout)
        self.assertEqual(first_blob, self.read_bytes(BLOB_REL))
        self.assertEqual(first_meta, self.read(META_REL))

    def test_clean_meta_header_constants(self):
        self.assert_succeeds(self.compile())
        text = self.read(META_REL)
        for needle in (
            "constexpr uint16_t MAGIC = 0x5343;",
            "constexpr uint8_t VERSION = 1;",
            "constexpr uint8_t HEADER_SIZE = 8;",
            "constexpr uint8_t DEF_OFF_OFF = 8;",
            "constexpr uint8_t SCREEN_COUNT = 2;",
            "constexpr uint16_t ROW_COUNT = 3;",
            "constexpr uint8_t ACTION_LEAVE = 0;",
            "constexpr uint8_t ACTION_BUY_UPGRADE = 1;",
            "constexpr uint8_t ACTION_TAKE_QUEST = 2;",
            "constexpr uint8_t ACTION_TURN_IN_QUEST = 3;",
            "constexpr uint8_t ACTION_NONE = 4;",
            "constexpr uint8_t ACTION_HUNT = 5;",
            "constexpr uint8_t ACTION_OPEN_QUESTS = 6;",
            "constexpr uint8_t ACTION_EQUIP_WEAPON = 7;",
            "constexpr uint8_t ACTION_OPEN_GEAR = 8;",
            "constexpr uint8_t ACTION_EQUIP_ARMOR = 9;",
            "constexpr uint8_t COND_ALWAYS = 0;",
            "constexpr uint8_t COND_ZENNY = 1;",
            "constexpr uint8_t COND_FLAG = 2;",
            "constexpr uint8_t COND_TIER = 3;",
            "constexpr uint8_t COND_QUEST = 4;",
            "constexpr uint8_t COND_UPGRADE = 5;",
            "constexpr uint8_t ROW_F_HIDE_LOCKED = 0x01;",
            "constexpr uint8_t SCREEN_HUB = 0;",
            "constexpr uint16_t SCREEN_HUB_OFF = 12;",
            "constexpr uint8_t SCREEN_HUB_ROWS = 2;",
            "constexpr uint8_t SCREEN_HUB_TITLE_LEN = 3;",
            "constexpr uint16_t SCREEN_HUB_FIRST_ROW = 30;",
            "constexpr uint8_t SCREEN_SMITH = 1;",
            "constexpr uint16_t SCREEN_SMITH_OFF = 20;",
            "constexpr uint8_t SCREEN_SMITH_ROWS = 1;",
            "constexpr uint16_t SCREEN_SMITH_FIRST_ROW = 58;",
            "constexpr uint16_t PAGE_TABLE_OFF = 71;",
            "constexpr uint8_t SCREEN_PAGE_STRIDE = 13;",
            "constexpr uint8_t SCREEN_PAGE_MAX = 4;",
            "constexpr uint8_t SCREEN_HUB_PAGES = 0;",
            "constexpr uint16_t SCREEN_HUB_PAGE_TABLE = 71;",
            "constexpr uint8_t SCREEN_SMITH_PAGES = 0;",
            "constexpr uint16_t SCREEN_SMITH_PAGE_TABLE = 84;",
        ):
            self.assertIn(needle, text)

    def test_clean_blob_layout(self):
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        magic, version, flags, count, reserved, row_count = HEADER.unpack_from(blob, 0)
        self.assertEqual((magic, version, flags, count, reserved, row_count),
                         (0x5343, 1, 0, 2, 0, 3))
        self.assertEqual(struct.unpack_from("<2H", blob, DEF_OFF), (12, 20))
        # rows end at the page table (hbk.9): one fixed 13-byte slot per screen.
        self.assertEqual(len(blob), 71 + 2 * 13)
        self.assertEqual(blob[71], 0, "hub page count (not prebaked)")
        self.assertEqual(blob[72:84], bytes(12), "hub slot addresses zero")
        self.assertEqual(blob[84], 0, "smith page count (not prebaked)")
        self.assertEqual(blob[85:97], bytes(12), "smith slot addresses zero")

        hub = parse_def(blob, 12)
        self.assertEqual(hub, {"id": 0, "titleLen": 3, "title": "HUB", "rowCount": 2, "firstRow": 30})
        smith = parse_def(blob, 20)
        self.assertEqual(smith, {"id": 1, "titleLen": 5, "title": "SMITH", "rowCount": 1, "firstRow": 58})

        r0 = parse_row(blob, hub["firstRow"])
        self.assertEqual(r0["label"], "BUY SWORD")
        self.assertEqual((r0["cost"], r0["action"], r0["flags"], r0["cond"], r0["param"]), (100, 1, 0, 1, 0))
        r1 = parse_row(blob, hub["firstRow"] + r0["size"])
        self.assertEqual(r1["label"], "LEAVE")
        self.assertEqual((r1["cost"], r1["action"], r1["flags"], r1["cond"], r1["param"]), (0, 0, 0, 0, 0))

        r2 = parse_row(blob, smith["firstRow"])
        self.assertEqual(r2["label"], "TIER 2")
        self.assertEqual((r2["cost"], r2["action"], r2["flags"], r2["cond"], r2["param"]), (250, 1, 1, 3, 1))
        self.assertEqual(smith["firstRow"] + r2["size"], 71, "rows end at PAGE_TABLE_OFF")

    def test_dump_mode_lists_screens_and_writes_nothing(self):
        result = self.compile("--dump")
        self.assert_succeeds(result)
        self.assertIn("screen hub: id 0 title 'HUB' rows 2 off 12 pages 0", result.stdout)
        self.assertIn("row 'BUY SWORD' cost 100 action buy_upgrade flags 0x00 cond zenny param 0", result.stdout)
        self.assertIn("gen-screens: 2 screens, 3 rows, 0 pages, 97 B blob", result.stdout)
        self.assertFalse(os.path.exists(self.path(BLOB_REL)))
        self.assertFalse(os.path.exists(self.path(META_REL)))
        self.assertFalse(os.path.exists(self.path("fxdata", "screens", "Sprites.txt")))

    def test_screens_sort_by_id_not_file_name(self):
        # Rename smith so its file sorts before hub but its id (1) does not:
        # the defOff table order follows id, so index 0 is always hub.
        os.rename(self.path("data", "screens", "smith.json"), self.path("data", "screens", "aaa_smith.json"))
        self.assert_succeeds(self.compile())
        text = self.read(META_REL)
        self.assertIn("constexpr uint8_t SCREEN_HUB = 0;", text)
        self.assertIn("constexpr uint8_t SCREEN_AAA_SMITH = 1;", text)
        blob = self.read_bytes(BLOB_REL)
        offsets = struct.unpack_from("<2H", blob, DEF_OFF)
        self.assertEqual(parse_def(blob, offsets[0])["id"], 0, "index 0 is the id-0 screen")
        self.assertEqual(parse_def(blob, offsets[1])["id"], 1, "index 1 is the id-1 screen")

    # ------------------------------------------------------- schema errors

    def test_unknown_key_rejected(self):
        self.mutate("data/screens/hub.json", lambda doc: doc.__setitem__("theme", "dark"))
        self.assert_fails(self.compile(), "unknown key 'theme'")

    def test_missing_key_rejected(self):
        self.mutate("data/screens/hub.json", lambda doc: doc.pop("title"))
        self.assert_fails(self.compile(), "missing key 'title'")

    def test_duplicate_id_rejected(self):
        self.mutate("data/screens/smith.json", lambda doc: doc.__setitem__("id", 0))
        self.assert_fails(self.compile(), "duplicate screen id 0")

    def test_unknown_action_rejected(self):
        self.mutate("data/screens/hub.json", lambda doc: doc["rows"][0].__setitem__("action", "sell"))
        self.assert_fails(self.compile(), "action: unknown value 'sell'")

    def test_unknown_condition_rejected(self):
        self.mutate("data/screens/hub.json", lambda doc: doc["rows"][0].__setitem__("condition", "moon"))
        self.assert_fails(self.compile(), "condition: unknown value 'moon'")

    def test_unknown_flag_rejected(self):
        self.mutate("data/screens/hub.json", lambda doc: doc["rows"][0].__setitem__("flags", ["glow"]))
        self.assert_fails(self.compile(), "unknown flag 'glow'")

    def test_hub_navigation_actions_compile(self):
        # qs.4: the hub uses the navigation action set; the generated header must
        # expose their ids (pinned by test_clean_meta_header_constants).
        self.mutate("data/screens/hub.json",
                    lambda doc: doc["rows"][0].update({"action": "hunt", "condition": "always"}))
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        hub = parse_def(blob, struct.unpack_from("<H", blob, DEF_OFF)[0])
        row = parse_row(blob, hub["firstRow"])
        self.assertEqual(row["action"], 5, "hunt action id")
        self.assertEqual(row["cond"], 0, "always condition id")

    def test_equip_weapon_action_compiles(self):
        # hml.3: the gear screen equips a weapon; the generated header exposes
        # the appended action ids (pinned above) and the row packs its param.
        self.mutate("data/screens/hub.json",
                    lambda doc: doc["rows"][0].update({"action": "equip_weapon", "condition": "always",
                                                       "param": 2}))
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        hub = parse_def(blob, struct.unpack_from("<H", blob, DEF_OFF)[0])
        row = parse_row(blob, hub["firstRow"])
        self.assertEqual(row["action"], 7, "equip_weapon action id")
        self.assertEqual(row["param"], 2, "weapon index param")

    def test_open_gear_action_compiles(self):
        self.mutate("data/screens/hub.json",
                    lambda doc: doc["rows"][0].update({"action": "open_gear", "condition": "always"}))
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        hub = parse_def(blob, struct.unpack_from("<H", blob, DEF_OFF)[0])
        row = parse_row(blob, hub["firstRow"])
        self.assertEqual(row["action"], 8, "open_gear action id")

    def test_upgrade_row_action_compiles(self):
        # hbk.11: the UPGRADE class-row action is the next free action id (15);
        # the row packs the weapon class index in `param`.
        self.mutate("data/screens/hub.json",
                    lambda doc: doc["rows"][0].update({"action": "upgrade_row", "condition": "always",
                                                       "param": 2}))
        self.assert_succeeds(self.compile())
        meta = self.read(META_REL)
        self.assertIn("constexpr uint8_t ACTION_UPGRADE_ROW = 15;", meta)
        blob = self.read_bytes(BLOB_REL)
        hub = parse_def(blob, struct.unpack_from("<H", blob, DEF_OFF)[0])
        row = parse_row(blob, hub["firstRow"])
        self.assertEqual((row["action"], row["param"]), (15, 2), "upgrade_row id + class param")

    def test_equip_armor_action_compiles_and_slot_checked(self):
        # ui.3.1: the GEAR armor row opens the card; param packs (slot << 5) | piece.
        self.mutate("data/screens/hub.json",
                    lambda doc: doc["rows"][0].update({"action": "equip_armor", "condition": "always",
                                                       "param": (1 << 5) | 2}))
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        hub = parse_def(blob, struct.unpack_from("<H", blob, DEF_OFF)[0])
        row = parse_row(blob, hub["firstRow"])
        self.assertEqual(row["action"], 9, "equip_armor action id")
        self.assertEqual(row["param"], (1 << 5) | 2, "slot/piece param")

        self.mutate("data/screens/hub.json",
                    lambda doc: doc["rows"][0].update({"action": "equip_armor", "condition": "always",
                                                       "param": (3 << 5) | 2}))
        self.assert_fails(self.compile(), "armor slot must be 0..2")

    def test_forge_weapons_rows_generated_from_tree(self):
        # ui.4 (5co.4): a screen with "weapons" expands the forge tree into a
        # class header + one forge_node row per node (param = node id, cost =
        # upgrade cost, ROW_F_FORGE flag), before the authored rows.
        os.makedirs(self.path("data", "forge"), exist_ok=True)
        with open(self.path("data", "items.json"), "w", encoding="utf-8", newline="\n") as handle:
            json.dump({"version": 1, "items": [
                {"id": "ore", "kind": "material", "heal": 0, "stam": 0, "sell": 1},
                {"id": "scale", "kind": "material", "heal": 0, "stam": 0, "sell": 1},
            ]}, handle, indent=2)
        with open(self.path("data", "forge", "sword.json"), "w", encoding="utf-8", newline="\n") as handle:
            json.dump({
                "class": "sword", "header": "-- SWD --",
                "nodes": [
                    {"id": "sword_base", "label": "SWD T1", "parent": None, "direct": True,
                     "cost": 0, "mats": [], "directCost": 0, "directMats": [],
                     "dmgMul": 100, "spdMul": 100, "desc": ["BLADE."], "sheet": "mh_weapon_sword"},
                    {"id": "sword_t1", "label": "SWD T2", "parent": "sword_base", "direct": True,
                     "cost": 100, "mats": [{"item": "ore", "count": 2}],
                     "directCost": 180, "directMats": [{"item": "ore", "count": 3}],
                     "dmgMul": 110, "spdMul": 105, "desc": ["EDGE."], "sheet": "mh_weapon_sword"},
                ],
            }, handle, indent=2)
        with open(self.path("data", "screens", "forge.json"), "w", encoding="utf-8", newline="\n") as handle:
            json.dump({"id": 2, "title": "FORGE", "weapons": "forge", "rows": [
                {"label": "LEAVE", "cost": 0, "action": "leave"}]}, handle, indent=2)
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        meta = self.read(META_REL)
        self.assertIn("constexpr uint8_t SCREEN_FORGE = 2;", meta)
        self.assertIn("constexpr uint8_t ACTION_FORGE_NODE = 10;", meta)
        self.assertIn("constexpr uint8_t ACTION_OPEN_FORGE = 11;", meta)
        self.assertIn("constexpr uint8_t ROW_F_FORGE = 0x08;", meta)
        offsets = struct.unpack_from("<3H", blob, DEF_OFF)
        forge = parse_def(blob, offsets[2])
        self.assertEqual((forge["id"], forge["rowCount"]), (2, 4))
        rows = []
        off = forge["firstRow"]
        for _ in range(forge["rowCount"]):
            row = parse_row(blob, off)
            rows.append(row)
            off += row["size"]
        self.assertEqual(rows[0]["label"], "-- SWD --")
        self.assertEqual((rows[0]["action"], rows[0]["flags"]), (4, 0))
        self.assertEqual(rows[1]["label"], "SWD T1")
        self.assertEqual((rows[1]["action"], rows[1]["flags"], rows[1]["param"], rows[1]["cost"]),
                         (10, 8, 0, 0))
        self.assertEqual(rows[2]["label"], "+- SWD T2")
        self.assertEqual((rows[2]["param"], rows[2]["cost"]), (1, 100))
        self.assertEqual(rows[3]["label"], "LEAVE")
        # hbk.9: rows end at the fixed page table (13-byte slot per screen).
        self.assertEqual(off, len(blob) - 3 * 13, "rows end at the page table")
        self.assertEqual(blob[off:], bytes(3 * 13), "no screen prebakes pages")

    def write_items_and_forge(self):
        os.makedirs(self.path("data", "forge"), exist_ok=True)
        with open(self.path("data", "items.json"), "w", encoding="utf-8", newline="\n") as handle:
            json.dump({"version": 1, "items": [
                {"id": "ore", "kind": "material", "heal": 0, "stam": 0, "sell": 1},
                {"id": "scale", "kind": "material", "heal": 0, "stam": 0, "sell": 1},
            ]}, handle, indent=2)
        with open(self.path("data", "forge", "sword.json"), "w", encoding="utf-8", newline="\n") as handle:
            json.dump({
                "class": "sword", "header": "-- SWD --",
                "nodes": [
                    {"id": "sword_base", "label": "SWD T1", "parent": None, "direct": True,
                     "cost": 0, "mats": [], "directCost": 0, "directMats": [],
                     "dmgMul": 100, "spdMul": 100, "desc": ["BLADE."], "sheet": "mh_weapon_sword"},
                    {"id": "sword_t1", "label": "SWD T2", "parent": "sword_base", "direct": True,
                     "cost": 100, "mats": [{"item": "ore", "count": 2}],
                     "directCost": 180, "directMats": [{"item": "ore", "count": 3}],
                     "dmgMul": 110, "spdMul": 105, "desc": ["EDGE."], "sheet": "mh_weapon_sword"},
                ],
            }, handle, indent=2)

    def test_craft_weapons_rows_flat_direct_cost(self):
        # hbk.10: "weapons": "craft" emits FLAT forge_node rows (no class headers
        # or tree prefixes) with cost = the node's directCost, before the LEAVE.
        self.write_items_and_forge()
        with open(self.path("data", "screens", "craft.json"), "w", encoding="utf-8", newline="\n") as handle:
            json.dump({"id": 2, "title": "CRAFT", "weapons": "craft", "rows": [
                {"label": "LEAVE", "cost": 0, "action": "leave"}]}, handle, indent=2)
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        meta = self.read(META_REL)
        self.assertIn("constexpr uint8_t ACTION_OPEN_CRAFT = 12;", meta)
        self.assertIn("constexpr uint8_t ACTION_OPEN_UPGRADE = 13;", meta)
        self.assertIn("constexpr uint8_t ACTION_OPEN_ARMOR_FORGE = 14;", meta)
        offsets = struct.unpack_from("<3H", blob, DEF_OFF)
        craft = parse_def(blob, offsets[2])
        self.assertEqual((craft["id"], craft["rowCount"]), (2, 3))
        rows = []
        off = craft["firstRow"]
        for _ in range(craft["rowCount"]):
            row = parse_row(blob, off)
            rows.append(row)
            off += row["size"]
        # Flat: the first row is the node label, not the "-- SWD --" header.
        self.assertEqual(rows[0]["label"], "SWD T1")
        self.assertEqual((rows[0]["action"], rows[0]["flags"], rows[0]["param"], rows[0]["cost"]),
                         (10, 8, 0, 0))
        self.assertEqual(rows[1]["label"], "SWD T2")
        self.assertEqual((rows[1]["param"], rows[1]["cost"]), (1, 180))
        self.assertEqual(rows[2]["label"], "LEAVE")

    def write_armor_fixture(self):
        with open(self.path("data", "items.json"), "w", encoding="utf-8", newline="\n") as handle:
            json.dump({"version": 1, "items": [
                {"id": "ore", "kind": "material", "heal": 0, "stam": 0, "sell": 1},
                {"id": "scale", "kind": "material", "heal": 0, "stam": 0, "sell": 1},
            ]}, handle, indent=2)
        with open(self.path("data", "skills.json"), "w", encoding="utf-8", newline="\n") as handle:
            json.dump({"version": 1, "thresholds": {"s": 10, "m": 15}, "skills": [
                {"id": "attack_up", "kind": "ATTACK_UP", "maxPoints": 15, "perPoint": 1, "abbr": "ATK"},
            ]}, handle, indent=2)
        with open(self.path("data", "armor.json"), "w", encoding="utf-8", newline="\n") as handle:
            json.dump({"version": 1, "pieces": [
                {"id": "hunter_helm", "label": "HUNTER HELM", "slot": "head", "defense": 10,
                 "resist": {"fire": 1, "water": 0, "ice": 0, "thunder": -1},
                 "skills": [{"id": "attack_up", "points": 6}],
                 "recipe": {"materials": [{"item": "ore", "count": 3}], "zenny": 300}},
                {"id": "bone_cap", "label": "BONE CAP", "slot": "head", "defense": 6,
                 "resist": {"fire": 0, "water": 0, "ice": -1, "thunder": 1},
                 "skills": [{"id": "attack_up", "points": 4}],
                 "recipe": {"materials": [{"item": "scale", "count": 2}], "zenny": 200}},
                {"id": "hunter_mail", "label": "HUNTER MAIL", "slot": "body", "defense": 14,
                 "resist": {"fire": 1, "water": 0, "ice": 0, "thunder": -1},
                 "skills": [{"id": "attack_up", "points": 6}],
                 "recipe": {"materials": [{"item": "scale", "count": 3}], "zenny": 400}},
            ]}, handle, indent=2)

    def test_armor_rows_from_armor_json(self):
        # hbk.10: "armor": true emits one equip_armor row per data/armor.json
        # piece: label + recipe zenny + param = (slot << 5) | piece index.
        self.write_armor_fixture()
        with open(self.path("data", "screens", "armor_forge.json"), "w", encoding="utf-8", newline="\n") as handle:
            json.dump({"id": 2, "title": "ARMOR", "armor": True, "rows": [
                {"label": "LEAVE", "cost": 0, "action": "leave"}]}, handle, indent=2)
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        offsets = struct.unpack_from("<3H", blob, DEF_OFF)
        armor = parse_def(blob, offsets[2])
        self.assertEqual((armor["id"], armor["rowCount"]), (2, 4))
        rows = []
        off = armor["firstRow"]
        for _ in range(armor["rowCount"]):
            row = parse_row(blob, off)
            rows.append(row)
            off += row["size"]
        self.assertEqual(rows[0]["label"], "HUNTER HELM")
        self.assertEqual((rows[0]["action"], rows[0]["cost"], rows[0]["param"]), (9, 300, 0))
        self.assertEqual(rows[1]["label"], "BONE CAP")
        self.assertEqual((rows[1]["cost"], rows[1]["param"]), (200, 1))
        self.assertEqual(rows[2]["label"], "HUNTER MAIL")
        self.assertEqual((rows[2]["cost"], rows[2]["param"]), (400, (1 << 5) | 2))
        self.assertEqual(rows[3]["label"], "LEAVE")

    def write_slots_gear(self, action="slot_pick", param=0):
        self.write_armor_fixture()
        self.write_items_and_forge()
        with open(self.path("data", "screens", "gear.json"), "w", encoding="utf-8", newline="\n") as handle:
            json.dump({"id": 2, "title": "GEAR", "slots": True, "rows": [
                {"label": "WEAPON", "cost": 0, "action": action, "condition": "always", "param": param},
                {"label": "LEAVE", "cost": 0, "action": "leave"}]}, handle, indent=2)

    def test_slots_candidate_table(self):
        # hbk.12: a screen with "slots": true gets a candidate table appended to
        # the blob -- u8 slotStart[5] (cumulative), 4-byte {u8 id, u24 labelOff}
        # entries, then the label records; the header exposes its offset. Slot 0
        # is the forge nodes, slots 1/2/3 the armor pieces by slot.
        self.write_slots_gear()
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        meta = self.read(META_REL)
        self.assertIn("constexpr uint8_t ACTION_SLOT_PICK = 16;", meta)
        self.assertIn("constexpr uint16_t SCREEN_GEAR_SLOT_TABLE = ", meta)
        off = int(meta.split("SCREEN_GEAR_SLOT_TABLE = ")[1].split(";")[0])
        # The slot table trails the page table (3 screens x 13 B) and holds the
        # 5 entries plus their label records (u8 len + bytes).
        labels = (1 + 6) + (1 + 6) + (1 + 11) + (1 + 8) + (1 + 11)   # SWD T1/T2, HUNTER HELM, BONE CAP, HUNTER MAIL
        self.assertEqual(off, len(blob) - (5 + 5 * 4 + labels), "slot table trails the page table")
        self.assertEqual(list(blob[off:off + 5]), [0, 2, 4, 5, 5], "slotStart cumulative counts")
        entries = off + 5
        # slot 0: the two sword nodes, ids 0/1, labels from the node records.
        self.assertEqual(blob[entries], 0, "weapon cand0 id")
        self.assertEqual(blob[entries + 4], 1, "weapon cand1 id")
        label_off = blob[entries + 1] | (blob[entries + 2] << 8) | (blob[entries + 3] << 16)
        self.assertEqual(blob[label_off:label_off + 7], b"\x06SWD T1", "weapon cand0 label record")
        # slot 1: head pieces 0,1; slot 2: body piece 2; slot 3: empty.
        self.assertEqual((blob[entries + 2 * 4], blob[entries + 3 * 4], blob[entries + 4 * 4]),
                         (0, 1, 2), "armor candidate ids")
        body_off = blob[entries + 4 * 4 + 1] | (blob[entries + 4 * 4 + 2] << 8) | (blob[entries + 4 * 4 + 3] << 16)
        self.assertEqual(blob[body_off:body_off + 12], b"\x0bHUNTER MAIL", "body cand0 label record")

    def test_slot_pick_param_out_of_range_rejected(self):
        self.write_slots_gear(param=4)
        self.assert_fails(self.compile(), "slot must be 0..3")

    def test_weapons_and_armor_together_rejected(self):
        # A screen picks one generated row source (weapons OR armor).
        self.write_items_and_forge()
        with open(self.path("data", "screens", "craft.json"), "w", encoding="utf-8", newline="\n") as handle:
            json.dump({"id": 2, "title": "BAD", "weapons": "craft", "armor": True, "rows": [
                {"label": "LEAVE", "cost": 0, "action": "leave"}]}, handle, indent=2)
        self.assert_fails(self.compile(), "pick one generated row source")

    def test_zenny_flag_retired(self):
        # ui.5 retired the qs.4 zenny dynamic-value token (the live balance moved
        # to the list header), so the flag name is no longer accepted.
        self.mutate("data/screens/hub.json",
                    lambda doc: doc["rows"][1].update({"action": "none", "flags": ["zenny"]}))
        self.assert_fails(self.compile(), "unknown flag 'zenny'")

    def test_label_too_long_rejected(self):
        self.mutate("data/screens/hub.json", lambda doc: doc["rows"][0].__setitem__("label", "X" * 17))
        self.assert_fails(self.compile(), "label: length 17 outside 1..16")

    def test_title_too_long_rejected(self):
        self.mutate("data/screens/hub.json", lambda doc: doc.__setitem__("title", "X" * 17))
        self.assert_fails(self.compile(), "title: length 17 outside 1..16")

    def test_non_printable_char_rejected(self):
        self.mutate("data/screens/hub.json", lambda doc: doc.__setitem__("title", "HU\x01B"))
        self.assert_fails(self.compile(), "printable ASCII")

    def test_tier_param_out_of_range_rejected(self):
        self.mutate("data/screens/smith.json", lambda doc: doc["rows"][0].__setitem__("param", 3))
        self.assert_fails(self.compile(), "tier index must be 0..2")

    def test_flag_param_out_of_range_rejected(self):
        self.mutate("data/screens/hub.json",
                    lambda doc: doc["rows"][0].update({"condition": "flag", "param": 32}))
        self.assert_fails(self.compile(), "save flag bit must be 0..31")

    def test_quest_condition_requires_quest_action(self):
        self.mutate("data/screens/hub.json",
                    lambda doc: doc["rows"][0].update({"condition": "quest", "param": 0}))
        self.assert_fails(self.compile(), "condition 'quest' needs a take_quest/turn_in_quest action")

    def test_take_quest_need_nibble_must_be_zero(self):
        self.mutate("data/screens/hub.json",
                    lambda doc: doc["rows"][0].update({"action": "take_quest", "condition": "quest",
                                                       "param": 16}))
        self.assert_fails(self.compile(), "take_quest need nibble must be 0")

    def test_turn_in_quest_need_nibble_required(self):
        self.mutate("data/screens/hub.json",
                    lambda doc: doc["rows"][0].update({"action": "turn_in_quest", "condition": "quest",
                                                       "param": 0}))
        self.assert_fails(self.compile(), "turn_in_quest need nibble must be 1..15")

    def test_upgrade_condition_requires_buy_action(self):
        self.mutate("data/screens/hub.json",
                    lambda doc: doc["rows"][0].update({"action": "leave", "condition": "upgrade",
                                                       "param": 1}))
        self.assert_fails(self.compile(), "condition 'upgrade' needs a buy_upgrade action")

    def test_upgrade_weapon_out_of_range_rejected(self):
        self.mutate("data/screens/hub.json",
                    lambda doc: doc["rows"][0].update({"action": "buy_upgrade", "condition": "upgrade",
                                                       "param": (3 << 2) | 1}))
        self.assert_fails(self.compile(), "upgrade weapon index must be 0..2")

    def test_upgrade_tier_out_of_range_rejected(self):
        self.mutate("data/screens/hub.json",
                    lambda doc: doc["rows"][0].update({"action": "buy_upgrade", "condition": "upgrade",
                                                       "param": 0}))
        self.assert_fails(self.compile(), "upgrade tier must be 1..3")

    def test_empty_rows_rejected(self):
        self.mutate("data/screens/hub.json", lambda doc: doc.__setitem__("rows", []))
        self.assert_fails(self.compile(), "rows: expected a non-empty array")

    def test_cost_out_of_range_rejected(self):
        self.mutate("data/screens/hub.json", lambda doc: doc["rows"][0].__setitem__("cost", 70000))
        self.assert_fails(self.compile(), "cost: out of range 0..65535")

    def test_missing_screens_dir_rejected(self):
        shutil.rmtree(self.path("data", "screens"))
        self.assert_fails(self.compile(), "missing screens directory")

    # ------------------------------------------------------- prebake (hbk.2)

    def prebake_hub(self):
        self.mutate("data/screens/hub.json", lambda doc: doc.__setitem__("prebake", True))

    def test_prebake_bakes_pages_and_page_table(self):
        self.prebake_hub()
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        # Page table: fixed 13-byte slots; hub (1 page, address unresolved
        # without fxdata.h) then smith (0 pages).
        self.assertEqual(len(blob), 71 + 2 * 13)
        self.assertEqual(blob[71], 1, "hub page count")
        self.assertEqual(blob[72:75], b"\x00\x00\x00", "unresolved page address")
        self.assertEqual(blob[75:84], bytes(9), "hub unused slots zero")
        self.assertEqual(blob[84], 0, "smith page count")
        self.assertEqual(blob[85:97], bytes(12), "smith slot addresses zero")
        text = self.read(META_REL)
        self.assertIn("constexpr uint16_t PAGE_TABLE_OFF = 71;", text)
        self.assertIn("constexpr uint8_t SCREEN_HUB_PAGES = 1;", text)
        self.assertIn("constexpr uint16_t SCREEN_HUB_PAGE_TABLE = 71;", text)
        self.assertIn("constexpr uint8_t SCREEN_SMITH_PAGES = 0;", text)
        self.assertIn("constexpr uint16_t SCREEN_SMITH_PAGE_TABLE = 84;", text)
        self.assertTrue(os.path.isfile(self.path("images", "screens", "mh_screen_hub_0_128x64.png")))
        self.assertTrue(os.path.isfile(self.path("fxdata", "screens", "Sprites.txt")))

    def test_prebake_layers_match_the_frozen_layout(self):
        self.prebake_hub()
        self.assert_succeeds(self.compile())
        layers = parse_layers(self.read("fxdata", "screens", "Sprites.txt"), "mh_screen_hub_0")
        self.assertEqual(len(layers), 3 * 1024)
        # (0,0)..(0,7) is the dark title band (shade 1): plane 0 only.
        self.assertEqual(layers[0], 0xFF)
        self.assertEqual(layers[1024], 0x00)
        self.assertEqual(layers[2048], 0x00)
        # The rule at y=8 is light (shade 2): planes 0 + 1, not 2.
        self.assertEqual(layers[128], 0x01)
        self.assertEqual(layers[1024 + 128], 0x01)
        self.assertEqual(layers[2048 + 128], 0x00)
        # The first row's glyph lane (BUY SWORD, light) starts at x=10, y=11.
        self.assertEqual(layers[128 + 10] & 0x08, 0x08, "row glyph lit on plane 0")
        self.assertEqual(layers[1024 + 128 + 10] & 0x08, 0x08, "row glyph lit on plane 1")
        self.assertEqual(layers[2048 + 128 + 10] & 0x08, 0x00, "row glyph off on plane 2")
        # The cost 100 is white (shade 3, all planes) ending at x=112.
        self.assertEqual(layers[128 + 106] & 0x08, 0x08, "cost digit lit on plane 0")
        self.assertEqual(layers[1024 + 128 + 106] & 0x08, 0x08, "cost digit lit on plane 1")
        self.assertEqual(layers[2048 + 128 + 106] & 0x08, 0x08, "cost digit lit on plane 2")

    def test_prebake_page_address_resolves_from_fxdata_header(self):
        self.prebake_hub()
        os.makedirs(self.path("fxdata"), exist_ok=True)
        with open(self.path("fxdata", "fxdata.h"), "w", encoding="utf-8", newline="\n") as handle:
            handle.write("constexpr uint24_t mh_screen_hub_0 = 0x0342F7;\n")
        result = self.compile()
        self.assert_succeeds(result)
        blob = self.read_bytes(BLOB_REL)
        self.assertEqual(blob[72:75], bytes([0xF7, 0x42, 0x03]))
        self.assertNotIn("unresolved", result.stdout)

    def test_prebake_pages_are_deterministic(self):
        self.prebake_hub()
        self.assert_succeeds(self.compile())
        first_blob = self.read_bytes(BLOB_REL)
        first_sprites = self.read("fxdata", "screens", "Sprites.txt")
        first_png = self.read_bytes("images", "screens", "mh_screen_hub_0_128x64.png")
        second = self.compile()
        self.assert_succeeds(second)
        self.assertIn("%s (unchanged)" % BLOB_REL, second.stdout)
        self.assertIn("%s (unchanged)" % "fxdata/screens/Sprites.txt", second.stdout)
        self.assertEqual(first_blob, self.read_bytes(BLOB_REL))
        self.assertEqual(first_sprites, self.read("fxdata", "screens", "Sprites.txt"))
        self.assertEqual(first_png, self.read_bytes("images", "screens", "mh_screen_hub_0_128x64.png"))

    def test_prebake_stale_page_removed(self):
        self.prebake_hub()
        self.assert_succeeds(self.compile())
        stale = self.path("images", "screens", "mh_screen_old_0_128x64.png")
        with open(stale, "wb") as handle:
            handle.write(b"stale")
        self.assert_succeeds(self.compile())
        self.assertFalse(os.path.exists(stale))

    def test_prebake_sheet_renders(self):
        self.prebake_hub()
        result = self.compile("--dump", "--sheet", "build/sheet.png")
        self.assert_succeeds(result)
        self.assertIn("contact sheet", result.stdout)
        self.assertTrue(os.path.isfile(self.path("build", "sheet.png")))
        self.assertFalse(os.path.exists(self.path(BLOB_REL)))

    def test_prebake_cost_cap_rejected(self):
        def doc_fn(doc):
            doc["prebake"] = True
            doc["rows"][0]["cost"] = 1000
        self.mutate("data/screens/hub.json", doc_fn)
        self.assert_fails(self.compile(), "exceeds the 3-digit bake cap")

    def test_prebake_page_cap_rejected(self):
        # hbk.9: the fixed 13-byte slot carries 4 page addresses (24 rows).
        def doc_fn(doc):
            doc["prebake"] = True
            doc["rows"] = [{"label": "ROW %d" % i, "cost": 0, "action": "leave"} for i in range(25)]
        self.mutate("data/screens/hub.json", doc_fn)
        self.assert_fails(self.compile(), "exceed the 4 baked pages")

    def test_prebake_non_bool_rejected(self):
        self.mutate("data/screens/hub.json", lambda doc: doc.__setitem__("prebake", "yes"))
        self.assert_fails(self.compile(), "prebake: expected a boolean")

    def test_page_indicator_bakes_on_multi_page_screens(self):
        def doc_fn(doc):
            doc["prebake"] = True
            doc["rows"] = [{"label": "ROW %d" % i, "cost": 0, "action": "leave"} for i in range(7)]
        self.mutate("data/screens/hub.json", doc_fn)
        self.assert_succeeds(self.compile())
        text = self.read(META_REL)
        self.assertIn("constexpr uint8_t SCREEN_HUB_PAGES = 2;", text)
        for page in range(2):
            layers = parse_layers(self.read("fxdata", "screens", "Sprites.txt"), "mh_screen_hub_%d" % page)
            # `n/m` (light) sits after the 3-char title: x = 2 + 12 + 4 = 18.
            self.assertEqual(layers[18] & 0x01, 0x01, "page indicator ink on page %d" % page)


if __name__ == "__main__":
    unittest.main()
