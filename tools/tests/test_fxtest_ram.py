#!/usr/bin/env python3
"""Unit tests for tools/fxtest_ram.py (run: make test-tools).

Scratch logs live under build/tests/fxtest_ram/ (never /tmp); the fixture text
mimics arduino-cli's size report. Pins the player_art trap: a 2497 B globals
figure (63 B stack) must fail, and a build log with no size line must be a hard
error rather than a silent pass (bead monhun-ardu-d76).
"""

import os
import shutil
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
TOOL = os.path.join(TOOLS, "fxtest_ram.py")
SCRATCH = os.path.join(ROOT, "build", "tests", "fxtest_ram")

# arduino-cli reports the flash line first, then the dynamic-memory line we
# audit.
FLASH_LINE = (
    "Sketch uses 13546 bytes (47%) of program storage space. "
    "Maximum is 28672 bytes.\n"
)


def size_line(used):
    return (
        "Global variables use %d bytes (%d%%) of dynamic memory, leaving %d "
        "bytes for local variables. Maximum is 2560 bytes.\n"
        % (used, used * 100 // 2560, 2560 - used)
    )


class FxtestRamTests(unittest.TestCase):
    def setUp(self):
        self.case = os.path.join(SCRATCH, self._testMethodName)
        shutil.rmtree(self.case, ignore_errors=True)
        os.makedirs(self.case)

    def write(self, name, text):
        path = os.path.join(self.case, name)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text)
        return path

    def run_tool(self, log_path, *args, stdin=None):
        cmd = [sys.executable, TOOL, *args]
        if log_path is not None:
            cmd.append(log_path)
        return subprocess.run(
            cmd,
            input=stdin,
            capture_output=True,
            text=True,
        )

    def test_under_budget_passes(self):
        log = self.write("under.log", FLASH_LINE + size_line(1823))
        res = self.run_tool(log, "--label", "test_player_art")
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("OK test_player_art", res.stdout)
        self.assertIn("1823", res.stdout)
        self.assertIn("2350", res.stdout)

    def test_over_budget_fails(self):
        # The exact player_art pre-fix figure: 2497 B left only 63 B of stack.
        log = self.write("over.log", FLASH_LINE + size_line(2497))
        res = self.run_tool(log, "--label", "test_player_art")
        self.assertEqual(res.returncode, 1, res.stdout)
        self.assertIn("FAIL test_player_art", res.stderr)
        self.assertIn("2497", res.stderr)

    def test_budget_flag_lowers_threshold(self):
        log = self.write("mid.log", FLASH_LINE + size_line(2000))
        ok = self.run_tool(log, "--budget", "2350")
        self.assertEqual(ok.returncode, 0, ok.stderr)
        bad = self.run_tool(log, "--budget", "1900")
        self.assertEqual(bad.returncode, 1, bad.stdout)

    def test_no_size_line_is_error(self):
        log = self.write("empty.log", "Compiling everything...\nDone.\n")
        res = self.run_tool(log)
        self.assertEqual(res.returncode, 2, res.stdout)
        self.assertIn("no 'Global variables use N bytes' line", res.stderr)

    def test_stdin(self):
        res = self.run_tool(None, "--label", "piped", stdin=FLASH_LINE + size_line(2100))
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("OK piped", res.stdout)

    def test_picks_max_of_multiple_lines(self):
        # A later, larger figure must win even if an earlier one is safe.
        text = FLASH_LINE + size_line(900) + size_line(2497)
        log = self.write("multi.log", text)
        res = self.run_tool(log)
        self.assertEqual(res.returncode, 1, res.stdout)
        self.assertIn("2497", res.stderr)

    def test_missing_file_is_error(self):
        res = self.run_tool(os.path.join(self.case, "absent.log"))
        self.assertEqual(res.returncode, 2, res.stdout)
        self.assertIn("cannot read build log", res.stderr)

    def test_label_defaults_to_path(self):
        log = self.write("unlabeled.log", FLASH_LINE + size_line(1823))
        res = self.run_tool(log)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("unlabeled.log", res.stdout)


if __name__ == "__main__":
    unittest.main()
