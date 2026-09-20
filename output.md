# monhun-ardu-feel.22 — engine: items + gathering (sheathed gather/use, herb heal)

HEAD at start: `e0dc697` (feel.21), clean tree. No commit/push (orchestrator commits).

## Core

- `src/core/game.hpp`
  - `PState` appends `PS_GATHER`, `PS_ITEM` (existing 0..7 values unchanged).
  - `Game` appends `uint8_t items[ITEM_COUNT]` + `uint16_t gatherMask`;
    `Player` appends `uint8_t itemNode` (all appended last: existing field
    offsets/sizes unchanged, parity hash fields unaffected).
  - constants: `ITEM_HERB 0`, `ITEM_COUNT 1`, `ITEM_NODE_NONE 0xFF`,
    `GATHER_TICKS 40`, `ITEM_USE_TICKS 40`, `HERB_HEAL 20`.
- `src/core/items.hpp` (new): node query/depletion + inventory verbs.
  - `gatherNodeAt` scans the active room's prop range for an un-picked
    `gatherItem` node overlapping the hunter body (folds to -1 when the room
    runtime is carved out); `gatherNodeDepleted`; `tryStartGather`;
    `applyGather` (mark node, add `gatherYield` to `items[gatherItem-1]`, spark);
    `startItemUse`; `applyItemUse` (heal exactly 20 clamped at hpMax, decrement).
  - `static_assert(zone::PROPS_COUNT <= 16)` (u16 mask = one bit per prop record).
- `src/core/player.hpp`
  - `init` clears `itemNode`; `initGame` resets `items[]` + `gatherMask`
    (newGame reset; `loadRoom` deliberately untouched).
  - sheathed A (idle): `tryStartGather` wins over draw; else the existing draw.
  - sheathed B hold at `HOLD_TICKS`: `startItemUse` instead of `enterStance`.
  - `PS_GATHER`/`PS_ITEM` shared rooted case: move input cancels; completes
    atomically at the window end (cancel never half-applies node/inventory);
    `playerHurt`'s existing `state = PS_IDLE` is the damage cancel.
- `src/core/world.hpp`: successful `tryHeal` latches `player.bLocked = true`, so
  the tent press cannot also start a herb use on the same hold (clears on B
  release). Heal-rect press therefore wins.

## Render (`src/render.hpp`)

- `drawProps`: a `gatherItem` prop draws a procedural 3-shade plant (dark stem /
  light leaves / white flower) instead of its sheet; the flower grows taller as
  the prompt marker while the hunter body overlaps the node; a picked node draws
  nothing. No new art sheet.
- `drawHud`: herb count in the free `x=62..66` HUD lane (1 px stalk glyph +
  one digit; demo total is 9, so one digit always fits). Uses `drawNumber`
  rather than `hudNum`: both render the white font at y=1 identically for one
  digit, and `drawNumber` is already out-of-line (measured -52 B vs forcing
  `hudNum` out-of-line).
- `drawUseBar` (called in `renderScene` under `MH_ROOM_BOUNDS`, before the
  fade): a fixed 32x4 `hudBar` near the top of the arena, fill = `p.t` /
  window (`GATHER_TICKS` white, `ITEM_USE_TICKS` light).

## Audio (`src/audio.hpp`)

- `CUE_GATHER` / `CUE_EAT` appended (cue table 12 -> 14 rows, 16 B).
- `AudioState.itemHerb` snapshots `items[ITEM_HERB]`; `audioUpdate` fires
  gather on inventory up / eat on inventory down through the existing edge
  detector. No new Game event field, no blocking tones.

## Tests (permanent, native)

- Host `tst/gather_test.hpp` (co-located, added to `tst/main.cpp`): gather
  completes + depletes + adds yield (1 and 2); movement cancels; damage cancels;
  depleted node inert (A draws instead); `newGame` resets mask+inventory while
  `loadRoom` keeps nodes; item use heals exactly 20 and clamps at hpMax; zero
  herbs = no use; movement cancels item use; unsheathed B hold stays stance;
  heal-rect B press does not also eat.
- Device `tst/fxdatatest/hud_test.hpp`: 0 herbs = clear x62..66, 8 herbs = glyph
  ink + stalk, gather/item bars pin the 30-px fill (plane 1).
- Device `tst/fxdatatest/audio_test.hpp`: inventory-up -> `CUE_GATHER`,
  inventory-down -> `CUE_EAT`.
- `zones_test.hpp` already pins the node records (feel.21) and the prop draws
  still pass; unchanged.

## Verification (tails)

```
make gen-check   -> fxdata_manifest: PASS (82 generated artifacts unchanged)
make test        -> Total Passed: 6032 / Total Failed: 0
make test-tools  -> Ran 209 tests ... OK
make fxtest-headless (full, all 16 suites PASS)
  test_perf: B pUs=6366 pHz=157 lHz=52 lTk=452 rMx=4768 rAv=4540 ram=686
  (gates: pHz 157>=135, lHz 52>=45, rMx 4768<7407, ram 686>=300)
make size
  size: .text=28548 .data=28 .bss=1599
  size: flash=28576/29696 (1120 free)  ram=1627/2560
```

## Size delta (baseline `e0dc697`: flash 27572, ram 1622)

- **flash +1004 B** (target <=900 B; over by 104 B), **RAM +5 B**
  (`items[1]` + `gatherMask` 2 + `itemNode` 1 + `AudioState.itemHerb` 1).
- Measured breakdown (avr-nm whole-image, LTO): `updatePlayer` +448 (gather/item
  input + rooted case), render inlined into `main` +384 (plant + herb indicator
  + bar), `zonePropRead` now out-of-line +94, `hudBar` clone -> shared +42,
  `mhCueTable` +16, `initGame` +16.
- Trims already applied: merged the two rooted switch cases; merged the prompt
  into the flower (dropped 2 `blk` sites); single `hudBar` call in `drawUseBar`;
  `drawNumber` instead of forcing `hudNum` out-of-line (-52 B). Tried and
  rejected (all grew the image): noinline `zonePropRead`/`drawProps`/
  `gatherNodeAt`/`applyGather`/`applyItemUse`/`tryStartGather`, direct-int rect
  overlap, splitting `drawGatherProp`.
- Remaining headroom is 1120 B free; the build fits. If the 900 B target is a
  hard gate, the only clean lever left is carving the whole item/gather verb out
  of an image that does not need it (parity/hub already carve `MH_ROOM_BOUNDS`),
  which does not help the shipping image.

## Notes

- `test_parity` stays frozen and out of the default gate; not touched.
- `make gen-check` regenerated the FX image with no artifact drift (no data
  changes this bead).
