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
            "constexpr uint8_t ROW_F_ZENNY = 0x02;",
            "constexpr uint8_t SCREEN_HUB = 0;",
            "constexpr uint16_t SCREEN_HUB_OFF = 12;",
            "constexpr uint8_t SCREEN_HUB_ROWS = 2;",
            "constexpr uint8_t SCREEN_HUB_TITLE_LEN = 3;",
            "constexpr uint16_t SCREEN_HUB_FIRST_ROW = 30;",
            "constexpr uint8_t SCREEN_SMITH = 1;",
            "constexpr uint16_t SCREEN_SMITH_OFF = 20;",
            "constexpr uint8_t SCREEN_SMITH_ROWS = 1;",
            "constexpr uint16_t SCREEN_SMITH_FIRST_ROW = 58;",
        ):
            self.assertIn(needle, text)

    def test_clean_blob_layout(self):
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        magic, version, flags, count, reserved, row_count = HEADER.unpack_from(blob, 0)
        self.assertEqual((magic, version, flags, count, reserved, row_count),
                         (0x5343, 1, 0, 2, 0, 3))
        self.assertEqual(struct.unpack_from("<2H", blob, DEF_OFF), (12, 20))
        self.assertEqual(len(blob), 71)

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
        self.assertEqual(smith["firstRow"] + r2["size"], len(blob))

    def test_dump_mode_lists_screens_and_writes_nothing(self):
        result = self.compile("--dump")
        self.assert_succeeds(result)
        self.assertIn("screen hub: id 0 title 'HUB' rows 2", result.stdout)
        self.assertIn("row 'BUY SWORD' cost 100 action buy_upgrade flags 0x00 cond zenny param 0", result.stdout)
        self.assertIn("gen-screens: 2 screens, 3 rows, 71 B blob", result.stdout)
        self.assertFalse(os.path.exists(self.path(BLOB_REL)))
        self.assertFalse(os.path.exists(self.path(META_REL)))

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
        self.assertEqual(off, len(blob), "rows end the blob")

    def test_zenny_dynamic_value_flag_compiles(self):
        self.mutate("data/screens/hub.json",
                    lambda doc: doc["rows"][1].update({"action": "none", "flags": ["zenny"]}))
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        hub = parse_def(blob, struct.unpack_from("<H", blob, DEF_OFF)[0])
        first = parse_row(blob, hub["firstRow"])
        second = parse_row(blob, hub["firstRow"] + first["size"])
        self.assertEqual(second["flags"], 2, "ROW_F_ZENNY bit")
        self.assertEqual(second["action"], 4, "none action id")

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


if __name__ == "__main__":
    unittest.main()
