#!/usr/bin/env python3
"""Unit tests for tools/gen-forge.py (run: make test-tools).

Builds a synthetic data tree under build/tests/gen_forge/ (no /tmp) and pins:
the packed forge-node ABI (header + fixed 17 B records), the tree depth/branch
metadata, the generated node/offset/TIER_NODE constants, the row-label tree
prefixes, deterministic re-runs, and the schema validation failures.
"""
import importlib.util
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
TOOL = os.path.join(TOOLS, "gen-forge.py")
SCRATCH = os.path.join(ROOT, "build", "tests", "gen_forge")

spec = importlib.util.spec_from_file_location("gen_forge", TOOL)
gen_forge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen_forge)

HEADER = struct.Struct("<HBBBBH")
NODE = struct.Struct("<BBBBBH H")
MAT = struct.Struct("<BBBB")
HEADER_SIZE = 8
RECORD_SIZE = 17
BLOB_REL = "fxdata/tables/forge.bin"
META_REL = "src/generated/forge_meta.hpp"

ITEMS = {
    "version": 1,
    "items": [
        {"id": "herb", "kind": "consumable", "heal": 20, "stam": 0, "sell": 10},
        {"id": "ore", "kind": "material", "heal": 0, "stam": 0, "sell": 30},
        {"id": "scale", "kind": "material", "heal": 0, "stam": 0, "sell": 25},
    ],
}

FLAIL = {
    "class": "flail",
    "header": "-- FLAIL --",
    "nodes": [
        {"id": "flail_base", "label": "FL T1", "parent": None, "direct": True,
         "cost": 0, "mats": [], "directCost": 0, "directMats": [],
         "dmgMul": 100, "spdMul": 100, "desc": ["BALL FLAIL."], "sheet": "mh_weapon_flail"},
        {"id": "flail_t1", "label": "FL T2", "parent": "flail_base", "direct": True,
         "cost": 120, "mats": [{"item": "ore", "count": 2}],
         "directCost": 200, "directMats": [{"item": "ore", "count": 3}],
         "dmgMul": 112, "spdMul": 103, "desc": ["HEAVIER HEAD."], "sheet": "mh_weapon_flail"},
    ],
}

SWORD = {
    "class": "sword",
    "header": "-- SWD --",
    "nodes": [
        {"id": "sword_base", "label": "SWD T1", "parent": None, "direct": True,
         "cost": 0, "mats": [], "directCost": 0, "directMats": [],
         "dmgMul": 100, "spdMul": 100, "desc": ["PLAIN BLADE."], "sheet": "mh_weapon_sword"},
        {"id": "sword_t1", "label": "SWD T2", "parent": "sword_base", "direct": True,
         "cost": 100, "mats": [{"item": "ore", "count": 2}],
         "directCost": 180, "directMats": [{"item": "ore", "count": 3}],
         "dmgMul": 110, "spdMul": 105, "desc": ["SHARP EDGE."], "sheet": "mh_weapon_sword"},
        {"id": "sword_t2a", "label": "SWD T3A", "parent": "sword_t1", "direct": True,
         "cost": 250, "mats": [{"item": "scale", "count": 1}],
         "directCost": 400, "directMats": [{"item": "scale", "count": 2}],
         "dmgMul": 125, "spdMul": 115, "desc": ["MASTER A."], "sheet": "mh_weapon_sword"},
        {"id": "sword_t2b", "label": "SWD T3B", "parent": "sword_t1", "direct": False,
         "cost": 320, "mats": [{"item": "scale", "count": 2}],
         "directCost": 320, "directMats": [{"item": "scale", "count": 2}],
         "dmgMul": 120, "spdMul": 118, "desc": ["MASTER B."], "sheet": "mh_weapon_sword"},
    ],
}


def write_tree(case):
    data = os.path.join(case, "data")
    forge = os.path.join(data, "forge")
    os.makedirs(forge, exist_ok=True)
    os.makedirs(os.path.join(case, "fxdata", "tables"), exist_ok=True)
    os.makedirs(os.path.join(case, "src", "generated"), exist_ok=True)
    for path, doc in ((os.path.join(data, "items.json"), ITEMS),
                      (os.path.join(forge, "flail.json"), FLAIL),
                      (os.path.join(forge, "sword.json"), SWORD)):
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(doc, handle, indent=2)
            handle.write("\n")
    return case


def parse_records(blob):
    magic, version, flags, count, _r0, _r1 = HEADER.unpack_from(blob, 0)
    records = []
    for i in range(count):
        off = HEADER_SIZE + i * RECORD_SIZE
        cls, parent, nflags, dmg, spd, cost, direct_cost = NODE.unpack_from(blob, off)
        mats = list(MAT.unpack_from(blob, off + 9))
        direct = list(MAT.unpack_from(blob, off + 13))
        records.append({"class": cls, "parent": parent, "flags": nflags, "dmg": dmg,
                        "spd": spd, "cost": cost, "direct_cost": direct_cost,
                        "mats": mats, "direct_mats": direct, "off": off})
    return {"magic": magic, "version": version, "flags": flags, "count": count}, records


class GenForgeTests(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        case = os.path.join(SCRATCH, self._testMethodName)
        shutil.rmtree(case, ignore_errors=True)
        write_tree(case)
        self.case = case

    def path(self, *parts):
        return os.path.join(self.case, *parts)

    def read(self, *parts):
        with open(self.path(*parts), encoding="utf-8") as handle:
            return handle.read()

    def read_bytes(self, *parts):
        with open(self.path(*parts), "rb") as handle:
            return handle.read()

    def run_tool(self, *extra):
        return subprocess.run([sys.executable, TOOL, "--root", self.case, *extra],
                              capture_output=True, text=True)

    def run_ok(self, *extra):
        result = self.run_tool(*extra)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def mutate(self, rel, fn):
        with open(self.path(rel), encoding="utf-8") as handle:
            doc = json.load(handle)
        fn(doc)
        with open(self.path(rel), "w", encoding="utf-8", newline="\n") as handle:
            json.dump(doc, handle, indent=2)
            handle.write("\n")

    def load(self):
        return parse_records(self.read_bytes(BLOB_REL))

    # --------------------------------------------------------------- ABI
    def test_clean_compile_is_deterministic(self):
        first = self.run_ok()
        self.assertIn("gen-forge: 6 nodes, 110 B blob", first.stdout)
        blob = self.read_bytes(BLOB_REL)
        meta = self.read(META_REL)
        second = self.run_ok()
        self.assertIn("%s (unchanged)" % BLOB_REL, second.stdout)
        self.assertIn("%s (unchanged)" % META_REL, second.stdout)
        self.assertEqual(self.read_bytes(BLOB_REL), blob)
        self.assertEqual(self.read(META_REL), meta)

    def test_header_and_record_layout(self):
        self.run_ok()
        header, records = self.load()
        self.assertEqual(header["magic"], 0x4647)
        self.assertEqual(header["version"], 1)
        self.assertEqual(header["count"], 6)
        self.assertEqual(len(records), 6)
        blob = self.read_bytes(BLOB_REL)
        self.assertEqual(len(blob), HEADER_SIZE + RECORD_SIZE * 6)
        for i, record in enumerate(records):
            self.assertEqual(record["off"], HEADER_SIZE + i * RECORD_SIZE)

    def test_records_in_class_order_with_bills(self):
        self.run_ok()
        _header, records = self.load()
        # Class files order by WeaponId (sword, flail, gun), nodes in file order.
        self.assertEqual([r["class"] for r in records], [0, 0, 0, 0, 1, 1])
        self.assertEqual(records[0]["parent"], 0xFF, "sword root")
        self.assertEqual(records[1]["parent"], 0, "sword child")
        self.assertEqual(records[1]["cost"], 100)
        self.assertEqual(records[1]["direct_cost"], 180)
        self.assertEqual(records[1]["mats"], [2, 2, 0, 0], "ore idx 1 -> code 2 x2")
        self.assertEqual(records[1]["direct_mats"], [2, 3, 0, 0])
        self.assertEqual(records[4]["parent"], 0xFF, "flail root")
        self.assertEqual(records[5]["parent"], 4, "flail child")
        self.assertEqual(records[5]["cost"], 120)
        # sword_t2b is direct=false -> FLAG_DIRECT clear.
        self.assertEqual(records[3]["flags"] & gen_forge.FLAG_DIRECT, 0)
        self.assertEqual(records[2]["flags"] & gen_forge.FLAG_DIRECT, gen_forge.FLAG_DIRECT)

    def test_meta_header_constants(self):
        self.run_ok()
        meta = self.read(META_REL)
        for needle in (
            "constexpr uint16_t MAGIC = 0x4647;",
            "constexpr uint8_t VERSION = 1;",
            "constexpr uint8_t HEADER_SIZE = 8;",
            "constexpr uint8_t RECORD_SIZE = 17;",
            "constexpr uint8_t NODE_COUNT = 6;",
            "constexpr uint8_t WEAPON_COUNT = 3;",
            "constexpr uint8_t MAT_SLOTS = 2;",
            "constexpr uint8_t WEAPON_SWORD = 0;",
            "constexpr uint8_t WEAPON_FLAIL = 1;",
            "constexpr uint8_t WEAPON_GUN = 2;",
            "constexpr uint8_t NODE_SWORD_BASE = 0;",
            "constexpr uint16_t NODE_SWORD_BASE_OFF = 8;",
            "constexpr uint8_t NODE_FLAIL_BASE = 4;",
            "constexpr uint16_t NODE_FLAIL_BASE_OFF = 76;",
            "constexpr uint8_t NODE_SWORD_T2B = 3;",
            "constexpr uint16_t NODE_SWORD_T2B_OFF = 59;",
            "constexpr uint8_t NODE_SWORD_FIRST = 0;",
            "constexpr uint8_t NODE_FLAIL_FIRST = 4;",
            "constexpr uint8_t NODE_DEPTH[NODE_COUNT] = {0, 1, 2, 2, 0, 1};",
            "constexpr uint8_t NODE_BRANCH[NODE_COUNT] = {0, 0, 0, 1, 0, 0};",
            "constexpr uint16_t NODE_UPGRADE_COST[NODE_COUNT] = {0, 100, 250, 320, 0, 120};",
        ):
            self.assertIn(needle, meta)
        # The retired smith spine map is gone (save v5 has no migration).
        self.assertNotIn("TIER_NODE", meta)

    def test_load_model_api_and_row_labels(self):
        model = gen_forge.load_model(self.case)
        self.assertIsNotNone(model)
        self.assertEqual(len(model["nodes"]), 6)
        by_id = {n["id"]: n for n in model["nodes"]}
        self.assertEqual(gen_forge.row_label(model, by_id["sword_base"]), "SWD T1")
        self.assertEqual(gen_forge.row_label(model, by_id["sword_t1"]), "+- SWD T2")
        # sword_t1 has a later sibling? No (only child) -> no pipe at depth 2.
        self.assertEqual(gen_forge.row_label(model, by_id["sword_t2a"]), "   +- SWD T3A")
        self.assertEqual(gen_forge.row_label(model, by_id["sword_t2b"]), "   +- SWD T3B")
        self.assertIsNone(gen_forge.load_model(os.path.join(SCRATCH, "missing")))

    def test_dump_writes_nothing(self):
        before = sorted(os.path.relpath(os.path.join(d, n), self.case)
                        for d, _dirs, files in os.walk(self.case) for n in files)
        result = self.run_ok("--dump")
        self.assertIn("node sword_t2a: class sword parent sword_t1 direct True", result.stdout)
        self.assertIn("gen-forge: 6 nodes, 110 B blob", result.stdout)
        after = sorted(os.path.relpath(os.path.join(d, n), self.case)
                       for d, _dirs, files in os.walk(self.case) for n in files)
        self.assertEqual(before, after)

    # --------------------------------------------------------- validation
    def assert_fails(self, *needles):
        result = self.run_tool()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        for needle in needles:
            self.assertIn(needle, result.stderr)

    def test_unknown_class_rejected(self):
        self.mutate("data/forge/sword.json", lambda d: d.update({"class": "axe"}))
        self.assert_fails("class: unknown value 'axe'")

    def test_two_roots_rejected(self):
        self.mutate("data/forge/sword.json", lambda d: d["nodes"][1].update({"parent": None}))
        self.assert_fails("must have exactly one root node")

    def test_duplicate_id_rejected(self):
        self.mutate("data/forge/sword.json", lambda d: d["nodes"][1].update({"id": "sword_base"}))
        self.assert_fails("duplicate node id 'sword_base'")

    def test_unknown_parent_rejected(self):
        self.mutate("data/forge/sword.json", lambda d: d["nodes"][1].update({"parent": "ghost"}))
        self.assert_fails("parent: unknown node 'ghost'")

    def test_parent_other_class_rejected(self):
        # sword is processed first, so flail's parent can resolve to a known
        # sword node and trip the different-class check.
        self.mutate("data/forge/flail.json", lambda d: d["nodes"][1].update({"parent": "sword_base"}))
        self.assert_fails("is a different class")

    def test_too_many_mats_rejected(self):
        def add(doc):
            doc["nodes"][1]["mats"] = [{"item": "ore", "count": 1},
                                       {"item": "scale", "count": 1},
                                       {"item": "herb", "count": 1}]
        self.mutate("data/forge/sword.json", add)
        self.assert_fails("exceed the 2 packed slots")

    def test_unknown_item_rejected(self):
        self.mutate("data/forge/sword.json",
                    lambda d: d["nodes"][1]["mats"][0].update({"item": "ghost"}))
        self.assert_fails("unknown item 'ghost'")

    def test_duplicate_material_rejected(self):
        self.mutate("data/forge/sword.json",
                    lambda d: d["nodes"][1]["mats"].append({"item": "ore", "count": 1}))
        self.assert_fails("duplicate material 'ore'")

    def test_unknown_key_rejected(self):
        self.mutate("data/forge/sword.json", lambda d: d.update({"extra": 1}))
        self.assert_fails("unknown key 'extra'")

    def test_bad_direct_type_rejected(self):
        self.mutate("data/forge/sword.json", lambda d: d["nodes"][1].update({"direct": 1}))
        self.assert_fails("direct: expected a boolean")

    def test_empty_desc_rejected(self):
        self.mutate("data/forge/sword.json", lambda d: d["nodes"][1].update({"desc": []}))
        self.assert_fails("desc: expected a non-empty array")

    def test_missing_forge_dir_rejected(self):
        shutil.rmtree(self.path("data", "forge"))
        self.assert_fails("missing forge directory")

    def test_node_cap_rejected(self):
        # 64 is the save bitset cap; > 64 nodes must fail.
        def grow(doc):
            base = doc["nodes"][0]
            doc["nodes"] = []
            for i in range(70):
                node = dict(base)
                node["id"] = "n%d" % i
                node["label"] = "N%d" % i
                node["parent"] = None if i == 0 else "n%d" % (i - 1)
                doc["nodes"].append(node)
        self.mutate("data/forge/sword.json", grow)
        self.assert_fails("exceed the 64 slot cap")


if __name__ == "__main__":
    unittest.main()
