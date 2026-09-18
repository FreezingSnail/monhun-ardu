#!/usr/bin/env python3
"""Deterministic image<->fxdata provenance for the FX asset pipeline.

Scans images/**/*.png, fxdata/fxdata.txt, fxdata/*/Sprites.txt and the
generated artifacts, requires every image to be declared exactly once by an
included Sprites.txt (and every declared symbol to have exactly one image),
then records sorted sha256+size entries in fxdata/manifest.json.

    python3 tools/fxdata_manifest.py                  # record manifest.json
    python3 tools/fxdata_manifest.py --check          # verify, never writes
    python3 tools/fxdata_manifest.py --snapshot FILE  # hash generated outputs
    python3 tools/fxdata_manifest.py --verify-snapshot FILE

make gen-check snapshots the generated artifacts, re-runs `make gen` (the
single generation entry, tools/gen.sh), and verifies nothing changed, proving
the committed artifacts are fresh and the pipeline is deterministic.
"""
import argparse
import glob
import hashlib
import json
import os
import re
import sys

SCHEMA_VERSION = 1
GENERATOR = "tools/fxdata_manifest.py"
MANIFEST_REL = "fxdata/manifest.json"
FXDATA_REL = "fxdata/fxdata.txt"

# Generated artifacts re-created by `make gen`; gen-check hashes exactly these
# before and after the pipeline run to catch staleness and nondeterminism.
GENERATED_GLOBS = (
    "images/blocks/*.png",
    "images/fonts/*.png",
    "images/menu/*.png",
    "images/equip/*.png",
    "fxdata/blocks/Sprites.txt",
    "fxdata/fonts/Sprites.txt",
    "fxdata/menu/Sprites.txt",
    "fxdata/equip/Sprites.txt",
    "fxdata/tables/*.bin",
    "fxdata/fxdata.bin",
    "fxdata/fxdata-data.bin",
    "fxdata/fxdata.h",
    "src/fxdata.h",
    "src/generated/**/*",
    MANIFEST_REL,
)

OUTPUT_PATHS = (
    "fxdata/fxdata.bin",
    "fxdata/fxdata-data.bin",
    "fxdata/fxdata.h",
    "src/fxdata.h",
    "fxdata/tables/combat.bin",
    "fxdata/tables/equip.bin",
)

OUTPUT_GLOBS = ("src/generated/**/*",)

# JSON sources compiled into generated artifacts (tools/gen-combat.py reads
# data/skeletons.json + data/creatures/*.json; tools/gen-equipment.py reads
# data/equipment/*.json). The recursive data/**/*.json glob covers both trees,
# tracked as manifest inputs so a content edit without a regen fails
# make gen-check.
DATA_GLOBS = ("data/**/*.json",)

IMAGE_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)_(\d+)x(\d+)\.png$")
SPRITES_DECL_RE = re.compile(r"^uint8_t\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[\s*\]\s*=\s*\{?$")
SPRITES_END_RE = re.compile(r"^\};$")
INCLUDE_RE = re.compile(r'^\s*include\s+"([^"]+)"\s*$')
RAW_RE = re.compile(r'^\s*raw_t\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"([^"]+)"\s*$')

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class ManifestFailure(Exception):
    """A user-facing validation failure."""


def _rel(root, path):
    return os.path.relpath(path, root).replace(os.sep, "/")


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_entry(root, rel_path, **extra):
    entry = {"path": rel_path, "sha256": sha256_file(os.path.join(root, rel_path)),
             "size": os.path.getsize(os.path.join(root, rel_path))}
    entry.update(extra)
    return entry


def canonical_json(value):
    return json.dumps(value, sort_keys=True, indent=2) + "\n"


def parse_sprites(root, rel_path):
    """Return (symbols, issues) for one fxdata/*/Sprites.txt declaration file."""
    symbols = []
    issues = []
    seen = set()
    in_body = False
    with open(os.path.join(root, rel_path), encoding="utf-8") as handle:
        for line_no, raw in enumerate(handle, 1):
            line = raw.strip()
            if in_body:
                if SPRITES_END_RE.match(line):
                    in_body = False
                continue
            if not line or line.startswith("//"):
                continue
            match = SPRITES_DECL_RE.match(line)
            if not match:
                issues.append("malformed declaration: %s:%d: expected 'uint8_t <symbol>[] ='"
                              % (rel_path, line_no))
                continue
            symbol = match.group(1)
            if symbol in seen:
                issues.append("duplicate symbol: %s declares '%s' twice" % (rel_path, symbol))
            seen.add(symbol)
            symbols.append(symbol)
            in_body = True
    if in_body:
        issues.append("malformed declaration: %s: unterminated array" % rel_path)
    return symbols, issues


def parse_fxdata(root, rel_path):
    """Return (includes, payloads) for fxdata/fxdata.txt."""
    base = os.path.dirname(rel_path)
    includes = []
    payloads = []
    with open(os.path.join(root, rel_path), encoding="utf-8") as handle:
        for line_no, raw in enumerate(handle, 1):
            line = raw.strip()
            if not line or line.startswith("//"):
                continue
            match = INCLUDE_RE.match(line)
            if match:
                target = os.path.normpath(os.path.join(base, match.group(1))).replace(os.sep, "/")
                includes.append((line_no, match.group(1), target))
                continue
            match = RAW_RE.match(line)
            if match:
                target = os.path.normpath(os.path.join(base, match.group(2))).replace(os.sep, "/")
                payloads.append((match.group(1), target))
                continue
            # Unknown fxdata-build.py directives are left to that tool.
    return includes, payloads


def scan(root):
    """Build the provenance manifest and return (manifest, issues)."""
    issues = []

    images = sorted(_rel(root, p) for p in glob.glob(
        os.path.join(root, "images", "**", "*.png"), recursive=True) if os.path.isfile(p))
    sprite_files = sorted(_rel(root, p) for p in glob.glob(
        os.path.join(root, "fxdata", "*", "Sprites.txt")) if os.path.isfile(p))

    if not os.path.isfile(os.path.join(root, FXDATA_REL)):
        issues.append("missing definition: %s" % FXDATA_REL)
        return None, issues

    includes, payloads = parse_fxdata(root, FXDATA_REL)
    included = {}
    for line_no, raw_target, target in includes:
        if not os.path.isfile(os.path.join(root, target)):
            issues.append("missing include: %s:%d includes '%s' but %s does not exist"
                          % (FXDATA_REL, line_no, raw_target, target))
            continue
        included[target] = included.get(target, 0) + 1
    for target in sorted(included):
        if included[target] > 1:
            issues.append("duplicate include: %s included %d times by %s"
                          % (target, included[target], FXDATA_REL))

    declarations = {}
    symbols_by_file = {}
    for rel_path in sprite_files:
        if rel_path not in included:
            issues.append("unincluded declaration: %s is not included by %s"
                          % (rel_path, FXDATA_REL))
        symbols, sprite_issues = parse_sprites(root, rel_path)
        issues.extend(sprite_issues)
        symbols_by_file[rel_path] = symbols
        for symbol in symbols:
            if symbol in declarations:
                issues.append("duplicate declaration: symbol '%s' declared by %s and %s"
                              % (symbol, declarations[symbol], rel_path))
            else:
                declarations[symbol] = rel_path

    inputs = [file_entry(root, FXDATA_REL)]
    for rel_path in sprite_files:
        inputs.append(file_entry(root, rel_path, symbols=symbols_by_file[rel_path]))
    for symbol, target in payloads:
        if not os.path.isfile(os.path.join(root, target)):
            issues.append("missing payload: %s raw_t %s = '%s' does not exist"
                          % (FXDATA_REL, symbol, target))
            continue
        inputs.append(file_entry(root, target, symbol=symbol))
    for pattern in DATA_GLOBS:
        for path in glob.glob(os.path.join(root, pattern), recursive=True):
            if not os.path.isfile(path):
                continue
            inputs.append(file_entry(root, _rel(root, path)))
    inputs.sort(key=lambda entry: entry["path"])

    symbol_images = {}
    image_entries = []
    for image in images:
        name = os.path.basename(image)
        match = IMAGE_RE.match(name)
        if not match:
            issues.append("malformed image: %s (want <name>_<W>x<H>.png)" % image)
            continue
        symbol = match.group(1)
        symbol_images.setdefault(symbol, []).append(image)
        declaration = declarations.get(symbol)
        if declaration is None:
            issues.append("orphan image: %s is not declared by any %s/*/Sprites.txt"
                          % (image, os.path.dirname(FXDATA_REL)))
            continue
        image_dir = os.path.basename(os.path.dirname(image))
        declaration_dir = os.path.basename(os.path.dirname(declaration))
        if image_dir != declaration_dir:
            issues.append("declaration mismatch: %s is declared by %s (want %s/%s/Sprites.txt)"
                          % (image, declaration, os.path.dirname(FXDATA_REL), image_dir))
            continue
        image_entries.append(file_entry(root, image, symbol=symbol, declaration=declaration))
    for symbol in sorted(declarations):
        if symbol not in symbol_images:
            issues.append("missing image: %s declares '%s' but no image '%s_<W>x<H>.png' exists"
                          % (declarations[symbol], symbol, symbol))
        elif len(symbol_images[symbol]) > 1:
            issues.append("duplicate image: symbol '%s' is provided by %s"
                          % (symbol, " and ".join(symbol_images[symbol])))
    image_entries.sort(key=lambda entry: entry["path"])

    outputs = []
    output_paths = set()
    for rel_path in OUTPUT_PATHS:
        if not os.path.isfile(os.path.join(root, rel_path)):
            issues.append("missing output: %s (run make gen)" % rel_path)
            continue
        outputs.append(file_entry(root, rel_path))
        output_paths.add(rel_path)
    for pattern in OUTPUT_GLOBS:
        for path in glob.glob(os.path.join(root, pattern), recursive=True):
            if not os.path.isfile(path):
                continue
            rel_path = _rel(root, path)
            if rel_path in output_paths:
                continue
            output_paths.add(rel_path)
            outputs.append(file_entry(root, rel_path))
    outputs.sort(key=lambda entry: entry["path"])

    manifest = {
        "schema_version": SCHEMA_VERSION,
        "generator": GENERATOR,
        "images": image_entries,
        "inputs": inputs,
        "outputs": outputs,
    }
    return manifest, issues


def compare(on_disk, computed):
    """Return human-readable differences between a stored and computed manifest."""
    if not isinstance(on_disk, dict):
        return ["malformed manifest: expected a JSON object"]
    messages = []
    for key in ("schema_version", "generator"):
        if on_disk.get(key) != computed[key]:
            messages.append("manifest out of date: %s is %r, want %r"
                            % (key, on_disk.get(key), computed[key]))
    for section in ("images", "inputs", "outputs"):
        stored_entries = on_disk.get(section)
        if not isinstance(stored_entries, list):
            messages.append("malformed manifest: missing %s list" % section)
            continue
        stored = {}
        for entry in stored_entries:
            if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
                messages.append("malformed manifest: %s entry without a path" % section)
                continue
            stored[entry["path"]] = entry
        wanted = {entry["path"]: entry for entry in computed[section]}
        for path in sorted(set(stored) - set(wanted)):
            messages.append("manifest out of date: unexpected %s entry: %s" % (section, path))
        for path in sorted(set(wanted) - set(stored)):
            messages.append("manifest out of date: missing %s entry: %s" % (section, path))
        for path in sorted(set(stored) & set(wanted)):
            if stored[path] != wanted[path]:
                fields = [key for key in sorted(set(stored[path]) | set(wanted[path]))
                          if stored[path].get(key) != wanted[path].get(key)]
                messages.append("manifest out of date: changed %s: %s (%s)"
                                % (section, path, ", ".join(fields)))
    return messages


def record(root):
    """Validate, then write fxdata/manifest.json atomically. Returns (issues, ok)."""
    manifest, issues = scan(root)
    if issues:
        return issues, False
    path = os.path.join(root, MANIFEST_REL)
    text = canonical_json(manifest)
    if os.path.isfile(path):
        with open(path, encoding="utf-8") as handle:
            if handle.read() == text:
                print("fxdata_manifest: %s up to date (%s)" % (MANIFEST_REL, summary(manifest)))
                return [], True
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    try:
        with open(tmp, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)
    print("fxdata_manifest: wrote %s (%s)" % (MANIFEST_REL, summary(manifest)))
    return [], True


def check(root):
    """Verify declarations and the stored manifest; never writes. Returns (issues, manifest)."""
    manifest, issues = scan(root)
    if issues:
        return issues, manifest
    path = os.path.join(root, MANIFEST_REL)
    if not os.path.isfile(path):
        return ["missing manifest: %s (run make gen)" % MANIFEST_REL], manifest
    try:
        with open(path, encoding="utf-8") as handle:
            on_disk = json.load(handle)
    except (OSError, ValueError) as exc:
        return ["malformed manifest: %s: %s" % (MANIFEST_REL, exc)], manifest
    return compare(on_disk, manifest), manifest


def generated_paths(root):
    paths = set()
    for pattern in GENERATED_GLOBS:
        for path in glob.glob(os.path.join(root, pattern), recursive=True):
            if os.path.isfile(path):
                paths.add(_rel(root, path))
    return sorted(paths)


def snapshot(root):
    return {"schema_version": SCHEMA_VERSION,
            "files": {path: sha256_file(os.path.join(root, path)) for path in generated_paths(root)}}


def verify_snapshot(root, snap):
    if not isinstance(snap, dict) or not isinstance(snap.get("files"), dict):
        return ["malformed snapshot: expected {'files': {path: sha256}}"]
    before = snap["files"]
    after = snapshot(root)["files"]
    issues = []
    for path in sorted(set(before) | set(after)):
        if path not in after:
            issues.append("stale generated artifact: %s was present but make gen did not produce it" % path)
        elif path not in before:
            issues.append("stale generated artifact: %s was missing and make gen produced it" % path)
        elif before[path] != after[path]:
            issues.append("stale generated artifact: %s differed from make gen output" % path)
    return issues


def summary(manifest):
    return "%d images, %d inputs, %d outputs" % (
        len(manifest["images"]), len(manifest["inputs"]), len(manifest["outputs"]))


def fail(issues, remedy):
    for issue in issues:
        print("fxdata_manifest: FAIL: %s" % issue, file=sys.stderr)
    print("remedy: %s" % remedy, file=sys.stderr)
    return 1


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=None,
                        help="pipeline root (default: repository root)")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true",
                      help="verify declarations and manifest.json; never writes")
    mode.add_argument("--snapshot", metavar="FILE",
                      help="write sha256 of every generated artifact to FILE")
    mode.add_argument("--verify-snapshot", metavar="FILE",
                      help="fail if generated artifacts differ from FILE")
    args = parser.parse_args(argv)
    root = os.path.abspath(args.root) if args.root else REPO_ROOT

    if args.snapshot:
        snap = snapshot(root)
        path = os.path.abspath(args.snapshot)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(canonical_json(snap))
        print("fxdata_manifest: snapshot of %d generated artifacts -> %s"
              % (len(snap["files"]), args.snapshot))
        return 0

    if args.verify_snapshot:
        try:
            with open(args.verify_snapshot, encoding="utf-8") as handle:
                snap = json.load(handle)
        except (OSError, ValueError) as exc:
            return fail(["malformed snapshot: %s: %s" % (args.verify_snapshot, exc)],
                        "run make gen-check again")
        issues = verify_snapshot(root, snap)
        if issues:
            return fail(issues, "run make gen and commit the regenerated artifacts")
        print("fxdata_manifest: PASS (%d generated artifacts unchanged)" % len(snapshot(root)["files"]))
        return 0

    if args.check:
        issues, manifest = check(root)
        if issues:
            return fail(issues, "run make gen (and commit fxdata/manifest.json)")
        print("fxdata_manifest: PASS (%s; %s matches)" % (summary(manifest), MANIFEST_REL))
        return 0

    issues, _ = record(root)
    if issues:
        return fail(issues, "fix the declarations above, then run make gen")
    return 0


if __name__ == "__main__":
    sys.exit(main())
