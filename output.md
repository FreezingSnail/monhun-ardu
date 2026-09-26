# monhun-ardu-0ue — Demo: area 512x112 + hunt spawns at the room entrance

## Status

DONE (with one documented deviation, below). No commit/push (orchestrator
commits). Working tree holds the staged-together generated set:
`make gen` + `make gen-check` green, `fxdata.h == src/fxdata.h`.

## Files

- `images/maps/mh_map_area_512x112.png` (new; 512x112 RGBA). Old 384x112 art at
  (0,0); columns x=384..511 copy old column x=383. Old 384 PNG deleted.
- `images/masks/mh_map_area_512x112.png` (new; 512x448 = 4 mask bands). Old mask
  rects preserved, ridge door rect moved (376,72,8,24) -> (504,72,8,24); old mask
  deleted. (Required: a mask matching the room's W/H is what sources the area's
  prop/gather/door/heal geometry — `data/map.json` has behaviour only.)
- `data/map.json` — area `w` 384->512, image path -> `_512x112.png`,
  `from_ridge` spawn (360,80) -> (488,80). Ridge/h/camp/cavern/monster spawn
  unchanged.
- `src/demo_menu.hpp` — `demoHomeSpawn` (renamed in comments to the ENTRANCE
  spawn): area -> `SPAWN_AREA_FROM_CAMP` (8,80), ridge ->
  `SPAWN_RIDGE_FROM_AREA` (8,80); camp/cavern unchanged. `demoLaunch` drops the
  28-px-west placement + post-load `updateCamera`; hunter stays on the spawn
  record via `loadRoom`, beast at its home monster spawn; stowed-weapon start
  kept.
- `src/render.hpp` — **deviation** (see below): `roomImageInfo` now returns only
  the image base; `drawRoom` takes the blit stride/extent from `Game::roomW/H`
  (set by `loadRoom` from the same zone data as the meta `ROOM_*_W/H`) instead of
  a per-room width arm. Needed to hold the shipping flash anchor.
- `tst/demo_menu_test.hpp` — `demoHomeSpawn` -> entrance pins; launch -> hunter at
  the entrance spawn / beast at its home monster spawn; the old "on-screen at
  spawn" pin replaced by "present after a tick + hunter-beast x distance >
  `SCREEN_W`" (off-screen approach), per design C.
- `tst/zone_test.hpp` — area roomW 384->512; area camera 256->264 (inside the new
  384 max); area->ridge door probe (376->504) + from_ridge spawn (360->488);
  clamp pins 368->496 and 384->`zone::ROOM_AREA_W`, names 384x112->512x112.
- `tst/world_test.hpp` — area camMaxX 256->384 (512-128).
- `tst/fxdatatest/zones_test.hpp` — area-view comment 512x112; `roomImageInfo`
  selector block updated to the img-only signature (extents pinned by the
  roomBoundW/H==meta block + layer reads below).
- `docs/map-zones.md` — area 512x112, ridge door (504,72,8,24), entrance spawns.
- `README.md` — demo section (entrance spawn, off-screen approach, area 512x112);
  status snapshot: shipping flash 29658 (38 free), host tests 7106, FX image
  2,994,688 B / room images 50 KB, zones suite 100 (see deviations).
- Generated (staged together): `src/generated/zone_{data,meta}.hpp`,
  `fxdata/fxdata*.{h,bin}`, `fxdata/maps/Sprites.txt`,
  `fxdata/tables/{zones,cards,screens}.bin`, `fxdata/manifest.json`, `src/fxdata.h`.

## Verification

- `make gen` (twice; zone_meta resolves image offsets from the pre-pack header,
  so the second pass converges) then `make gen-check`:
  `fxdata_manifest: PASS (217 generated artifacts unchanged)`.
- `make test`: `Total Passed: 7106  Total Failed: 0`.
- `FXTEST_ONLY="test_zones test_data test_hub test_screens test_quests test_monster_art" make fxtest-headless`:
  - `test_data  PASSED=356 FAILED=0`
  - `test_hub   PASSED=86  FAILED=0`
  - `test_monster_art PASSED=182 FAILED=0`
  - `test_quests PASSED=110 FAILED=0`
  - `test_screens PASSED=214 FAILED=0`
  - `test_zones PASSED=100 FAILED=0`
  - RAM audit all OK (test_zones 2024 B, test_screens 2062 B, ...).
- `make demo ARDENS=/usr/bin/true`: `demo size: flash=23332/29696 (6364 free)  ram=1674/2560`.
- `make size-line` (shipping): `size: flash=29658/29696 (38 free)  ram=1867/2560`.

## Deviations

1. **Shipping flash 29658, not 29666.** The area's new 512 width is unique among
   the room arms, so keeping per-room `w` in `roomImageInfo` re-materialises the
   512 immediate into `drawRoom`'s inlined copy: measured **+4 B** (29666 ->
   29670; `avr-nm` diff: only `drawRoom` grew, 0x12e -> 0x132). Reordering the
   switch arms cannot restore the old area/ridge 384 sharing (all 6 orders
   measured >= 29670). To meet the "flash unchanged / no regression" intent, the
   blit stride/extent now come from the loaded room record (`Game::roomW/H`, set
   by `loadRoom` from the same zone record as the meta constants) and the
   selector returns only the image base: measured **29658 (-8 vs the 29666
   anchor)**. Behaviour is identical in every reachable state (hub/screens replace
   the scene before a room is loaded; `zones_test` loads every room before
   blitting). `tst/fxdatatest/zones_test.hpp`'s selector block was updated to the
   img-only signature; its extents are still pinned by `roomBoundW/H == meta`.
2. **README suite counts**: updated zones 108 -> 100 (the removed extent asserts)
   and the total accordingly; `test_quests` reports 110 where the README said 112
   — pre-existing README drift (untouched). Full 19-suite gate is the
   orchestrator's, per instructions.
3. **Mask PNG** changed as well as the art PNG: the design lists only the map
   image, but the area room is mask-sourced (`images/masks/`), so the door-rect
   move and the rename are impossible without it.

## Wall time

- worker (image authoring + regen + host/device gates + demo/size + docs): ~32 min
  (first edit ~19:02, report 19:34).
