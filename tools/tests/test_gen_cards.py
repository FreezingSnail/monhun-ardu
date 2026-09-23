#!/usr/bin/env python3
"""Unit tests for tools/gen-cards.py (run: make test-tools).

Builds a synthetic data tree under build/tests/gen_cards/ (no /tmp) and pins:
the packed card ABI (header, record offsets, page u24 offsets resolved from
fxdata/fxdata.h), page masks + absent pages, overlay slots, deterministic
re-runs, the dense quest-id rule, and the review contact sheet layout.
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
TOOL = os.path.join(TOOLS, "gen-cards.py")
SCRATCH = os.path.join(ROOT, "build", "tests", "gen_cards")

spec = importlib.util.spec_from_file_location("gen_cards", TOOL)
gen_cards = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen_cards)

HEADER = struct.Struct("<HBBBBH")
ITEM = struct.Struct("<BBB")
ITEM_SIZE = 27
HEADER_SIZE = 8
PAGE_MAX = 4
OVERLAY_MAX = 2

ITEMS = {
    "version": 1,
    "items": [
        {"id": "herb", "kind": "consumable", "heal": 20, "stam": 0, "sell": 10},
        {"id": "ore", "kind": "material", "heal": 0, "stam": 0, "sell": 30},
        {"id": "scale", "kind": "material", "heal": 0, "stam": 0, "sell": 25},
    ],
}

SKILLS = {
    "version": 1,
    "thresholds": {"s": 10, "m": 15},
    "skills": [
        {"id": "attack_up", "abbr": "ATK", "kind": "ATTACK_UP", "maxPoints": 15, "perPoint": 2},
        {"id": "health_up", "abbr": "HP", "kind": "HEALTH_UP", "maxPoints": 15, "perPoint": 1},
    ],
}

# alpha_helm: full card (desc/parts/stats/skill). beta_cap: no recipe, no
# skills -> DESC + STATS only. gamma_mail: zenny-only recipe -> PARTS with just
# the ZENNY line.
ARMOR = {
    "version": 1,
    "pieces": [
        {
            "id": "alpha_helm", "slot": "head", "defense": 10,
            "desc": ["ALPHA COPY ONE", "ALPHA COPY TWO"],
            "resist": {"fire": 1, "water": 0, "ice": 0, "thunder": -1},
            "skills": [{"id": "attack_up", "points": 6}],
            "recipe": {"materials": [{"item": "ore", "count": 2}, {"item": "scale", "count": 1}], "zenny": 100},
        },
        {
            "id": "beta_cap", "slot": "body", "defense": 4,
            "resist": {"fire": 0, "water": 0, "ice": 0, "thunder": 0},
            "skills": [],
            "recipe": {},
        },
        {
            "id": "gamma_mail", "slot": "charm", "defense": 0,
            "resist": {"fire": 0, "water": 0, "ice": 0, "thunder": 0},
            "skills": [{"id": "health_up", "points": 4}],
            "recipe": {"zenny": 50},
        },
    ],
}

QUESTS = {
    "slay_lunge": {"id": 0, "goalKind": "kill", "target": "lunge", "need": 3,
                   "desc": ["HUNT THE BEAST", "IN THE AREA."],
                   "rewardZenny": 150, "unlockFlag": 0},
    "gather_ore": {"id": 1, "goalKind": "gather", "target": "ore", "need": 2,
                   "rewardZenny": 0, "unlockFlag": 0},
}

# The card table order: armor in data/armor.json order, then quests by (id, name).
ORDER = ["armor_alpha_helm", "armor_beta_cap", "armor_gamma_mail",
         "quest_slay_lunge", "quest_gather_ore"]
# Pages per item id (from the synthetic data above).
PAGES = {
    "armor_alpha_helm": [0, 1, 2, 3],
    "armor_beta_cap": [0, 2],
    "armor_gamma_mail": [0, 1, 2, 3],
    "quest_slay_lunge": [0, 1, 2],
    "quest_gather_ore": [0, 1],
}
MASKS = {
    "armor_alpha_helm": 0x0F,
    "armor_beta_cap": 0x05,
    "armor_gamma_mail": 0x0F,
    "quest_slay_lunge": 0x07,
    "quest_gather_ore": 0x03,
}


def page_symbol(name, page_id):
    return "mh_card_%s_%d" % (name, page_id)


def write_tree(case):
    data = os.path.join(case, "data")
    quests = os.path.join(data, "quests")
    os.makedirs(quests, exist_ok=True)
    os.makedirs(os.path.join(case, "fxdata"), exist_ok=True)
    os.makedirs(os.path.join(case, "src", "generated"), exist_ok=True)
    docs = {
        os.path.join(data, "items.json"): ITEMS,
        os.path.join(data, "skills.json"): SKILLS,
        os.path.join(data, "armor.json"): ARMOR,
    }
    for quest_name, quest in QUESTS.items():
        docs[os.path.join(quests, quest_name + ".json")] = quest
    for path, doc in docs.items():
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(doc, handle, indent=2)
    # Page symbol addresses as fxdata-build.py would emit them: contiguous
    # 3 x 1024 B pages.
    addr = 0x001000
    lines = ["#pragma once", ""]
    for name in ORDER:
        for page_id in PAGES[name]:
            lines.append("constexpr uint24_t %s = 0x%06X;" % (page_symbol(name, page_id), addr))
            addr += 3072
    with open(os.path.join(case, "fxdata", "fxdata.h"), "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")
    return case


def png_size(path):
    with open(path, "rb") as handle:
        head = handle.read(24)
    assert head[:8] == b"\x89PNG\r\n\x1a\n"
    return struct.unpack(">II", head[16:24])


def parse_records(blob):
    magic, version, flags, count, _pad0, _pad1 = HEADER.unpack_from(blob, 0)
    records = []
    for i in range(count):
        off = HEADER_SIZE + i * ITEM_SIZE
        kind, mask, overlay_count = ITEM.unpack_from(blob, off)
        pages = []
        for p in range(PAGE_MAX):
            base = off + 3 + p * 3
            pages.append(blob[base] | (blob[base + 1] << 8) | (blob[base + 2] << 16))
        overlays = []
        for o in range(OVERLAY_MAX):
            base = off + 15 + o * 6
            overlays.append(tuple(blob[base:base + 6]))
        records.append({"kind": kind, "mask": mask, "overlay_count": overlay_count,
                        "pages": pages, "overlays": overlays, "off": off})
    return {"magic": magic, "version": version, "flags": flags, "count": count}, records


class GenCardsTests(unittest.TestCase):
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

    def load(self):
        return parse_records(self.read_bytes("fxdata", "tables", "cards.bin"))

    # --------------------------------------------------------------- ABI
    def test_header_and_record_layout(self):
        self.run_ok()
        header, records = self.load()
        self.assertEqual(header["magic"], 0x4341)
        self.assertEqual(header["version"], 1)
        self.assertEqual(header["count"], len(ORDER))
        blob = self.read_bytes("fxdata", "tables", "cards.bin")
        self.assertEqual(len(blob), HEADER_SIZE + ITEM_SIZE * len(ORDER))
        for i, record in enumerate(records):
            self.assertEqual(record["off"], HEADER_SIZE + i * ITEM_SIZE)

    def test_page_offsets_resolve_from_fxdata_header(self):
        self.run_ok()
        _header, records = self.load()
        addr = 0x001000
        for i, name in enumerate(ORDER):
            for page_id in range(PAGE_MAX):
                want = addr if page_id in PAGES[name] else 0
                self.assertEqual(records[i]["pages"][page_id], want,
                                 "%s page %d offset" % (name, page_id))
                if page_id in PAGES[name]:
                    addr += 3072

    def test_meta_header_matches_blob(self):
        self.run_ok()
        meta = self.read("src", "generated", "card_meta.hpp")
        self.assertIn("constexpr uint8_t ITEM_SIZE = 27;", meta)
        self.assertIn("constexpr uint8_t ITEM_COUNT = 5;", meta)
        self.assertIn("constexpr uint8_t QUEST_BASE = 3;", meta)
        self.assertIn("constexpr uint8_t CARD_ARMOR_ALPHA_HELM = 0;", meta)
        self.assertIn("constexpr uint16_t CARD_ARMOR_ALPHA_HELM_OFF = 8;", meta)
        self.assertIn("constexpr uint8_t CARD_QUEST_GATHER_ORE = 4;", meta)
        self.assertIn("constexpr uint16_t CARD_QUEST_GATHER_ORE_OFF = 116;", meta)

    # ---------------------------------------------------- masks/pages/overlays
    def test_masks_and_absent_pages_not_generated(self):
        self.run_ok()
        _header, records = self.load()
        for i, name in enumerate(ORDER):
            self.assertEqual(records[i]["mask"], MASKS[name], "%s mask" % name)
        # beta_cap has no recipe and no skills -> no PARTS and no SKILL page.
        self.assertEqual(records[1]["mask"] & (1 << gen_cards.PAGE_PARTS), 0, "beta no PARTS")
        self.assertEqual(records[1]["mask"] & (1 << gen_cards.PAGE_SKILL), 0, "beta no SKILL")
        self.assertFalse(os.path.isfile(self.path("images", "cards", "mh_card_armor_beta_cap_1_128x64.png")))
        self.assertFalse(os.path.isfile(self.path("images", "cards", "mh_card_armor_beta_cap_3_128x64.png")))
        # gather_ore has no reward -> no REWARD page.
        self.assertEqual(records[4]["mask"] & (1 << gen_cards.PAGE_REWARD), 0, "gather no REWARD")
        # Every generated page image exists and is 128x64.
        for name in ORDER:
            for page_id in PAGES[name]:
                png = self.path("images", "cards", "%s_128x64.png" % page_symbol(name, page_id))
                self.assertEqual(png_size(png), (128, 64), png)

    def test_overlay_slots(self):
        self.run_ok()
        _header, records = self.load()
        # alpha_helm PARTS (page 1): two HAVE slots for ore (idx 1) and scale
        # (idx 2) at x=112, y=13/21.
        alpha = records[0]
        self.assertEqual(alpha["overlay_count"], 2)
        self.assertEqual(alpha["overlays"][0][:5], (gen_cards.OVERLAY_HAVE, 1, 112, 13, 1))
        self.assertEqual(alpha["overlays"][1][:5], (gen_cards.OVERLAY_HAVE, 1, 112, 21, 2))
        # slay_lunge PROG (page 1): need 3, 112 px bar at (8, 27).
        lunge = records[3]
        self.assertEqual(lunge["overlay_count"], 1)
        self.assertEqual(lunge["overlays"][0][:6], (gen_cards.OVERLAY_PROG, 1, 8, 27, 3, 112))
        # No overlays on the quest GOAL/REWARD pages or the questless armor.
        self.assertEqual(records[1]["overlay_count"], 0, "beta has no overlays")
        self.assertEqual(records[2]["overlay_count"], 0, "gamma has no overlays")

    def test_sprites_declare_every_page_once(self):
        self.run_ok()
        text = self.read("fxdata", "cards", "Sprites.txt")
        for name in ORDER:
            for page_id in PAGES[name]:
                self.assertIn("uint8_t %s[] =" % page_symbol(name, page_id), text)
        self.assertEqual(text.count("uint8_t mh_card_"), sum(len(v) for v in PAGES.values()))

    # -------------------------------------------------------- determinism/CLI
    def test_rerun_is_byte_identical(self):
        first = self.run_ok()
        self.assertIn("gen-cards: 5 items, 15 pages", first.stdout)
        before = {rel: self.read_bytes(*rel.split("/")) for rel in
                  ("fxdata/tables/cards.bin", "fxdata/cards/Sprites.txt",
                   "src/generated/card_meta.hpp")}
        second = self.run_ok()
        self.assertIn("(unchanged)", second.stdout)
        for rel, data in before.items():
            self.assertEqual(self.read_bytes(*rel.split("/")), data, rel)

    def test_dump_writes_nothing(self):
        before = sorted(os.path.relpath(os.path.join(dirpath, name), self.case)
                        for dirpath, _dirs, files in os.walk(self.case) for name in files)
        result = self.run_ok("--dump")
        self.assertIn("card armor alpha_helm: mask 0x0F", result.stdout)
        self.assertIn("card quest gather_ore: mask 0x03", result.stdout)
        after = sorted(os.path.relpath(os.path.join(dirpath, name), self.case)
                       for dirpath, _dirs, files in os.walk(self.case) for name in files)
        self.assertEqual(before, after)

    def test_missing_fxdata_header_warns_and_zeroes_offsets(self):
        os.remove(self.path("fxdata", "fxdata.h"))
        result = self.run_ok()
        self.assertIn("page symbols unresolved", result.stdout)
        _header, records = self.load()
        for record in records:
            self.assertEqual(record["pages"], [0, 0, 0, 0])

    def test_nondense_quest_ids_fail(self):
        with open(self.path("data", "quests", "gather_ore.json"), encoding="utf-8") as handle:
            quest = json.load(handle)
        quest["id"] = 2   # gap: ids 0 and 2, no 1
        with open(self.path("data", "quests", "gather_ore.json"), "w", encoding="utf-8", newline="\n") as handle:
            json.dump(quest, handle, indent=2)
        result = self.run_tool()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("quest ids must be dense 0..N-1", result.stderr)

    def test_contact_sheet_layout_and_determinism(self):
        out_rel = os.path.join("build", "sheet.png")
        self.run_ok("--sheet", out_rel)
        out = self.path(out_rel)
        rows = (sum(len(v) for v in PAGES.values()) + gen_cards.SHEET_COLS - 1) // gen_cards.SHEET_COLS
        self.assertEqual(png_size(out), (gen_cards.SHEET_COLS * 128, rows * (64 + gen_cards.SHEET_LABEL_H)))
        first = self.read_bytes(out_rel)
        self.run_ok("--sheet", out_rel)
        self.assertEqual(self.read_bytes(out_rel), first, "sheet is deterministic")

    # ------------------------------------------------------------ unit-level
    def test_card_layers_page_major_planes(self):
        from PIL import Image
        img = Image.new("RGBA", (128, 64), (0, 0, 0, 0))
        img.load()[0, 0] = (255, 255, 255, 255)     # white: all planes
        img.load()[5, 3] = (170, 170, 170, 255)     # light: planes 0+1
        img.load()[7, 9] = (85, 85, 85, 255)        # dark: plane 0
        layers = gen_cards.card_layers(img)
        self.assertEqual(len(layers), 3 * 1024)
        # White pixel (0,0): page 0 byte 0, bit 0 on all three planes.
        self.assertEqual(layers[0] & 1, 1, "white lights plane 0")
        self.assertEqual(layers[1024] & 1, 1, "white lights plane 1")
        self.assertEqual(layers[2048] & 1, 1, "white lights plane 2")
        # Light pixel (5,3): page 0 byte 5, bit 3 on planes 0/1 only.
        self.assertEqual(layers[5] & (1 << 3), 1 << 3, "light lights plane 0")
        self.assertEqual(layers[1024 + 5] & (1 << 3), 1 << 3, "light lights plane 1")
        self.assertEqual(layers[2048 + 5] & (1 << 3), 0, "light skips plane 2")
        # Dark pixel (7,9): page 1 byte 7, bit 1 on plane 0 only.
        self.assertEqual(layers[128 + 7] & (1 << 1), 1 << 1, "dark lights plane 0")
        self.assertEqual(layers[1024 + 128 + 7] & (1 << 1), 0, "dark skips plane 1")


if __name__ == "__main__":
    unittest.main()
