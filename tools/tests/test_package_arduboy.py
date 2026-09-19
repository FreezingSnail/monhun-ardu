#!/usr/bin/env python3
"""Unit tests for tools/package-arduboy.py (run: make test-tools).

Scratch artifacts live under build/tests/package_arduboy/ (never /tmp); the
fixture hex images are generated in the scratch case per test.
"""

import json
import os
import shutil
import subprocess
import sys
import unittest
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
TOOL = os.path.join(TOOLS, "package-arduboy.py")
SCRATCH = os.path.join(ROOT, "build", "tests", "package_arduboy")

# Minimal valid Intel HEX: one data record plus the EOF record.
HEX = ":100000000C9434000C943E000C943E000C943E0094\n:00000001FF\n"
MEMBER_ORDER = [
    "info.json",
    "monhun-ardu-fx.hex",
    "monhun-ardu-mini.hex",
    "fxdata.bin",
    "LICENSE.txt",
]


class PackageArduboyTests(unittest.TestCase):
    def setUp(self):
        self.case = os.path.join(SCRATCH, self._testMethodName)
        shutil.rmtree(self.case, ignore_errors=True)
        os.makedirs(self.case)
        self.hex = self.write("monhun-ardu-fx.hex", HEX)
        self.mini = self.write("monhun-ardu-mini.hex", HEX)
        self.fxdata_path = self.write("fxdata.bin", b"\x01\x02\x03\x04", binary=True)
        self.license = self.write("LICENSE", "MIT License\n")
        self.out = os.path.join(self.case, "dist", "monhun-ardu-v0.1.0.arduboy")

    def write(self, name, data, binary=False):
        path = os.path.join(self.case, name)
        if binary:
            with open(path, "wb") as handle:
                handle.write(data)
        else:
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(data)
        return path

    def run_tool(self, *extra):
        return subprocess.run(
            [sys.executable, TOOL, *extra],
            capture_output=True,
            text=True,
        )

    def build(self, *extra):
        return self.run_tool(
            "--version", "v0.1.0",
            "--hex", self.hex,
            "--fxdata", self.fxdata_path,
            "--license", self.license,
            "--out", self.out,
            *extra,
        )

    def read_info(self):
        with zipfile.ZipFile(self.out) as archive:
            return json.loads(archive.read("info.json"))

    def test_layout_and_info(self):
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(os.path.isfile(self.out))

        with zipfile.ZipFile(self.out) as archive:
            self.assertEqual(archive.namelist(), ["info.json","monhun-ardu-fx.hex","fxdata.bin","LICENSE.txt"])
            self.assertTrue(all("/" not in name for name in archive.namelist()))
            self.assertEqual(archive.read("fxdata.bin"), b"\x01\x02\x03\x04")
            self.assertEqual(archive.read("monhun-ardu-fx.hex").decode("utf-8"), HEX)
            self.assertEqual(archive.read("LICENSE.txt").decode("utf-8"), "MIT License\n")

        info = self.read_info()
        self.assertEqual(info["schemaVersion"], 3)
        self.assertEqual(info["version"], "v0.1.0")
        self.assertEqual(info["title"], "Monhun Ardu")
        self.assertEqual(info["author"], "FreezingSnail")
        self.assertEqual(info["binaries"], [{
            "title": "Monhun Ardu - Arduboy FX",
            "filename": "monhun-ardu-fx.hex",
            "device": "ArduboyFX",
            "flashdata": "fxdata.bin",
        }])

    def test_mini_binary_and_member_order(self):
        result = self.build("--mini-hex", self.mini)
        self.assertEqual(result.returncode, 0, result.stderr)

        with zipfile.ZipFile(self.out) as archive:
            self.assertEqual(archive.namelist(), MEMBER_ORDER)

        devices = [binary["device"] for binary in self.read_info()["binaries"]]
        self.assertEqual(devices, ["ArduboyFX", "ArduboyMini"])

    def test_deterministic_repack(self):
        first = self.build("--mini-hex", self.mini)
        self.assertEqual(first.returncode, 0, first.stderr)
        with open(self.out, "rb") as handle:
            first_bytes = handle.read()

        second = self.build("--mini-hex", self.mini)
        self.assertEqual(second.returncode, 0, second.stderr)
        with open(self.out, "rb") as handle:
            second_bytes = handle.read()

        self.assertEqual(first_bytes, second_bytes)

    def test_rejects_hex_without_eof_record(self):
        bad = self.write("bad.hex", ":100000000C9434000C943E000C943E000C943E0094\n")
        result = self.run_tool(
            "--version", "v0.1.0",
            "--hex", bad,
            "--fxdata", self.fxdata_path,
            "--out", self.out,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("end-of-file record", result.stderr)
        self.assertFalse(os.path.exists(self.out))

    def test_rejects_malformed_hex_record(self):
        bad = self.write("bad.hex", "not-a-record\n:00000001FF\n")
        result = self.run_tool(
            "--version", "v0.1.0",
            "--hex", bad,
            "--fxdata", self.fxdata_path,
            "--out", self.out,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("malformed record", result.stderr)

    def test_rejects_missing_inputs(self):
        result = self.run_tool(
            "--version", "v0.1.0",
            "--hex", os.path.join(self.case, "nope.hex"),
            "--fxdata", self.fxdata_path,
            "--out", self.out,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("FX hex not found", result.stderr)

        result = self.run_tool(
            "--version", "v0.1.0",
            "--hex", self.hex,
            "--fxdata", os.path.join(self.case, "nope.bin"),
            "--out", self.out,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("FX data image not found", result.stderr)

    def test_rejects_empty_fxdata(self):
        empty = self.write("empty.bin", b"", binary=True)
        result = self.run_tool(
            "--version", "v0.1.0",
            "--hex", self.hex,
            "--fxdata", empty,
            "--out", self.out,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("FX data image is empty", result.stderr)

    def test_rejects_non_arduboy_output(self):
        result = self.run_tool(
            "--version", "v0.1.0",
            "--hex", self.hex,
            "--fxdata", self.fxdata_path,
            "--out", os.path.join(self.case, "out.zip"),
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must end in .arduboy", result.stderr)

    def test_rejects_duplicate_member_names(self):
        result = self.build("--mini-hex", self.hex)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("duplicate archive member name", result.stderr)


if __name__ == "__main__":
    unittest.main()
