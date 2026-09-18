#!/usr/bin/env python3
"""Unit tests for tools/gen-smith.py (run: make test-tools).

The clean fixture under fixtures/gen_smith/clean is a minimal smith tree (one
weapon, two tiers). Every failure case copies it to build/tests/gen_smith/ and
mutates the copy, so the tests stay read-only on the repository fixtures and
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
TOOL = os.path.join(TOOLS, "gen-smith.py")
FIXTURE = os.path.join(HERE, "fixtures", "gen_smith", "clean")
SCRATCH = os.path.join(ROOT, "build", "tests", "gen_smith")

BLOB_REL = "fxdata/tables/smith.bin"
META_REL = "src/generated/smith_meta.hpp"

HEADER = struct.Struct("<HBBBBH")
RECORD = struct.Struct("<BBHBBB")


def parse_record(blob, off):
    weapon, tier, cost, dmg, spd, unlock = RECORD.unpack_from(blob, off)
    return {"weapon": weapon, "tier": tier, "cost": cost,
            "dmg": dmg, "spd": spd, "unlock": unlock}


def run_tool(*args):
    return subprocess.run([sys.executable, TOOL, *args], capture_output=True, text=True)


class GenSmithTests(unittest.TestCase):
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
            "constexpr uint16_t MAGIC = 0x534D;",
            "constexpr uint8_t VERSION = 1;",
            "constexpr uint8_t HEADER_SIZE = 8;",
            "constexpr uint8_t RECORD_SIZE = 7;",
            "constexpr uint8_t UPGRADE_COUNT = 2;",
            "constexpr uint8_t TIER_COUNT = 2;",
            "constexpr uint8_t WEAPON_COUNT = 3;",
            "constexpr uint8_t UPG_WEAPON_OFF = 0;",
            "constexpr uint8_t UPG_TIER_OFF = 1;",
            "constexpr uint8_t UPG_COST_OFF = 2;",
            "constexpr uint8_t UPG_DMG_OFF = 4;",
            "constexpr uint8_t UPG_SPD_OFF = 5;",
            "constexpr uint8_t UPG_UNLOCK_OFF = 6;",
            "constexpr uint8_t WEAPON_SWORD = 0;",
            "constexpr uint8_t WEAPON_FLAIL = 1;",
            "constexpr uint8_t WEAPON_GUN = 2;",
            "constexpr uint8_t UPG_SWORD_T1 = 0;",
            "constexpr uint16_t UPG_SWORD_T1_OFF = 8;",
            "constexpr uint8_t UPG_SWORD_T2 = 1;",
            "constexpr uint16_t UPG_SWORD_T2_OFF = 15;",
        ):
            self.assertIn(needle, text)

    def test_clean_blob_layout(self):
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        magic, version, flags, count, reserved, reserved2 = HEADER.unpack_from(blob, 0)
        self.assertEqual((magic, version, flags, count, reserved, reserved2),
                         (0x534D, 1, 0, 2, 0, 0))
        self.assertEqual(len(blob), 22)
        self.assertEqual(parse_record(blob, 8),
                         {"weapon": 0, "tier": 1, "cost": 100, "dmg": 110, "spd": 105, "unlock": 0})
        self.assertEqual(parse_record(blob, 15),
                         {"weapon": 0, "tier": 2, "cost": 250, "dmg": 125, "spd": 115, "unlock": 0})

    def test_dump_mode_lists_upgrades_and_writes_nothing(self):
        result = self.compile("--dump")
        self.assert_succeeds(result)
        self.assertIn("upgrade sword_t1: weapon sword tier 1 cost 100 dmg 110 spd 105 unlock 0",
                      result.stdout)
        self.assertIn("upgrade sword_t2: weapon sword tier 2 cost 250 dmg 125 spd 115 unlock 0",
                      result.stdout)
        self.assertIn("gen-smith: 2 upgrades, 22 B blob", result.stdout)
        self.assertFalse(os.path.exists(self.path(BLOB_REL)))
        self.assertFalse(os.path.exists(self.path(META_REL)))

    def test_upgrades_sort_by_weapon_then_tier_not_file_name(self):
        os.rename(self.path("data", "smith", "sword_t1.json"),
                  self.path("data", "smith", "zzz_t1.json"))
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        self.assertEqual(parse_record(blob, 8)["tier"], 1, "tier 1 sorts first")
        self.assertEqual(parse_record(blob, 15)["tier"], 2, "tier 2 second")
        text = self.read(META_REL)
        self.assertIn("constexpr uint8_t UPG_ZZZ_T1 = 0;", text)

    # ------------------------------------------------------- schema errors

    def test_unknown_key_rejected(self):
        self.mutate("data/smith/sword_t1.json", lambda doc: doc.__setitem__("color", "red"))
        self.assert_fails(self.compile(), "unknown key 'color'")

    def test_missing_key_rejected(self):
        self.mutate("data/smith/sword_t1.json", lambda doc: doc.pop("dmgMul"))
        self.assert_fails(self.compile(), "missing key 'dmgMul'")

    def test_unknown_weapon_rejected(self):
        self.mutate("data/smith/sword_t1.json", lambda doc: doc.__setitem__("weapon", "lance"))
        self.assert_fails(self.compile(), "weapon: unknown value 'lance'")

    def test_duplicate_tier_rejected(self):
        self.mutate("data/smith/sword_t2.json", lambda doc: doc.__setitem__("tier", 1))
        self.assert_fails(self.compile(), "duplicate upgrade for sword tier 1")

    def test_missing_tier_rejected(self):
        os.remove(self.path("data", "smith", "sword_t2.json"))
        self.assert_fails(self.compile(), "weapon sword: tiers [1], want [1, 2]")

    def test_tier_out_of_range_rejected(self):
        self.mutate("data/smith/sword_t1.json", lambda doc: doc.__setitem__("tier", 3))
        self.assert_fails(self.compile(), "tier: out of range 1..2")

    def test_cost_out_of_range_rejected(self):
        self.mutate("data/smith/sword_t1.json", lambda doc: doc.__setitem__("cost", 70000))
        self.assert_fails(self.compile(), "cost: out of range 0..65535")

    def test_bad_file_name_rejected(self):
        os.rename(self.path("data", "smith", "sword_t1.json"),
                  self.path("data", "smith", "SWORD.json"))
        self.assert_fails(self.compile(), "file name: expected [a-z][a-z0-9_]*.json")

    def test_missing_smith_dir_rejected(self):
        shutil.rmtree(self.path("data", "smith"))
        self.assert_fails(self.compile(), "missing smith directory")


if __name__ == "__main__":
    unittest.main()
