#!/usr/bin/env python3
"""Unit tests for tools/gen-armor.py (run: make test-tools).

The clean fixture under fixtures/gen_armor/clean is a minimal armor tree (two
skills, one head piece + one charm). Every failure case copies it to
build/tests/gen_armor/ and mutates the copy, so the tests stay read-only on the
repository fixtures and never write into /tmp.
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
TOOL = os.path.join(TOOLS, "gen-armor.py")
FIXTURE = os.path.join(HERE, "fixtures", "gen_armor", "clean")
SCRATCH = os.path.join(ROOT, "build", "tests", "gen_armor")

BLOB_REL = "fxdata/tables/armor.bin"
DATA_HPP_REL = "src/generated/armor_data.hpp"
META_REL = "src/generated/armor_meta.hpp"
EXPECT_REL = "src/generated/armor_expect.hpp"

HEADER = struct.Struct("<HBBBBH")
PIECE = struct.Struct("<BBbbbbH")
SKILL = struct.Struct("<BBB")
PIECE_SIZE = 18
SKILL_SIZE = 3


def parse_piece(blob, off):
    slot, defense, rf, rw, ri, rt, zenny = PIECE.unpack_from(blob, off)
    mat = struct.unpack_from("<BBBB", blob, off + 8)
    sheet, skill_count = struct.unpack_from("<BB", blob, off + 12)
    skills = struct.unpack_from("<BBBB", blob, off + 14)
    return {"slot": slot, "defense": defense, "resist": [rf, rw, ri, rt], "zenny": zenny,
            "mat": [(mat[0], mat[1]), (mat[2], mat[3])], "sheet": sheet,
            "skillCount": skill_count, "skills": [(skills[0], skills[1]), (skills[2], skills[3])]}


def parse_skill(blob, off):
    return SKILL.unpack_from(blob, off)


def run_tool(*args):
    return subprocess.run([sys.executable, TOOL, *args], capture_output=True, text=True)


class GenArmorTests(unittest.TestCase):
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
            "constexpr uint16_t MAGIC = 0x5241;",
            "constexpr uint8_t VERSION = 1;",
            "constexpr uint8_t HEADER_SIZE = 8;",
            "constexpr uint8_t PIECE_SIZE = 18;",
            "constexpr uint8_t SKILL_SIZE = 3;",
            "constexpr uint8_t PIECE_COUNT = 2;",
            "constexpr uint8_t SKILL_COUNT = 2;",
            "constexpr uint8_t MAT_SLOTS = 2;",
            "constexpr uint8_t SKILL_SLOTS = 2;",
            "constexpr uint16_t PIECES_OFF = 8;",
            "constexpr uint16_t SKILLS_OFF = 44;",
            "constexpr uint8_t SLOT_HEAD = 0;",
            "constexpr uint8_t SLOT_BODY = 1;",
            "constexpr uint8_t SLOT_CHARM = 2;",
            "constexpr uint8_t KIND_ATTACK_UP = 0;",
            "constexpr uint8_t KIND_EVADE_WINDOW = 4;",
            "constexpr uint8_t PIECE_SLOT_OFF = 0;",
            "constexpr uint8_t PIECE_DEFENSE_OFF = 1;",
            "constexpr uint8_t PIECE_RESIST_OFF = 2;",
            "constexpr uint8_t PIECE_ZENNY_OFF = 6;",
            "constexpr uint8_t PIECE_MAT_OFF = 8;",
            "constexpr uint8_t PIECE_MAT_STRIDE = 2;",
            "constexpr uint8_t PIECE_SHEET_OFF = 12;",
            "constexpr uint8_t PIECE_SKILL_COUNT_OFF = 13;",
            "constexpr uint8_t PIECE_SKILLS_OFF = 14;",
            "constexpr uint8_t PIECE_SKILL_STRIDE = 2;",
            "constexpr uint8_t SKILL_KIND_OFF = 0;",
            "constexpr uint8_t SKILL_MAX_OFF = 1;",
            "constexpr uint8_t SKILL_PER_POINT_OFF = 2;",
            "constexpr uint8_t THRESHOLD_S = 10;",
            "constexpr uint8_t THRESHOLD_M = 15;",
            "constexpr uint8_t ARMOR_HELM_A = 0;",
            "constexpr uint16_t ARMOR_HELM_A_OFF = 8;",
            "constexpr uint8_t ARMOR_CHARM_A = 1;",
            "constexpr uint16_t ARMOR_CHARM_A_OFF = 26;",
            "constexpr uint8_t SKILL_ATTACK_UP = 0;",
            "constexpr uint16_t SKILL_ATTACK_UP_OFF = 44;",
            "constexpr uint8_t SKILL_EVADE_WINDOW = 1;",
            "constexpr uint16_t SKILL_EVADE_WINDOW_OFF = 47;",
            "constexpr uint8_t MH_HEAD_A = 1;",
            "constexpr uint8_t ORE = 1;",
        ):
            self.assertIn(needle, text)
        data = self.read(DATA_HPP_REL)
        self.assertIn("struct Piece {", data)
        self.assertIn("inline constexpr std::array<Piece, 2> PIECES", data)
        self.assertIn("inline constexpr std::array<Skill, 2> SKILLS", data)
        expect = self.read(EXPECT_REL)
        self.assertIn("constexpr uint16_t BLOB_SIZE = 50;", expect)
        self.assertIn("constexpr uint8_t ARMOR_HELM_A_DEFENSE = 5;", expect)
        self.assertIn("constexpr int8_t ARMOR_HELM_A_RESIST_ICE = -1;", expect)
        self.assertIn("constexpr uint8_t SKILL_ATTACK_UP_PER_POINT = 2;", expect)

    def test_clean_blob_layout(self):
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        magic, version, flags, pieces, skills, reserved = HEADER.unpack_from(blob, 0)
        self.assertEqual((magic, version, flags, pieces, skills, reserved),
                         (0x5241, 1, 0, 2, 2, 0))
        self.assertEqual(len(blob), 50)
        self.assertEqual(parse_piece(blob, 8),
                         {"slot": 0, "defense": 5, "resist": [1, 0, -1, 0], "zenny": 100,
                          "mat": [(2, 2), (0, 0)], "sheet": 1, "skillCount": 1,
                          "skills": [(1, 3), (0, 0)]})
        self.assertEqual(parse_piece(blob, 26),
                         {"slot": 2, "defense": 0, "resist": [0, 0, 0, 0], "zenny": 50,
                          "mat": [(0, 0), (0, 0)], "sheet": 0, "skillCount": 1,
                          "skills": [(2, 2), (0, 0)]})
        self.assertEqual(parse_skill(blob, 44), (0, 15, 2))
        self.assertEqual(parse_skill(blob, 47), (4, 15, 1))

    def test_dump_mode_lists_pieces_and_skills_and_writes_nothing(self):
        result = self.compile("--dump")
        self.assert_succeeds(result)
        self.assertIn("skill attack_up: kind ATTACK_UP maxPoints 15 perPoint 2", result.stdout)
        self.assertIn("piece helm_a: slot head defense 5 resist 1/0/-1/0 skills attack_up 3 "
                      "recipe ore x2 100 zenny", result.stdout)
        self.assertIn("piece charm_a: slot charm defense 0 resist 0/0/0/0 skills evade_window 2 "
                      "recipe - 50 zenny", result.stdout)
        self.assertIn("gen-armor: thresholds s=10 m=15, 2 pieces, 2 skills, 50 B blob", result.stdout)
        self.assertFalse(os.path.exists(self.path(BLOB_REL)))
        self.assertFalse(os.path.exists(self.path(META_REL)))

    def test_source_order_sets_indices(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"].reverse())
        self.assert_succeeds(self.compile())
        text = self.read(META_REL)
        self.assertIn("constexpr uint8_t ARMOR_CHARM_A = 0;", text)
        self.assertIn("constexpr uint8_t ARMOR_HELM_A = 1;", text)

    # ------------------------------------------------------- schema errors

    def test_unknown_root_key_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc.__setitem__("theme", "dark"))
        self.assert_fails(self.compile(), "unknown key 'theme'")

    def test_missing_armor_file_rejected(self):
        os.remove(self.path("data", "armor.json"))
        self.assert_fails(self.compile(), "missing armor file")

    def test_missing_skills_file_rejected(self):
        os.remove(self.path("data", "skills.json"))
        self.assert_fails(self.compile(), "missing skill file")

    def test_missing_items_file_rejected(self):
        os.remove(self.path("data", "items.json"))
        self.assert_fails(self.compile(), "missing item file")

    def test_unknown_piece_key_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0].__setitem__("color", "red"))
        self.assert_fails(self.compile(), "unknown key 'color'")

    def test_missing_piece_key_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0].pop("defense"))
        self.assert_fails(self.compile(), "missing key 'defense'")

    def test_bad_piece_id_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0].__setitem__("id", "Helm A"))
        self.assert_fails(self.compile(), "must match [a-z][a-z0-9_]*")

    def test_duplicate_piece_id_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][1].__setitem__("id", "helm_a"))
        self.assert_fails(self.compile(), "duplicate id 'helm_a'")

    def test_unknown_slot_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0].__setitem__("slot", "legs"))
        self.assert_fails(self.compile(), "slot: unknown value 'legs'")

    def test_defense_out_of_range_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0].__setitem__("defense", 256))
        self.assert_fails(self.compile(), "defense: out of range 0..255")

    def test_defense_float_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0].__setitem__("defense", 5.0))
        self.assert_fails(self.compile(), "defense: expected an integer")

    def test_defense_bool_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0].__setitem__("defense", True))
        self.assert_fails(self.compile(), "defense: expected an integer")

    def test_missing_resist_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0].pop("resist"))
        self.assert_fails(self.compile(), "resist: expected an object")

    def test_unknown_resist_key_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0]["resist"].__setitem__("dark", 1))
        self.assert_fails(self.compile(), "unknown key 'dark'")

    def test_resist_out_of_range_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0]["resist"].__setitem__("fire", 200))
        self.assert_fails(self.compile(), "fire: out of range -128..127")

    def test_missing_zenny_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0]["recipe"].pop("zenny"))
        self.assert_fails(self.compile(), "missing key 'zenny'")

    def test_zenny_out_of_range_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0]["recipe"].__setitem__("zenny", 70000))
        self.assert_fails(self.compile(), "zenny: out of range 0..65535")

    def test_unknown_material_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0]["recipe"]["materials"].__setitem__(
            0, {"item": "dragonite", "count": 1}))
        self.assert_fails(self.compile(), "item: unknown item 'dragonite'")

    def test_too_many_materials_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0]["recipe"].__setitem__(
            "materials", [{"item": "ore", "count": 1}, {"item": "scale", "count": 1},
                          {"item": "herb", "count": 1}]))
        self.assert_fails(self.compile(), "materials: 3 pairs exceed the 2 packed slots")

    def test_duplicate_material_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0]["recipe"].__setitem__(
            "materials", [{"item": "ore", "count": 1}, {"item": "ore", "count": 2}]))
        self.assert_fails(self.compile(), "duplicate material 'ore'")

    def test_material_count_out_of_range_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0]["recipe"]["materials"].__setitem__(
            0, {"item": "ore", "count": 0}))
        self.assert_fails(self.compile(), "count: out of range 1..255")

    def test_empty_piece_skills_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0].__setitem__("skills", []))
        self.assert_fails(self.compile(), "skills: expected at least one skill")

    def test_too_many_piece_skills_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0].__setitem__("skills", [
            {"id": "attack_up", "points": 1}, {"id": "evade_window", "points": 1},
            {"id": "attack_up", "points": 1}]))
        self.assert_fails(self.compile(), "skills: 3 entries exceed the 2 packed slots")

    def test_unknown_piece_skill_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0]["skills"].__setitem__(
            0, {"id": "sharpen", "points": 1}))
        self.assert_fails(self.compile(), "unknown skill 'sharpen'")

    def test_duplicate_piece_skill_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0].__setitem__(
            "skills", [{"id": "attack_up", "points": 1}, {"id": "attack_up", "points": 2}]))
        self.assert_fails(self.compile(), "duplicate skill 'attack_up'")

    def test_piece_skill_points_out_of_range_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0]["skills"][0].__setitem__("points", 16))
        self.assert_fails(self.compile(), "points: out of range 0..15")

    def test_skill_points_over_max_rejected(self):
        # Two pieces granting attack_up: 14 + 2 = 16 > maxPoints 15.
        def bump(doc):
            doc["pieces"][0]["skills"][0]["points"] = 14
            doc["pieces"][1]["skills"] = [{"id": "attack_up", "points": 2}]
        self.mutate("data/armor.json", bump)
        self.assert_fails(self.compile(), "skill attack_up: 16 total points exceed maxPoints 15")

    def test_bad_sheet_name_rejected(self):
        self.mutate("data/armor.json", lambda doc: doc["pieces"][0].__setitem__("sheet", "MH Head"))
        self.assert_fails(self.compile(), "sheet: expected a [a-z][a-z0-9_]* symbol")

    # -------------------------------------------------------- skill schema

    def test_unknown_skill_kind_rejected(self):
        self.mutate("data/skills.json", lambda doc: doc["skills"][0].__setitem__("kind", "SHARPNESS"))
        self.assert_fails(self.compile(), "kind: unknown value 'SHARPNESS'")

    def test_skill_max_points_out_of_range_rejected(self):
        self.mutate("data/skills.json", lambda doc: doc["skills"][0].__setitem__("maxPoints", 16))
        self.assert_fails(self.compile(), "maxPoints: out of range 1..15")

    def test_skill_per_point_out_of_range_rejected(self):
        self.mutate("data/skills.json", lambda doc: doc["skills"][0].__setitem__("perPoint", 256))
        self.assert_fails(self.compile(), "perPoint: out of range 0..255")

    def test_skill_duplicate_id_rejected(self):
        self.mutate("data/skills.json", lambda doc: doc["skills"][1].__setitem__("id", "attack_up"))
        self.assert_fails(self.compile(), "duplicate id 'attack_up'")

    def test_thresholds_inverted_rejected(self):
        self.mutate("data/skills.json", lambda doc: doc.__setitem__(
            "thresholds", {"s": 15, "m": 10}))
        self.assert_fails(self.compile(), "s (15) must be below m (10)")

    def test_thresholds_missing_rejected(self):
        self.mutate("data/skills.json", lambda doc: doc.pop("thresholds"))
        self.assert_fails(self.compile(), "missing key 'thresholds'")

    def test_threshold_out_of_range_rejected(self):
        self.mutate("data/skills.json", lambda doc: doc.__setitem__(
            "thresholds", {"s": 0, "m": 15}))
        self.assert_fails(self.compile(), "s: out of range 1..15")


if __name__ == "__main__":
    unittest.main()
