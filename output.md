# monhun-ardu-fie.4 — Core: room runtime — per-room bounds, doors, spawns, monster persistence, heal

Status: **PASS** (not committed; orchestrator commits)

## What changed

- `src/core/game.hpp`:
  - `MH_ROOM_BOUNDS` carve (default 1), same pattern as `MH_SHEATHE`. The
    parity/hub images fold the whole room runtime back to legacy `WORLD_W/H`;
    shipping/perf keep runtime bounds.
  - `camX/camY` widened `uint8_t` -> `int16_t` (384 px room needs CAM_MAX_X 256).
  - New room-runtime fields on `Game`: `roomW/roomH`, `roomId`,
    `roomMonsterKind`, `roomFirstDoor/roomDoorCount`, `roomFirstHeal/roomHealCount`,
    `doorLatch`, `menuRequest`.
  - `roomBoundW/H(g)` helpers (fold to `WORLD_W/H` when carved).
- `src/core/zones.hpp` (new): room-graph loader mirroring `core/combat.hpp` —
  host reads `src/generated/zone_data.hpp` (identity), AVR reads the packed
  `mhZones` blob at `zone_meta.hpp` offsets via `mhFxRead*`. Value structs
  `ZoneRoom/ZoneDoor/ZoneSpawn/ZoneHeal`, readers `zoneRoomRead/zoneDoorRead/
  zoneSpawnRead/zoneHealRead`, and `roomIsSafe(g)`.
- `src/core/world.hpp`:
  - `loadRoom(g, roomId, spawn)`: installs the room record, places the hunter at
    a global spawn, clears projectiles/effects, resets the camera clamp, arms the
    door latch. Monster is **not** reset (hp/zones/pos/FSM persist). Safe room
    clears the target.
  - `updateDoors(g)` in `stepGame`: player-rect overlap with any door rect ->
    transition at `door.toSpawn`; `DOOR_MENU` sets `menuRequest` only (core never
    switches screens); post-spawn latch clears once the hunter leaves every door.
  - `tryHeal(g, bP)`: sheathed B-press edge inside a heal rect -> hp/stam to max
    + spark (existing effect path, `HEAL_SPARK_LIFE = 8`). No heal unsheathed.
  - Hold-B sheathed in camp at exactly `HOLD_TICKS` sets `menuRequest` (shared
    hold constant, no new magic number).
  - `updateActiveTarget` handles safe rooms (no target); `camMaxX/Y` per room.
- `src/core/player.hpp`: `clampPlayer(p, roomW, roomH)`; `initGame` defaults the
  room fields to legacy extents + live room (carved out for parity).
- `src/core/monster.hpp`: `clampMonster` uses active-room extents.
- `src/core/projectiles.hpp`: projectile cull uses active-room extents;
  `stepWorldBody` skips monster/target updates in a safe room.
- `src/render.hpp`: `drawArena(camX, camY, roomW, roomH)` (live dims, border per
  active room); `renderScene` clamps the camera to the active room. fie.5
  replaces the arena placeholder with the room-image blit.
- `tst/fxdatatest/test_parity.ino` + `test_hub.ino`: `#define MH_ROOM_BOUNDS 0`
  carve (both images are at the board flash limit; neither loads a room, and
  every fixture extent equals `WORLD_W/H`, so behavior is byte-identical).
- Tests: new `tst/zone_test.hpp` (registered in `tst/main.cpp`); new case in
  `tst/world_test.hpp`. Permanent, native, co-located. Symbolic `zone::*` ids
  only, never literal record indices.

## Tests (12 cases / 74 asserts)

Room record load (extents/monster kind/spawn placement), transient clear +
camera reset, door latch (no ping-pong on spawn-in-door), area->camp round-trip
spawn, menu-door request, per-room player clamps at 128x56 and 384x112,
per-room monster clamps, monster persistence across a door round-trip
(hp/x/y/state/stun/kind), safe-room behavior (no target, beast untouched, player
still walks), heal requires sheathed + inside rect (unsheathed/outside no-op),
hold-B flag gating (sheathed in camp yes; unsheathed no; outside camp no).

## Verification (exact tails)

- `make test` -> `Total Passed: 5361` / `Total Failed: 0`
- `make fxtest-headless` (full, 16 suites) -> all PASS:
  assets 270, audio 17, boot 4, combat 293, data 368, hub 57, hud 17,
  menu_art 81, menu 78, monster_art 111, parity 660, perf 5, player_art 111,
  quests 50, screens 78, smith 66.
- `FXTEST_ONLY=test_parity make fxtest-headless` ->
  `Sketch uses 29680 bytes` / `parity_test PASSED=660 FAILED=0` / `P`
- `make gen-check` -> `fxdata_manifest: PASS (81 generated artifacts unchanged)`
- `make test-tools` -> `Ran 178 tests in 11.105s` / `OK`
- `make size`:
  ```
  size: .text=28986 .data=40 .bss=1736
  size: flash=29026/29696 (670 free)  ram=1776/2560
  size: data facts: HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false HAS_GUARD_HP:false HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true HAS_HIT_STAGGER:false HAS_MULTI_STEP:false HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false HAS_STAGGER:true HAS_STEP_AFTER:false HAS_STEP_CHANCE:false HAS_WAIT_STEPS:false HAS_ZONES:true
  ```

## Budget

| | baseline 7e82b3c | after | delta |
|---|---|---|---|
| flash | 27474/29696 (2222 free) | 29026/29696 (670 free) | **+1552 B** |
| RAM | 1760/2560 | 1776/2560 | **+16 B** |
| data facts | unchanged | unchanged | no `HAS_*` flip |

+1552 B is the room loader (`zones.hpp` readers) + door/heal/latch logic +
per-room clamp/camera indirection, over the spike's +286 B bounds-only
prototype. Fits with 670 B free; no zone data trimmed.

## Deviations / notes

- `MH_ROOM_BOUNDS` carve added to **test_hub** as well as test_parity: the
  room runtime pushed test_hub to 29828 B (132 B over); with the carve it is
  27772 B. Neither image exercises rooms.
- `Game::menuRequest` is a plain flag the app layer consumes (fie.6); the core
  never switches to the menu, per acceptance. No menu routing wired here.
- `loadRoom` takes a global spawn index (`zone::SPAWN_*`), matching the blob's
  `door.toSpawn`/`monsterSpawn` representation; invalid ids fall back to 0.
- Door/heal rects are read from the blob on demand (door/heal sections are tiny
  and the checks are edge-driven), so `Game` caches only scalars.
- No float/double in core (grep clean; only comments mention the words).
- Not committed/pushed (orchestrator commits).
