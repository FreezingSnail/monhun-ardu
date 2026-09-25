#!/usr/bin/env python3
"""Unit tests for tools/gen-equipment.py (run: make test-tools).

The clean fixture under fixtures/gen_equipment/clean is a minimal equipment
tree (one item per slot/order). Every failure case copies it to
build/tests/gen_equipment/ and mutates the copy, so the tests stay read-only on
the repository fixtures and never write into /tmp.
"""
import json
import importlib.util
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
            "constexpr uint8_t PART_SIZE = 19;",
            "constexpr uint8_t PART_SHEET_OFF = 0;",
            "constexpr uint8_t PART_ANCHOR_X_OFF = 3;",
            "constexpr uint8_t PART_ANCHOR_Y_OFF = 4;",
            "constexpr uint8_t PART_ORDER_OFF = 5;",
            "constexpr uint8_t PART_FRAMES_OFF = 6;",
            "constexpr uint8_t PART_FRAME_OFF = 7;",
            "constexpr uint16_t PART_VARIANT_OFFSETS_OFF = 122;",
            "constexpr uint16_t PART_VARIANT_DATA_OFF = 126;",
            "constexpr uint8_t PART_VARIANT_COUNT = 6;",
        ):
            self.assertIn(needle, text)
        self.assertNotIn("PART_FLAT_OFF", text)
        # The catalog blob carries 4 authored + 1 gen-art item, then the part
        # record (sheet/anchor/order/frames/frame), the u16 variant index table
        # and the variant bytes.
        blob = self.read_bytes(BLOB_REL)
        self.assertEqual(len(blob), 8 + 19 * 5 + 19 + 2 * 2 + 6)
        sheet, ax, ay = struct.unpack_from("<3sbb", blob, 103)
        self.assertEqual(sheet, b"\x23\x01\x00")   # fxslash = 0x000123
        self.assertEqual((ax, ay), (16, 16))
        order, frames = struct.unpack_from("<BB", blob, 108)
        self.assertEqual((order, frames), (2, 5))   # ORDER_POSE, fxslash frames
        self.assertEqual(list(blob[110:122]), [0] * 12)
        self.assertEqual(list(struct.unpack_from("<2H", blob, 122)), [0, 6])
        self.assertEqual(list(blob[126:132]), [0, 0, 1, 2, 3, 4])

    def test_gen_art_unknown_sheet_rejected(self):
        self.add_gen_art(symbol="fxother")
        self.assert_fails(self.compile(), "sheet: 'fxslash' is not declared in fxdata/fxdata.h")

    def test_gen_art_sheet_offset_rebakes_from_fxdata(self):
        self.add_gen_art()
        self.assert_succeeds(self.compile())
        text = self.read(META_REL)
        # Baked offset (fxslash 0x000123) and its AVR stale-blob guard.
        self.assertIn("constexpr uint32_t SHEET_OFF_FXSLASH = 291;", text)
        self.assertIn(
            'static_assert(SHEET_OFF_FXSLASH == static_cast<uint32_t>(fxslash), "equip blob stale: re-run make gen");',
            text)
        # A shifted fxdata.h is picked up on the next run (the two-pass note):
        # new offset lands in the blob record and the generated constant.
        self.write_text("fxdata/fxdata.h",
                        "using uint24_t = __uint24;\nconstexpr uint24_t fxslash = 0x000456;\n")
        self.assert_succeeds(self.compile())
        self.assertEqual(self.read_bytes(BLOB_REL)[103:106], b"\x56\x04\x00")
        self.assertIn("constexpr uint32_t SHEET_OFF_FXSLASH = 1110;", self.read(META_REL))

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

    # ------------------------------------------------------- mirror weapons

    ATK_ENUM = """enum AtkId : int8_t {
    ATK_NONE = 0,
    ATK_STEPSLASH,
    ATK_SPINCUT,
    ATK_TRIP,
    ATK_POINTBLANK,
    ATK_GUARDBASH,
    ATK_ALT,
    ATK_ROLL,
    ATK_CHARGE,
    ATK_BRANCH2
};
"""

    @staticmethod
    def move(reach, hw, hh, mid=0, active=5, lunge=0):
        return {"startup": 3, "active": active, "recover": 8, "dmg": 5, "reach": reach,
                "hw": hw, "hh": hh, "stam": 5, "lunge": lunge, "push": 0,
                "effect": 0, "shell": 0, "id": mid}

    def install_mirror_sword(self):
        """Turn the fixture's weapon_sword into a mirror-authored sheet plus the
        fxdump/game.hpp inputs the weapon art derives its poses from."""
        def patch(doc):
            doc["source"] = "mirror"
            doc["frames"] = 216
            doc["poseMap"] = {"idle": 0, "attack_startup": 2, "attack_active": 3,
                              "attack_recover": 1, "parry": 22, "whirl": 22, "guard": 22,
                              "shove": 24, "dodge": 23, "deflect": 24, "stun": 25, "dead": 0}
        self.mutate("data/equipment/weapon_sword.json", patch)
        sword = {
            "name": "sword",
            "attacks": [self.move(13, 12, 10), self.move(13, 12, 10), self.move(16, 18, 14)],
            "special": self.move(18, 20, 16),
            "branches": [self.move(18, 14, 12, mid=1),
                         self.move(12, 28, 26, mid=2, active=7),
                         self.move(16, 20, 22, mid=9, active=4)],
            "alt": self.move(22, 10, 10, mid=6, lunge=20),
            "roll": self.move(15, 16, 14, mid=7),
            "charge": [self.move(0, 0, 0), self.move(0, 0, 0)],
        }
        zero = {"name": "x", "attacks": [self.move(0, 0, 0)] * 3,
                "special": self.move(0, 0, 0), "branches": [self.move(0, 0, 0)] * 3,
                "alt": self.move(0, 0, 0), "roll": self.move(0, 0, 0),
                "charge": [self.move(0, 0, 0)] * 2}
        os.makedirs(self.path("build"), exist_ok=True)
        with open(self.path("build", "fxdump.json"), "w", encoding="utf-8", newline="\n") as handle:
            json.dump({"weapons": [sword, zero, zero]}, handle)
        self.write_text("src/core/game.hpp", self.ATK_ENUM)

    def test_mirror_source_authors_sheet_and_layout_plan(self):
        self.install_mirror_sword()
        self.assert_succeeds(self.compile())
        with Image.open(self.path(IMAGES_REL, "mh_weapon_sword_32x32.png")) as img:
            self.assertEqual(img.size, (5 * 32, 27 * 32))
        layout = json.loads(self.read(IMAGES_REL, "layout.json"))
        plan = layout["sheets"]["mh_weapon_sword"]
        self.assertEqual(len(plan), 216)
        row_plan = ((0, False), (1, False), (2, False), (1, True), (0, True), (4, True), (3, False), (4, False))
        for row in range(27):
            for packed, (src, mirror) in enumerate(row_plan):
                self.assertEqual(plan[row * 8 + packed], [row * 5 + src, mirror],
                                 "row %d packed %d" % (row, packed))
        # Authored weapon sheets carry a cart part record (+19 B record and one
        # more 2-byte variant offset): 8 + 19*4 + 24 = 108 B.
        blob = self.read_bytes(BLOB_REL)
        self.assertEqual(len(blob), 108)
        text = self.read(META_REL)
        self.assertIn("constexpr uint8_t PART_WEAPON_SWORD", text)

    def test_clean_tree_has_no_layout_json(self):
        self.assert_succeeds(self.compile())
        self.assertFalse(os.path.exists(self.path(IMAGES_REL, "layout.json")))

    def test_mirror_source_rejects_wrong_row_table(self):
        self.install_mirror_sword()
        self.mutate("data/equipment/weapon_sword.json", lambda doc: doc.__setitem__("frames", 24))
        self.assert_fails(self.compile(), "frames must be 216")

    def test_mirror_source_rejects_unknown_weapon(self):
        self.install_mirror_sword()
        os.rename(self.path("data", "equipment", "weapon_sword.json"),
                  self.path("data", "equipment", "weapon_axe.json"))
        self.mutate("data/equipment/weapon_axe.json",
                    lambda doc: doc.update({"id": "weapon_axe", "sheet": "mh_weapon_axe"}))
        self.assert_fails(self.compile(), "no weapon art spec")

    def load_art_module(self):
        spec = importlib.util.spec_from_file_location("gen_equipment", TOOL)
        ge = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(ge)
        return ge

    def test_mirror_art_inks_the_active_boxes(self):
        # The art derives each move from the fixture's move records: every
        # active row must ink its hit box and the box front half
        # (docs/weapon-art.md). Startup rows keep the blade on the hunter's side
        # of the box (no ink in the box's front half).
        self.install_mirror_sword()
        ge = self.load_art_module()
        ge.ATK_IDS = ge.load_atk_ids(self.case)
        ge.WEAPON_MOVES = ge.load_weapon_moves(self.case)
        for slot, record in enumerate(ge.WEAPON_MOVES[0]):
            if record is None or not record["hw"]:
                continue
            hw, hh = record["hw"], record["hh"]
            box = [(x, y) for y in range(16 - hh // 2, 16 + (hh + 1) // 2)
                   for x in range(16 - hw // 2, 16 + (hw + 1) // 2)]
            for row, what in ((ge.WEAPON_ROW_MOVE0 + 2 * slot, "startup"),
                              (ge.WEAPON_ROW_MOVE0 + 2 * slot + 1, "active")):
                img = ge.sword_cell(row, 0)
                self.assertIsNotNone(img, "slot %d %s: no art" % (slot, what))
                px = img.load()
                inside = [(x, y) for (x, y) in box if px[x, y][3] > 0]
                if what == "active":
                    self.assertTrue(inside, "slot %d active: no ink in the hit box" % slot)
                    self.assertTrue(any(x > 16 for x, _ in inside),
                                    "slot %d active: no ink in the box front half" % slot)
                    self.assertTrue(any(px[x, y] == (255, 255, 255, 255) for (x, y) in inside),
                                    "slot %d active: no blade core in the hit box" % slot)
                else:
                    self.assertTrue(any(px[x, y][3] > 0 for (x, y) in box),
                                    "slot %d startup: no ink near the box" % slot)
                    self.assertNotEqual(img.tobytes(), ge.sword_cell(row + 1, 0).tobytes(),
                                        "slot %d: startup equals active" % slot)


LAYERED_FIXTURE = os.path.join(HERE, "fixtures", "gen_equipment", "layered")


class GenEquipmentLayeredTests(unittest.TestCase):
    """Authored shadow/body/head layers + the default draw set (bead ikp)."""

    maxDiff = None

    def setUp(self):
        case = os.path.join(SCRATCH, self._testMethodName)
        shutil.rmtree(case, ignore_errors=True)
        shutil.copytree(LAYERED_FIXTURE, case)
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

    def image(self, name):
        return Image.open(self.path(IMAGES_REL, name)).convert("RGBA")

    def test_layered_default_set_ids(self):
        result = self.compile()
        self.assert_succeeds(result)
        text = self.read(META_REL)
        for needle in (
            "constexpr uint8_t DEFAULT_SHADOW = PART_SHADOW_BASE;",
            "constexpr uint8_t DEFAULT_BODY = PART_BODY_BASE;",
            "constexpr uint8_t DEFAULT_HEAD = PART_HEAD_BASE;",
            "constexpr uint8_t ITEM_SHADOW_BASE =",
            "constexpr uint8_t ITEM_BODY_BASE =",
            "constexpr uint8_t ITEM_HEAD_BASE =",
            "constexpr uint8_t ITEM_HEAD_HELM =",
            "constexpr uint8_t ITEM_HEAD_BANDANA =",
        ):
            self.assertIn(needle, text)
        # No fxdata.h in the fixture: authored part offsets bake 0 until the
        # second `make gen` (the AVR static_assert forces the re-bake).
        for sheet in ("MH_SHADOW_BASE", "MH_BODY_BASE", "MH_HEAD_BASE",
                      "MH_HEAD_HELM", "MH_HEAD_BANDANA"):
            self.assertIn("constexpr uint32_t SHEET_OFF_%s = 0;" % sheet, text)
            self.assertIn('static_assert(SHEET_OFF_%s == static_cast<uint32_t>(%s)'
                          % (sheet, sheet.lower()), text)

    def test_layered_part_records_layout(self):
        self.assert_succeeds(self.compile())
        text = self.read(META_REL)
        # Parts sorted by id: body_base, head_bandana, head_base, head_helm,
        # shadow_base (the layered slots always emit a part record).
        for needle in (
            "constexpr uint8_t PART_COUNT = 5;",
            "constexpr uint8_t PART_BODY_BASE = 0;",
            "constexpr uint8_t PART_HEAD_BANDANA = 1;",
            "constexpr uint8_t PART_HEAD_BASE = 2;",
            "constexpr uint8_t PART_HEAD_HELM = 3;",
            "constexpr uint8_t PART_SHADOW_BASE = 4;",
            "constexpr uint16_t PARTS_OFF = 103;",   # 8 + 5 items * 19
            "constexpr uint8_t PART_SIZE = 19;",
        ):
            self.assertIn(needle, text)
        blob = self.read_bytes(BLOB_REL)
        # header + 5 item records + 5 part records + 6 u16 variant index
        # entries + the single placeholder variant byte.
        self.assertEqual(len(blob), 8 + 19 * 5 + 19 * 5 + 2 * 6 + 1)
        # body_base: ORDER_FACING_POSE(1), 16 frames, idle row 0 / dodge row 1.
        order, frames = struct.unpack_from("<BB", blob, 103 + 5)
        self.assertEqual((order, frames), (1, 16))
        rows = list(blob[103 + 7:103 + 19])
        self.assertEqual(rows[0], 0)
        self.assertEqual(rows[8], 1)   # POSE_DODGE
        # head_base (part 2): ORDER_FACING(0), 8 frames.
        order, frames = struct.unpack_from("<BB", blob, 103 + 2 * 19 + 5)
        self.assertEqual((order, frames), (0, 8))
        # shadow_base (part 4): ORDER_FACING(0), 1 frame.
        order, frames = struct.unpack_from("<BB", blob, 103 + 4 * 19 + 5)
        self.assertEqual((order, frames), (0, 1))

    def test_layered_sheet_eye_slot_and_distinctness(self):
        self.assert_succeeds(self.compile())
        heads = {}
        for item in ("head_base", "head_helm", "head_bandana"):
            img = self.image("mh_%s_16x16.png" % item)
            self.assertEqual(img.size, (128, 16), item)
            cells = [img.crop((f * 16, 0, f * 16 + 16, 16)) for f in range(8)]
            for f in range(5):
                self.assertIn((0, 0, 0, 255), list(cells[f].getdata()),
                              "%s cell %d has no eye slot" % (item, f))
                for g in range(f + 1, 5):
                    self.assertNotEqual(cells[f].tobytes(), cells[g].tobytes(),
                                        "%s cells %d/%d identical" % (item, f, g))
            for f in range(5, 8):
                self.assertNotIn((0, 0, 0, 255), list(cells[f].getdata()),
                                 "%s cell %d must face away (no slot)" % (item, f))
            heads[item] = cells

        body = self.image("mh_body_base_16x16.png")
        self.assertEqual(body.size, (128, 32))
        for f in range(8):
            idle = list(body.crop((f * 16, 0, f * 16 + 16, 16)).getdata())
            dodge = list(body.crop((f * 16, 16, f * 16 + 16, 32)).getdata())
            self.assertIn((255, 255, 255, 255), idle, "body idle facing %d" % f)
            self.assertNotIn((170, 170, 170, 255), idle, "body idle facing %d" % f)
            self.assertIn((170, 170, 170, 255), dodge, "body dodge facing %d" % f)
            self.assertNotIn((255, 255, 255, 255), dodge, "body dodge facing %d" % f)

        shadow = self.image("mh_shadow_base_16x16.png")
        self.assertEqual(shadow.size, (16, 16))
        for f in range(8):
            cells = [
                shadow.crop((0, 0, 16, 16)).tobytes(),
                body.crop((f * 16, 0, f * 16 + 16, 16)).tobytes(),
                heads["head_base"][f].tobytes(),
                heads["head_helm"][f].tobytes(),
                heads["head_bandana"][f].tobytes(),
            ]
            self.assertEqual(len(set(cells)), 5, "facing %d cells not pairwise distinct" % f)

    def test_layered_body_bakes_shadow_row(self):
        # Bead monhun-ardu-3fh: the ground-shadow bar (2,15,12,1 DARK, the same
        # rect the shadow_base sheet uses) is painted into every body frame of
        # both pose rows and all 8 facings, so the body sheet alone reproduces
        # the old body+shadow composite at the same cell coordinates.
        self.assert_succeeds(self.compile())
        body = self.image("mh_body_base_16x16.png")
        self.assertEqual(body.size, (128, 32))
        dark = (85, 85, 85, 255)
        clear = (0, 0, 0, 0)
        for row in range(2):
            for f in range(8):
                cell = body.crop((f * 16, row * 16, f * 16 + 16, row * 16 + 16))
                px = cell.load()
                for x in range(2, 14):
                    self.assertEqual(px[x, 15], dark,
                                     "body row %d facing %d: shadow missing at x=%d" % (row, f, x))
                self.assertEqual(px[0, 15], clear, "body row %d facing %d: x=0" % (row, f))
                self.assertEqual(px[14, 15], clear, "body row %d facing %d: x=14" % (row, f))

    def test_layered_default_set_shadow_optional(self):
        # The default set may omit a layered slot: the layer is simply not drawn
        # (bead monhun-ardu-3fh drops `shadow`). The shadow item record/sheet
        # stays in the catalog as an unused default.
        path = self.path("data", "equipment", "sets", "default.json")
        with open(path, encoding="utf-8") as handle:
            doc = json.load(handle)
        doc.pop("shadow")
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(doc, handle, indent=2)
            handle.write("\n")
        result = self.compile()
        self.assert_succeeds(result)
        text = self.read(META_REL)
        self.assertNotIn("DEFAULT_SHADOW", text)
        self.assertIn("constexpr uint8_t DEFAULT_BODY = PART_BODY_BASE;", text)
        self.assertIn("constexpr uint8_t DEFAULT_HEAD = PART_HEAD_BASE;", text)
        self.assertIn("constexpr uint8_t ITEM_SHADOW_BASE =", text)
        self.assertIn("constexpr uint8_t PART_SHADOW_BASE =", text)
        dump = self.compile("--dump")
        self.assert_succeeds(dump)
        self.assertIn("default set: body=body_base, head=head_base", dump.stdout)

    def test_layered_dump_lists_default_set(self):
        result = self.compile("--dump")
        self.assert_succeeds(result)
        self.assertIn("default set: shadow=shadow_base, body=body_base, head=head_base", result.stdout)
        self.assertIn("gen-equipment: 5 items,", result.stdout)

    def test_layered_default_set_unknown_item_rejected(self):
        path = self.path("data", "equipment", "sets", "default.json")
        with open(path, encoding="utf-8") as handle:
            doc = json.load(handle)
        doc["head"] = "head_missing"
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(doc, handle, indent=2)
            handle.write("\n")
        self.assert_fails(self.compile(), "unknown item id 'head_missing'")

    def test_layered_default_set_wrong_slot_rejected(self):
        path = self.path("data", "equipment", "sets", "default.json")
        with open(path, encoding="utf-8") as handle:
            doc = json.load(handle)
        doc["head"] = "body_base"
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(doc, handle, indent=2)
            handle.write("\n")
        self.assert_fails(self.compile(), "item 'body_base' has slot 'body'")

    def assert_fails(self, result, *needles):
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        for needle in needles:
            self.assertIn(needle, result.stderr)


if __name__ == "__main__":
    unittest.main()
