# monhun-ardu-ryh.2 — sources: one facing per sheet + packer mirror

## What changed

Every in-scope creature art **source** is now authored east-only; the packer
(`tools/convert-sprite.py`) rebuilds the shipped frames by mirroring each source
column into its west twin, so the packed cart image is byte-identical. Frame
order/layout and `art_dims` are unchanged (no runtime change).

- `tools/gen-art.py`
  - New `flip()` (horizontal mirror) and `compose()` (blk rects -> frame image)
    helpers, plus a `MIRROR_SOURCES` registry keyed by packed symbol
    (`fx<id>`) -> `(east-only source image, packed plan)`.
  - `monster_lunge`/`monster_sweep`/`monster_heavy`: `_beast_sheet()` authors
    the seven east poses and builds the shipped 14 frames as
    `east(0..6) + flip(east(0..5)) + east(6)` (dead reused). Plan:
    `[(i,F) x7, (i,T) x6, (6,F)]`. Removed the `east=False` draw path.
  - `chickenatk`/`bullatk`/`heavyatk`: `_attack_sheet()` authors the east poses
    only and interleaves `flip` for the shipped frames. Removed the per-pose
    `east=False` draws.
  - `head_chicken`/`legs_chicken`/`head_bull`/`hooves_bull`/`tail_heavy`:
    `_zone_part_defs()` now takes the east intact+broken block defs and emits
    the shipped `[E intact, E broken, flip(E intact), flip(E broken)]` order;
    the `_mirror_rect()` bbox-duplication helper is deleted.
  - Writes `images/blocks/layout.json` (packer plan) via `emit_pack_layout()`;
    `check_disk()` now verifies the east-only sources; main returns/uses a
    `sources` map so the shipped in-memory sheet stays for the pixel checks and
    `art_dims` while only the one-facing PNG is written.
- `tools/convert-sprite.py`: optional `plan` (loaded from
  `<srcdir>/layout.json`): each packed frame is `[source_frame_index, mirror]`
  in shipped order; a mirrored frame reads its east column flipped. No plan ->
  unchanged in-order packing (fonts/equip/out-of-scope sheets).
- `tools/fxdata_manifest.py`: `images/blocks/layout.json` added to
  `GENERATED_GLOBS` so `make gen-check` also pins its determinism.

## Exceptions (non-mirror west frames, kept explicit)

The design's exception rule: a west frame that is not the mirror of its east
twin stays authored. Measured on the packed sheets:

- `fxmonster` (ravager): the four alive-frame feet rects (x = 3,11,19,27) are not
  x-mirror-symmetric, so **all four west frames are drawn explicitly** and the
  sheet ships authored in full (no mirror plan registered; the PNG is unchanged).
  Documented on `monster_frames()`.
- `monster_lunge`/`monster_sweep`/`monster_heavy` dead heap: facing-independent
  (drawn once) and asymmetric for lunge/heavy, so the shipped west dead frame
  reuses east dead unflipped (plan source 6, mirror False). Documented on
  `_beast_sheet()`.
- `fxpole` (+ `fxpole_sever/_break/_crack`) and `fxtailspin`/`fxtail`/
  `fxtail_spin`: no facing pair (identity) — out of the mirror plan, packed
  in order unchanged.
- `fxtail` (legacy 18x10) hand-mirrors its west rects and is out of scope;
  left untouched.

## Scope / interfaces

- `MIRROR_SOURCES[symbol] = (PIL.Image east-only source, [(src_idx, mirror), ...])`
- `images/blocks/layout.json` `{"version":1,"sheets":{"<symbol>":[[src,mirror],...]}}`
  (11 sheets): `fxmonster_lunge/_sweep/_heavy`, `fxchickenatk/fxbullatk/fxheavyatk`,
  `fxhead_chicken/fxlegs_chicken/fxhead_bull/fxhooves_bull/fxtail_heavy`.
- One new tracked source: `images/blocks/layout.json`. 11 in-scope source PNGs
  shrink to their east frame(s); `fxmonster_32x24.png` is unchanged.

## Gate (exact tails)

`make gen` then `git diff --stat fxdata/`:
```
 fxdata/manifest.json | 44 ++++++++++++++++++++++----------------------
 1 file changed, 22 insertions(+), 22 deletions(-)
```
`fxdata/fxdata.bin`, `fxdata/fxdata-data.bin`, `fxdata/fxdata.h`,
`src/fxdata.h`, all `src/generated/**` and `fxdata/blocks/Sprites.txt`: **empty
diff**. Verified explicitly:
```
git diff --quiet fxdata/fxdata.bin fxdata/fxdata-data.bin -> UNCHANGED
git diff --stat src/  -> (empty)
git diff --stat fxdata/blocks/Sprites.txt -> (empty)
```
The only `fxdata/` change is `fxdata/manifest.json` (the provenance sidecar that
sha256+size-hashes every source image; the 11 east-only PNGs changed). Approved
by the dispatcher: the east-only sources live in `images/blocks`, so the manifest
must rehash them; the packed cart artifacts are byte-identical.

`make gen-check`:
```
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (163 generated artifacts unchanged)
```

`make test`:
```
Total Passed: 6795
Total Failed: 0
```

`FXTEST_ONLY=test_monster_art make fxtest-headless`:
```
test_monster_art PASSED=180 FAILED=0
P
test_monster_art: PASS
```

`FXTEST_ONLY=test_assets make fxtest-headless`:
```
asset_test PASSED=264 FAILED=0
P
test_assets: PASS
```

`make size`:
```
size: .text=29420 .data=50 .bss=1764
size: flash=29470/29696 (226 free)  ram=1814/2560
```

`make size-line`:
```
size: flash=29470/29696 (226 free)  ram=1814/2560
```

## Size delta

| | before | after | delta |
|---|---|---|---|
| flash | 29470/29696 (226 free) | 29470/29696 (226 free) | **0 B** |
| RAM | 1814/2560 | 1814/2560 | 0 B |

Exact 0 B: art bytes and `art_dims` are unchanged. Matches the checkpoint
(29470, 226 free) and clears the ~150 B wave floor.

## Notes

- No runtime / `art_dims` / frame-order change: `art_dims.hpp` diff is empty.
- Generated artifacts staged as expected; `fxdata/manifest.json` is the only
  `fxdata/` diff (approved).
- `images/blocks/layout.json` is generated by `make gen` (untracked until commit).
- No commit/push (orchestrator owns the wave commit).
