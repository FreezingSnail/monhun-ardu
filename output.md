# monhun-ardu-fie.9 — trim: props+fade flash reclaim

Baseline HEAD b719a23. Worker was cancelled mid-run; orchestrator finished and
verified inline. No commit/push at worker time (orchestrator commits).

## What changed

- `src/core/zones.hpp` — `zonePropRead()` reads the packed 9 B prop record in
  one FX transaction (`mhFxReadBytes`) and unpacks the little-endian ABI
  locally, instead of seven per-field `mhFxRead*` seeks + offset math.
- `src/render.hpp` — `propSheet()` flattened to `sheet ? fxpole : mh_map_tent`
  (sheet ids are 0/1); `drawProps()` hoists the `-camX`/`HUD_H-camY` add once
  per call; `drawFade()` collapsed to a constant full-arena shade-0 `blk`
  (was: height scaled by `fade`, i.e. a growing wipe). HUD strip still spared.
- `tst/fxdatatest/zones_test.hpp` — pins the collapsed fade: any armed tick
  covers the whole arena band, HUD page untouched.

## Verification

```
make test                  Total Passed: 5375 / Failed: 0
make fxtest-headless       17/17 PASS (test_zones 61 asserts, forced image path)
test_perf                  B pUs=6372 pHz=156 lHz=52 lTk=568 rMx=4764 rAv=4573 ram=544
make gen-check             PASS (82 generated artifacts unchanged)
make size                  flash=29272/29696 (424 free)  ram=1780/2560
```

Flash delta vs fie.8: **-20 B** (29292 -> 29272). Perf unchanged (rMx 4764).

## Finding: the 1430 B props+fade attribution was wrong

fie.8 derived "props+fade = 1430 B" as `measure8 (29042) - measure0 (27612)`,
but `measure0` was compiled with `MH_ROOM_BOUNDS=0`, which also removes
per-room bounds, the room runtime doors/heal/menu paths and the props/fade
block (they all live inside that carve). So 1430 B = bounds + room runtime +
props/fade + loadRoom folding, not props+fade alone. The micro-trims above can
only reach the small surface: -20 B.

Remaining map-wave overhead vs cff226d baseline is ~1.8 KB total (27470 ->
29272) spread across per-room bounds integration, room runtime
(doors/spawns/heal/menu/safe-room), the generated zone accessors and the
demo-flow wiring. Locating it precisely needs carve-by-carve whole-image
measurement (LTO clone churn defeats symbol math), i.e. another spike; the
off-path hub/quests/smith/save symbols in the shipping ELF are small
(saveStore 200 B, appNavApply 192 B, upgradeApplyToGame 118 B,
questApplyToGame 78 B, screenFirstRow 42 B), so a shelf carve is not the lever.

## Options (not applied)

- Accept 424 B free.
- Accounting spike: carve variants (bounds / room runtime / zone accessors /
  flow) with whole-image measures, then a targeted refactor of the biggest
  block.
- Revert this bead's fade change if the constant blackout is not wanted
  (costs the 20 B).
