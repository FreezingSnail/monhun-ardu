# monhun-ardu-ngf — gunshield rasterization + player-sized shield with gunport

## Status: DONE (inline orchestrator fix; Python visual iteration required)

Owner, two passes:
1. "gunsheilds sprites are messed up, the diagonal, stance, and fire
   animation" — the diagonal cells were punched with holes.
2. "doesnt look right, should be a big sheild, player sized with gunport in
   middle" — the plate was a 6x15 bar; it should read as a shield.

## Root cause (pass 1)

`tools/gen-equipment.py` drew every filled shape by forward-sampling weapon
space and plotting one pixel per sample. At the 45 deg DIR8 facings the
direction vector is `(11,11)/16`, so the mapping is many-to-one: the gunshield
plate and the flail ball rasterized as a checkerboard with holes, and the 1 px
muzzle-flash rays read as scattered specks. Cardinal facings sample 1:1 and
were clean — hence only diagonal/stance/fire looked broken.

## Change (tools/gen-equipment.py)

- New `fill_weapon(img, facing, u0, v0, color_at)`: iterate **cell** pixels and
  inverse-map each to weapon space, skipping uncovered pixels. Cardinal facings
  stay pixel-exact. Used by `ball()` (flail shared the defect).
- `shield()` rewritten to the owner's spec: a **portrait 12x16 plate**
  (player-sized 16x16 body) anchored along the facing vector instead of rotated
  in weapon space, with a dark rim, light/white face and a **4x4 gunport**
  (1 px dark ring, black hole) on the barrel line.
- Barrel/flash redrawn to fire through the port: idle/recover/stance/startup
  show the barrel through the port; the shot active rows draw the barrel out
  past the plate front plus the chunky 2 px muzzle star (with a 1 px shot glow
  trailing back to the port, which also keeps bright ink inside the cell-space
  box the tool test pins).

## Verification

- `make gen-check` — PASS (regenerated sheets/blobs deterministic; sheet dims,
  layout plan and FX offsets unchanged).
- `make test` — 6877 / 0. `make test-tools` — 390 OK.
- `FXTEST_ONLY=test_assets` — 264 / 0.
- `FXTEST_ONLY=test_player_art` — 120 / 0 after the documented golden regen
  (`PRINT_GOLDENS` capture; regen-history note extended).
  Changed cases: 27-36 (the whole gun block — every case draws the plate or the
  shot art). Flail and sword rows are byte-identical to the previous commit.
- `make size` — unchanged (cart bytes only; 29182/29696, 514 free).

## Eyeball artifacts (build/, untracked)

- `build/scratch/fixed_gun.png` / `fixed_flail.png` — first pass (solid
  diagonal plate/ball).
- `build/scratch/bigshield_portrait.png` — final in-game composite, 8 facings
  x idle/stance/shot/shove.
