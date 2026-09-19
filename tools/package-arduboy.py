#!/usr/bin/env python3
"""Package monhun-ardu release artifacts as a `.arduboy` archive.

The `.arduboy` container is a plain ZIP with a flat root: `info.json` plus one
program (Intel HEX) per device, the single FX data image (`flashdata`) and
`LICENSE.txt`. The metadata follows the schemaVersion 3 fields used by current
readers (`schemaVersion`/`title`/`author`/`version`/`binaries` with
`title`/`filename`/`device`/`flashdata`; see the Arduboy file-format guide and
the `arduboy_toolset` parser). Consumers:

- MrBlinky's `uploader.py` takes the first `.hex` in the archive (so the FX
  program is written before the Mini one),
- Arduboy Toolset / Ardens / brow1067 tooling / the web package editor pick the
  binary by `device` (`ArduboyFX` / `ArduboyMini`) and flash `flashdata`.

Output is byte-deterministic (fixed timestamps, fixed order, deflate), so a
release can be rebuilt and compared.

Usage:
    tools/package-arduboy.py --version v0.1.0 \
        --hex dist/monhun-ardu-fx.hex --mini-hex dist/monhun-ardu-mini.hex \
        --fxdata fxdata/fxdata.bin --license LICENSE \
        --out dist/monhun-ardu-v0.1.0.arduboy
"""

import argparse
import json
import os
import sys
import zipfile

SCHEMA_VERSION = 3
SOURCE_URL = "https://github.com/FreezingSnail/monhun-ardu"
HEX_EOF = ":00000001FF"
FIXED_TIME = (1980, 1, 1, 0, 0, 0)


def fail(message):
    sys.stderr.write("package-arduboy: %s\n" % message)
    sys.exit(2)


def read_text(path, what):
    if not os.path.isfile(path):
        fail("%s not found: %s" % (what, path))
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def read_bytes(path, what):
    if not os.path.isfile(path):
        fail("%s not found: %s" % (what, path))
    with open(path, "rb") as handle:
        return handle.read()


def validate_hex(text, path):
    records = [line.strip() for line in text.splitlines() if line.strip()]
    if not records or records[-1] != HEX_EOF:
        fail("hex file missing end-of-file record %s: %s" % (HEX_EOF, path))
    if not all(record.startswith(":") for record in records):
        fail("hex file has a malformed record: %s" % path)


def add_file(archive, name, data):
    info = zipfile.ZipInfo(name, date_time=FIXED_TIME)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    archive.writestr(info, data)


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Package monhun-ardu release artifacts as a .arduboy archive."
    )
    parser.add_argument("--version", required=True, help="release version, e.g. v0.1.0")
    parser.add_argument("--hex", required=True, help="ArduboyFX program (Intel HEX)")
    parser.add_argument("--mini-hex", help="ArduboyMini program (Intel HEX); optional")
    parser.add_argument("--fxdata", required=True, help="single FX data image (flashdata)")
    parser.add_argument("--license", help="license text to include as LICENSE.txt")
    parser.add_argument("--out", required=True, help="output .arduboy path")
    parser.add_argument("--title", default="Monhun Ardu")
    parser.add_argument("--author", default="FreezingSnail")
    parser.add_argument(
        "--description",
        default="Monster-Hunter-style 4-shade grayscale duel for Arduboy FX.",
    )
    parser.add_argument("--genre", default="Action")
    parser.add_argument("--source-url", default=SOURCE_URL)
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    if not args.out.endswith(".arduboy"):
        fail("output path must end in .arduboy: %s" % args.out)

    entries = []
    binaries = []

    def add_program(device, label, path):
        text = read_text(path, "%s hex" % device)
        validate_hex(text, path)
        name = os.path.basename(path)
        if any(name == existing for existing, _ in entries):
            fail("duplicate archive member name: %s" % name)
        entries.append((name, text))
        binaries.append(
            {
                "title": "%s - %s" % (args.title, label),
                "filename": name,
                "device": device,
                "flashdata": os.path.basename(args.fxdata),
            }
        )

    add_program("ArduboyFX", "Arduboy FX", args.hex)
    if args.mini_hex:
        add_program("ArduboyMini", "Arduboy Mini", args.mini_hex)

    fxdata = read_bytes(args.fxdata, "FX data image")
    if not fxdata:
        fail("FX data image is empty: %s" % args.fxdata)
    fxdata_name = os.path.basename(args.fxdata)
    if any(fxdata_name == existing for existing, _ in entries):
        fail("duplicate archive member name: %s" % fxdata_name)
    entries.append((fxdata_name, fxdata))

    if args.license:
        entries.append(("LICENSE.txt", read_text(args.license, "license")))

    info = {
        "schemaVersion": SCHEMA_VERSION,
        "title": args.title,
        "author": args.author,
        "version": args.version,
        "description": args.description,
        "genre": args.genre,
        "sourceUrl": args.source_url,
        "binaries": binaries,
    }
    info_json = json.dumps(info, indent=2) + "\n"

    out_dir = os.path.dirname(os.path.abspath(args.out))
    os.makedirs(out_dir, exist_ok=True)
    tmp = args.out + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as archive:
        add_file(archive, "info.json", info_json)
        for name, data in entries:
            add_file(archive, name, data)
    os.replace(tmp, args.out)

    print("package-arduboy: %s (%d bytes)" % (args.out, os.path.getsize(args.out)))
    for name, data in [("info.json", info_json)] + entries:
        print("  %-28s %8d B" % (name, len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
