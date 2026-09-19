# monhun-ardu-fie.8 — render: procedural dot ground + carve stored-image blit

Baseline HEAD 750f348, tree clean. No commit/push (orchestrator commits).

## What changed

- `src/render.hpp` — new default-off ground carve `MH_ROOM_IMAGE`
  (`#ifndef` / `#define 0`) next to `DEBUG_HURTBOXES`, plus a
  `static_assert(MH_ROOM_IMAGE == 0 || MH_ROOM_IMAGE == 1, ...)`. Under
  `MH_ROOM_BOUNDS` the block now reads:
  - `MH_ROOM_IMAGE 0` (shipping default): `drawArena` (procedural dot field +
    room border, per-room dims) then `drawProps` then `drawFade`.
  - `MH_ROOM_IMAGE 1`: the fie.5 stored-image path exactly as-is (`drawRoom`
    FX-plane streaming + `roomImageInfo`), then `drawProps` + `drawFade`.
  - The image-only code (`roomImageInfo`, `roomAsmDual`, `roomAsmCopy`,
    `drawRoom`) is wrapped in its own `#if MH_ROOM_IMAGE ... #endif`; props
    (`propSheet`, `drawProps`) and `drawFade` stay unconditional inside
    `MH_ROOM_BOUNDS`. `MH_ROOM_BOUNDS` stays 1, so the room graph, doors,
    spawns, heal, per-room bounds, the tent prop and the door fade are all
    unchanged.
  - `renderScene`: `#if MH_ROOM_IMAGE drawRoom #else drawArena(...) #endif`
    followed by the shared `drawProps`; the `MH_ROOM_BOUNDS 0` fallback still
    draws only `drawArena`. No float/double.
- `tst/fxdatatest/zones_test.hpp` — forces `#define MH_ROOM_IMAGE 1` before the
  first `render.hpp` include so the permanent blit pixel evidence survives. All
  existing blob / doors / props / fade / demo-flow assertions unchanged.
- Nothing deleted: `images/*.png`, `data/map.json`, the gen-zones pipeline, the
  `mhZones` blob, `zone_data/zone_meta`, `mh_map_*` layers and `tools/gen-zones.py`
  all stay in the tree / single FX image.

## Interfaces

- `MH_ROOM_IMAGE` (new render.hpp compile-time carve): 0 = procedural dot ground
  (shipping default), 1 = stored room-image blit. `MH_ROOM_BOUNDS` remains the
  separate room-runtime carve (still 1 in shipping).
- No new runtime symbols or generated data.

## Verification

`make size` (shipping, dots default)
```
Sketch uses 29292 bytes (98%) of program storage space. Maximum is 29696 bytes.
Global variables use 1780 bytes (69%) of dynamic memory, leaving 780 bytes for local variables. Maximum is 2560 bytes.
size: .text=29252 .data=40 .bss=1740
size: flash=29292/29696 (404 free)  ram=1780/2560
```
Flash reclaimed vs baseline 29468 = **176 B** (228 -> 404 free). RAM unchanged
(1780).

`FXTEST_ONLY=test_perf make fxtest-headless`
```
B pUs=6372 pHz=156 lHz=52 lTk=568 rMx=4764 rAv=4573 ram=548
perf_test PASSED=5 FAILED=0
P
test_perf: PASS
```
rMx 4764 <= current 5552 (dots path is cheaper than the blit); plane 156 Hz /
logic 52 Hz / ram 548 free.

`make fxtest-headless` (all 17 suites, exit 0)
```
test_assets: PASS   (asset_test PASSED=270 FAILED=0)
test_audio: PASS    (test_audio PASSED=17 FAILED=0)
test_boot: PASS     (test_boot PASSED=4 FAILED=0)
test_combat: PASS   (combat_test PASSED=293 FAILED=0)
test_data: PASS     (data_test PASSED=368 FAILED=0)
test_hub: PASS      (test_hub PASSED=57 FAILED=0)
test_hud: PASS      (test_hud PASSED=17 FAILED=0)
test_menu_art: PASS (test_menu_art PASSED=81 FAILED=0)
test_menu: PASS     (menu_test PASSED=80 FAILED=0)
test_monster_art: PASS (test_monster_art PASSED=111 FAILED=0)
test_parity: PASS   (parity_test PASSED=660 FAILED=0)
test_perf: PASS     (perf_test PASSED=5 FAILED=0)
test_player_art: PASS (test_player_art PASSED=111 FAILED=0)
test_quests: PASS   (test_quests PASSED=50 FAILED=0)
test_screens: PASS  (test_screens PASSED=78 FAILED=0)
test_smith: PASS    (test_smith PASSED=66 FAILED=0)
test_zones: PASS    (zones_test PASSED=61 FAILED=0, 25992 B; forced MH_ROOM_IMAGE 1)
```

`make test`
```
Total Passed: 5375
Total Failed: 0
```

`make gen-check`
```
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (82 generated artifacts unchanged)
```

`git status --short`: only `src/render.hpp` and `tst/fxdatatest/zones_test.hpp`
modified (plus this ledger); no generated drift.

## Budget finding (deviation from the estimated 27600-28200)

Measured whole-image ELFs (same flags as `make size`; scratch builds under
gitignored `build/`, native arduino-cli, no /tmp):

| build | flash | note |
|---|---|---|
| baseline 750f348 (`drawRoom`, `drawArena` dead) | 29468 | shipping before |
| ground blank, props+fade kept | 29042 | isolates `drawRoom` stack |
| **shipping after carve** (`drawArena` + props + fade) | **29292** | this bead |
| baseline minus the whole room-render stack (`drawRoom`+props+fade) | 27612 | fie.8 experiment `build/measure0` |

Derived component sizes: `drawRoom` stack (roomImageInfo + asm + drawRoom) =
29468 - 29042 = **426 B**; `drawArena` = 29292 - 29042 = **250 B**; props+fade =
29042 - 27612 = **1430 B**. Net reclaim = 426 - 250 = **176 B**.

Why the estimate was high: the 1856 B experiment (`build/measure0`, Data 1780 so
`MH_ROOM_BOUNDS` was still 1) removed `drawRoom` **and** props **and** fade. At
baseline `drawArena` was dead code in the `MH_ROOM_BOUNDS 1` image build, so
switching to the dots ground re-links it (250 B); and the bead requires props
(tent) and fade to stay live (1430 B). Under the stated constraints reclaim is
drawRoom(426) - drawArena(250) = 176 B, which is what the tree delivers.

Options if a larger reclaim is wanted (both violate the current bead text, not
applied): drop `drawProps`/`drawFade` from the dots path -> 27612+250 = ~27862 B
(in the estimated range, loses the camp tent once image path is off); drop the
dots ground too -> 27612 B (blank, no motion cue).

## Notes

- No float/double added; dot field / camera / sim paths untouched.
- A host-testable carve check is not feasible: `render.hpp` is device-only (it
  includes `common.hpp`/Arduboy/FX and is never compiled by the host `tst/`
  suite), so the permanent evidence stays the device suite `test_zones`, which
  forces the image path on and still asserts the blit plane bytes; the shipping
  default is pinned by the `static_assert` plus the compile-time `#ifndef`.
- Image pipeline, blob, meta, PNG sources and `gen-check` are untouched.
