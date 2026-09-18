#!/usr/bin/env python3
"""Unit tests for tools/contact_sheet.py (run: make test-tools).

Renders a tiny synthetic JSON tree from build/tests/contact_sheet/ (no /tmp)
and pins: the sheet is deterministic, the timeline band height tracks the
attack timing, and the PNG loader round-trips through main()."""
import importlib.util
import json
import os
import struct
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
SCRATCH = os.path.join(ROOT, "build", "tests", "contact_sheet")

sys.path.insert(0, TOOLS)

spec = importlib.util.spec_from_file_location("contact_sheet", os.path.join(TOOLS, "contact_sheet.py"))
contact_sheet = importlib.util.module_from_spec(spec)
spec.loader.exec_module(contact_sheet)

SKELETONS = {
    "version": 1,
    "skeletons": [{
        "id": "quad_32x24",
        "anchors": [{"id": "origin", "ox": 0, "oy": 0}],
    }],
}

CREATURE = {
    "id": "tester",
    "skeleton": "quad_32x24",
    "stats": {"w": 32, "h": 24, "hp": 100, "spd": 5, "spawnX": 200, "spawnY": 40},
    "profile": {
        "engageDist": 36, "keepDist": 24, "attackDist": 42, "circleNum": 8,
        "circleDen": 10, "retreatNum": 6, "retreatDen": 10, "cdBase": 50,
        "cdJitter": 30, "spawnT": 90, "spawnCd": 120, "stunRecoverT": 24,
    },
    "zones": {
        "head": {"box": {"ox": 10, "oy": 2, "w": 8, "h": 8}, "dmgMul": 120, "hp": 20,
                 "bodyShare": 100, "breakTypes": ["SLASH"], "hurtOn": True, "staggerOnHit": 5},
        "appendage": {"box": {"ox": -6, "oy": 4, "w": 10, "h": 6}, "dmgMul": 150, "hp": 30,
                      "bodyShare": 40, "breakTypes": ["SLASH"], "hurtOn": True, "staggerOnHit": 10},
    },
    "attacks": [{
        "id": "triple", "windup": 10, "active": 9, "recover": 20, "dmg": 9,
        "phys": "BLUNT", "elem": "NONE", "move": {"type": "none"},
        "facing": "track",
        "windows": [
            {"t0": 0, "t1": 2, "box": {"ox": 17, "oy": -6, "w": 24, "h": 10}, "dmgMul": 100},
            {"t0": 3, "t1": 5, "box": {"ox": 20, "oy": 0, "w": 26, "h": 22}, "dmgMul": 100},
            {"t0": 6, "t1": 8, "box": {"ox": 17, "oy": 6, "w": 24, "h": 10}, "dmgMul": 100},
        ],
        "onHit": {"effect": "none", "push": 0, "stun": 0},
    }],
    "patterns": [{"id": "p_triple", "guard": {}, "steps": [{"atk": "triple"}]}],
}


def write_tree():
    data = os.path.join(SCRATCH, "data")
    creatures = os.path.join(data, "creatures")
    os.makedirs(creatures, exist_ok=True)
    with open(os.path.join(data, "skeletons.json"), "w", encoding="utf-8") as handle:
        json.dump(SKELETONS, handle)
    with open(os.path.join(creatures, "tester.json"), "w", encoding="utf-8") as handle:
        json.dump(CREATURE, handle)
    return SCRATCH


def png_size(path):
    with open(path, "rb") as handle:
        head = handle.read(24)
    assert head[:8] == b"\x89PNG\r\n\x1a\n"
    return struct.unpack(">II", head[16:24])


class ContactSheetTests(unittest.TestCase):
    def setUp(self):
        self.root = write_tree()
        self.skeletons, self.creatures = contact_sheet.load_data(self.root)

    def test_render_is_deterministic(self):
        first = contact_sheet.render(self.creatures, self.skeletons)
        second = contact_sheet.render(self.creatures, self.skeletons)
        self.assertEqual(first.tobytes(), second.tobytes())

    def test_render_includes_one_band_per_attack(self):
        img = contact_sheet.render(self.creatures, self.skeletons)
        self.assertEqual(img.size[0], 480)
        self.assertEqual(img.size[1], 8 + contact_sheet.band_height(self.creatures[0]))

    def test_timeline_columns_track_attack_ticks(self):
        attack = self.creatures[0]["attacks"][0]
        self.assertEqual(contact_sheet.attack_ticks(attack), 39)
        img = contact_sheet.render(self.creatures, self.skeletons)
        px = img.load()
        # band layout: margin + creature header row + attack label row
        y = 8 + 10 + 10
        self.assertEqual(px[contact_sheet.MARGIN, y], contact_sheet.PHASE_COLORS["windup"])
        self.assertEqual(px[contact_sheet.MARGIN + 10 * contact_sheet.TICK_W, y],
                         contact_sheet.PHASE_COLORS["active"])

    def test_main_writes_png_under_build(self):
        out = os.path.join(SCRATCH, "sheet.png")
        rc = contact_sheet.main(["--root", self.root, "--out", out, "--creature", "tester"])
        self.assertEqual(rc, 0)
        self.assertTrue(os.path.isfile(out))
        self.assertEqual(png_size(out), (480, contact_sheet.render(self.creatures, self.skeletons).size[1]))

    def test_main_rejects_unknown_creature(self):
        with self.assertRaises(SystemExit):
            contact_sheet.main(["--root", self.root, "--out", os.path.join(SCRATCH, "x.png"),
                                "--creature", "nope"])


if __name__ == "__main__":
    unittest.main()
