# monhun-ardu-ryh.5 — player masks: hurtbox/collide + per-attack weapon hitboxes

## What changed

The player's body box and every weapon attack box now come from a painted mask,
same treatment as the creatures. `images/masks/mh_player_base_16x16.png` is the
authoring source; `tools/gen-hitboxes.py` compiles it into a generated header
that `game.hpp` WEAPON_DEFS and `Player::init` read. Shipped values are
byte-identical (player 16x16, every reach/hw/hh unchanged), so the gate is the
packed weapon-defs blob plus every sim suite.

- **`images/masks/mh_player_base_16x16.png` (new source).** Cell 16x16, one
  facing (east). Three bands: collision yellow (0,0,16,16), hurtbox green body
  (0,0,16,16), hitbox 33 columns = one per weapon attack entry in the mask's
  table order (per weapon: attacks 0..2, special, roll, alt, charge 0..1,
  branches 0..2). Column margins are 40 px x / 5 px y (the gun arrowshot reach 44
  and the sword spin-cut hh 26 set them); 3168x78 px, 1572 B. The bootstrapped
  values are the shipped literals, so the artist can tighten the green body to
  the measured (2,1,12,15) and repaint any arc with no code change.
- **`tools/gen-hitboxes.py`.** Added a player path: the player mask resolves by
  symbol (`PLAYER_SYMBOL`) instead of creature JSON. `derive_player` enforces
  the creature validations the player has: dims vs the cell, one *solid* hurt
  region (green body, no head/appendage — the player has no zones), the body
  box inside the cell+margin, one window per painted hitbox column, and 33
  columns == the attack table count. `cell_rect_to_attack` maps a mask rect to
  the packed `(reach, hw, hh)` (rect centred on the body y; reach = rect ox -
  body centre + hw/2). `_player_header_text` writes the header; the player box
  also lands in `build/hitboxes.json` under a `player` key (gen-combat ignores
  it). The mask's green body origin *is* the art anchor, the same contract the
  creature zone boxes use, so "body box + its art bbox origin" is emitted as
  BODY_OX/OY/W/H (+ COLLIDE_*).
- **`src/generated/player_boxes.hpp` (new generated header).** Emits
  `playerboxes::BODY_{OX,OY,W,H}` (0,0,16,16), `COLLIDE_*`, and
  `playerboxes::ATTACKS[3][11]` (mask-derived reach/hw/hh). Unused storage costs
  0 B: the values are `constexpr`, folded at the use sites.
- **`src/core/game.hpp`.** Includes `player_boxes.hpp` and defines
  `MH_PLAYER_BOX(w, i)` to splice the three mask-derived fields into each
  `Attack` literal; the `Attack`/`Branch`/`ShellDef`/`WeaponDef` structs and
  their 23/27/15/329 static_asserts are untouched. Only the reach/hw/hh numbers
  changed source (all identical values).
- **`src/core/player.hpp`.** `Player::init` reads `playerboxes::BODY_W/H`
  instead of the 16/16 literals.
- **`tools/gen.sh`.** gen-hitboxes now runs before the fxdump host build
  (game.hpp includes `player_boxes.hpp`, and fxdump/gen-fxtables pack the same
  table). Ordered: gen-items -> gen-items-ids -> gen-hitboxes -> fxdump ->
  gen-art -> gen-fxtables -> gen-combat -> ... Unchanged otherwise.

## Interfaces

- `mh::playerboxes::ATTACKS[w][i]` — `{int16_t reach, hw, hh}`; column order per
  weapon: attacks 0..2, special, roll, alt, charge 0..1, branches 0..2. A zero
  box is an all-zero attack (mask column blank).
- `mh::playerboxes::BODY_{OX,OY,W,H}`, `COLLIDE_{OX,OY,W,H}`.
- `MH_PLAYER_BOX(w, i)` macro in game.hpp (host table only).

## Evidence (exact)

- `make gen; git diff --stat fxdata/`:
  ` fxdata/manifest.json | 10 ++++++++++` (1 file changed, 10 insertions) —
  adds the new mask input and the new `src/generated/player_boxes.hpp` output.
  `git diff --stat fxdata/tables/` and `git diff --stat src/generated/ src/fxdata.h`
  are **empty**: the packed `weapondefs.bin` blob and every pre-existing
  generated header are byte-identical.
- `make gen-check`: `fxdata_manifest: PASS (164 generated artifacts unchanged)`
  (was 163; +1 = the new player_boxes.hpp).
- `make test`: `Total Passed: 6815  Total Failed: 0` (unchanged).
- `FXTEST_ONLY=test_player_art make fxtest-headless`:
  `test_player_art PASSED=120 FAILED=0` / `test_player_art: PASS`.
- `FXTEST_ONLY=test_combat make fxtest-headless`:
  `combat_test PASSED=252 FAILED=0` / `test_combat: PASS`.
- `FXTEST_ONLY=test_parity make fxtest-headless`:
  `parity_test PASSED=515 FAILED=145` / `test_parity: FAIL`.
  **Pre-existing, not this bead:** the same suite fails identically
  (515/145, same `s=19 snap/ticks` lines) on a clean aa15ba6 tree
  (`git stash -u`, rerun, `git stash pop`). Reported as-is per BLOCKED honesty;
  the failure is unrelated to the player boxes (identical artifact bytes).
- `make size`: `size: flash=29470/29696 (226 free)  ram=1814/2560`.
- `make size-line`: `size: flash=29470/29696 (226 free)  ram=1814/2560`.
- **Delta: 0 B.** Checkpoint aa15ba6 was 29470/29696 (226 free), RAM 1814/2560;
  this bead is identical. 226 B free >= the ~150 B floor.
- `make test-tools` (extra safety): `Ran 373 tests ... OK`.

## Notes / deviations

- Mask column order follows the bead's parenthetical ("main 0..2, special, roll,
  alt, charge, branches"), i.e. attacks, special, roll, alt, charge 0..1,
  branches — not the WeaponDef struct declaration order. Both the Python
  emitter and game.hpp's `MH_PLAYER_BOX` indices share this one order; nothing
  else consumes a column index.
- "art bbox origin" is emitted as the body box origin (BODY_OX/OY), mirroring
  the epic's zone contract ("zone box origin is also the part-art anchor").
  Reading the sprite's opaque bbox (2,1,12,15) into the header was rejected on
  purpose: gen-hitboxes runs before gen-art, so a sprite-derived value would lag
  one `make gen` pass and break `make gen-check` after any art change.
- The mask is a source (tracked via `fxdata_manifest.py` MASK_GLOBS), never
  shipped, never rewritten by `make gen`. Not committed by the worker.
