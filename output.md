# monhun-ardu-9kn — Room art: wire cavern/ridge + enable the stored-image ground

STATUS: DONE (fits; no BLOCKED). Shipping ground is now the stored room image
for all four rooms; perf gate stays green.

## What changed

### render.hpp
- `roomImageInfo(roomId, img, w, h)`: 4-way switch — camp/area/cavern/ridge map
  to `mh_map_*` + `zone::ROOM_*_W/H` (previously only camp, all else area, so
  cavern/ridge would have blitted the area art). `default` (pre-room) -> area.
  Trim (c): the if-chain measured **6 B smaller** than a 4-entry PROGMEM
  record table (29666 vs 29672), so the switch stays.
- Trim (a): dropped the `v == 0` fast path + `roomAsmCopy`. `ROOM_ROW_COEF[0]`
  is now `1`; the split reader always streams 8 pages. At `v == 0` `mul b,1`
  gives `r0 == b`, `r1 == 0`; `drawRoom` passes the low page as the r0 (`X`)
  dest and the same low page as the r1 (`Z`) dest, so `r0` carries the whole
  byte into page `q+1` and its `r1 == 0` OR is a no-op. Costs one dummy page
  read per plane at `camY & 7 == 0` (q == 7 -> `dummy`). `roomAsmCopy` was
  referenced only by `drawRoom` (not the card blit, despite the stale comment),
  so it is deleted.
- Trim (b): dropped `drawRoom`'s rx/ry re-clamp. `renderScene` already clamps
  `camX/camY` to `camMaxX/Y` = `roomBoundW/H - {SCREEN_W, ARENA_H}`, and
  `roomBoundW/H == the image W/H` for every shipped room; `test_zones` now pins
  that equality.

### Makefile
- `-DMH_ROOM_IMAGE=1` added to `SIZE_FLAGS` (build/mini/size/debug) and to the
  inline dev + dev-hitboxes flag strings.

### Tests (tst/fxdatatest/zones_test.hpp)
- Selector pins for all four rooms: `roomImageInfo` img == `mh_map_*`, w/h ==
  `ROOM_*_W/H`.
- Camera-window proof (trim b): after `loadRoom`, `roomBoundW/H == image W/H`
  and `camMaxX` pins for camp/area/cavern/ridge.
- Cavern blit pin (3b): v == 0 copy at the 256 px stride on plane 2 — fb pages
  1..7 byte-equal the cavern layer-2 source pages at column 64, page 0 (HUD)
  untouched. Existing camp (v==0) + area (v==4 split) pixel pins kept.

### Docs
- README: shipping row 29634/62 -> 29666/30 free, RAM 1867; perf row 3412 ->
  4280 µs (image ground); ground description (stored image, `drawArena` stays as
  the `MH_ROOM_IMAGE=0` carve); host 7000 / device 2158, zones 108; history
  (`dap` landed, `9kn` +50 B) and free-flash challenge line.
- docs/map-zones.md: "Shipping render (monhun-ardu-9kn)" note in the room-layer
  section.
- docs/feel-design.md: the prg.1 ground-dot rationale corrected (it was the
  shipping ground then; `9kn` switched shipping to the stored images).

## Verification

### make size-line (shipping, `-DMH_ROOM_IMAGE=1`)
```
Sketch uses 29666 bytes (99%) of program storage space. Maximum is 29696 bytes.
Global variables use 1867 bytes (72%) of dynamic memory, leaving 693 bytes for local variables. Maximum is 2560 bytes.
size: flash=29666/29696 (30 free)  ram=1867/2560
```
- Delta vs HEAD 838e32f baseline (29616/80 free): **+50 B**. Flag alone was
  measured at +136 B; trims (a)+(b) recovered 86 B. Fits with 30 B free.

### Perf gate with the image flag (`FXTEST_ONLY=test_perf`, `-DMH_ROOM_IMAGE=1`)
```
B pUs=6351 pHz=157 lHz=52 lTk=164 rMx=4280 rAv=3709 ram=505
perf_test PASSED=5 FAILED=0
```
- rMx 4280 <= 7407, pHz 157 >= 135, lHz 52 >= 45. All inside.

### Touched device suites (default test flags)
```
test_boot   PASSED=4   FAILED=0   (globals 1187 B)
test_hub    PASSED=86  FAILED=0   (globals 1316 B)
test_perf   PASSED=5   FAILED=0   (globals 1798 B)  rMx=3408 pHz=157 lHz=52
test_screens PASSED=214 FAILED=0  (globals 2062 B)
test_zones  PASSED=108 FAILED=0   (globals 2024 B)
```
- test_zones note: 108 (was 77) with the new selector/camera/cavern pins.

### Other gates
- `make gen-check`: PASS (217 generated artifacts unchanged; header sync OK).
- `make test` (host): 7000 passed / 0 failed.
- `clang-format --dry-run --Werror`: clean on render.hpp + zones_test.hpp.

## Wall time
- Worker (implement + measure + suites + docs): ~55 min.

## Notes
- No `make gen` data change; generated sets untouched.
- mock/ + parity untouched; no commit/push (orchestrator commits).
