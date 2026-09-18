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
            "constexpr uint8_t TIER_COUNT = 3;",
            "constexpr uint8_t ACTION_LEAVE = 0;",
            "constexpr uint8_t ACTION_BUY_UPGRADE = 1;",
            "constexpr uint8_t ACTION_TAKE_QUEST = 2;",
            "constexpr uint8_t ACTION_TURN_IN_QUEST = 3;",
            "constexpr uint8_t COND_ALWAYS = 0;",
            "constexpr uint8_t COND_ZENNY = 1;",
            "constexpr uint8_t COND_FLAG = 2;",
            "constexpr uint8_t COND_TIER = 3;",
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
