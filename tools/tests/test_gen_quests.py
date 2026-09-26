#!/usr/bin/env python3
"""Unit tests for tools/gen-quests.py (run: make test-tools).

The clean fixture under fixtures/gen_quests/clean is a minimal quest tree (two
quests). Every failure case copies it to build/tests/gen_quests/ and mutates the
copy, so the tests stay read-only on the repository fixtures and never write
into /tmp.
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
TOOL = os.path.join(TOOLS, "gen-quests.py")
FIXTURE = os.path.join(HERE, "fixtures", "gen_quests", "clean")
SCRATCH = os.path.join(ROOT, "build", "tests", "gen_quests")

BLOB_REL = "fxdata/tables/quests.bin"
META_REL = "src/generated/quest_meta.hpp"

HEADER = struct.Struct("<HBBBBH")
RECORD = struct.Struct("<BBBBHBBB")


def parse_record(blob, off):
    qid, goal, target, need, zenny, ritem, rcount, unlock = RECORD.unpack_from(blob, off)
    return {"id": qid, "goal": goal, "target": target, "need": need,
            "zenny": zenny, "ritem": ritem, "rcount": rcount, "unlock": unlock}


def run_tool(*args):
    return subprocess.run([sys.executable, TOOL, *args], capture_output=True, text=True)


class GenQuestsTests(unittest.TestCase):
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
            "constexpr uint16_t MAGIC = 0x5153;",
            "constexpr uint8_t VERSION = 2;",
            "constexpr uint8_t HEADER_SIZE = 8;",
            "constexpr uint8_t RECORD_SIZE = 9;",
            "constexpr uint8_t QUEST_COUNT = 2;",
            "constexpr uint8_t DEF_ID_OFF = 0;",
            "constexpr uint8_t DEF_GOAL_OFF = 1;",
            "constexpr uint8_t DEF_TARGET_OFF = 2;",
            "constexpr uint8_t DEF_NEED_OFF = 3;",
            "constexpr uint8_t DEF_REWARD_ZENNY_OFF = 4;",
            "constexpr uint8_t DEF_REWARD_ITEM_OFF = 6;",
            "constexpr uint8_t DEF_REWARD_COUNT_OFF = 7;",
            "constexpr uint8_t DEF_UNLOCK_OFF = 8;",
            "constexpr uint8_t GOAL_KILL = 0;",
            "constexpr uint8_t GOAL_GATHER = 1;",
            "constexpr uint8_t TARGET_LUNGE = 0;",
            "constexpr uint8_t TARGET_SWEEP = 1;",
            "constexpr uint8_t TARGET_HEAVY = 2;",
            "constexpr uint8_t TARGET_RAVAGER = 3;",
            "constexpr uint8_t QUEST_ROOM_HINT_NONE = 0xFF;",
            "constexpr uint8_t MAP_ROOM_COUNT = 3;",
            "constexpr uint8_t QUEST_ROOM_HINT[QUEST_COUNT] = {0xFF, 0xFF};",
            "constexpr uint8_t QUEST_SLAY_LUNGE = 0;",
            "constexpr uint16_t QUEST_SLAY_LUNGE_OFF = 8;",
            "constexpr uint8_t QUEST_SLAY_SWEEP = 1;",
            "constexpr uint16_t QUEST_SLAY_SWEEP_OFF = 17;",
        ):
            self.assertIn(needle, text)

    def test_clean_blob_layout(self):
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        magic, version, flags, count, reserved, reserved2 = HEADER.unpack_from(blob, 0)
        self.assertEqual((magic, version, flags, count, reserved, reserved2),
                         (0x5153, 2, 0, 2, 0, 0))
        self.assertEqual(len(blob), 26)
        self.assertEqual(parse_record(blob, 8),
                         {"id": 0, "goal": 0, "target": 0, "need": 3, "zenny": 150,
                          "ritem": 0, "rcount": 0, "unlock": 0})
        self.assertEqual(parse_record(blob, 17),
                         {"id": 1, "goal": 0, "target": 1, "need": 2, "zenny": 250,
                          "ritem": 0, "rcount": 0, "unlock": 0})

    def test_dump_mode_lists_quests_and_writes_nothing(self):
        result = self.compile("--dump")
        self.assert_succeeds(result)
        self.assertIn("quest slay_lunge: id 0 goal kill target 0 need 3 zenny 150 item 0 count 0 unlock 0",
                      result.stdout)
        self.assertIn("quest slay_sweep: id 1 goal kill target 1 need 2 zenny 250 item 0 count 0 unlock 0",
                      result.stdout)
        self.assertIn("gen-quests: 2 quests, 26 B blob", result.stdout)
        self.assertFalse(os.path.exists(self.path(BLOB_REL)))
        self.assertFalse(os.path.exists(self.path(META_REL)))

    def test_quests_sort_by_id_not_file_name(self):
        os.rename(self.path("data", "quests", "slay_sweep.json"),
                  self.path("data", "quests", "aaa_sweep.json"))
        self.assert_succeeds(self.compile())
        text = self.read(META_REL)
        self.assertIn("constexpr uint8_t QUEST_SLAY_LUNGE = 0;", text)
        self.assertIn("constexpr uint8_t QUEST_AAA_SWEEP = 1;", text)
        blob = self.read_bytes(BLOB_REL)
        self.assertEqual(parse_record(blob, 8)["id"], 0, "record 0 is the id-0 quest")
        self.assertEqual(parse_record(blob, 17)["id"], 1, "record 1 is the id-1 quest")

    # ------------------------------------------------------- schema errors

    def test_unknown_key_rejected(self):
        self.mutate("data/quests/slay_lunge.json", lambda doc: doc.__setitem__("theme", "dark"))
        self.assert_fails(self.compile(), "unknown key 'theme'")

    def test_missing_key_rejected(self):
        self.mutate("data/quests/slay_lunge.json", lambda doc: doc.pop("rewardZenny"))
        self.assert_fails(self.compile(), "missing key 'rewardZenny'")

    def test_duplicate_id_rejected(self):
        self.mutate("data/quests/slay_sweep.json", lambda doc: doc.__setitem__("id", 0))
        self.assert_fails(self.compile(), "duplicate quest id 0")

    def test_unknown_goal_kind_rejected(self):
        self.mutate("data/quests/slay_lunge.json", lambda doc: doc.__setitem__("goalKind", "escort"))
        self.assert_fails(self.compile(), "goalKind: unknown value 'escort'")

    def test_unknown_kill_target_rejected(self):
        self.mutate("data/quests/slay_lunge.json", lambda doc: doc.__setitem__("target", "dragon"))
        self.assert_fails(self.compile(), "target: unknown kill value 'dragon'")

    def test_unknown_gather_target_rejected(self):
        def mutate(doc):
            doc["goalKind"] = "gather"
            doc["target"] = "dragon"
        self.mutate("data/quests/slay_lunge.json", mutate)
        self.assert_fails(self.compile(), "target: unknown gather item 'dragon'")

    def test_gather_quest_encodes_item_index(self):
        def mutate(doc):
            doc["goalKind"] = "gather"
            doc["target"] = "ore"
        self.mutate("data/quests/slay_lunge.json", mutate)
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        rec = parse_record(blob, 8)
        self.assertEqual(rec["goal"], 1, "gather goal kind")
        self.assertEqual(rec["target"], 1, "ore is item index 1")

    def test_reward_item_requires_count(self):
        def mutate(doc):
            doc["rewardItem"] = "scale"
        self.mutate("data/quests/slay_lunge.json", mutate)
        self.assert_fails(self.compile(), "missing key 'rewardCount'")

    def test_reward_count_requires_item(self):
        self.mutate("data/quests/slay_lunge.json", lambda doc: doc.__setitem__("rewardCount", 2))
        self.assert_fails(self.compile(), "rewardCount: requires rewardItem")

    def test_reward_item_unknown_rejected(self):
        def mutate(doc):
            doc["rewardItem"] = "dragon"
            doc["rewardCount"] = 1
        self.mutate("data/quests/slay_lunge.json", mutate)
        self.assert_fails(self.compile(), "rewardItem: unknown item 'dragon'")

    def test_reward_item_encodes_index_plus_one(self):
        def mutate(doc):
            doc["rewardItem"] = "scale"
            doc["rewardCount"] = 2
        self.mutate("data/quests/slay_lunge.json", mutate)
        self.assert_succeeds(self.compile())
        rec = parse_record(self.read_bytes(BLOB_REL), 8)
        self.assertEqual(rec["ritem"], 3, "scale is item index 2 -> packed 3")
        self.assertEqual(rec["rcount"], 2)

    def test_room_hint_valid_resolves_sorted_room_index(self):
        # imx: roomHint must name a data/map.json room id; the emitted table
        # indexes rooms by sorted id (the zone::ROOM_* order), not file order.
        # The fixture map is file-ordered camp/area/cavern, so "camp" is index 1.
        self.mutate("data/quests/slay_lunge.json", lambda doc: doc.__setitem__("roomHint", "camp"))
        self.assert_succeeds(self.compile())
        meta = self.read(META_REL)
        self.assertIn("constexpr uint8_t QUEST_ROOM_HINT[QUEST_COUNT] = {0x01, 0xFF};", meta)
        # The hint is not packed into the blob: the record layout is unchanged.
        self.assertEqual(len(self.read_bytes(BLOB_REL)), 26)

    def test_room_hint_reads_the_map_room_ids(self):
        self.mutate("data/quests/slay_lunge.json", lambda doc: doc.__setitem__("roomHint", "cavern"))
        self.assert_succeeds(self.compile())
        self.assertIn("constexpr uint8_t QUEST_ROOM_HINT[QUEST_COUNT] = {0x02, 0xFF};", self.read(META_REL))

    def test_room_hint_unknown_room_rejected(self):
        self.mutate("data/quests/slay_lunge.json", lambda doc: doc.__setitem__("roomHint", "volcano"))
        self.assert_fails(self.compile(), "roomHint: unknown room 'volcano'")

    def test_room_hint_non_string_rejected(self):
        self.mutate("data/quests/slay_lunge.json", lambda doc: doc.__setitem__("roomHint", 2))
        self.assert_fails(self.compile(), "roomHint: unknown room 2")

    def test_missing_map_file_rejected(self):
        os.remove(self.path("data", "map.json"))
        self.assert_fails(self.compile(), "data/map.json")

    def test_need_zero_rejected(self):
        self.mutate("data/quests/slay_lunge.json", lambda doc: doc.__setitem__("need", 0))
        self.assert_fails(self.compile(), "need: out of range 1..255")

    def test_reward_out_of_range_rejected(self):
        self.mutate("data/quests/slay_lunge.json", lambda doc: doc.__setitem__("rewardZenny", 70000))
        self.assert_fails(self.compile(), "rewardZenny: out of range 0..65535")

    def test_unlock_out_of_range_rejected(self):
        self.mutate("data/quests/slay_lunge.json", lambda doc: doc.__setitem__("unlockFlag", 256))
        self.assert_fails(self.compile(), "unlockFlag: out of range 0..255")

    def test_bad_file_name_rejected(self):
        os.rename(self.path("data", "quests", "slay_lunge.json"),
                  self.path("data", "quests", "SLAY.json"))
        self.assert_fails(self.compile(), "file name: expected [a-z][a-z0-9_]*.json")

    def test_missing_quests_dir_rejected(self):
        shutil.rmtree(self.path("data", "quests"))
        self.assert_fails(self.compile(), "missing quests directory")


if __name__ == "__main__":
    unittest.main()
