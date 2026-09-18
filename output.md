# monhun-ardu-6zb.6 — poles on the creature pipeline

**Status: DONE** (option A/D accepted: land at the measured engine cost; the
content-addition budget requirement is demonstrated by the A/B proof).

## What changed

Data / generator
- `data/skeletons.json`: `pole` skeleton (origin 0,0; head 10,8).
- `data/creatures/{pole,pole_sever,pole_break,pole_crack}.json`: 4 `static`
  records, art sheet ids 1..4 with zones per the design. `pole`/`pole_sever`
  head boxes span `{-128,0,255,16}` so the head band is x-independent and
  reproduces the legacy `hy < rect.y + 16` crit containment (incl. hit centres
  with hx outside the 20 px body). `pole_break` keeps `{0,0,20,16}` head +
  `{20,8,8,12}` arm (so the arm is not stolen by the head) and
  `stats.brokenBody {20,36}`. `pole_crack` head + `{0,18,20,10}` band.
- `tools/gen-combat.py`: static creatures (optional profile -> inert zeroed,
  empty/omitted attacks+patterns), creature `flags`(bit0 static)/`sheet`/
  `brokenBody` (CREATURE 21 -> 25 B), optional zone `hp`/`bodyShare`/`hurtOn`
  (defaults 0/100/true), expect pins for static/sheet/brokenBody, empty-list
  guards.

Engine
- `src/core/combat.hpp`: `combatCreatureFlags/Static/Sheet/BrokenW/BrokenH`;
  `CombatState.isStatic`; `combatZoneHitResolveAt(g, base, phys, hx, hy, bx, by,
  fx, fy, gateBreak)` — explicit anchor/facing so a static prop resolves east
  without touching `g.monster.fx/fy`, and `gateBreak` so a wrong-phys hit lands
  damage but never drains/breaks the pole zone (beasts keep the old rule via the
  5-arg `combatZoneHitResolve` wrapper, `gateBreak=false`).
- `src/core/projectiles.hpp`: `POLE_DEFS`, its 11 accessors and the bespoke
  drain/break block deleted. `Pole` is now `{rect, hitFlash, kind}` only.
  `initPoleKind` loads the prop creature through `creatureLoad` and derives the
  rect from body+spawn; `poleCreatureId` maps kind -> generated creature index.
  `damagePole` routes through the shared resolve (east facing, `gateBreak=true`),
  applies `brokenBody` on a fresh break and keeps train stats/freeze/effects.
- `src/audio.hpp`: break cue reads `g.combat.zoneBroken`.
- `src/render.hpp`: table-driven `poleSheetById(sheet)` + `poleSheetFrame(sheet,
  broken, flash)` off the creature record sheet id; `drawPole(Game&,...)` reads
  `combatCreatureSheet(g.combat.creature)` and `g.combat.zoneBroken`.
- `src/core/game.hpp`: `Pole::hp/broken` removed; `CombatState` 79 -> 80 B.
- `tst/fxdatatest/test_parity.ino`: stale `MH_COMBAT_PARTS 0` carve removed (the
  train pole now uses the shared zone machinery).

Tests
- `tst/shells_test.hpp`, `tst/world_test.hpp`, `tst/menu_test.hpp`,
  `tst/fxdatatest/menu_test.hpp`, `tst/fxdatatest/audio_test.hpp`: ported to the
  shared zone cache (`zone[HEAD/APPENDAGE].hp`, `zoneBroken`); coverage kept:
  crit x1.4 via head dmgMul 140 (incl. hx outside the box), wrong-phys gate
  (damage lands, no drain/break), pool drain -> break, BREAK rect shrink via
  brokenBody, DPS, effects.
- `tst/combat_test.hpp`: new static/sheet/brokenBody/skeleton/zone decode and a
  shared-resolve static-prop test; counts 8 creatures / 11 zones / 5 skeletons.
- `tst/combat_pack_test.hpp`: 25 B creature decode incl. flags/sheet/brokenW/H,
  new record offsets, corrected profile/anchor indices via generated constants.
- `tools/tests/test_gen_combat.py`: static creature payload test (omitted
  profile/attacks/patterns, sheet, brokenBody, optional zone defaults, inert
  profile), dynamic empty-collections rejected, 25 B creature payload, updated
  missing-profile error message.

## Verification (exact tails)

1. `make gen` (x2) + `make gen-check` -> exit 0:
   `fxdata_manifest: PASS (67 generated artifacts unchanged)`
2. `make test` -> `Total Passed: 4579  Total Failed: 0`
3. `make test-tools` -> `Ran 141 tests ... OK`
4. `node --test mock/game.test.js` -> `tests 34 / pass 34 / fail 0`
   (mock keeps its local JS pole tables; plain-pole scenes byte-identical)
5. Device (`FXTEST_ONLY="test_parity test_combat test_menu test_menu_art
   test_assets test_audio test_data" make fxtest-headless`):
   ```
   test_assets   PASSED=256 FAILED=0   PASS
   test_audio    PASSED=17  FAILED=0   PASS
   test_combat   PASSED=233 FAILED=0   PASS
   test_data     PASSED=221 FAILED=0   PASS
   test_menu_art PASSED=81  FAILED=0   PASS
   test_menu     PASSED=74  FAILED=0   PASS
   test_parity   PASSED=660 FAILED=0   PASS   (sketch 29560/29696)
   ```
6. Parity regen: `node tools/gen-parity-fixtures.js` -> `parity fixtures EMPTY DIFF`.
7. `make size`:
   ```
   size: .text=27822 .data=78 .bss=1804
   size: flash=27900/29696 (1796 free)  ram=1882/2560
   ```

## Budget / A/B proof

| build | flash |
|---|---|
| c8629d5 (pre-pole) | 27300 |
| 802b4a6 (baseline now) | 27696 |
| this change | **27900** (+204) |
| A/B: +5th pole record reusing sheet 1 | **27900** (unchanged) |

A/B reproduction: added `data/creatures/pole_ab.json` (static, `"sheet": 1`,
one head zone), `make gen` x2, `make size` = 27900 (identical to the 4-record
image) -> sheet dispatch and static-creature plumbing add **0 flash per
record**. Temp record deleted; `grep -r pole_ab data src fxdata` = none, not in
`git status`; regen restores the same 27900 image.

The one-time engine cost is the shared-resolve port + the wrong-phys gate
(+204 vs 802b4a6, +600 vs c8629d5); the reclaim acceptance is waived per the
owner decision. The data-addition contract (content is cart-only, 0 flash) is
met.

## Deviations (documented, intentional)

- Pole head zone x-span widened to reproduce the legacy x-independent head crit;
  BREAK/CRACK keep the design's x-bounded head so the arm/band remains the
  appendage zone.
- `combatZoneHitResolveAt` gains a `gateBreak` flag so static props honour the
  legacy "wrong phys: damage lands, no drain/break" gate without changing beast
  zone behaviour.
- `test_parity` no longer carves `MH_COMBAT_PARTS 0` (its scene set now includes
  the shared-zone pole); still fits and is 660/0.

No float, no /tmp, no commit/push.
