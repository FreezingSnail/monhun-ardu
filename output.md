# monhun-ardu-fie.11 — Spike: single-interactive-part ceiling

Measurement only. No shipped data/code change. Baseline HEAD 43ed644, main tree
clean before and after; `make gen-check` PASS. All builds used the shipping
flags (`-mcall-prologues -mrelax -DMH_NO_USB`, FQBN
`arduboy-homemade:avr:arduboy-fx`) and the packaged
`avr-gcc/7.3.0-atmel3.6.1-arduino7/bin/avr-size`. Flash = `.text + .data`.

## Method / deviations

- `git worktree add --detach build/spike-zones/monhun-ardu HEAD`. **Nested one
  level** (not `build/spike-zones` itself): arduino-cli requires the sketch
  folder basename to equal the main `.ino` name (`monhun-ardu`), else
  `Can't open sketch: main file missing from sketch: .../spike-zones.ino`.
- `Arduboy-Python-Utilities/` is gitignored (`.gitignore:34 Arduboy*`) and is
  absent from a fresh worktree, so `tools/gen.sh` failed at `fxdata-build.py`;
  symlinked it from the main tree into the worktree.
- After any change that shifts `combat.bin` size, `tools/gen.sh` must run
  **twice**: `gen-zones.py`/`gen-equipment.py` read the *existing*
  `fxdata/fxdata.h` for symbol offsets (gen-zones.py `load_fxdata_symbols`), so
  the first pass still sees the old image and the meta `static_assert`s fail on
  the first build. Second pass converges (normal case: committed `fxdata.h` is
  already current, hence single-pass `gen-check`).
- `V2` is a bounded code experiment (resolver/seed/disable edits), not a
  mergeable implementation; the sketch compiles, tests were not run.

## Per-variant build command

```sh
AVRSZ=$HOME/Library/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin/avr-size
arduino-cli compile --fqbn "arduboy-homemade:avr:arduboy-fx" --optimize-for-debug \
  --output-dir DIST \
  --build-property "compiler.cpp.extra_flags=-mcall-prologues -mrelax -DMH_NO_USB ${EXTRA}" \
  --build-property compiler.c.extra_flags="-mrelax" \
  --build-property compiler.c.elf.extra_flags="-mrelax"
$AVRSZ -A DIST/monhun-ardu.ino.elf
```

## Results

| Variant | flash | Δ vs 29258 | RAM | cart image | kept / lost |
|---|---:|---:|---:|---:|---|
| V0 baseline | 29258 | 0 | 1780 | 204153 | all |
| V1 data-only 1 zone/beast (keep head) | 29258 | **0** | 1780 | 204117 (−36) | head kept; appendage gone on lunge/sweep/ravager (legs/hooves/tail break+disable); `ZONES_COUNT` 11→8 |
| V1b = V1 + drop dead `p_enraged` | 29206 | **−52** | 1780 | — | same parts; `HAS_GUARD_ZONES`→false, `HAS_SIMPLE_GUARDS`→true |
| V2 single-candidate resolver+seed+disable (approx) | 28886 | **−372** | 1780 | — | one part slot (needs heavy/pole remap to slot 0); whole zone machinery otherwise kept |
| V2a resolver branch only | 28974 | −284 | 1780 | — | as above |
| V3a data-strip all zones | 27890 | **−1368** | 1780 | combat.bin 1019→872 | `HAS_ZONES`→false; STAGGER + MULTI_WINDOW still true |
| V3b `-DMH_COMBAT_PARTS=0` (upper bound) | 27774 | **−1484** | 1780 | — | zones + multi-window + stagger all carved |
| V4 zone-art overlay carve (`-DMH_ZONE_ART=0`) | 28868 | **−390** | 1780 | — | hit/pool/break mechanics kept; loses broken/intact part overlays (legs/hooves/tail/head art) |

Decomposition from the measurements:

- Whole zone machinery (`HAS_ZONES` false): **−1368**; of that the zones-broken
  guard + simple-guard fast path is **−52**, so zone records/cache/resolve +
  pole/overlay paths ≈ **−1316**.
- Extra to reach the full parts carve (`MH_COMBAT_PARTS=0`): **−116**
  (multi-window + stagger code).
- RAM is **unchanged in every variant** (1780): `CombatState` is a fixed
  83 B / 2-slot layout; removing zones/data does not shrink `.bss`.
- Note: `flash` does not include the FX cart. V1 shrank the cart by only 36 B
  (36× faster: 3×12 B zone records); the blob is flashed separately, so
  data-only zone edits move **no** device flash.

## Recommendation

**Single-part specialization is not an honest lever.** Data-only single-zone is
exactly **0 B** of device flash (−36 B cart). A real body+1-part refactor
(current bounded approximation: one resolver candidate, one seed, one
disable-check) is only **−372 B**, i.e. ~25% of the full −1484 carve, and still
requires the entire zone resolve/cache/render stack plus remapping heavy/pole's
single zone to slot 0, collapsing the two broken bits, and updating render,
`static_assert`s and `tst/`. Owner keeps the one interactive part, so the
−1368/−1484 numbers are unreachable.

The honest smaller levers (no mechanic loss):

- **V4 part-art overlay carve −390 B** — wrap the `drawZonePart` block in
  `src/render.hpp` (≈ lines 708–721) behind a `MH_ZONE_ART`/`HAS_ZONES`-style
  carve. Hit tests, pools, break bits, disables and the stagger meter all stay;
  only the intact/broken part overlay art is dropped. Trade: the `part_break`
  visual cue is gone (owner-visible), and pole break art is separate and stays.
- **Guard data cleanup −52 B** — with a single zone per beast the
  `p_enraged` `zonesBroken:["appendage"]` guard is dead data; removing it flips
  `HAS_GUARD_ZONES` false / `HAS_SIMPLE_GUARDS` true (fast guard path).
- V4 + guard cleanup ≈ **−442 B** with the one-part mechanic intact.

For the remaining headroom the levers are elsewhere: full
zones/multi-window/stagger carve is −1484 (rejected), so shelf/audio/player-
feature carves are the larger honest targets. Exact commands above; V2 edits
were reverted.

## Hygiene

- `git worktree remove --force build/spike-zones/monhun-ardu`; parent
  `build/spike-zones` removed. `git worktree list` shows only main +
  the pre-existing `mh-baseline`.
- Main tree `git status --short` clean after removal; `make gen-check` →
  `fxdata_manifest: PASS (82 generated artifacts unchanged)`.
- No /tmp, no float, no generated-file hand edits in the main tree. No commit.

---

# NOTE — flash reclaim plan (owner decision, 2026-09-19)

Decision: **keep at least one interactive/breakable part per beast** — it is a
core mechanic and does not get carved. Consequence of `fie.11`: parts are FX
cart data, so reducing two zones to one per creature is worth **0 B of MCU
flash**; a code specialization to a single part slot would reclaim only ~372 B
and is not worth the churn. The zone framework stays as-is.

Measured levers (shipping 29258/29696, 438 B free):

| # | lever | reclaim | cost |
|---|---|---:|---|
| 1 | dead guard-zone data cleanup | ~52 B | none |
| 2 | shelf carve: hub/quests/smith + EEPROM save | ~400-700 B est. | none on demo path; measure first |
| 3 | part-art overlay carve (`drawZonePart`+`partDraw`) | ~390 B | broken-part visuals lost; mechanic kept |
| 4 | `MH_AUDIO=0` | 320 B | all cue tones lost |
| 5 | player-feature carves (charge/sheathe/stances/riposte/whirl/gun reload) | 100-500 B each | gameplay trade, per-feature spike |

Not levers: zone count/data (0 B), full zone strip (-1368 B but kills the
mechanic), multi-window/stagger/guard (~116 B), per-room bounds (~250 B).

Planned order: 1 + 2 first (no gameplay loss), then decide 3 vs 4 vs 5 against
the next feature's budget (vx2 art / equipment 05x).
