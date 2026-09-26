# monhun-ardu-udb — second arena (ridge) + per-room beast home

Status: DONE. Landed with orchestrator trims (see below); no worker commit.

## What landed

- `tools/gen-zones.py` bakes `MONSTER_HOME_ROOM[MONSTER_KIND_COUNT]` (+ the
  count) into `zone_meta.hpp`: per kind, the first room whose `monsterKind`
  matches, else the first monster room, `0xFF` when the map hosts no monster.
  One table read at hunt start instead of a per-room cart scan.
- `src/core/zones.hpp`: `beastHomeRoom(kind)` (table lookup; non-roster kinds
  like pole -> `0xFF`) and `beastHomeSpawn(g, kind)` (override the just-spawned
  beast's x/y with the home room's `monsterSpawn` -> `zoneSpawnRead`). hp/spd/
  FSM/body stay the creature record's. Header comment states the invariant: the
  beast's coords live in its home room's space, no per-room simulation, the
  distance gate keeps it idle off-room.
- `src/app_setup.hpp`: `huntStart` calls `beastHomeSpawn(g, kind)` right after
  `newGame`/`initMonster`, before `loadRoom`.
- `src/core/items.hpp`: gather mask byte-indexed (`gatherMarkPicked`), so the
  32-bit `1 << idx` AVR shift helper is gone; one-bit-per-prop semantics kept.
- `data/map.json`: `ridge` 384x112 (heavy beast, `start` spawn, 4 nodes: herb
  x2 / ore / bug); area gains the east door (376,72,8,24) + `from_ridge` spawn;
  ridge west door (0,72,8,24) -> area.
- Masks/art: ridge mask authored (+ gen-zones placeholder art; bead kcj owns
  the art pass), area mask door idx2 (0,128,255).
- `data/quests/crush_heavy.json`: desc "FELL THE HEAVY / IN THE RIDGE."
- Tests: `tst/zone_test.hpp` pins the ridge record (extents, MONSTER_HEAVY,
  spawns), the area<->ridge round-trip, and the home mapping (heavy->ridge,
  lunge->area, sweep/ravager->area fallback, pole->no home, spawn coords moved,
  hp/spd/FSM untouched).
- Docs: `docs/map-zones.md` room graph + gather table (23 props).

## Budget (measured whole-image)

Order: worker first cut 29606/29696 (90 free, test_hub over board) -> trims:
1. inline single-use readers + drop the gen-validated spawn bounds check: -4 B.
2. byte-indexed gather mask (drops the AVR 32-bit shift helper): -68 B.
3. generated `MONSTER_HOME_ROOM` table (replaces the runtime room scan): -32 B.
Final 29502/29696 (**194 free**), RAM 1920/2560; test_hub 29670/29696 (26 free)
with no extra carve — an `MH_AUDIO 0` carve attempt was a no-op (the suite never
pulls audio) and was reverted.

## Verification (orchestrator full gate)

- `make gen-check` OK; host 6954/0; tools 394 OK.
- 19/19 device suites PASS (incl. the previously-overflowing test_hub).
- `make size`: flash 29502/29696 (194 free), ram 1920/2560.
- `ARDENS=/usr/bin/true make dev-hitboxes` builds.

## Time

Worker ~45 min (read/design ~30, edits + focused gates ~15; gen x2 + device
compiles dominate). Orchestrator review + trims + gates ~40 min.
