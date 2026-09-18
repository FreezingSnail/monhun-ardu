#!/usr/bin/env python3
"""Unit tests for tools/gen-equipment.py (run: make test-tools).

The clean fixture under fixtures/gen_equipment/clean is a minimal equipment
tree (one item per slot/order). Every failure case copies it to
build/tests/gen_equipment/ and mutates the copy, so the tests stay read-only on
the repository fixtures and never write into /tmp.
"""
import json
import os
import shutil
import struct
import subprocess
import sys
import unittest

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
TOOL = os.path.join(TOOLS, "gen-equipment.py")
FIXTURE = os.path.join(HERE, "fixtures", "gen_equipment", "clean")
SCRATCH = os.path.join(ROOT, "build", "tests", "gen_equipment")

BLOB_REL = "fxdata/tables/equip.bin"
META_REL = "src/generated/equip_meta.hpp"
IMAGES_REL = "images/equip"

# docs/art/ canonical template -> generated placeholder sheet.
BASE_SHEETS = (
    ("mh_player_base_16x16.png", "player_base_16x16.png"),
)
ALL_SHEETS = ("mh_player_base_16x16.png", "mh_weapon_flail_32x32.png",
              "mh_weapon_gun_32x32.png", "mh_weapon_sword_32x32.png")


def run_tool(*args):
    return subprocess.run([sys.executable, TOOL, *args], capture_output=True, text=True)


class GenEquipmentTests(unittest.TestCase):
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

    # ---------------------------------------------------------------- clean

    def test_clean_compile_is_deterministic(self):
        self.assert_succeeds(self.compile())
        for rel in (BLOB_REL, META_REL):
            self.assertTrue(os.path.isfile(self.path(rel)), rel)
        images = list(ALL_SHEETS)
        first = {name: self.read_bytes(IMAGES_REL, name) for name in images}
        second = self.compile()
        self.assert_succeeds(second)
        self.assertIn("%s (unchanged)" % BLOB_REL, second.stdout)
        self.assertIn("%s (unchanged)" % META_REL, second.stdout)
        for name in images:
            self.assertIn("%s/%s (" % (IMAGES_REL, name), second.stdout)
            self.assertEqual(first[name], self.read_bytes(IMAGES_REL, name), name)

    def test_clean_meta_header_constants(self):
        self.assert_succeeds(self.compile())
        text = self.read(META_REL)
        for needle in (
            "constexpr uint16_t MAGIC = 0x4551;",
            "constexpr uint8_t VERSION = 1;",
            "constexpr uint8_t HEADER_SIZE = 8;",
            "constexpr uint8_t ITEM_SIZE = 19;",
            "constexpr uint8_t FACINGS = 8;",
            "constexpr uint8_t SLOT_COUNT = 6;",
            "constexpr uint8_t POSE_COUNT = 12;",
            "constexpr uint8_t ITEM_COUNT = 4;",
            "constexpr uint8_t ITEM_PLAYER_BASE = 0;",
            "constexpr uint8_t ITEM_WEAPON_SWORD = 3;",
            'constexpr const char *SHEET_PLAYER_BASE = "mh_player_base";',
            "constexpr uint8_t FRAME_WEAPON_SWORD[POSE_COUNT][FACINGS] = {",
        ):
            self.assertIn(needle, text)
        self.assertIn("constexpr uint16_t SIZE = %d;" % (8 + 19 * 4), text)

    def test_clean_blob_matches_record_layout(self):
        self.assert_succeeds(self.compile())
        blob = self.read_bytes(BLOB_REL)
        self.assertEqual(len(blob), 8 + 19 * 4)
        magic, version, flags, count, reserved = struct.unpack_from("<HBBHH", blob, 0)
        self.assertEqual((magic, version, flags, count, reserved), (0x4551, 1, 0, 4, 0))
        records = {}
        for i in range(count):
            off = 8 + i * 19
            slot, order, frames, cw, ch, ax, ay = struct.unpack_from("<BBBBBbb", blob, off)
            rows = list(blob[off + 7:off + 19])
            records[i] = {"slot": slot, "order": order, "frames": frames, "cell": (cw, ch),
                          "anchor": (ax, ay), "rows": rows}
        base = records[0]
        self.assertEqual((base["slot"], base["order"], base["frames"]), (0, 0, 8))
        self.assertEqual((base["cell"], base["anchor"]), ((16, 16), (8, 8)))
        self.assertEqual(base["rows"], [0] * 12)
        sword = records[3]
        self.assertEqual((sword["slot"], sword["order"], sword["frames"]), (4, 1, 24))
        self.assertEqual((sword["cell"], sword["anchor"]), ((32, 32), (16, 16)))
        self.assertEqual(sword["rows"], [0, 0, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0])

    def test_generated_base_sheets_match_docs_art(self):
        self.assert_succeeds(self.compile())
        for generated, reference in BASE_SHEETS:
            img = Image.open(self.path(IMAGES_REL, generated)).convert("RGBA")
            ref = Image.open(os.path.join(ROOT, "docs", "art", reference)).convert("RGBA")
            self.assertEqual(img.tobytes(), ref.tobytes(), generated)
            # Helmet eye slot: visible for the 5 toward-viewer facings (0..4,
            # pairwise distinct), hidden for the 3 away facings (5..7, no black
            # pixel; their cells are the plain helmet back).
            cells = [img.crop((i * 16, 0, i * 16 + 16, 16)) for i in range(8)]
            for i in range(5):
                self.assertIn((0, 0, 0, 255), list(cells[i].getdata()),
                              "cell %d has no eye slot" % i)
            for i in range(5, 8):
                self.assertNotIn((0, 0, 0, 255), list(cells[i].getdata()),
                                 "cell %d must face away (no slot)" % i)
            for i in range(5):
                for j in range(i + 1, 5):
                    self.assertNotEqual(cells[i].tobytes(), cells[j].tobytes(),
                                        "cells %d and %d are identical" % (i, j))

    def test_generated_sheet_dimensions(self):
        self.assert_succeeds(self.compile())
        expected = {
            "mh_player_base_16x16.png": (128, 16),
            "mh_weapon_sword_32x32.png": (256, 96),
            "mh_weapon_flail_32x32.png": (256, 96),
            "mh_weapon_gun_32x32.png": (256, 96),
        }
        for name, size in expected.items():
            with Image.open(self.path(IMAGES_REL, name)) as img:
                self.assertEqual(img.size, size, name)

    def test_dump_mode_lists_catalog_and_writes_nothing(self):
        result = self.compile("--dump")
        self.assert_succeeds(result)
        self.assertIn("item player_base: slot player sheet mh_player_base cell 16x16 anchor 8,8 order facing frames 8 rows 1",
                      result.stdout)
        self.assertIn("item weapon_sword: slot weapon sheet mh_weapon_sword cell 32x32 anchor 16,16", result.stdout)
        self.assertIn("pose rows: idle=0, attack_startup=0, attack_active=1, attack_recover=2", result.stdout)
        self.assertIn("gen-equipment: 4 items, 84 B blob", result.stdout)
        self.assertFalse(os.path.exists(self.path(BLOB_REL)))
        self.assertFalse(os.path.exists(self.path(META_REL)))
        self.assertFalse(os.path.exists(self.path(IMAGES_REL)))

    # -------------------------------------------------------- gen-art refs

    def write_text(self, rel_path, text):
        path = self.path(rel_path)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)

    GEN_ART_JSON = {
        "id": "sword_slash",
        "slot": "weapon",
        "source": "gen-art",
        "sheet": "fxslash",
        "cell": [32, 32],
        "anchor": [16, 16],
        "order": "pose",
        "frames": 5,
        "poseMap": {"idle": 0},
        "variants": [0, 0, 1, 2, 3, 4],
        "flags": [],
    }

    def add_gen_art(self, symbol="fxslash"):
        self.write_text("fxdata/fxdata.h",
                        "using uint24_t = __uint24;\n"
                        "constexpr uint24_t %s = 0x000123;\n" % symbol)
        self.write_text("data/equipment/sword_slash.json",
                        json.dumps(self.GEN_ART_JSON, indent=2) + "\n")

    def test_gen_art_record_needs_no_png_and_emits_part_view(self):
        self.add_gen_art()
        result = self.compile()
        self.assert_succeeds(result)
        self.assertIn("fxslash (gen-art sword_slash, no PNG)", result.stdout)
        # The referenced sprite is not authored into images/equip.
        self.assertFalse(os.path.exists(self.path(IMAGES_REL, "fxslash_32x32.png")))
        text = self.read(META_REL)
        for needle in (
            "constexpr uint8_t PART_SWORD_SLASH =",
            "constexpr uint16_t PARTS_OFF = 103;",
            "constexpr uint8_t PART_SIZE = 17;",
            "constexpr uint8_t PART_SHEET_OFF = 0;",
            "constexpr uint8_t PART_ANCHOR_X_OFF = 3;",
            "constexpr uint8_t PART_ANCHOR_Y_OFF = 4;",
            "constexpr uint8_t PART_FRAME_OFF = 5;",
            "constexpr uint16_t PART_VARIANT_OFFSETS_OFF = 120;",
            "constexpr uint16_t PART_VARIANT_DATA_OFF = 124;",
            "constexpr uint8_t PART_VARIANT_COUNT = 6;",
        ):
            self.assertIn(needle, text)
        self.assertNotIn("PART_FLAT_OFF", text)
        # The catalog blob carries 4 authored + 1 gen-art item, then the part
        # record, the u16 variant index table and the variant bytes.
        blob = self.read_bytes(BLOB_REL)
        self.assertEqual(len(blob), 8 + 19 * 5 + 17 + 2 * 2 + 6)
        sheet, ax, ay = struct.unpack_from("<3sbb", blob, 103)
        self.assertEqual(sheet, b"\x23\x01\x00")   # fxslash = 0x000123
        self.assertEqual((ax, ay), (16, 16))
        self.assertEqual(list(blob[108:120]), [0] * 12)
        self.assertEqual(list(struct.unpack_from("<2H", blob, 120)), [0, 6])
        self.assertEqual(list(blob[124:130]), [0, 0, 1, 2, 3, 4])

    def test_gen_art_unknown_sheet_rejected(self):
        self.add_gen_art(symbol="fxother")
        self.assert_fails(self.compile(), "sheet: 'fxslash' is not declared in fxdata/fxdata.h")

    def test_gen_art_sheet_offset_rebakes_from_fxdata(self):
        self.add_gen_art()
        self.assert_succeeds(self.compile())
        text = self.read(META_REL)
        # Baked offset (fxslash 0x000123) and its AVR stale-blob guard.
        self.assertIn("constexpr uint16_t SHEET_OFF_FXSLASH = 291;", text)
        self.assertIn(
            'static_assert(SHEET_OFF_FXSLASH == static_cast<uint16_t>(fxslash), "equip blob stale: re-run make gen");',
            text)
        # A shifted fxdata.h is picked up on the next run (the two-pass note):
        # new offset lands in the blob record and the generated constant.
        self.write_text("fxdata/fxdata.h",
                        "using uint24_t = __uint24;\nconstexpr uint24_t fxslash = 0x000456;\n")
        self.assert_succeeds(self.compile())
        self.assertEqual(self.read_bytes(BLOB_REL)[103:106], b"\x56\x04\x00")
        self.assertIn("constexpr uint16_t SHEET_OFF_FXSLASH = 1110;", self.read(META_REL))

    def test_gen_art_flat_key_rejected(self):
        # The flat workaround is retired (partDraw always applies the per-plane
        # frame stride), so the schema no longer accepts the key.
        self.add_gen_art()
        self.mutate("data/equipment/sword_slash.json", lambda doc: doc.__setitem__("flat", True))
        self.assert_fails(self.compile(), "unknown key 'flat'")

    # ------------------------------------------------------- schema errors

    def test_unknown_key_rejected(self):
        self.mutate("data/equipment/player_base.json", lambda doc: doc.__setitem__("rarity", 1))
        self.assert_fails(self.compile(), "unknown key 'rarity'")

    def test_missing_key_rejected(self):
        self.mutate("data/equipment/player_base.json", lambda doc: doc.pop("poseMap"))
        self.assert_fails(self.compile(), "missing key 'poseMap'")

    def test_id_filename_mismatch_rejected(self):
        self.mutate("data/equipment/player_base.json", lambda doc: doc.__setitem__("id", "head_helm"))
        self.assert_fails(self.compile(), "does not match file name")

    def test_unknown_slot_rejected(self):
        self.mutate("data/equipment/player_base.json", lambda doc: doc.__setitem__("slot", "wing"))
        self.assert_fails(self.compile(), "slot: unknown value 'wing'")

    def test_sheet_must_match_id(self):
        self.mutate("data/equipment/player_base.json", lambda doc: doc.__setitem__("sheet", "mh_player_hero"))
        self.assert_fails(self.compile(), "sheet: expected 'mh_player_base'")

    def test_cell_height_multiple_of_8(self):
        self.mutate("data/equipment/weapon_sword.json", lambda doc: doc.__setitem__("cell", [32, 20]))
        self.assert_fails(self.compile(), "height 20 must be a multiple of 8")

    def test_cell_must_match_slot(self):
        self.mutate("data/equipment/player_base.json", lambda doc: doc.__setitem__("cell", [32, 32]))
        self.assert_fails(self.compile(), "player cells are 16x16")

    def test_anchor_must_match_slot(self):
        self.mutate("data/equipment/player_base.json", lambda doc: doc.__setitem__("anchor", [0, 0]))
        self.assert_fails(self.compile(), "player anchor is 8,8")

    def test_facing_frames_limit(self):
        self.mutate("data/equipment/player_base.json", lambda doc: doc.__setitem__("frames", 5))
        self.assert_fails(self.compile(), "frames must be 1 or 8")

    def test_facing_pose_frames_multiple_of_8(self):
        self.mutate("data/equipment/weapon_sword.json", lambda doc: doc.__setitem__("frames", 20))
        self.assert_fails(self.compile(), "frames must be a multiple of 8")

    def test_unknown_pose_rejected(self):
        self.mutate("data/equipment/weapon_sword.json", lambda doc: doc["poseMap"].__setitem__("fly", 0))
        self.assert_fails(self.compile(), "unknown pose 'fly'")

    def test_pose_row_out_of_range(self):
        self.mutate("data/equipment/weapon_sword.json", lambda doc: doc["poseMap"].__setitem__("attack_recover", 3))
        self.assert_fails(self.compile(), "poseMap.attack_recover: out of range 0..2: 3")

    def test_missing_idle_rejected(self):
        self.mutate("data/equipment/weapon_sword.json", lambda doc: doc["poseMap"].pop("idle"))
        self.assert_fails(self.compile(), "missing 'idle'")

    def test_missing_equipment_dir_rejected(self):
        shutil.rmtree(self.path("data", "equipment"))
        self.assert_fails(self.compile(), "missing equipment directory")


if __name__ == "__main__":
    unittest.main()
