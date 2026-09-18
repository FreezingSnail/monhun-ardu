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
RECORD = struct.Struct("<BBBHB")


def parse_record(blob, off):
    qid, target, need, reward, unlock = RECORD.unpack_from(blob, off)
    return {"id": qid, "target": target, "need": need, "reward": reward, "unlock": unlock}


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
            "constexpr uint8_t VERSION = 1;",
            "constexpr uint8_t HEADER_SIZE = 8;",
            "constexpr uint8_t RECORD_SIZE = 6;",
            "constexpr uint8_t QUEST_COUNT = 2;",
            "constexpr uint8_t DEF_ID_OFF = 0;",
            "constexpr uint8_t DEF_TARGET_OFF = 1;",
            "constexpr uint8_t DEF_NEED_OFF = 2;",
            "constexpr uint8_t DEF_REWARD_OFF = 3;",
            "constexpr uint8_t DEF_UNLOCK_OFF = 5;",
            "constexpr uint8_t TARGET_LUNGE = 0;",
            "constexpr uint8_t TARGET_SWEEP = 1;",
            "constexpr uint8_t TARGET_HEAVY = 2;",
            "constexpr uint8_t TARGET_RAVAGER = 3;",
            "constexpr uint8_t QUEST_SLAY_LUNGE = 0;",
            "constexpr uint16_t QUEST_SLAY_LUNGE_OFF = 8;",
            "constexpr uint8_t QUEST_SLAY_SWEEP = 1;",
            "constexpr uint16_t QUEST_SLAY_SWEEP_OFF = 14;",
        ):
            self.assertIn(needle, text)

    def test_clean_blob_layout(self):
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        magic, version, flags, count, reserved, reserved2 = HEADER.unpack_from(blob, 0)
        self.assertEqual((magic, version, flags, count, reserved, reserved2),
                         (0x5153, 1, 0, 2, 0, 0))
        self.assertEqual(len(blob), 20)
        self.assertEqual(parse_record(blob, 8),
                         {"id": 0, "target": 0, "need": 3, "reward": 150, "unlock": 0})
        self.assertEqual(parse_record(blob, 14),
                         {"id": 1, "target": 1, "need": 2, "reward": 250, "unlock": 0})

    def test_dump_mode_lists_quests_and_writes_nothing(self):
        result = self.compile("--dump")
        self.assert_succeeds(result)
        self.assertIn("quest slay_lunge: id 0 target lunge need 3 reward 150 unlock 0", result.stdout)
        self.assertIn("quest slay_sweep: id 1 target sweep need 2 reward 250 unlock 0", result.stdout)
        self.assertIn("gen-quests: 2 quests, 20 B blob", result.stdout)
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
        self.assertEqual(parse_record(blob, 14)["id"], 1, "record 1 is the id-1 quest")

    # ------------------------------------------------------- schema errors

    def test_unknown_key_rejected(self):
        self.mutate("data/quests/slay_lunge.json", lambda doc: doc.__setitem__("theme", "dark"))
        self.assert_fails(self.compile(), "unknown key 'theme'")

    def test_missing_key_rejected(self):
        self.mutate("data/quests/slay_lunge.json", lambda doc: doc.pop("reward"))
        self.assert_fails(self.compile(), "missing key 'reward'")

    def test_duplicate_id_rejected(self):
        self.mutate("data/quests/slay_sweep.json", lambda doc: doc.__setitem__("id", 0))
        self.assert_fails(self.compile(), "duplicate quest id 0")

    def test_unknown_target_rejected(self):
        self.mutate("data/quests/slay_lunge.json", lambda doc: doc.__setitem__("targetKind", "dragon"))
        self.assert_fails(self.compile(), "targetKind: unknown value 'dragon'")

    def test_need_zero_rejected(self):
        self.mutate("data/quests/slay_lunge.json", lambda doc: doc.__setitem__("need", 0))
        self.assert_fails(self.compile(), "need: out of range 1..255")

    def test_reward_out_of_range_rejected(self):
        self.mutate("data/quests/slay_lunge.json", lambda doc: doc.__setitem__("reward", 70000))
        self.assert_fails(self.compile(), "reward: out of range 0..65535")

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
