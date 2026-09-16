# monhun-ardu-42n.4 — Pipeline: image<->fxdata manifest + orphan/staleness checks (make gen-check)

Status: **DONE** — all acceptance criteria met, all required verifications green.
Repo: /Users/connorfranc/monhun-ardu at HEAD 4e332f0 (tree left dirty for orchestrator; no commits/pushes).

## Summary

The `make gen` Python pipeline now has a deterministic provenance manifest
(`fxdata/manifest.json`) and a staleness/determinism gate (`make gen-check`):

- `tools/fxdata_manifest.py` scans `images/**/*.png`, `fxdata/fxdata.txt`,
  `fxdata/*/Sprites.txt`, `src/fxdata.h` and `src/generated/`, and:
  - requires every image to be declared **exactly once** by an included
    `fxdata/*/Sprites.txt` (symbol from `<name>_<W>x<H>.png`, directory must
    match the Sprites directory),
  - requires every declared symbol to have **exactly one** image,
  - validates `include "..."` targets and `raw_t` payload paths,
  - records sorted sha256+size for images, inputs and outputs.
  - record mode validates first, then replaces the manifest atomically
    (temp file + `os.replace`), so a failure never touches the manifest.
  - `--check` verifies declarations and byte-compares the stored manifest,
    and never writes.
- `make gen` (`tools/gen.sh`, still the single generation entry) runs
  `python3 tools/fxdata_manifest.py` as its last step.
- `make gen-check` snapshots sha256 of every generated artifact, re-runs
  `make gen`, and fails if any artifact changed (staleness or nondeterminism).
  The snapshot lives in gitignored `build/` and is deleted on success.
- `make test-tools` runs the Python unittest suite (stdlib `unittest`;
  `pytest` is not installed locally) from `tools/tests/`.

## Files changed

| File | Change |
| --- | --- |
| `tools/fxdata_manifest.py` | new: manifest recorder + `--check` + snapshot/verify |
| `fxdata/manifest.json` | new (generated): 19 images, 5 inputs, 5 outputs |
| `tools/tests/test_fxdata_manifest.py` | new: 15 unittest cases (clean/orphan/missing/malformed/staleness/determinism) |
| `tools/tests/fixtures/fxdata_manifest/clean/**` | new: committed fixture tree + pinned manifest |
| `Makefile` | new `gen-check` and `test-tools` targets; `.PHONY` updated |
| `tools/gen.sh` | manifest recorded as the final pipeline step |
| `README.md` | pipeline diagram/docs: gen-art.py authorship, manifest/gen-check/test-tools |
| `.gitignore` | `!tools/tests/fixtures/**/*.bin` so fixture blobs can be committed |
| `images/ArduFontTrimmed_5x6.png` | deleted (orphan, zero consumers) |
| `tools/text2bmp.py` | deleted (dead tool; gen-art.py owns font sheets) |

## Orphan resolution

- `images/ArduFontTrimmed_5x6.png`: no reference anywhere in the build
  (`gen.sh`, `gen-art.py`, Makefile, tst/, src/); it was a legacy 5x6 font
  authoring asset. `gen-art.py` authors every `images/blocks|fonts/fx*.png`
  sheet and `clean_stale()` removes stale `fx*` sheets. **Deleted** (rather
  than allowlisted) so the manifest invariant "every image is declared exactly
  once" holds unconditionally.
- `tools/text2bmp.py`: unused by `gen.sh`/Makefile/any module; only the README
  pipeline diagram still mentioned it. **Deleted**, README diagram/layout
  updated to the actual gen-art.py/convert-sprite.py flow. (`tools/imageConverter.py`
  was not flagged by the bead and is left untouched.)

## Ardugotools evaluation (directive: future option, no swap)

Reviewed github.com/randomouscrap98/ardugotools (single Go binary CLI): its
selling point for this pipeline is replacing `fxdata.txt` parsing with lua
config scripts for FX data/header/save generation, plus flashcart tooling.
Not adopted now because (a) the directive keeps the Python pipeline
authoritative, (b) it does not provide image<->declaration orphan provenance
(our manifest/`gen-check` would still be needed), and (c) adopting it means a
Go toolchain plus a lua rewrite of the `fxdata.txt`/fxdata-build.py layer.
Revisit if the FX generation layer is ever rewritten; the manifest tooling is
independent of that choice by design.

## Verification (exact tails)

### 1. `git status` before (start of work)

```
On branch main
Your branch is ahead of 'origin/main' by 38 commits.
nothing to commit, working tree clean
4e332f0 render overlays/effects from FX sheets; exact-size telegraphs (monhun-ardu-42n.3)
```

### 2. `make gen-check` PASS on clean tree

```
$ make gen-check
fxdata_manifest: fxdata/manifest.json up to date (19 images, 5 inputs, 5 outputs)
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (29 generated artifacts unchanged)
gen-check exit=0
```

### 3. Synthetic orphan FAILS

Fixture (copied clean fixture + undeclared `synthetic_orphan_4x4.png`):

```
$ python3 tools/fxdata_manifest.py --root build/verify/orphan-demo --check
fxdata_manifest: FAIL: orphan image: images/blocks/synthetic_orphan_4x4.png is not declared by any fxdata/*/Sprites.txt
remedy: run make gen (and commit fxdata/manifest.json)
orphan-fixture exit=1
```

Same detection live in the repo (temporary `images/blocks/zzorphan_4x4.png`):

```
$ python3 tools/fxdata_manifest.py --check
fxdata_manifest: FAIL: orphan image: images/blocks/zzorphan_4x4.png is not declared by any fxdata/*/Sprites.txt
remedy: run make gen (and commit fxdata/manifest.json)
check exit=1
```

And the end-to-end gate catches the absorbed orphan as staleness:

```
$ make gen-check
fxdata_manifest: FAIL: stale generated artifact: fxdata/blocks/Sprites.txt differed from make gen output
fxdata_manifest: FAIL: stale generated artifact: fxdata/fxdata-data.bin differed from make gen output
fxdata_manifest: FAIL: stale generated artifact: fxdata/fxdata.bin differed from make gen output
fxdata_manifest: FAIL: stale generated artifact: fxdata/fxdata.h differed from make gen output
fxdata_manifest: FAIL: stale generated artifact: fxdata/manifest.json differed from make gen output
fxdata_manifest: FAIL: stale generated artifact: src/fxdata.h differed from make gen output
remedy: run make gen and commit the regenerated artifacts
make: *** [gen-check] Error 1
orphan gen-check exit=2
```

After removing the orphan, `make gen` restores byte-identical artifacts and
`make gen-check` passes again (0 tracked-generated-file diffs; see tail above
in section 2, re-run after restore).

### 4. `make test-tools` (tools/tests suite) PASS

```
$ make test-tools
...
test_unincluded_sprites_fails (test_fxdata_manifest.FxdataManifestTests.test_unincluded_sprites_fails) ... ok
----------------------------------------------------------------------
Ran 15 tests in 0.895s

OK
```

Cases: clean pass (and check-writes-nothing), orphan image, missing image,
malformed declaration, duplicate declaration, malformed image name, missing
payload, unincluded Sprites, missing include, directory mismatch, changed
image vs manifest, deterministic sorted record, snapshot verify detects
changed/new/missing artifacts.

### 5. `make test` (host C++)

```
Passed: 104
Failed: 0
========== Total Counts ==========
Total Passed: 702
Total Failed: 0
```

### 6. `make build` (flash/RAM unchanged)

```
Sketch uses 26644 bytes (89%) of program storage space. Maximum is 29696 bytes.
Global variables use 1941 bytes (75%) of dynamic memory, leaving 619 bytes for local variables. Maximum is 2560 bytes.
```

Baseline 26644 B / 1941 B — identical (tooling changes do not enter the sketch).

### 7. `make fxtest-headless` (Ardens present)

```
asset_test PASSED=254 FAILED=0
test_assets: PASS
test_audio PASSED=14 FAILED=0
test_audio: PASS
test_boot PASSED=4 FAILED=0
test_boot: PASS
data_test PASSED=194 FAILED=0
test_data: PASS
parity_test PASSED=660 FAILED=0
test_parity: PASS
perf_test PASSED=5 FAILED=0
test_perf: PASS
fxtest exit=0
```

### 8. `git status` after (intended files only)

```
 M Makefile
 M README.md
 M .gitignore
 M tools/gen.sh
D  images/ArduFontTrimmed_5x6.png
D  tools/text2bmp.py
?? fxdata/manifest.json
?? tools/fxdata_manifest.py
?? tools/tests/... (fixtures + test_fxdata_manifest.py)
```

`git diff --name-only -- fxdata src/fxdata.h src/generated images` reports 0
tracked generated-file diffs (bijection of committed artifacts and `make gen`
output); `git check-ignore` confirms `fxdata/manifest.json` and the fixture
`*.bin` files are committable.

## Acceptance criteria mapping

- `make gen-check` PASS on clean tree + fails on synthetic orphan (fixture
  test proves it): sections 2/3, `make test-tools` case `test_orphan_image_fails`.
- unittest suite runs from the Makefile: `make test-tools` (section 4).
- orphans removed or allowlisted with reasons: both orphans deleted (reasons above).
- `make test`/`fxtest` unchanged and green: sections 5/7; flash/RAM unchanged
  (section 6).

## Notes for orchestrator

- Commit as-is: deletions are staged (`git rm`), new files must be added
  (`fxdata/manifest.json`, `tools/fxdata_manifest.py`, `tools/tests/**`,
  `.gitignore` edit). Fixture `.bin` blobs are only committable because of the
  new `.gitignore` negation.
- Tooling reference patterns harvested from `~/code/CreatureGathererFX`
  (record/assert split, snapshot hashing, fixture tests, `remedy: run make gen`
  diagnostics); no code copied.
- Temporary verification logs lived in gitignored `build/verify/` and were
  removed before completing.
