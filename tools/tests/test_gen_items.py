#!/usr/bin/env python3
"""Unit tests for tools/gen-items.py (run: make test-tools).

The clean fixture under fixtures/gen_items/clean is a minimal item table (a
consumable + a material). Every failure case copies it to build/tests/gen_items/
and mutates the copy, so the tests stay read-only on the repository fixtures and
never write into /tmp.
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
TOOL = os.path.join(TOOLS, "gen-items.py")
FIXTURE = os.path.join(HERE, "fixtures", "gen_items", "clean")
SCRATCH = os.path.join(ROOT, "build", "tests", "gen_items")

DATA_REL = "data/items.json"
BLOB_REL = "fxdata/tables/items.bin"
DATA_HPP_REL = "src/generated/items_data.hpp"
META_REL = "src/generated/items_meta.hpp"
EXPECT_REL = "src/generated/items_expect.hpp"

HEADER = struct.Struct("<HBBBBH")
RECORD = struct.Struct("<BBBH")


def parse_record(blob, off):
    kind, heal, stam, sell = RECORD.unpack_from(blob, off)
    return {"kind": kind, "heal": heal, "stam": stam, "sell": sell}


def run_tool(*args):
    return subprocess.run([sys.executable, TOOL, *args], capture_output=True, text=True)


class GenItemsTests(unittest.TestCase):
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

    def mutate(self, fn):
        with open(self.path(DATA_REL), encoding="utf-8") as handle:
            doc = json.load(handle)
        fn(doc)
        with open(self.path(DATA_REL), "w", encoding="utf-8", newline="\n") as handle:
            json.dump(doc, handle, indent=2)
            handle.write("\n")

    # ---------------------------------------------------------------- clean

    def test_clean_compile_is_deterministic(self):
        self.assert_succeeds(self.compile())
        for rel in (BLOB_REL, DATA_HPP_REL, META_REL, EXPECT_REL):
            self.assertTrue(os.path.isfile(self.path(rel)), rel)
        first = {rel: self.read_bytes(rel) for rel in (BLOB_REL, DATA_HPP_REL, META_REL, EXPECT_REL)}
        second = self.compile()
        self.assert_succeeds(second)
        for rel in (BLOB_REL, DATA_HPP_REL, META_REL, EXPECT_REL):
            self.assertIn("%s (unchanged)" % rel, second.stdout)
            self.assertEqual(first[rel], self.read_bytes(rel), rel)

    def test_clean_meta_header_constants(self):
        self.assert_succeeds(self.compile())
        text = self.read(META_REL)
        for needle in (
            "constexpr uint16_t MAGIC = 0x5449;",
            "constexpr uint8_t VERSION = 1;",
            "constexpr uint8_t HEADER_SIZE = 8;",
            "constexpr uint8_t ITEM_SIZE = 5;",
            "constexpr uint8_t ITEM_COUNT = 2;",
            "constexpr uint8_t ITEM_MAX = 16;",
            "constexpr uint16_t ITEMS_OFF = 8;",
            "constexpr uint8_t KIND_CONSUMABLE = 0;",
            "constexpr uint8_t KIND_MATERIAL = 1;",
            "constexpr uint8_t ITEM_KIND_OFF = 0;",
            "constexpr uint8_t ITEM_HEAL_OFF = 1;",
            "constexpr uint8_t ITEM_STAM_OFF = 2;",
            "constexpr uint8_t ITEM_SELL_OFF = 3;",
            "constexpr uint8_t ITEM_HERB = 0;",
            "constexpr uint16_t ITEM_HERB_OFF = 8;",
            "constexpr uint8_t ITEM_ORE = 1;",
            "constexpr uint16_t ITEM_ORE_OFF = 13;",
        ):
            self.assertIn(needle, text)
        data = self.read(DATA_HPP_REL)
        self.assertIn("struct Item {", data)
        self.assertIn("inline constexpr std::array<Item, 2> ITEMS", data)
        expect = self.read(EXPECT_REL)
        self.assertIn("constexpr uint16_t BLOB_SIZE = 18;", expect)
        self.assertIn("constexpr uint8_t ITEM_HERB_HEAL = 20;", expect)
        self.assertIn("constexpr uint16_t ITEM_ORE_SELL = 30;", expect)

    def test_clean_blob_layout(self):
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        magic, version, flags, count, reserved, reserved2 = HEADER.unpack_from(blob, 0)
        self.assertEqual((magic, version, flags, count, reserved, reserved2),
                         (0x5449, 1, 0, 2, 0, 0))
        self.assertEqual(len(blob), 18)
        self.assertEqual(parse_record(blob, 8),
                         {"kind": 0, "heal": 20, "stam": 0, "sell": 10})
        self.assertEqual(parse_record(blob, 13),
                         {"kind": 1, "heal": 0, "stam": 0, "sell": 30})

    def test_dump_mode_lists_items_and_writes_nothing(self):
        result = self.compile("--dump")
        self.assert_succeeds(result)
        self.assertIn("item herb: kind consumable heal 20 stam 0 sell 10", result.stdout)
        self.assertIn("item ore: kind material heal 0 stam 0 sell 30", result.stdout)
        self.assertIn("gen-items: 2 items, 18 B blob", result.stdout)
        self.assertFalse(os.path.exists(self.path(BLOB_REL)))
        self.assertFalse(os.path.exists(self.path(META_REL)))

    def test_source_order_sets_item_indices(self):
        self.mutate(lambda doc: doc["items"].reverse())
        self.assert_succeeds(self.compile())
        text = self.read(META_REL)
        self.assertIn("constexpr uint8_t ITEM_ORE = 0;", text)
        self.assertIn("constexpr uint8_t ITEM_HERB = 1;", text)
        blob = self.read_bytes(BLOB_REL)
        self.assertEqual(parse_record(blob, 8)["kind"], 1, "ore is record 0")

    # ------------------------------------------------------- schema errors

    def test_unknown_key_rejected(self):
        self.mutate(lambda doc: doc["items"][0].__setitem__("color", "red"))
        self.assert_fails(self.compile(), "unknown key 'color'")

    def test_missing_key_rejected(self):
        self.mutate(lambda doc: doc["items"][0].pop("heal"))
        self.assert_fails(self.compile(), "missing key 'heal'")

    def test_unknown_kind_rejected(self):
        self.mutate(lambda doc: doc["items"][0].__setitem__("kind", "tool"))
        self.assert_fails(self.compile(), "kind: unknown value 'tool'")

    def test_duplicate_id_rejected(self):
        self.mutate(lambda doc: doc["items"][1].__setitem__("id", "herb"))
        self.assert_fails(self.compile(), "duplicate id 'herb'")

    def test_bad_id_rejected(self):
        self.mutate(lambda doc: doc["items"][0].__setitem__("id", "Blue Mushroom"))
        self.assert_fails(self.compile(), "must match [a-z][a-z0-9_]*")

    def test_heal_out_of_range_rejected(self):
        self.mutate(lambda doc: doc["items"][0].__setitem__("heal", 300))
        self.assert_fails(self.compile(), "heal: out of range 0..255")

    def test_stam_out_of_range_rejected(self):
        self.mutate(lambda doc: doc["items"][0].__setitem__("stam", -1))
        self.assert_fails(self.compile(), "stam: out of range 0..255")

    def test_sell_out_of_range_rejected(self):
        self.mutate(lambda doc: doc["items"][1].__setitem__("sell", 70000))
        self.assert_fails(self.compile(), "sell: out of range 0..65535")

    def test_float_rejected(self):
        self.mutate(lambda doc: doc["items"][0].__setitem__("heal", 20.0))
        self.assert_fails(self.compile(), "heal: expected an integer")

    def test_bool_rejected(self):
        self.mutate(lambda doc: doc["items"][0].__setitem__("stam", True))
        self.assert_fails(self.compile(), "stam: expected an integer")

    def test_empty_items_rejected(self):
        self.mutate(lambda doc: doc.__setitem__("items", []))
        self.assert_fails(self.compile(), "items: expected a non-empty array")

    def test_unknown_root_key_rejected(self):
        self.mutate(lambda doc: doc.__setitem__("theme", "dark"))
        self.assert_fails(self.compile(), "unknown key 'theme'")

    def test_item_cap_rejected(self):
        def grow(doc):
            doc["items"] = [
                {"id": "item_%02d" % i, "kind": "material", "heal": 0, "stam": 0, "sell": 1}
                for i in range(17)
            ]
        self.mutate(grow)
        self.assert_fails(self.compile(), "size limit: 17 items exceed the 16 item cap")

    def test_missing_file_rejected(self):
        os.remove(self.path(DATA_REL))
        self.assert_fails(self.compile(), "missing item file")


if __name__ == "__main__":
    unittest.main()
