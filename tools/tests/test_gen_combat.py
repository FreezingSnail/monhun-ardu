#!/usr/bin/env python3
"""Unit tests for tools/gen-combat.py (run: make test-tools).

The clean fixture under fixtures/gen_combat/clean is a minimal, fully featured
schema tree (skeleton with anchors, one creature with an attack, one window,
head + appendage zones, a zones-broken guard, a two-step pattern). Every failure
case copies it to build/tests/gen_combat/ and mutates the copy, so the tests
stay read-only on the repository fixtures and never write into /tmp.
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
        self.assertIn("creature beast (skeleton beast_16x12, stats w16 h12 hp80 spd4, spawn 100,32, collide body, enrage hpPct0 spdMul0 faceHold0 cue0) zones appendage D150 HP30 S40 ST20 head D120 HP10 S100 ST5", result.stdout)
        self.assertIn("zone head: box(10,2,6,6) dmgMul 120 hp 10 share 100 break 0x02 stagger 5 partSheet 0 brokenOverride 120 hurtOff 1 disable -", result.stdout)
        self.assertIn("zone appendage: box(-6,4,8,4) dmgMul 150 hp 30 share 40 break 0x01 stagger 20 partSheet 0 brokenOverride 200 hurtOff 1 disable jab", result.stdout)
        self.assertIn("attack jab: windup20 active6 recover30 dmg7 move lunge(20) windows 1 wallStun 0", result.stdout)
        self.assertIn("window 0: t[0,6] box(8,0,12,10) dmgMul 100", result.stdout)
        self.assertIn("pattern p_jab: guard minDist0 maxDist36 hp[0,100] player0x01 cd0 chance100 zonesBroken appendage facing any", result.stdout)
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
        self.assert_fails(self.compile(), "profile: required for a non-static creature")

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

    def test_face_hold_default_and_emit(self):
        # nch.4: profile.faceHold is optional (default 0) and packs as the 11th
        # u8 scalar, right after zoneFlags.
        self.assert_succeeds(self.compile())
        meta = self.meta_constants()
        o = meta["PROFILE_BEAST_OFF"]
        self.assertEqual(self.blob()[o + 10], 0, "faceHold defaults to 0")
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["profile"].__setitem__("faceHold", 10))
        self.assert_succeeds(self.compile())
        self.assertEqual(self.blob()[o + 10], 10, "faceHold emitted")
        self.assertIn("constexpr uint8_t PROFILE_BEAST_FACE_HOLD = 10;", self.read(EXPECT_REL))

    def test_face_hold_range_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["profile"].__setitem__("faceHold", 256))
        self.assert_fails(self.compile(), "profile: faceHold: out of range 0..255: 256")

    def test_turn_rate_default_emit_range_and_dump(self):
        # feel.14: profile.turnRate is optional (default 0), packs as the 12th u8
        # scalar right after faceHold (shifting the six u16 timers by one), and
        # flips HAS_TURN_RATE once a creature authors it.
        self.assert_succeeds(self.compile())
        meta = self.meta_constants()
        self.assertEqual(meta["PROFILE_SIZE"], 26, "profile record grew for restAfter/restT")
        o = meta["PROFILE_BEAST_OFF"]
        self.assertEqual(self.blob()[o + 11], 0, "turnRate defaults to 0")
        self.assertIn("constexpr uint8_t PROFILE_BEAST_TURN_RATE = 0;", self.read(EXPECT_REL))
        self.assertIn("constexpr bool HAS_TURN_RATE = false;", self.read(META_REL))
        self.assertIn("turnRate0", self.compile("--dump").stdout)
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["profile"].__setitem__("turnRate", 2))
        self.assert_succeeds(self.compile())
        self.assertEqual(self.blob()[o + 11], 2, "turnRate emitted at byte 11")
        self.assertIn("constexpr uint8_t PROFILE_BEAST_TURN_RATE = 2;", self.read(EXPECT_REL))
        self.assertIn("constexpr bool HAS_TURN_RATE = true;", self.read(META_REL))
        self.assertIn("turnRate2", self.compile("--dump").stdout)
        # The u16 timers stay contiguous right after the new byte.
        self.assertEqual(self.read_int16(self.blob(), o + 12), 40, "cdBase follows turnRate")

    def test_rest_after_default_emit_range_and_dump(self):
        # dzr: profile.restAfter/restT are optional (default 0 = disabled), pack
        # as the trailing byte pair after the six u16 timers (26 B record).
        self.assert_succeeds(self.compile())
        meta = self.meta_constants()
        o = meta["PROFILE_BEAST_OFF"]
        self.assertEqual(self.blob()[o + 24], 0, "restAfter defaults to 0")
        self.assertEqual(self.blob()[o + 25], 0, "restT defaults to 0")
        self.assertIn("constexpr uint8_t PROFILE_BEAST_REST_AFTER = 0;", self.read(EXPECT_REL))
        self.assertIn("constexpr uint8_t PROFILE_BEAST_REST_T = 0;", self.read(EXPECT_REL))
        self.assertIn("rest0/0", self.compile("--dump").stdout)
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["profile"].__setitem__("restAfter", 2))
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["profile"].__setitem__("restT", 110))
        self.assert_succeeds(self.compile())
        self.assertEqual(self.blob()[o + 24], 2, "restAfter emitted at byte 24")
        self.assertEqual(self.blob()[o + 25], 110, "restT emitted at byte 25")
        self.assertIn("constexpr uint8_t PROFILE_BEAST_REST_AFTER = 2;", self.read(EXPECT_REL))
        self.assertIn("constexpr uint8_t PROFILE_BEAST_REST_T = 110;", self.read(EXPECT_REL))
        self.assertIn("rest2/110", self.compile("--dump").stdout)

    def test_rest_after_out_of_range_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["profile"].__setitem__("restAfter", 256))
        self.assert_fails(self.compile(), "profile: restAfter: out of range 0..255: 256")
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["profile"].__setitem__("restT", -1))
        self.assert_fails(self.compile(), "profile: restT: out of range 0..255: -1")

    def test_turn_rate_out_of_range_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["profile"].__setitem__("turnRate", 9))
        self.assert_fails(self.compile(), "profile: turnRate: out of range 0..8: 9")

    def test_turn_rate_integer_only(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["profile"].__setitem__("turnRate", 1.5))
        self.assert_fails(self.compile(), "profile: turnRate: expected an integer, got 1.5")
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["profile"].__setitem__("turnRate", True))
        self.assert_fails(self.compile(), "profile: turnRate: expected an integer, got True")

    def test_zone_hp_range_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["zones"]["appendage"].__setitem__("hp", 300))
        self.assert_fails(self.compile(), "zones.appendage: hp: out of range 0..255: 300")

    def test_phys_enum_membership(self):
        self.mutate("data/creatures/beast.json", lambda doc: doc["attacks"][0].__setitem__("phys", "FIRE"))
        self.assert_fails(self.compile(), "attacks[0]: phys: unknown value 'FIRE' (want one of BLUNT, SHOT, SLASH)")

    def test_elem_enum_membership(self):
        self.mutate("data/creatures/beast.json", lambda doc: doc["attacks"][0].__setitem__("elem", "SLASH"))
        self.assert_fails(self.compile(), "attacks[0]: elem: unknown value 'SLASH'")

    def test_facing_enum_membership(self):
        self.mutate("data/creatures/beast.json", lambda doc: doc["attacks"][0].__setitem__("facing", "spin"))
        self.assert_fails(self.compile(), "attacks[0]: facing: unknown value 'spin'"
                          " (want one of lock-at-windup, lock-away, track)")

    def test_facing_lock_away_encodes_two(self):
        # nch.2: the FACINGS table grows lock-away = 2; the packed attack record
        # carries it in the facing byte (offset 4, after moveType/speed/moveDx/moveDy).
        self.mutate("data/creatures/beast.json", lambda doc: doc["attacks"][0].__setitem__("facing", "lock-away"))
        self.assert_succeeds(self.compile())
        blob = self.blob()
        meta = self.meta_constants()
        attack = blob[meta["ATTACK_BEAST_JAB_OFF"]:meta["ATTACK_BEAST_JAB_OFF"] + meta["ATTACK_SIZE"]]
        self.assertEqual(attack[4], 2)

    def test_attack_wallstun_default_and_emit(self):
        # feel.4: wallStun is optional (default 0) and packs as the 13th u8 scalar
        # (byte 12, right after cue), shifting firstWindow/windowCount/quad by one.
        self.assert_succeeds(self.compile())
        meta = self.meta_constants()
        o = meta["ATTACK_BEAST_JAB_OFF"]
        self.assertEqual(self.blob()[o + 12], 0, "wallStun defaults to 0")
        self.assertIn("constexpr uint8_t ATTACK_BEAST_JAB_WALLSTUN = 0;", self.read(EXPECT_REL))
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0].__setitem__("wallStun", 14))
        self.assert_succeeds(self.compile())
        self.assertEqual(self.blob()[o + 12], 14, "wallStun emitted at byte 12")
        self.assertIn("constexpr uint8_t ATTACK_BEAST_JAB_WALLSTUN = 14;", self.read(EXPECT_REL))
        self.assertIn("wallStun 14", self.compile("--dump").stdout)

    def test_enrage_default_emit_and_dump(self):
        # feel.6: stats.enrage is optional (all-zero = disabled) and packs as the
        # last four creature bytes (hpPct, spdMul, faceHold, cue); --dump prints
        # the quad and combat_expect pins the spot values.
        self.assert_succeeds(self.compile())
        meta = self.meta_constants()
        o = meta["CREATURE_BEAST_OFF"]
        self.assertEqual(meta["CREATURE_SIZE"], 40, "creature record after the dead sheet byte")
        self.assertEqual(self.blob()[o + 24:o + 28], bytes([0, 0, 0, 0]), "enrage defaults to disabled")
        self.assertEqual(self.blob()[o + 28:o + 40], bytes([0] * 12), "carve tail defaults to empty slots")
        # No expect pins while the creature disables enrage (device-image budget).
        self.assertNotIn("CREATURE_BEAST_ENRAGE_", self.read(EXPECT_REL))
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["stats"].__setitem__("enrage", {"hpPct": 40, "spdMul": 150, "faceHold": 8, "cue": "part_break"}))
        self.assert_succeeds(self.compile())
        self.assertEqual(self.blob()[o + 24:o + 28], bytes([40, 150, 8, 2]), "enrage emitted hpPct/spdMul/faceHold/cue")
        expect = self.read(EXPECT_REL)
        self.assertIn("constexpr uint8_t CREATURE_BEAST_ENRAGE_HP_PCT = 40;", expect)
        self.assertIn("constexpr uint8_t CREATURE_BEAST_ENRAGE_SPD_MUL = 150;", expect)
        self.assertIn("constexpr uint8_t CREATURE_BEAST_ENRAGE_FACE_HOLD = 8;", expect)
        self.assertIn("constexpr uint8_t CREATURE_BEAST_ENRAGE_CUE = 2;", expect)
        self.assertIn("enrage hpPct40 spdMul150 faceHold8 cue2", self.compile("--dump").stdout)

    def test_enrage_missing_keys_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["stats"].__setitem__("enrage", {"hpPct": 40, "spdMul": 150}))
        self.assert_fails(self.compile(), "stats.enrage: missing key 'faceHold'")

    def test_enrage_unknown_key_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["stats"].__setitem__("enrage", {"hpPct": 40, "spdMul": 150, "faceHold": 8, "roar": 1}))
        self.assert_fails(self.compile(), "stats.enrage: unknown key 'roar'")

    def test_enrage_range_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["stats"].__setitem__("enrage", {"hpPct": 101, "spdMul": 150, "faceHold": 8}))
        self.assert_fails(self.compile(), "stats.enrage: hpPct: out of range 0..100: 101")
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["stats"].__setitem__("enrage", {"hpPct": 40, "spdMul": 256, "faceHold": 8}))
        self.assert_fails(self.compile(), "stats.enrage: spdMul: out of range 0..255: 256")

    def test_enrage_integer_only(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["stats"].__setitem__("enrage", {"hpPct": 40.0, "spdMul": 150, "faceHold": 8}))
        self.assert_fails(self.compile(), "stats.enrage: hpPct: expected an integer, got 40.0")
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["stats"].__setitem__("enrage", {"hpPct": 40, "spdMul": True, "faceHold": 8}))
        self.assert_fails(self.compile(), "stats.enrage: spdMul: expected an integer, got True")

    def test_enrage_cue_enum_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["stats"].__setitem__("enrage", {"hpPct": 40, "spdMul": 150, "faceHold": 8, "cue": "roar"}))
        self.assert_fails(self.compile(), "stats.enrage: cue: unknown value 'roar' (want one of none, part_break, windup)")

    def test_attack_tell_default_emit_and_dump(self):
        # feel.5: tell is optional (default 0 dot) and packs at attack byte 23
        # (bih.4 appended the 3-byte art triple after it); --dump prints the
        # shape and combat_expect pins it for the first attack.
        self.assert_succeeds(self.compile())
        meta = self.meta_constants()
        self.assertEqual(meta["ATTACK_SIZE"], 27, "attack record grew for the tell + art bytes")
        o = meta["ATTACK_BEAST_JAB_OFF"]
        self.assertEqual(self.blob()[o + 23], 0, "tell defaults to dot")
        self.assertIn("constexpr uint8_t ATTACK_BEAST_JAB_TELL = 0;", self.read(EXPECT_REL))
        self.assertIn("tell dot", self.compile("--dump").stdout)
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0].__setitem__("tell", "ring"))
        self.assert_succeeds(self.compile())
        self.assertEqual(self.blob()[o + 23], 3, "tell emitted at byte 23")
        self.assertIn("constexpr uint8_t ATTACK_BEAST_JAB_TELL = 3;", self.read(EXPECT_REL))
        self.assertIn("tell ring", self.compile("--dump").stdout)

    def test_attack_tell_enum_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0].__setitem__("tell", "spiral"))
        self.assert_fails(self.compile(),
                          "attacks[0]: tell: unknown value 'spiral' (want one of arc, dot, line, ring, zone)")

    def test_attack_wallstun_range_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0].__setitem__("wallStun", 256))
        self.assert_fails(self.compile(), "attacks[0]: wallStun: out of range 0..255: 256")

    def test_attack_wallstun_integer_only(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0].__setitem__("wallStun", 1.5))
        self.assert_fails(self.compile(), "attacks[0]: wallStun: expected an integer, got 1.5")

    def test_hop_move_required_dx_dy_rejected(self):
        # feel.7: move.type hop requires face-relative dx and dy.
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0].__setitem__("move", {"type": "hop"}))
        self.assert_fails(self.compile(),
                          "move: dx: required for move type 'hop'",
                          "move: dy: required for move type 'hop'")

    def test_hop_move_speedf_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0].__setitem__("move", {"type": "hop", "dx": 6, "dy": -10, "speedF": 20}))
        self.assert_fails(self.compile(), "move: speedF: not allowed for move type 'hop'")

    def test_hop_move_unknown_key_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0].__setitem__("move", {"type": "hop", "dx": 6, "dy": -10, "accel": 1}))
        self.assert_fails(self.compile(), "move: unknown key 'accel'")

    def test_hop_move_range_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0].__setitem__("move", {"type": "hop", "dx": 128, "dy": 0}))
        self.assert_fails(self.compile(), "move: dx: out of range -128..127: 128")
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0].__setitem__("move", {"type": "hop", "dx": 0, "dy": -129}))
        self.assert_fails(self.compile(), "move: dy: out of range -128..127: -129")

    def test_hop_move_integer_only(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0].__setitem__("move", {"type": "hop", "dx": 1.5, "dy": 0}))
        self.assert_fails(self.compile(), "move: dx: expected an integer, got 1.5")
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0].__setitem__("move", {"type": "hop", "dx": 0, "dy": True}))
        self.assert_fails(self.compile(), "move: dy: expected an integer, got True")

    def test_hop_move_packs_signed_and_dumps(self):
        # feel.7: hop packs dx/dy at attack bytes 2/3 (int8, signed); --dump
        # prints the vector. ATTACK_SIZE is 27 since bih.4 appended the attack
        # art triple, which does not move the move-prefix offsets.
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0].__setitem__("move", {"type": "hop", "dx": 6, "dy": -10}))
        self.assert_succeeds(self.compile())
        meta = self.meta_constants()
        self.assertEqual(meta["ATTACK_SIZE"], 27)
        o = meta["ATTACK_BEAST_JAB_OFF"]
        self.assertEqual(self.blob()[o + 0], 3, "hop moveType 3")
        self.assertEqual(self.blob()[o + 1], 0, "hop speedF stays 0")
        self.assertEqual(self.blob()[o + 2], 6, "hop dx emitted")
        self.assertEqual(self.blob()[o + 3], 0xF6, "hop dy emitted signed (-10)")
        self.assertIn("move hop(6,-10)", self.compile("--dump").stdout)

    def test_guard_facing_unknown_value_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["patterns"][0]["guard"].__setitem__("facing", "sideways"))
        self.assert_fails(self.compile(),
                          "guard: facing: unknown value 'sideways' (want one of behind, front)")

    def test_guard_facing_encodes_behind_front_and_fact(self):
        # Guard facing clause: behind = 1, front = 2 in the guard record's 9th
        # byte (after zonesBroken); the fact flips true once a shipped guard uses it.
        self.assert_succeeds(self.compile())
        meta = self.meta_constants()
        off = meta["GUARD_BEAST_P_JAB_OFF"]
        self.assertEqual(self.blob()[off + 8], 0, "facing defaults to any")
        self.assertIn("constexpr bool HAS_GUARD_FACING = false;", self.read(META_REL))
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["patterns"][0]["guard"].__setitem__("facing", "behind"))
        self.assert_succeeds(self.compile())
        self.assertEqual(self.blob()[off + 8], 1, "behind emits 1")
        self.assertIn("constexpr bool HAS_GUARD_FACING = true;", self.read(META_REL))
        self.assertIn("facing behind", self.compile("--dump").stdout)
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["patterns"][0]["guard"].__setitem__("facing", "front"))
        self.assert_succeeds(self.compile())
        self.assertEqual(self.blob()[off + 8], 2, "front emits 2")

    def test_unknown_zone_rejected(self):
        def add_zone(doc):
            doc["zones"]["wing"] = json.loads(json.dumps(doc["zones"]["head"]))

        self.mutate("data/creatures/beast.json", add_zone)
        self.assert_fails(self.compile(), "zones: unknown zone 'wing' (want head or appendage)")

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

    def test_unknown_zones_broken_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["patterns"][0]["guard"].__setitem__("zonesBroken", ["wing"]))
        self.assert_fails(self.compile(), "zonesBroken[0]: unknown zone 'wing' (want head or appendage)")

    def test_duplicate_zones_broken_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["patterns"][0]["guard"].__setitem__("zonesBroken", ["appendage", "appendage"]))
        self.assert_fails(self.compile(), "zonesBroken[1]: duplicate zone 'appendage'")

    def test_unknown_zone_disable_ref_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["zones"]["appendage"]["broken"].__setitem__("disableAttacks", ["kick"]))
        self.assert_fails(self.compile(), "zones.appendage.broken: disableAttacks[0]: unknown attack id 'kick'")

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

    def test_attack_count_may_exceed_eight_when_mask_fits(self):
        # unlockMask is a u16 over the *disabled attack's global index*, not the
        # total attack count: the kits add attacks without widening the mask and
        # the runtime ignores indices >= 16 (combatAttackDisabled). Sixteen
        # attacks whose disable list stays on index 0 must therefore compile.
        def widen(doc):
            base = doc["attacks"][0]
            for i in range(2, 17):
                clone = json.loads(json.dumps(base))
                clone["id"] = "jab%d" % i
                doc["attacks"].append(clone)

        self.mutate("data/creatures/beast.json", widen)
        self.assert_succeeds(self.compile())

    def test_disabled_attack_index_must_fit_unlockmask(self):
        # The only unrepresentable case: a zone disables an attack whose global
        # index is >= 16 (bit 16 does not fit the u16 mask). Still rejected.
        def widen(doc):
            base = doc["attacks"][0]
            for i in range(2, 18):
                clone = json.loads(json.dumps(base))
                clone["id"] = "jab%d" % i
                doc["attacks"].append(clone)
            doc["zones"]["appendage"]["broken"]["disableAttacks"] = ["jab17"]

        self.mutate("data/creatures/beast.json", widen)
        self.assert_fails(self.compile(), "unlockMask overflows u16 for beast appendage")

    # ----------------------------------------------------------- blob ABI

    def test_data_facts_match_fixture(self):
        # The interpreter gates generic machinery on these constexpr bools, so a
        # wrong fact silently changes shipping behaviour. The clean fixture has
        # a stagger meter, a WAIT step, a step `after`, a two-step pattern, a
        # player-flag + zones-broken guard, head + appendage zones and one
        # window; no multi-window attack.
        self.assert_succeeds(self.compile())
        text = self.read(META_REL)
        facts = dict(re.findall(r"^constexpr bool ([A-Z0-9_]+) = (true|false);$", text, re.M))
        expected = {
            "HAS_STAGGER": "true",
            "HAS_HIT_STAGGER": "true",
            "HAS_WAIT_STEPS": "true",
            "HAS_STEP_AFTER": "true",
            "HAS_STEP_CHANCE": "false",
            "HAS_MULTI_STEP": "true",
            "HAS_MULTI_WINDOW": "false",
            "HAS_SIMPLE_GUARDS": "false",
            "HAS_ZONES": "true",
            "HAS_GUARD_HP": "false",
            "HAS_GUARD_PLAYER": "true",
            "HAS_GUARD_COOLDOWN": "false",
            "HAS_GUARD_CHANCE": "false",
            "HAS_GUARD_ZONES": "true",
            "HAS_GUARD_FACING": "false",
            "HAS_ENRAGE": "false",
            "HAS_TURN_RATE": "false",
            "HAS_CARVE": "false",
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
        self.assertEqual(meta["ZONES_COUNT"], 2)
        self.assertEqual(meta["ANCHORS_COUNT"], 1)
        self.assertEqual(meta["ATTACKS_COUNT"], 1)
        self.assertEqual(meta["WINDOWS_COUNT"], 1)
        self.assertEqual(meta["PATTERNS_COUNT"], 1)
        self.assertEqual(meta["GUARDS_COUNT"], 1)
        self.assertEqual(meta["STEPS_COUNT"], 2)
        self.assertEqual(meta["ART_COUNT"], 1)
        for i in range(3):
            self.assertEqual(self.read_int16(blob, 26 + i * 2), 0)
        self.assertEqual(self.read_int16(blob, 24), meta["ART_COUNT"])
        # sections must tile the blob without gaps or padding.
        sections = ["CREATURES", "PROFILES", "SKELETONS", "ZONES", "ANCHORS",
                    "ATTACKS", "WINDOWS", "PATTERNS", "GUARDS", "STEPS", "ART"]
        records = ["CREATURE", "PROFILE", "SKELETON", "ZONE", "ANCHOR",
                   "ATTACK", "WINDOW", "PATTERN", "GUARD", "STEP", "ART"]
        for i, section in enumerate(sections):
            end = meta["%s_OFF" % section] + meta["%s_SIZE" % records[i]] * meta["%s_COUNT" % section]
            if i + 1 < len(sections):
                self.assertEqual(end, meta["%s_OFF" % sections[i + 1]], section)
            else:
                self.assertEqual(end, meta["SIZE"], section)

    @staticmethod
    def read_int16(blob, off):
        return blob[off] | (blob[off + 1] << 8)

    def test_blob_record_payloads(self):
        self.assert_succeeds(self.compile())
        blob = self.blob()
        meta = self.meta_constants()

        creature = blob[meta["CREATURE_BEAST_OFF"]:meta["CREATURE_BEAST_OFF"] + meta["CREATURE_SIZE"]]
        # 40 B creature record: stats then the default collide box (body 16x12
        # at the origin) then hp/spawnX/spawnY (epic monhun-ardu-nch), then the
        # static/brokenBody fields (6zb.6; 0 = dynamic, no broken shrink), then
        # the feel.6 enrage quad (all 0 = disabled) and the prg.3 carve tail (4
        # empty item/count/chance slots). The dead per-kind sheet byte was
        # dropped in bih.6 (the art descriptor replaced it).
        self.assertEqual(creature, bytes([0, 0, 0, 1, 0, 1, 0, 1, 16, 12, 4, 0, 0, 16, 12, 80, 0, 100, 0, 32, 0, 0, 0, 0, 0, 0, 0, 0] + [0] * 12))

        profile = blob[meta["PROFILE_BEAST_OFF"]:meta["PROFILE_BEAST_OFF"] + meta["PROFILE_SIZE"]]
        # 26 B profile: 12 u8 scalars (zoneFlags, the nch.4 faceHold byte, then
        # the feel.14 turnRate byte), six u16 timers, then the dzr rest pair.
        self.assertEqual(profile, bytes([30, 18, 36, 8, 10, 6, 10, 40, 1, 3, 0, 0, 40, 0, 20, 0,
                                         60, 0, 90, 0, 20, 0, 15, 0, 0, 0]))

        skeleton = blob[meta["SKELETON_BEAST_16X12_OFF"]:meta["SKELETON_BEAST_16X12_OFF"] + meta["SKELETON_SIZE"]]
        self.assertEqual(skeleton, bytes([0, 1]))

        # 14 B zone: 13 legacy bytes then the bih.5 partSheet byte (0 = no part).
        head = blob[meta["ZONE_BEAST_HEAD_OFF"]:meta["ZONE_BEAST_HEAD_OFF"] + meta["ZONE_SIZE"]]
        self.assertEqual(head, bytes([10, 2, 6, 6, 10, 120, 100, 2, 5, 120, 1, 0, 0, 0]))

        tail = blob[meta["ZONE_BEAST_APPENDAGE_OFF"]:meta["ZONE_BEAST_APPENDAGE_OFF"] + meta["ZONE_SIZE"]]
        self.assertEqual(tail, bytes([0xFA, 4, 8, 4, 30, 150, 40, 1, 20, 200, 3, 1, 0, 0]))

        attack = blob[meta["ATTACK_BEAST_JAB_OFF"]:meta["ATTACK_BEAST_JAB_OFF"] + meta["ATTACK_SIZE"]]
        # 27 B attack: 12 scalars (cue then the feel.4 wallStun byte), then
        # firstWindow/windowCount, the contiguous u16 timing quad, the feel.5
        # tell byte (0 = dot), then the bih.4 art triple (sheet 0, frame 0,
        # mode 0 = generic body draw; the fixture authors no attack art).
        self.assertEqual(attack, bytes([1, 20, 0, 0, 0, 1, 1, 1, 2, 4, 10, 1, 0, 0, 1,
                                        20, 0, 6, 0, 30, 0, 7, 0, 0, 0, 0, 0]))

        window = blob[meta["WINDOW_BEAST_JAB_0_OFF"]:meta["WINDOW_BEAST_JAB_0_OFF"] + meta["WINDOW_SIZE"]]
        self.assertEqual(window, bytes([0, 0, 6, 0, 8, 0, 12, 10, 100, 0]))

        pattern = blob[meta["PATTERN_BEAST_P_JAB_OFF"]:meta["PATTERN_BEAST_P_JAB_OFF"] + meta["PATTERN_SIZE"]]
        self.assertEqual(pattern, bytes([0, 2, 0]))
        guard = blob[meta["GUARD_BEAST_P_JAB_OFF"]:meta["GUARD_BEAST_P_JAB_OFF"] + meta["GUARD_SIZE"]]
        self.assertEqual(guard, bytes([0, 36, 0, 100, 1, 0, 100, 2, 0]))   # zone bit 2 = appendage; facing any

        step0 = blob[meta["STEP_BEAST_P_JAB_0_OFF"]:meta["STEP_BEAST_P_JAB_0_OFF"] + meta["STEP_SIZE"]]
        self.assertEqual(step0, bytes([0, 0, 2, 100]))
        step1 = blob[meta["STEP_BEAST_P_JAB_1_OFF"]:meta["STEP_BEAST_P_JAB_1_OFF"] + meta["STEP_SIZE"]]
        self.assertEqual(step1, bytes([1, 5, 0, 100]))

        anchor = blob[meta["ANCHOR_BEAST_16X12_ORIGIN_OFF"]:meta["ANCHOR_BEAST_16X12_ORIGIN_OFF"] + meta["ANCHOR_SIZE"]]
        self.assertEqual(anchor, bytes([0, 0]))

    def test_static_creature_record(self):
        # A static prop: no profile/attacks/patterns keys at all, optional
        # brokenBody, optional zone hp/bodyShare (defaults 0/100).
        doc = {
            "id": "pole",
            "skeleton": "beast_16x12",
            "static": True,
            "stats": {"w": 20, "h": 36, "hp": 0, "spd": 0, "spawnX": 140, "spawnY": 40,
                      "brokenBody": {"w": 20, "h": 36}},
            "zones": {
                "head": {"box": {"ox": -128, "oy": 0, "w": 255, "h": 16}, "dmgMul": 140},
                "appendage": {"box": {"ox": 20, "oy": 8, "w": 8, "h": 12}, "dmgMul": 101,
                              "hp": 40, "breakTypes": ["BLUNT"], "broken": {"hurtOn": False}},
            },
        }
        with open(self.path("data", "creatures", "pole.json"), "w", encoding="utf-8", newline="\n") as handle:
            json.dump(doc, handle, indent=2)
            handle.write("\n")
        self.assert_succeeds(self.compile())
        meta = self.meta_constants()
        self.assertEqual(meta["CREATURES_COUNT"], 2)
        self.assertEqual(meta["ZONES_COUNT"], 4)
        self.assertEqual(meta["CREATURE_POLE"], 1)
        self.assertEqual(meta["ZONE_POLE_HEAD"], 2)
        self.assertEqual(meta["ZONE_POLE_APPENDAGE"], 3)

        blob = self.blob()
        o = meta["CREATURE_POLE_OFF"]
        rec = blob[o:o + meta["CREATURE_SIZE"]]
        # skeleton, profile(=creature idx), head/append zone, no attacks/patterns,
        # body 20x36, default collide, hp/spawn, static flags, brokenBody,
        # then the inert feel.6 enrage quad and the prg.3 carve tail (empty).
        self.assertEqual(rec, bytes([0, 1, 2, 3, 0, 0, 0, 0, 20, 36, 0,
                                     0, 0, 20, 36, 0, 0, 140, 0, 40, 0,
                                     1, 20, 36, 0, 0, 0, 0] + [0] * 12))

        # Static profile is inert (all zero, denominators 1, zoneFlags 0x03).
        p = meta["PROFILE_POLE_OFF"]
        prof = blob[p:p + meta["PROFILE_SIZE"]]
        self.assertEqual(prof, bytes([0, 0, 0, 0, 1, 0, 1, 0, 0, 3, 0, 0,
                                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]))

        head = blob[meta["ZONE_POLE_HEAD_OFF"]:meta["ZONE_POLE_HEAD_OFF"] + meta["ZONE_SIZE"]]
        # box -128,0,255,16; hp 0 (omitted), dmgMul 140, bodyShare 100 (default),
        # no breakTypes/broken/stagger/part.
        self.assertEqual(head, bytes([0x80, 0, 255, 16, 0, 140, 100, 0, 0, 140, 0, 0, 0, 0]))
        append = blob[meta["ZONE_POLE_APPENDAGE_OFF"]:meta["ZONE_POLE_APPENDAGE_OFF"] + meta["ZONE_SIZE"]]
        self.assertEqual(append, bytes([20, 8, 8, 12, 40, 101, 100, 2, 0, 101, 1, 0, 0, 0]))

        expect = self.read(EXPECT_REL)
        self.assertIn("constexpr uint8_t CREATURE_POLE_STATIC = 1;", expect)
        self.assertIn("constexpr uint8_t CREATURE_POLE_BROKEN_W = 20;", expect)
        self.assertIn("constexpr uint8_t CREATURE_POLE_BROKEN_H = 36;", expect)

    # ------------------------------------------------------------- art (bih)

    def write_art_sheets(self, names):
        with open(self.path("data", "art_sheets.json"), "w", encoding="utf-8", newline="\n") as handle:
            json.dump({"version": 1, "sheets": names}, handle, indent=2)
            handle.write("\n")

    def test_art_default_empty_no_pins(self):
        # A creature with no art block packs an all-zero ART record (sheet 0 =
        # legacy draw) and emits no per-creature art pins (device-image budget).
        self.assert_succeeds(self.compile())
        meta = self.meta_constants()
        self.assertEqual(meta["ART_SIZE"], 10, "art record is 10 B")
        self.assertEqual(meta["ART_COUNT"], 1, "one art record per creature")
        self.assertEqual(meta["ART_BEAST_OFF"], meta["STEPS_OFF"] + meta["STEP_SIZE"] * meta["STEPS_COUNT"], "art appended after steps")
        self.assertEqual(meta["ART_BEAST_OFF"] + meta["ART_SIZE"] * meta["ART_COUNT"], meta["SIZE"], "art ends the blob")
        blob = self.blob()
        self.assertEqual(blob[meta["ART_BEAST_OFF"]:meta["ART_BEAST_OFF"] + 10], bytes([0] * 10), "empty art defaults")
        self.assertNotIn("CREATURE_BEAST_ART_", self.read(EXPECT_REL))

    def test_art_emit_validate_and_dump(self):
        self.write_art_sheets(["fxpole", "fxbeast"])
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc.__setitem__("art", {
                        "sheet": "fxbeast", "anchorY": -3, "stride": 7,
                        "idle0": 0, "idleCount": 2, "windup": 5, "attack": 6,
                        "recover": 4, "flash": 8, "dead": 9,
                    }))
        self.assert_succeeds(self.compile())
        meta = self.meta_constants()
        o = meta["ART_BEAST_OFF"]
        blob = self.blob()
        # sheet resolves 1-based in list order (fxbeast -> 2); anchorY i8.
        self.assertEqual(blob[o:o + 10], bytes([2, 0xFD, 7, 0, 2, 5, 6, 4, 8, 9]))
        expect = self.read(EXPECT_REL)
        self.assertIn("constexpr uint8_t CREATURE_BEAST_ART_SHEET = 2;", expect)
        self.assertIn("constexpr int8_t CREATURE_BEAST_ART_ANCHOR_Y = -3;", expect)
        self.assertIn("constexpr uint8_t CREATURE_BEAST_ART_STRIDE = 7;", expect)
        self.assertIn("constexpr uint8_t CREATURE_BEAST_ART_FLASH = 8;", expect)
        self.assertIn("constexpr uint8_t CREATURE_BEAST_ART_DEAD = 9;", expect)
        self.assertIn("art: sheet2 anchorY-3 stride7 idle0+2 windup5 attack6 recover4 flash8 dead9", self.compile("--dump").stdout)
        # The host mirror carries the same Art row.
        self.assertIn("std::array<Art, 1> ART", self.read(DATA_REL))

    def test_art_unknown_sheet_rejected(self):
        self.write_art_sheets(["fxpole"])
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc.__setitem__("art", {
                        "sheet": "missing", "anchorY": 0, "stride": 0,
                        "idle0": 0, "idleCount": 0, "windup": 0, "attack": 0,
                        "recover": 0, "flash": 0, "dead": 0,
                    }))
        self.assert_fails(self.compile(), "art: sheet: unknown art sheet 'missing'")

    def test_art_requires_sheet_file(self):
        # An authored art block with no data/art_sheets.json fails loudly.
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc.__setitem__("art", {
                        "sheet": "fxpole", "anchorY": 0, "stride": 0,
                        "idle0": 0, "idleCount": 0, "windup": 0, "attack": 0,
                        "recover": 0, "flash": 0, "dead": 0,
                    }))
        self.assert_fails(self.compile(), "missing art sheet file (art.sheet resolves against it)")

    def test_art_missing_and_unknown_keys_rejected(self):
        self.write_art_sheets(["fxpole"])
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc.__setitem__("art", {"sheet": "fxpole", "anchorY": 0}))
        self.assert_fails(self.compile(), "art: missing key 'stride'")
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc.__setitem__("art", {
                        "sheet": "fxpole", "anchorY": 0, "stride": 0, "idle0": 0,
                        "idleCount": 0, "windup": 0, "attack": 0, "recover": 0,
                        "flash": 0, "dead": 0, "loop": 1}))
        self.assert_fails(self.compile(), "art: unknown key 'loop'")

    def test_art_range_and_integer_only(self):
        self.write_art_sheets(["fxpole"])
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc.__setitem__("art", {
                        "sheet": "fxpole", "anchorY": 200, "stride": 0, "idle0": 0,
                        "idleCount": 0, "windup": 0, "attack": 0, "recover": 0,
                        "flash": 0, "dead": 0}))
        self.assert_fails(self.compile(), "art: anchorY: out of range -128..127: 200")
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc.__setitem__("art", {
                        "sheet": "fxpole", "anchorY": 0, "stride": 1.5, "idle0": 0,
                        "idleCount": 0, "windup": 0, "attack": 0, "recover": 0,
                        "flash": 0, "dead": 0}))
        self.assert_fails(self.compile(), "art: stride: expected an integer, got 1.5")

    # ---------------------------------------------------- attack art (bih.4)

    def test_attack_art_emit_and_dump(self):
        self.write_art_sheets(["fxpole", "fxatk"])
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0].__setitem__("art", {"sheet": "fxatk", "frame": 2, "mode": "spin"}))
        self.assert_succeeds(self.compile())
        meta = self.meta_constants()
        o = meta["ATTACK_BEAST_JAB_OFF"]
        self.assertEqual(self.blob()[o + 24], 2, "attack art sheet index (1-based)")
        self.assertEqual(self.blob()[o + 25], 2, "attack art frame base")
        self.assertEqual(self.blob()[o + 26], 1, "attack art spin mode")
        expect = self.read(EXPECT_REL)
        self.assertIn("constexpr uint8_t ATTACK_BEAST_JAB_ART_SHEET = 2;", expect)
        self.assertIn("constexpr uint8_t ATTACK_BEAST_JAB_ART_FRAME = 2;", expect)
        self.assertIn("constexpr uint8_t ATTACK_BEAST_JAB_ART_MODE = 1;", expect)
        self.assertIn("art(sheet2 frame2 modespin)", self.compile("--dump").stdout)

    def test_attack_art_unknown_sheet_rejected(self):
        self.write_art_sheets(["fxpole"])
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0].__setitem__("art", {"sheet": "missing"}))
        self.assert_fails(self.compile(), "art: sheet: unknown art sheet 'missing'")

    def test_attack_art_requires_sheet_file(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0].__setitem__("art", {"sheet": "fxpole"}))
        self.assert_fails(self.compile(), "missing art sheet file (attack art.sheet resolves against it)")

    def test_attack_art_unknown_key_rejected(self):
        self.write_art_sheets(["fxpole"])
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["attacks"][0].__setitem__("art", {"sheet": "fxpole", "bogus": 1}))
        self.assert_fails(self.compile(), "art: unknown key 'bogus'")

    # ------------------------------------------------------- zone part (bih.5)

    def test_zone_part_emit_and_dump(self):
        # A zone's optional `part` names a data/art_sheets.json entry and packs
        # as its 1-based index in the 14th zone byte; the record grows by one.
        self.write_art_sheets(["fxpole", "fxtail"])
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["zones"]["appendage"].__setitem__("part", "fxtail"))
        self.assert_succeeds(self.compile())
        meta = self.meta_constants()
        self.assertEqual(meta["ZONE_SIZE"], 14, "zone grew by the part sheet byte")
        self.assertEqual(self.blob()[meta["ZONE_BEAST_APPENDAGE_OFF"] + 13], 2, "zone part sheet index (1-based)")
        self.assertEqual(self.blob()[meta["ZONE_BEAST_HEAD_OFF"] + 13], 0, "unauthored zone part is 0")
        self.assertIn("partSheet 2", self.compile("--dump").stdout)

    def test_zone_part_unknown_sheet_rejected(self):
        self.write_art_sheets(["fxpole"])
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["zones"]["head"].__setitem__("part", "missing"))
        self.assert_fails(self.compile(), "part: unknown art sheet 'missing'")

    def test_zone_part_requires_sheet_file(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc["zones"]["head"].__setitem__("part", "fxpole"))
        self.assert_fails(self.compile(), "missing art sheet file (zone part resolves against it)")

    def test_static_omitted_collections_rejected_for_dynamic(self):
        # A dynamic creature still needs non-empty attacks/patterns.
        self.mutate("data/creatures/beast.json", lambda doc: doc.__setitem__("attacks", []))
        self.assert_fails(self.compile(), "attacks: expected a non-empty array")

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
        for record in ("CREATURE", "PROFILE", "SKELETON", "ZONE", "ANCHOR",
                       "ATTACK", "WINDOW", "PATTERN", "GUARD", "STEP", "ART"):
            self.assertEqual(expect["%s_SIZE" % record], meta["%s_SIZE" % record])
        self.assertEqual(expect["BLOB_SIZE"], len(blob))
        self.assertEqual(expect["CREATURE_SIZE"], 40)
        self.assertEqual(expect["CREATURE_CORE_SIZE"], 28)
        self.assertEqual(expect["CARVE_SIZE"], 3)
        self.assertEqual(expect["CARVE_SLOTS"], 4)
        self.assertEqual(expect["CREATURE_BEAST_HP"], 80)
        self.assertNotIn("CREATURE_BEAST_ENRAGE_HP_PCT", expect)
        self.assertEqual(expect["CREATURE_BEAST_SPD"], 4)
        self.assertEqual(expect["CREATURE_BEAST_ATTACKS"], 1)
        self.assertEqual(expect["CREATURE_BEAST_PATTERNS"], 1)
        self.assertEqual(expect["PROFILE_BEAST_TURN_RATE"], 0)
        self.assertEqual(expect["PROFILE_BEAST_REST_AFTER"], 0)
        self.assertEqual(expect["PROFILE_BEAST_REST_T"], 0)
        self.assertEqual(expect["ATTACK_BEAST_JAB_WINDUP"], 20)
        self.assertEqual(expect["ATTACK_BEAST_JAB_DMG"], 7)
        self.assertEqual(expect["ATTACK_BEAST_JAB_WALLSTUN"], 0)
        self.assertEqual(expect["ATTACK_BEAST_JAB_TELL"], 0)
        self.assertEqual(expect["PATTERN_BEAST_P_JAB_MAX_DIST"], 36)
        self.assertEqual(expect["ZONE_BEAST_HEAD_DMG_MUL"], 120)
        self.assertEqual(expect["ZONE_BEAST_APPENDAGE_HP"], 30)
        match = re.search(r"BLOB_SHA256\[32\] = \{(.*?)\};", text, re.S)
        digest = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-F]{2})", match.group(1)))
        self.assertEqual(digest, hashlib.sha256(blob).digest())

    def test_data_header_has_host_structs_and_arrays(self):
        self.assert_succeeds(self.compile())
        text = self.read(DATA_REL)
        for needle in ("struct Creature {", "struct Attack {", "struct Guard {", "struct Window {",
                       "struct Zone {", "uint8_t partSheet;", "struct Carve {", "struct Art {", "carve[4]", "std::array<Creature, 1> CREATURES", "std::array<Attack, 1> ATTACKS",
                       "std::array<Guard, 1> GUARDS", "std::array<Step, 2> STEPS", "std::array<Art, 1> ART"):
            self.assertIn(needle, text)
        self.assertIn("constexpr uint8_t CREATURE_BEAST = 0;", text)
        self.assertIn("constexpr uint8_t ATTACK_BEAST_JAB = 0;", text)
        self.assertIn("constexpr uint8_t ZONE_BEAST_APPENDAGE = 1;", text)

    # ------------------------------------------------------------- carve (prg.3)

    def test_carve_default_empty_and_fact(self):
        # A creature with no carve key packs the fixed tail as empty slots and
        # leaves HAS_CARVE false (no crate machinery opted in).
        self.assert_succeeds(self.compile())
        meta = self.meta_constants()
        self.assertEqual(meta["CARVE_SIZE"], 3, "carve slot is item/count/chance")
        self.assertEqual(meta["CARVE_SLOTS"], 4, "four fixed slots")
        self.assertEqual(meta["CREATURE_CARVE_OFF"], 28, "carve tail follows the creature core")
        self.assertEqual(self.blob()[meta["CREATURE_BEAST_OFF"] + 28:meta["CREATURE_BEAST_OFF"] + 40], bytes([0] * 12), "empty tail")
        self.assertIn("constexpr bool HAS_CARVE = false;", self.read(META_REL))
        self.assertIn("constexpr uint8_t CREATURE_BEAST_CARVES = 0;", self.read(EXPECT_REL))
        self.assertNotIn("CREATURE_BEAST_CARVE0_", self.read(EXPECT_REL))

    def test_carve_emit_validate_and_dump(self):
        # Item names resolve to data/items.json indices; count 1..3; chance
        # 0..100. The tail packs item/count/chance per authored slot, pads the
        # rest with count 0, the expect header pins each entry and the fact flips.
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc.__setitem__("carve", [
                        {"item": "scale", "count": 2, "chance": 75},
                        {"item": "herb", "count": 1, "chance": 100},
                    ]))
        self.assert_succeeds(self.compile())
        meta = self.meta_constants()
        o = meta["CREATURE_BEAST_OFF"] + meta["CREATURE_CARVE_OFF"]
        blob = self.blob()
        self.assertEqual(blob[o + 0:o + 6], bytes([1, 2, 75, 0, 1, 100]), "authored slots packed in order")
        self.assertEqual(blob[o + 6:o + 12], bytes([0] * 6), "unused slots padded")
        self.assertIn("constexpr bool HAS_CARVE = true;", self.read(META_REL))
        expect = self.read(EXPECT_REL)
        self.assertIn("constexpr uint8_t CREATURE_BEAST_CARVES = 2;", expect)
        self.assertIn("constexpr uint8_t CREATURE_BEAST_CARVE0_ITEM = 1;", expect)
        self.assertIn("constexpr uint8_t CREATURE_BEAST_CARVE0_COUNT = 2;", expect)
        self.assertIn("constexpr uint8_t CREATURE_BEAST_CARVE0_CHANCE = 75;", expect)
        self.assertIn("constexpr uint8_t CREATURE_BEAST_CARVE1_ITEM = 0;", expect)
        self.assertIn("carve: item1 x2 @75% item0 x1 @100%", self.compile("--dump").stdout)

    def test_carve_unknown_item_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc.__setitem__("carve", [{"item": "wing", "count": 1, "chance": 100}]))
        self.assert_fails(self.compile(), "carve[0]: item: unknown item id 'wing'")

    def test_carve_count_and_chance_range_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc.__setitem__("carve", [{"item": "herb", "count": 0, "chance": 100}]))
        self.assert_fails(self.compile(), "carve[0]: count: out of range 1..3: 0")
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc.__setitem__("carve", [{"item": "herb", "count": 4, "chance": 100}]))
        self.assert_fails(self.compile(), "carve[0]: count: out of range 1..3: 4")
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc.__setitem__("carve", [{"item": "herb", "count": 1, "chance": 101}]))
        self.assert_fails(self.compile(), "carve[0]: chance: out of range 0..100: 101")

    def test_carve_slot_cap_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc.__setitem__("carve", [{"item": "herb", "count": 1, "chance": 100}] * 5))
        self.assert_fails(self.compile(), "size limit: 5 carve entries exceed the 4 slot cap")

    def test_carve_missing_and_unknown_keys_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc.__setitem__("carve", [{"item": "herb", "count": 1}]))
        self.assert_fails(self.compile(), "carve[0]: missing key 'chance'")
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc.__setitem__("carve", [{"item": "herb", "count": 1, "chance": 100, "rate": 5}]))
        self.assert_fails(self.compile(), "carve[0]: unknown key 'rate'")

    def test_carve_duplicate_item_rejected(self):
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc.__setitem__("carve", [
                        {"item": "herb", "count": 1, "chance": 100},
                        {"item": "herb", "count": 2, "chance": 50},
                    ]))
        self.assert_fails(self.compile(), "carve[1]: duplicate item 'herb'")

    def test_carve_requires_item_file(self):
        # data/items.json resolves the item ids; a tree with carve but no item
        # file fails loudly instead of packing bogus indices.
        os.remove(self.path("data", "items.json"))
        self.mutate("data/creatures/beast.json",
                    lambda doc: doc.__setitem__("carve", [{"item": "herb", "count": 1, "chance": 100}]))
        self.assert_fails(self.compile(), "missing item file (carve item ids resolve against it)")


if __name__ == "__main__":
    unittest.main()
