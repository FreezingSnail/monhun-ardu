# monhun-ardu-ngf — gunshield diagonal/stance/fire rasterization

## Status: DONE (inline orchestrator fix; Python visual iteration required)

Owner: "gunsheilds sprites are messed up, the diagonal, stance, and fire
animation".

## Root cause

`tools/gen-equipment.py` drew every filled shape by forward-sampling weapon
space and plotting one pixel per sample (`put(img, *wpt(...))`). At the 45 deg
DIR8 facings the direction vector is `(11,11)/16`, so the mapping is
many-to-one: the gunshield plate and the flail ball fell apart into a
checkerboard with holes, and the 1 px muzzle-flash rays read as scattered
specks. Cardinal facings sample 1:1 and were clean, which is why only the
diagonal/stance (lit plate) and fire (muzzle star) cells looked broken.

## Fix (tools/gen-equipment.py)

- New `fill_weapon(img, facing, u0, v0, color_at)`: iterate **cell** pixels and
  inverse-map each to weapon space
  (`u = (px*dx + py*dy) / ((dx*dx+dy*dy)/16)`, `v = (py*dx - px*dy) / ...`),
  skipping pixels the shape does not cover. Cardinal facings stay pixel-exact.
- `shield()`: one inverse-mapped plate pass (rim `du >= 2` / top+bottom rows,
  face otherwise) + the unchanged 1 px black slot.
- `ball()`: inverse-mapped disc (core `(r-1)^2`, dark rim to `r^2`) — the flail
  shared the same defect class, fixed with the same helper.
- Shot-mode muzzle flash: the three outer rays `(8,1)/(6,-1)/(6,3)` now draw
  `wline(..., w=2)` (chunky star); the short backward ray and muzzle dot stay
  1 px. Only the arrowshot/pointblank active rows change.

## Verification

- `make gen-check` — PASS (regenerated sheets/blobs deterministic; sheet dims,
  layout plan and FX offsets unchanged).
- `make test` — 6877 / 0.
- `make test-tools` — 390 OK (no pinned weapon pixels in the tool tests).
- `FXTEST_ONLY=test_assets` — 264 / 0.
- `FXTEST_ONLY=test_player_art` — 120 / 0 after the documented golden regen
  (`PRINT_GOLDENS=true` capture, table + regen-history note updated).
  Changed cases: 17, 18, 20-25 (flail fractional ball centres) and 29, 31-35,
  39 (gun plate/flash rows + the diagonal flail armor row). Idle/recover rows
  with integer ball/plate centres (15, 16, 19, 26, 30, 36) and all sword cases
  are byte-identical.
- `make size` — unchanged (cart bytes only).

## Eyeball artifacts (build/, untracked)

- `build/scratch/fixed_gun.png` — gun rows 9/22 across the 5 source facings.
- `build/scratch/fixed_flail.png` — flail rows 0/3, solid balls.
- `build/scratch/proto_flash_w2.png` — before/after fire star.

Whole-image deltas: cart sprite bytes change (fxdata-data.bin + equip
Sprites.txt); MCU flash/RAM unchanged.
