#!/bin/bash
# FX asset pipeline: author the 4-shade block/font sheets, convert them to the
# SpritesU plus-mask triplane format, then pack fxdata/fxdata.txt into
# fxdata/fxdata.bin + fxdata/fxdata-data.bin and copy the header to src/.
set -e

# Dump the core table dimensions (attack hw/hh/reach, monster hw/hh, whirl
# radii) as JSON for gen-art.py. Host g++; the dumper includes the same
# src/core/game.hpp the firmware uses, so art never duplicates a number.
mkdir -p build
g++ -std=c++17 -O2 -w tools/fxdump.cpp -o build/fxdump
./build/fxdump > build/fxdump.json

# Author source PNGs from mock/game.js shapes + FONT (deterministic). The
# overlay/effect sheets are derived from build/fxdump.json; the same run emits
# src/generated/art_dims.hpp for the render bead + the host dims-drift test.
python3 tools/gen-art.py --dims build/fxdump.json

# Serialize the core content tables (weapon/monster) to packed AVR-layout
# little-endian blobs. Host g++; deterministic output, asserted sizes.
mkdir -p fxdata/tables
g++ -std=c++17 -O2 -w tools/gen-fxtables.cpp -o build/gen-fxtables
./build/gen-fxtables fxdata/tables

# Compile the creature combat JSON (data/skeletons.json + data/creatures/*.json)
# into the packed blob + generated headers. Schema-validated, deterministic;
# runs before fxdata-build.py so the raw_t mhCombat payload exists. The blob is
# a build intermediate: it is packed into the one fxdata/fxdata.bin, never
# flashed separately.
python3 tools/gen-combat.py

# Equipment catalog (bead monhun-ardu-3o9): author the placeholder 4-shade
# sheets into images/equip/, emit src/generated/equip_meta.hpp and the packed
# fxdata/tables/equip.bin catalog. Schema-validated, deterministic; ships
# unused (no render change yet).
python3 tools/gen-equipment.py

# Convert each sprite directory into a Sprites.txt of uint8_t plus-mask blobs.
# convert-sprite.py appends to Sprites.txt (it does not truncate), so a sheet
# renamed in gen-art.py would leave a stale symbol behind: remove the generated
# files first. convert-sprite.py resolves paths relative to tools/, hence ../.
mkdir -p fxdata/blocks fxdata/fonts fxdata/menu fxdata/equip
rm -f fxdata/blocks/Sprites.txt fxdata/fonts/Sprites.txt fxdata/menu/Sprites.txt fxdata/equip/Sprites.txt
python3 tools/convert-sprite.py ../images/blocks -s 4 -o ../fxdata/blocks/
python3 tools/convert-sprite.py ../images/fonts -s 4 -o ../fxdata/fonts/
python3 tools/convert-sprite.py ../images/menu -s 4 -o ../fxdata/menu/
python3 tools/convert-sprite.py ../images/equip -s 4 -o ../fxdata/equip/

# Pack the FX image and emit the generated header.
python3 Arduboy-Python-Utilities/fxdata-build.py fxdata/fxdata.txt
cp fxdata/fxdata.h src/fxdata.h

# Record deterministic image<->fxdata provenance: every images/**/*.png must be
# declared exactly once by an included fxdata/*/Sprites.txt and every declared
# symbol must have an image. Validation runs first, then the manifest is
# replaced atomically; `make gen-check` fails if this file (or any other
# generated artifact) changed.
python3 tools/fxdata_manifest.py

echo "gen.sh: FX data + src/fxdata.h regenerated"
