#!/usr/bin/env python3
"""Unit tests for tools/gen-combat.py (run: make test-tools).

The clean fixture under fixtures/gen_combat/clean is a minimal, fully featured
schema tree (skeleton with two parts, stages, multipliers, one creature with an
attack, two windows... one window, two patterns steps). Every failure case
copies it to build/tests/gen_combat/ and mutates the copy, so the tests stay
read-only on the repository fixtures and never write into /tmp.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
TOOL = os.path.join(TOOLS, "gen-combat.py")
FIXTURE = os.path.join(HERE, "fixtures", "gen_combat", "clean")
SCRATCH = os.path.join(ROOT, "build", "tests", "gen_combat")

BLOB_REL = "fxdata/tables/combat.bin"
META_REL = "src/generated/combat_meta.hpp"
DATA_REL = "src/generated/combat_data.hpp"
EXPECT_REL = "src/generated/combat_expect.hpp"
CONST_RE = re.compile(r"^constexpr (uint8_t|uint16_t) ([A-Z0-9_]+) = (0x[0-9A-Fa-f]+|\d+);$")


def run_tool(*args):
    return subprocess.run([sys.executable, TOOL, *args], capture_output=True, text=True)


class GenCombatTests(unittest.TestCase):
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

    def mutate(self, rel_path, fn):
        with open(self.path(rel_path), encoding="utf-8") as handle:
            doc = json.load(handle)
        fn(doc)
        with open(self.path(rel_path), "w", encoding="utf-8", newline="\n") as handle:
            json.dump(doc, handle, indent=2)
            handle.write("\n")

    def assert_fails(self, result, *needles):
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        for needle in needles:
            self.assertIn(needle, result.stderr)

    def assert_succeeds(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def meta_constants(self):
        text = self.read(META_REL)
        constants = {}
        for line in text.splitlines():
            match = CONST_RE.match(line)
            if match:
                constants[match.group(2)] = int(match.group(3), 0)
        return constants

    def blob(self):
        return self.read_bytes(BLOB_REL)

    # ---------------------------------------------------------------- clean

    def test_clean_compile_is_deterministic(self):
        self.assert_succeeds(self.compile())
        for rel in (BLOB_REL, DATA_REL, META_REL, EXPECT_REL):
            self.assertTrue(os.path.isfile(self.path(rel)), rel)
        first = {rel: self.read_bytes(rel) for rel in (BLOB_REL, DATA_REL, META_REL, EXPECT_REL)}
        second = self.compile()
        self.assert_succeeds(second)
        for rel in (BLOB_REL, DATA_REL, META_REL, EXPECT_REL):
            self.assertIn("%s (unchanged)" % rel, second.stdout)
            self.assertEqual(first[rel], self.read_bytes(rel), rel)

    def test_dump_mode_lists_model_and_writes_nothing(self):
        result = self.compile("--dump")
        self.assert_succeeds(result)
        self.assertIn("creature beast (skeleton beast_16x12", result.stdout)
        self.assertIn("attack jab: windup20 active6 recover30 dmg7 move lunge(20) windows 1", result.stdout)
        self.assertIn("window 0: t[0,6] box(8,0,12,10) dmgMul 100", result.stdout)
        self.assertIn("pattern p_jab: guard minDist0 maxDist36 hp[0,100] player0x01 cd0 chance100", result.stdout)
        self.assertIn("step 0: ATK beast.jab after2 chance100", result.stdout)
        self.assertIn("step 1: WAIT 5 after0", result.stdout)
        self.assertFalse(os.path.exists(self.path(BLOB_REL)))
        self.assertFalse(os.path.exists(self.path(META_REL)))

    # ------------------------------------------------------- schema errors

    def test_unknown_key_rejected(self):
        self.mutate("data/creatures/beast.json", lambda doc: doc.__setitem__("roar", 1))
        self.assert_fails(self.compile(), "unknown key 'roar'")

    def test_missing_key_rejected(self):
        self.mutate("data/creatures/beast.json", lambda doc: doc.pop("profile"))
        self.assert_fails(self.compile(), "missing key 'profile'")

    def test_float_rejected_for_quantized_field(self):
        self.mutate("data/creatures/beast.json", lambda doc: doc["attacks"][0].__setitem__("windup", 20.0))
        self.assert_fails(self.compile(), "attacks[0]: windup: expected an integer, got 20.0")

    def test_bool_rejected_for_quantized_field(self):
        self.mutate("data/creatures/beast.json", lambda doc: doc["attacks"][0].__setitem__("dmg", True))
        self.assert_fails(self.compile(), "attacks[0]: dmg: expected an integer, got True")

    def test_range_violation_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0]["windows"][0].__setitem__("dmgMul", 300))
        self.assert_fails(self.compile(), "windows[0]: dmgMul: out of range 0..255: 300")

    def test_phys_enum_membership(self):
        self.mutate("data/creatures/beast.json", lambda doc: doc["attacks"][0].__setitem__("phys", "FIRE"))
        self.assert_fails(self.compile(), "attacks[0]: phys: unknown value 'FIRE' (want one of BLUNT, SHOT, SLASH)")

    def test_elem_enum_membership(self):
        self.mutate("data/creatures/beast.json", lambda doc: doc["attacks"][0].__setitem__("elem", "SLASH"))
        self.assert_fails(self.compile(), "attacks[0]: elem: unknown value 'SLASH'")

    def test_stage_thresholds_must_descend(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["parts"][0]["stages"][1].__setitem__("at", 60))
        self.assert_fails(self.compile(),
                          "parts[0].stages[1]: at: stage thresholds must be strictly descending (got 60 after 50)")

    def test_window_must_fit_active_phase(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0]["windows"][0].__setitem__("t1", 7))
        self.assert_fails(self.compile(),
                          "windows[0]: window ends outside the active phase: t1 7 > active 6")

    def test_window_must_not_be_inverted(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0]["windows"][0].update({"t0": 3, "t1": 2}))
        self.assert_fails(self.compile(), "windows[0]: window is inverted: t0 3 > t1 2")

    def test_guard_ordering(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["patterns"][0]["guard"].update({"minDist": 40, "maxDist": 10}))
        self.assert_fails(self.compile(), "guard is inverted: minDist 40 > maxDist 10")

    # -------------------------------------------------------- id/ref errors

    def test_unknown_step_ref_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["patterns"][0]["steps"][0].__setitem__("atk", "claw"))
        self.assert_fails(self.compile(), "steps[0]: atk: unknown attack id 'claw'")

    def test_unknown_skeleton_ref_rejected(self):
        self.mutate("data/creatures/beast.json", lambda doc: doc.__setitem__("skeleton", "quad_64x48"))
        self.assert_fails(self.compile(), "skeleton: unknown skeleton id 'quad_64x48'")

    def test_unknown_part_predicate_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["patterns"][0]["guard"].__setitem__("parts", {"wing": ">=1"}))
        self.assert_fails(self.compile(), "parts: unknown part id 'wing'")

    def test_unknown_stage_attack_ref_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["parts"][0]["stages"][0].__setitem__("disableAttacks", ["kick"]))
        self.assert_fails(self.compile(), "disableAttacks[0]: unknown attack id 'kick'")

    def test_part_override_skeleton_collision_rejected(self):
        def collide(doc):
            part = json.loads(json.dumps(doc["parts"][0]))
            part["id"] = "body"
            doc["parts"].append(part)

        self.mutate("data/creatures/beast.json", collide)
        self.assert_fails(self.compile(), "parts[1]: part override id 'body' collides with skeleton part")

    def test_duplicate_local_attack_id_rejected(self):
        def duplicate(doc):
            attack = json.loads(json.dumps(doc["attacks"][0]))
            doc["attacks"].append(attack)

        self.mutate("data/creatures/beast.json", duplicate)
        self.assert_fails(self.compile(), "attacks[1]: duplicate local id 'jab'")

    def test_duplicate_creature_id_rejected(self):
        shutil.copyfile(self.path("data", "creatures", "beast.json"),
                        self.path("data", "creatures", "beast2.json"))
        self.assert_fails(self.compile(),
                          "data/creatures/beast2.json: id 'beast' does not match file name 'beast2.json'",
                          "duplicate creature id 'beast'")

    def test_duplicate_skeleton_id_rejected(self):
        def duplicate(doc):
            skeleton = json.loads(json.dumps(doc["skeletons"][0]))
            doc["skeletons"].append(skeleton)

        self.mutate("data/skeletons.json", duplicate)
        self.assert_fails(self.compile(), "duplicate local id 'beast_16x12'")

    def test_id_length_limit(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc.__setitem__("id", "b" * 32))
        self.assert_fails(self.compile(), "is longer than 31 characters")

    # ------------------------------------------------------------ size limit

    def test_window_count_size_limit(self):
        def flood(doc):
            window = doc["attacks"][0]["windows"][0]
            doc["attacks"][0]["windows"] = [json.loads(json.dumps(window)) for _ in range(256)]

        self.mutate("data/creatures/beast.json", flood)
        self.assert_fails(self.compile(), "size limit: 256 windows exceed the 255 record limit")

    # ----------------------------------------------------------- blob ABI

    def test_data_facts_match_fixture(self):
        # The interpreter gates generic machinery on these constexpr bools, so a
        # wrong fact silently changes shipping behaviour. The clean fixture has
        # a stagger meter, a WAIT step, a step `after`, a two-step pattern, a
        # player-flag guard and one window; no multi-window attack.
        self.assert_succeeds(self.compile())
        text = self.read(META_REL)
        facts = dict(re.findall(r"^constexpr bool ([A-Z0-9_]+) = (true|false);$", text, re.M))
        expected = {
            "HAS_STAGGER": "true",
            "HAS_WAIT_STEPS": "true",
            "HAS_STEP_AFTER": "true",
            "HAS_STEP_CHANCE": "false",
            "HAS_MULTI_STEP": "true",
            "HAS_MULTI_WINDOW": "false",
            "HAS_SIMPLE_GUARDS": "false",
        }
        self.assertEqual(facts, expected)

    def test_blob_header_matches_meta(self):
        self.assert_succeeds(self.compile())
        blob = self.blob()
        meta = self.meta_constants()
        self.assertEqual(blob[0:2], b"\x43\x4d")   # magic 'CM' little-endian
        self.assertEqual(blob[2], meta["VERSION"])
        self.assertEqual(blob[3], meta["FLAGS"])
        self.assertEqual(meta["MAGIC"], 0x4D43)
        self.assertEqual(meta["SIZE"], len(blob))
        self.assertEqual(meta["HEADER_SIZE"], 32)
        self.assertEqual(meta["CREATURES_COUNT"], 1)
        self.assertEqual(meta["PROFILES_COUNT"], 1)
        self.assertEqual(meta["SKELETONS_COUNT"], 1)
        self.assertEqual(meta["PARTS_COUNT"], 2)
        self.assertEqual(meta["STAGES_COUNT"], 2)
        self.assertEqual(meta["ANCHORS_COUNT"], 1)
        self.assertEqual(meta["ELEMS_COUNT"], 1)
        self.assertEqual(meta["REFS_COUNT"], 2)
        self.assertEqual(meta["ATTACKS_COUNT"], 1)
        self.assertEqual(meta["WINDOWS_COUNT"], 1)
        self.assertEqual(meta["PATTERNS_COUNT"], 1)
        self.assertEqual(meta["GUARDS_COUNT"], 1)
        self.assertEqual(meta["PREDICATES_COUNT"], 1)
        self.assertEqual(meta["STEPS_COUNT"], 2)
        # sections must tile the blob without gaps or padding.
        sections = ["CREATURES", "PROFILES", "SKELETONS", "PARTS", "STAGES", "ANCHORS",
                    "ELEMS", "REFS", "ATTACKS", "WINDOWS", "PATTERNS", "GUARDS", "PREDICATES", "STEPS"]
        records = ["CREATURE", "PROFILE", "SKELETON", "PART", "STAGE", "ANCHOR", "ELEM", "REF",
                   "ATTACK", "WINDOW", "PATTERN", "GUARD", "PREDICATE", "STEP"]
        for i, section in enumerate(sections):
            end = meta["%s_OFF" % section] + meta["%s_SIZE" % records[i]] * meta["%s_COUNT" % section]
            if i + 1 < len(sections):
                self.assertEqual(end, meta["%s_OFF" % sections[i + 1]], section)
            else:
                self.assertEqual(end, meta["SIZE"], section)

    def test_blob_record_payloads(self):
        self.assert_succeeds(self.compile())
        blob = self.blob()
        meta = self.meta_constants()

        creature = blob[meta["CREATURE_BEAST_OFF"]:meta["CREATURE_BEAST_OFF"] + meta["CREATURE_SIZE"]]
        self.assertEqual(creature, bytes([0, 0, 1, 1, 0, 1, 0, 1, 16, 12, 4, 80, 0, 100, 0, 32, 0]))

        profile = blob[meta["PROFILE_BEAST_OFF"]:meta["PROFILE_BEAST_OFF"] + meta["PROFILE_SIZE"]]
        self.assertEqual(profile, bytes([30, 18, 36, 8, 10, 6, 10, 40, 1, 2, 40, 0, 20, 0,
                                         60, 0, 90, 0, 20, 0, 15, 0]))

        skeleton = blob[meta["SKELETON_BEAST_16X12_OFF"]:meta["SKELETON_BEAST_16X12_OFF"] + meta["SKELETON_SIZE"]]
        self.assertEqual(skeleton, bytes([0, 1, 0, 1]))

        body = blob[meta["PART_BEAST_16X12_BODY_OFF"]:meta["PART_BEAST_16X12_BODY_OFF"] + meta["PART_SIZE"]]
        self.assertEqual(body, bytes([0, 0, 16, 12, 100, 100, 0, 1, 100, 100, 100, 0, 0, 0, 0, 0, 0, 0]))

        tail = blob[meta["PART_BEAST_TAIL_OFF"]:meta["PART_BEAST_TAIL_OFF"] + meta["PART_SIZE"]]
        self.assertEqual(tail, bytes([0xFA, 4, 8, 4, 150, 40, 1, 1, 150, 75, 100, 0, 2, 0, 1, 30, 0, 0]))

        stage0 = blob[meta["STAGE_BEAST_TAIL_0_OFF"]:meta["STAGE_BEAST_TAIL_0_OFF"] + meta["STAGE_SIZE"]]
        self.assertEqual(stage0, bytes([50, 0, 100, 0, 20, 2, 0, 1, 1, 0]))
        stage1 = blob[meta["STAGE_BEAST_TAIL_1_OFF"]:meta["STAGE_BEAST_TAIL_1_OFF"] + meta["STAGE_SIZE"]]
        self.assertEqual(stage1, bytes([0, 3, 200, 0, 0, 0, 1, 0, 1, 1]))

        attack = blob[meta["ATTACK_BEAST_JAB_OFF"]:meta["ATTACK_BEAST_JAB_OFF"] + meta["ATTACK_SIZE"]]
        self.assertEqual(attack, bytes([1, 20, 0, 0, 0, 1, 1, 1, 2, 4, 10, 1, 0, 1,
                                        20, 0, 6, 0, 30, 0, 7, 0]))

        window = blob[meta["WINDOW_BEAST_JAB_0_OFF"]:meta["WINDOW_BEAST_JAB_0_OFF"] + meta["WINDOW_SIZE"]]
        self.assertEqual(window, bytes([0, 0, 6, 0, 8, 0, 12, 10, 100, 0]))

        pattern = blob[meta["PATTERN_BEAST_P_JAB_OFF"]:meta["PATTERN_BEAST_P_JAB_OFF"] + meta["PATTERN_SIZE"]]
        self.assertEqual(pattern, bytes([0, 2, 0]))
        guard = blob[meta["GUARD_BEAST_P_JAB_OFF"]:meta["GUARD_BEAST_P_JAB_OFF"] + meta["GUARD_SIZE"]]
        self.assertEqual(guard, bytes([0, 36, 0, 100, 1, 0, 100, 0, 1]))
        predicate = blob[meta["PREDICATES_OFF"]:meta["PREDICATES_OFF"] + meta["PREDICATE_SIZE"]]
        self.assertEqual(predicate, bytes([1, 0, 1]))   # tail part idx, '>=', stage 1

        elems = blob[meta["ELEMS_OFF"]:meta["ELEMS_OFF"] + meta["ELEM_SIZE"] * meta["ELEMS_COUNT"]]
        self.assertEqual(elems, bytes([1, 200]))        # FIRE at 200%
        self.assertEqual(blob[meta["REFS_OFF"]], 0)     # disable attack 0
        self.assertEqual(blob[meta["REFS_OFF"] + 1], 0) # enable attack 0

        step0 = blob[meta["STEP_BEAST_P_JAB_0_OFF"]:meta["STEP_BEAST_P_JAB_0_OFF"] + meta["STEP_SIZE"]]
        self.assertEqual(step0, bytes([0, 0, 2, 100]))
        step1 = blob[meta["STEP_BEAST_P_JAB_1_OFF"]:meta["STEP_BEAST_P_JAB_1_OFF"] + meta["STEP_SIZE"]]
        self.assertEqual(step1, bytes([1, 5, 0, 100]))

        anchor = blob[meta["ANCHOR_BEAST_16X12_ORIGIN_OFF"]:meta["ANCHOR_BEAST_16X12_ORIGIN_OFF"] + meta["ANCHOR_SIZE"]]
        self.assertEqual(anchor, bytes([0, 0]))

    def test_expect_header_pins_sizes_spot_values_and_sha(self):
        self.assert_succeeds(self.compile())
        blob = self.blob()
        text = self.read(EXPECT_REL)
        expect = {}
        for line in text.splitlines():
            match = CONST_RE.match(line)
            if match:
                expect[match.group(2)] = int(match.group(3), 0)
        meta = self.meta_constants()
        for record in ("CREATURE", "PROFILE", "SKELETON", "PART", "STAGE", "ANCHOR", "ELEM",
                       "REF", "ATTACK", "WINDOW", "PATTERN", "GUARD", "PREDICATE", "STEP"):
            self.assertEqual(expect["%s_SIZE" % record], meta["%s_SIZE" % record])
        self.assertEqual(expect["BLOB_SIZE"], len(blob))
        self.assertEqual(expect["CREATURE_BEAST_HP"], 80)
        self.assertEqual(expect["CREATURE_BEAST_SPD"], 4)
        self.assertEqual(expect["CREATURE_BEAST_ATTACKS"], 1)
        self.assertEqual(expect["CREATURE_BEAST_PATTERNS"], 1)
        self.assertEqual(expect["ATTACK_BEAST_JAB_WINDUP"], 20)
        self.assertEqual(expect["ATTACK_BEAST_JAB_DMG"], 7)
        self.assertEqual(expect["PATTERN_BEAST_P_JAB_MAX_DIST"], 36)
        match = re.search(r"BLOB_SHA256\[32\] = \{(.*?)\};", text, re.S)
        digest = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-F]{2})", match.group(1)))
        self.assertEqual(digest, hashlib.sha256(blob).digest())

    def test_data_header_has_host_structs_and_arrays(self):
        self.assert_succeeds(self.compile())
        text = self.read(DATA_REL)
        for needle in ("struct Creature {", "struct Attack {", "struct Guard {", "struct Window {",
                       "std::array<Creature, 1> CREATURES", "std::array<Attack, 1> ATTACKS",
                       "std::array<Guard, 1> GUARDS", "std::array<Step, 2> STEPS"):
            self.assertIn(needle, text)
        self.assertIn("constexpr uint8_t CREATURE_BEAST = 0;", text)
        self.assertIn("constexpr uint8_t ATTACK_BEAST_JAB = 0;", text)


if __name__ == "__main__":
    unittest.main()
