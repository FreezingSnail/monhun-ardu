#!/usr/bin/env python3
"""Fail a device-test sketch that leaves too little RAM for the stack.

arduino-cli prints a line like

    Global variables use 1823 bytes (71%) of dynamic memory, leaving 737
    bytes for local variables. Maximum is 2560 bytes.

That N is .data + .bss, i.e. every global the sketch drags into SRAM. If it
climbs toward the 2560 B ATmega32u4 limit the remaining bytes are the *only*
stack the device test gets. The player_art case matrix sat at 2497/2560 (63 B
of stack) and silently corrupted earlier cases -- tests that looked like render
drift were really RAM corruption (bead monhun-ardu-d76, fix 2bb7742). This tool
is the guard so the next suite (or appended case) can never cross the line
quietly.

Reads a fxtest build log from a path argument (or stdin), prints the maximum
"Global variables use N bytes" it finds, and exits:

    0  measured N <= budget (prints N, budget, and stack margin)
    1  measured N >  budget (prints the offending N)
    2  no measurement found / log missing (a build the tool cannot audit is a
       hard error, never a silent pass)

Budget default is 2350 B (2560 - 210 B stack floor); override with --budget.
The Makefile fxtest-build-% target feeds each suite's compile log here so an
over-budget sketch fails the *build*, before the serial run can be skipped.
"""

import argparse
import re
import sys

# arduino-cli's size report. Newlines inside the sentence are irrelevant: we
# only need the byte count. Anchored on the exact phrase so unrelated "N bytes"
# chatter (linker warnings, upload sizes) can never be mistaken for the RAM
# figure.
GLOBALS_RE = re.compile(r"Global variables use (\d+) bytes")

DEFAULT_BUDGET = 2350
DEVICE_RAM = 2560


def parse_max_globals(text):
    """Return the largest "Global variables use N bytes" value, or None."""
    values = [int(m.group(1)) for m in GLOBALS_RE.finditer(text)]
    return max(values) if values else None


def main(argv=None):
    ap = argparse.ArgumentParser(description="Guard fxtest sketch RAM headroom")
    ap.add_argument(
        "log",
        nargs="?",
        default="-",
        help="build log path, or '-' for stdin (default)",
    )
    ap.add_argument(
        "--budget",
        type=int,
        default=DEFAULT_BUDGET,
        help="maximum allowed .data+.bss bytes (default %d)" % DEFAULT_BUDGET,
    )
    ap.add_argument(
        "--label",
        default=None,
        help="suite name to name in the message",
    )
    args = ap.parse_args(argv)

    label = args.label or args.log
    if args.log == "-":
        text = sys.stdin.read()
    else:
        try:
            with open(args.log, "r", encoding="utf-8", errors="replace") as fh:
                text = fh.read()
        except OSError as exc:
            print(
                "fxtest_ram: ERROR %s: cannot read build log: %s"
                % (label, exc),
                file=sys.stderr,
            )
            return 2

    used = parse_max_globals(text)
    if used is None:
        print(
            "fxtest_ram: ERROR %s: no 'Global variables use N bytes' line in "
            "build log (compile failed or output not captured)" % label,
            file=sys.stderr,
        )
        return 2

    if used > args.budget:
        print(
            "fxtest_ram: FAIL %s: globals %d B > budget %d B "
            "(%d B left for stack would be below the %d B floor; move const "
            "tables to PROGMEM)"
            % (label, used, args.budget, DEVICE_RAM - used, DEVICE_RAM - args.budget),
            file=sys.stderr,
        )
        return 1

    print(
        "fxtest_ram: OK %s: globals %d B <= budget %d B (%d B stack margin)"
        % (label, used, args.budget, DEVICE_RAM - used)
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
