#!/bin/bash
# FX asset pipeline: author the 4-shade block/font sheets, convert them to the
# SpritesU plus-mask triplane format, then pack fxdata/fxdata.txt into
# fxdata/fxdata.bin + fxdata/fxdata-data.bin and copy the header to src/.
set -e

# Author source PNGs from mock/game.js shapes + FONT (deterministic).
python3 tools/gen-art.py

# Serialize the core content tables (weapon/monster) to packed AVR-layout
# little-endian blobs. Host g++; deterministic output, asserted sizes.
mkdir -p build fxdata/tables
g++ -std=c++17 -O2 -w tools/gen-fxtables.cpp -o build/gen-fxtables
./build/gen-fxtables fxdata/tables

# Convert each sprite directory into a Sprites.txt of uint8_t plus-mask blobs.
# convert-sprite.py resolves paths relative to tools/, hence the ../ prefixes.
mkdir -p fxdata/blocks fxdata/fonts
python3 tools/convert-sprite.py ../images/blocks -s 4 -o ../fxdata/blocks/
python3 tools/convert-sprite.py ../images/fonts -s 4 -o ../fxdata/fonts/

# Pack the FX image and emit the generated header.
python3 Arduboy-Python-Utilities/fxdata-build.py fxdata/fxdata.txt
cp fxdata/fxdata.h src/fxdata.h

echo "gen.sh: FX data + src/fxdata.h regenerated"
