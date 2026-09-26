# monhun-ardu-ve2 — demo fixes: beast room presence + clearer transition + dev hurtbox zones

Status: DONE (orchestrator inline; full gate green).

## What landed

- **Beast presence** (`beastHere`, `src/core/zones.hpp`): a hunt beast (carcass
  included) exists only in its home room. Gates: per-tick sim + target
  (`stepWorldBody`, `updateActiveTarget`), the beast draw (`drawMonster`) and
  the HUD monster bar (`target.alive`). A monster room that is not the hunt
  beast's home (the ridge while a lunge hunt runs) now reads empty instead of
  showing the chicken frozen at its area coordinates; `camp`/`cavern` never
  host it. Non-roster kinds (home `0xFF`, e.g. the pole) keep the legacy
  any-monster-room behaviour.
- **Door transition** (`world.hpp` / `render.hpp`): `Game::fade` is now the
  arrival toast (`ROOM_TOAST_TICKS` 40); the black wipe covers the first
  `FADE_TICKS` 12 (was 4 — easy to miss) of the whole arena band, HUD spared.
  The room art names the place.
- **Dev hurtbox zones** (`make dev-hitboxes`): the wire overlay draws the
  beast's **three hurt boxes** — body, head and appendage (from the cached zone
  slots) — with the zone offsets mirrored through the new
  `combatZoneOffsetX` (`src/core/combat.hpp`), the same helper
  `combatZoneContains` uses, so the wire can never drift from the tested rect.
  The target carves `MH_CARD_OFF` (detail cards ~1.4 KB, irrelevant to combat
  feel); `make dev` keeps them.
- Tests: host `zone_test` "beast presence" block; `monster_test` heavy blocks +
  device `monster_art` `setupBeast` load the beast's home room; device
  `zones_test` pins the 12-of-40 wipe + toast decay.
- Docs: `docs/map-zones.md` (presence + transition), `docs/creature-framework.md`
  (dev wire overlay + the carve).

## Budget (honest)

- Shipping **29660/29696 (36 free)**, ram 1920; `dev-hitboxes` 28218 (1478 free,
  carved); `make dev` builds.
- The room-name banner (HUD lane, `fxroom` 24x8 sheet) is **budget-gated**: it
  costs 46 B and shipping is already below the ~150 B reserve, so the sheet is
  authored + committed but not drawn. Bead `monhun-ardu-dap` (trim wave) lands
  it after reclaiming bytes.
- Measured trim leads are in `monhun-ardu-dap`: weaponSheet nested switch 114 B
  (table variants measured *worse*: u32 29704, packed u24 29716), banner 46 B,
  MAP page-pick ~30 B, presence caching ~16 B.

## Verification (orchestrator full gate)

- `make gen-check` OK; host 6973/0; tools 411 OK; 19/19 device suites PASS;
  `make size` 29660/29696 (36 free); `make dev-hitboxes` builds (28218/1478
  free); `make dev` builds.
