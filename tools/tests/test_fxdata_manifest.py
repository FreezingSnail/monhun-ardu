#!/usr/bin/env python3
"""Unit tests for tools/fxdata_manifest.py (run: make test-tools).

The clean fixture under fixtures/fxdata_manifest/clean ships a committed
manifest.json; every failure case copies it to build/tests/fxdata_manifest/
and mutates the copy, so the tests stay read-only on the repository fixtures.
"""
import json
import os
import shutil
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
TOOL = os.path.join(TOOLS, "fxdata_manifest.py")
FIXTURE = os.path.join(HERE, "fixtures", "fxdata_manifest", "clean")
SCRATCH = os.path.join(ROOT, "build", "tests", "fxdata_manifest")


def run_tool(*args):
    return subprocess.run([sys.executable, TOOL, *args], capture_output=True, text=True)


class FxdataManifestTests(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        case = os.path.join(SCRATCH, self._testMethodName)
        shutil.rmtree(case, ignore_errors=True)
        shutil.copytree(FIXTURE, case)
        self.case = case
        self.manifest = os.path.join(case, "fxdata", "manifest.json")
        self.assertTrue(os.path.isfile(self.manifest),
                        "clean fixture must ship a manifest.json")

    def path(self, *parts):
        return os.path.join(self.case, *parts)

    def read(self, *parts):
        with open(self.path(*parts), encoding="utf-8") as handle:
            return handle.read()

    def write(self, rel_path, text, binary=False):
        if binary:
            with open(self.path(rel_path), "ab") as handle:
                handle.write(text)
        else:
            with open(self.path(rel_path), "w", encoding="utf-8") as handle:
                handle.write(text)

    def append(self, rel_path, text):
        with open(self.path(rel_path), "a", encoding="utf-8") as handle:
            handle.write(text)

    def check(self):
        return run_tool("--root", self.case, "--check")

    def assert_fails(self, result, *needles):
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        for needle in needles:
            self.assertIn(needle, result.stderr)

    def test_clean_check_passes_and_writes_nothing(self):
        before = self.read("fxdata", "manifest.json")
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PASS (3 images, 4 inputs, 6 outputs", result.stdout)
        self.assertEqual(before, self.read("fxdata", "manifest.json"))
        manifest = json.loads(before)
        images = {entry["symbol"]: entry for entry in manifest["images"]}
        self.assertEqual(images["foo"]["declaration"], "fxdata/blocks/Sprites.txt")
        self.assertEqual(images["bar"]["declaration"], "fxdata/fonts/Sprites.txt")
        self.assertEqual(images["foo"]["size"],
                         os.path.getsize(self.path("images", "blocks", "foo_4x4.png")))

    def test_orphan_image_fails(self):
        shutil.copyfile(self.path("images", "blocks", "foo_4x4.png"),
                        self.path("images", "blocks", "orphan_4x4.png"))
        result = self.check()
        self.assert_fails(result, "orphan image: images/blocks/orphan_4x4.png",
                          "remedy: run make gen")

    def test_missing_image_fails(self):
        os.remove(self.path("images", "blocks", "foo_4x4.png"))
        result = self.check()
        self.assert_fails(result,
                          "missing image: fxdata/blocks/Sprites.txt declares 'foo'")

    def test_malformed_declaration_fails(self):
        self.append("fxdata/blocks/Sprites.txt", "uint8_t broken = 3;\n")
        result = self.check()
        self.assert_fails(result, "malformed declaration: fxdata/blocks/Sprites.txt")

    def test_duplicate_declaration_fails(self):
        self.append("fxdata/fonts/Sprites.txt", "uint8_t foo[] =\n{\n      0,\n};\n")
        result = self.check()
        self.assert_fails(result, "duplicate declaration: symbol 'foo' declared by")

    def test_malformed_image_name_fails(self):
        os.rename(self.path("images", "blocks", "foo_4x4.png"),
                  self.path("images", "blocks", "foo.png"))
        result = self.check()
        self.assert_fails(result, "malformed image: images/blocks/foo.png")

    def test_missing_payload_fails(self):
        os.remove(self.path("fxdata", "tables", "testdata.bin"))
        result = self.check()
        self.assert_fails(result,
                          "missing payload: fxdata/fxdata.txt raw_t mhTestData")

    def test_unincluded_sprites_fails(self):
        text = self.read("fxdata", "fxdata.txt")
        self.write("fxdata/fxdata.txt",
                   text.replace('include "fonts/Sprites.txt"\n', "// fonts dropped\n"))
        result = self.check()
        self.assert_fails(result,
                          "unincluded declaration: fxdata/fonts/Sprites.txt is not included")

    def test_missing_include_fails(self):
        self.append("fxdata/fxdata.txt", 'include "blocks/Ghost.txt"\n')
        result = self.check()
        self.assert_fails(result, "missing include: fxdata/fxdata.txt")

    def test_declaration_directory_mismatch_fails(self):
        os.rename(self.path("images", "blocks", "foo_4x4.png"),
                  self.path("images", "fonts", "foo_4x4.png"))
        result = self.check()
        self.assert_fails(result,
                          "declaration mismatch: images/fonts/foo_4x4.png is declared by "
                          "fxdata/blocks/Sprites.txt")

    def test_changed_image_fails_manifest_check(self):
        self.write("images/blocks/baz_2x2.png", b"\x00", binary=True)
        result = self.check()
        self.assert_fails(result, "manifest out of date: changed images: images/blocks/baz_2x2.png",
                          "sha256")

    def test_record_is_deterministic_and_sorted(self):
        os.remove(self.manifest)
        first = run_tool("--root", self.case)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertIn("wrote fxdata/manifest.json", first.stdout)
        with open(self.manifest, "rb") as handle:
            written_once = handle.read()
        second = run_tool("--root", self.case)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertIn("up to date", second.stdout)
        with open(self.manifest, "rb") as handle:
            self.assertEqual(written_once, handle.read())
        manifest = json.loads(written_once)
        for section in ("images", "inputs", "outputs"):
            paths = [entry["path"] for entry in manifest[section]]
            self.assertEqual(paths, sorted(paths), section)

    def test_data_json_tracked_as_input(self):
        os.makedirs(self.path("data", "creatures"), exist_ok=True)
        self.write("data/creatures/beast.json", '{"id": "beast"}\n')
        os.remove(self.manifest)
        result = run_tool("--root", self.case)
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads(self.read("fxdata", "manifest.json"))
        self.assertIn("data/creatures/beast.json",
                      [entry["path"] for entry in manifest["inputs"]])

    def test_snapshot_verify_detects_changed_artifact(self):
        snap = self.path("build", "snapshot.json")
        result = run_tool("--root", self.case, "--snapshot", snap)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = run_tool("--root", self.case, "--verify-snapshot", snap)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PASS (13 generated artifacts unchanged)", result.stdout)
        self.write("src/fxdata.h", "// edited\n")
        result = run_tool("--root", self.case, "--verify-snapshot", snap)
        self.assert_fails(result, "stale generated artifact: src/fxdata.h")
        self.assertIn("remedy: run make gen and commit", result.stderr)

    def test_snapshot_verify_detects_new_artifact(self):
        snap = self.path("build", "snapshot.json")
        result = run_tool("--root", self.case, "--snapshot", snap)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.write("src/generated/extra.hpp", "// new generated header\n")
        result = run_tool("--root", self.case, "--verify-snapshot", snap)
        self.assert_fails(result, "stale generated artifact: src/generated/extra.hpp")

    def test_snapshot_verify_detects_missing_artifact(self):
        snap = self.path("build", "snapshot.json")
        result = run_tool("--root", self.case, "--snapshot", snap)
        self.assertEqual(result.returncode, 0, result.stderr)
        os.remove(self.path("fxdata", "fxdata.bin"))
        result = run_tool("--root", self.case, "--verify-snapshot", snap)
        self.assert_fails(result, "stale generated artifact: fxdata/fxdata.bin")


if __name__ == "__main__":
    unittest.main()
