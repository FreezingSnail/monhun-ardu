#!/bin/bash
# FX asset pipeline: author the 4-shade block/font sheets, convert them to the
# SpritesU plus-mask triplane format, then pack fxdata/fxdata.txt into
# fxdata/fxdata.bin + fxdata/fxdata-data.bin and copy the header to src/.
set -e

# Item table (bead monhun-ardu-prg.2): data/items.json compiled by
# tools/gen-items.py into the packed fxdata/tables/items.bin blob +
# src/generated/items_{data,meta,expect}.hpp (item ids/kinds/heal/stam/sell).
# Schema-validated, deterministic. Runs FIRST: the fxdump host build below
# includes src/core/game.hpp, which includes the generated items_meta.hpp, and
# fxdata-build.py needs the raw_t mhItems payload. gen-zones.py reads the same
# JSON to map zone gather ids onto the item indices.
python3 tools/gen-items.py

# Item-id vocabulary only (bead monhun-ardu-prg.7): src/generated/items_ids.hpp
# carries item::ID_<NAME> + ID_COUNT derived from the same source order, so tools
# and compile-time name tables can use the ids without the packed blob offsets.
python3 tools/gen-items-ids.py

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

# Screen data (bead monhun-ardu-cgz): compile data/screens/*.json into the
# packed fxdata/tables/screens.bin blob + src/generated/screen_meta.hpp (screen
# indices/offsets + action/condition enums). Schema-validated, deterministic.
python3 tools/gen-screens.py

# Quest defs (bead monhun-ardu-me6): compile data/quests/*.json into the packed
# fxdata/tables/quests.bin blob + src/generated/quest_meta.hpp (quest
# indices/offsets + target-kind enums). Schema-validated, deterministic; runs
# before fxdata-build.py so the raw_t mhQuests payload exists.
python3 tools/gen-quests.py

# Armor + skills (bead monhun-ardu-arm.1): compile data/skills.json +
# data/armor.json into the packed fxdata/tables/armor.bin blob +
# src/generated/armor_{data,meta,expect}.hpp (piece/skill ids, offsets, spot
# values). Schema-validated, deterministic; runs before fxdata-build.py so the
# raw_t mhArmor payload exists and before gen-smith.py, which derives each
# piece's smith armor recipe from the same JSON.
python3 tools/gen-armor.py

# Smith upgrade tiers (bead monhun-ardu-4ug): compile data/smith/*.json into the
# packed fxdata/tables/smith.bin blob + src/generated/smith_meta.hpp (upgrade
# indices/offsets + weapon enums). Schema-validated, deterministic; runs before
# fxdata-build.py so the raw_t mhSmith payload exists. arm.1 also derives the
# armor recipe records from data/armor.json into the same blob.
python3 tools/gen-smith.py

# Room-graph data (bead monhun-ardu-fie.3): data/map.json compiled by
# tools/gen-zones.py into the packed fxdata/tables/zones.bin graph blob +
# src/generated/zone_{data,meta}.hpp, plus the 3x 1bpp page-major room layer
# arrays in fxdata/maps/Sprites.txt (declare the mhZones raw_t and the maps
# include in fxdata.txt before fxdata-build). Placeholder PNGs under
# images/maps/ are authored only when missing, so fie.5's refined art survives.
python3 tools/gen-zones.py

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
