# monhun-ardu-bih.2 — pole spawn path: MON_POLE kind + training quest

## What changed

The pole creature (shipped statically in the combat blob by bih.1/.5) is now a
reachable hunt target. No creature/art data changed — only the roster kind, the
packed defs blob entry, the routing case, and the training quest + its board
rows.

Code / data:

- `src/core/game.hpp`: `MonsterKind` gains `MON_POLE = 4`; the host
  `MONSTER_DEFS` mirror gains `{MON_POLE, 20, 36, 300, 0, -1}` (atkDist -1 =
  never lunge; the creature's zero profile already carries attackDist 0, no
  patterns). Blob comment 33 B -> 55 B.
- `tools/gen-fxtables.cpp`: the packed AVR `monsterdefs.bin` blob now emits 5
  entries; `MONSTER_DEFS_BYTES` 44 -> 55 (11 B/entry packed layout unchanged).
- `src/core/monster.hpp`: `monsterCreatureId()` gains
  `case MON_POLE -> combat::CREATURE_POLE`; `initMonster` clamp
  `kind > MON_RAVAGER` -> `kind > MON_POLE`.
- `data/quests/train_pole.json` (new): `{id:4, goalKind:kill, target:pole,
  need:1, rewardZenny:0, unlockFlag:0}` (unlockFlag 0 = available from the
  start; 0 zenny keeps it out of the economy).
- `tools/gen-quests.py`: `TARGET_NAMES` gains `"pole"` (index 4 == MON_POLE);
  docstring sync updated.
- `data/screens/quests.json`: two rows appended —
  `TRAIN POLE` take (param 4) and `COMPLETE TRAIN` turn-in (param 20 =
  `(need 1 << 4) | quest 4`, verified against the shipped 48/33/50/19).

New interfaces: `MON_POLE` (MonsterKind), `quests::TARGET_POLE`,
`quests::QUEST_TRAIN_POLE` (+ off), `cards::CARD_QUEST_TRAIN_POLE`. Generated
symbols referenced everywhere — no literal record indices.

## Exact verification tails

- `make gen` — clean. `gen-fxtables: ... monsterdefs.bin (55 B)`.
  `gen-quests: 5 quests, 53 B blob`. `gen-cards: 19 items, 58 pages,
  635 B blob`. Two-pass: the first pass baked the new quest card pages
  (`mh_card_quest_train_pole_0/1`, "unresolved on this pass"), the second
  resolved them; the equip/art/zone headers then carry shifted addresses.
- `make gen-check` — `fxdata_manifest: PASS (162 generated artifacts
  unchanged)`.
- `make test` — `Total Passed: 6801  Total Failed: 0`.
- `FXTEST_ONLY=test_quests make fxtest-headless` — `test_quests PASSED=112
  FAILED=0`, `test_quests: PASS`.
- `FXTEST_ONLY=test_combat make fxtest-headless` — `combat_test PASSED=251
  FAILED=0`, `test_combat: PASS`.
- `FXTEST_ONLY=test_monster_art make fxtest-headless` — `test_monster_art
  PASSED=180 FAILED=0`, `test_monster_art: PASS`.
- Full device gate (`make fxtest-headless`, 18 suites) — all PASS:
  assets 264, audio 9, boot 4, cards 85, combat 251, data 354, forge 63,
  hub 79, hud 29, items 35, monster_art 180, perf 5, player_art 120,
  quests 112, screens 227 (screens 212 + screens_smithy 98), smith 51,
  tell 18, zones 82.
- `make test-tools` — `Ran 373 tests ... OK`.
- `make size` / `make size-line` — `flash=29348/29696 (348 free)  ram=1814/2560`.

## Size delta

Baseline 29350/29696 (346 free), RAM 1814/2560.

- Flash: **29348/29696 (348 free)** -> **-2 B** whole-image (346 -> 348 free).
- RAM: unchanged 1814/2560.
- Data: cart-only (monsterdefs +11 B, quests +9 B, cards +33 B, page layers);
  no compile-time `HAS_*` fact flipped.

Well inside the wave rule (>= ~150 B free; never below).

## Behaviour proof (hard rules)

Host `monster_test` — `MON_POLE over 300 ticks: never attacks, never moves,
never damages`: with the hunter parked inside the pole's body box for 300
ticks, 0 attacks chosen (state never WINDUP/ATTACK, atkIdx stays
COMBAT_NO_ATTACK), pole x/y pinned at spawn (140, 40), player hp unchanged,
`over` stays OVER_NONE. Also `initMonster(MON_POLE)` pins static=1, spd 0,
no pattern list.

Device `test_combat` — pole head zone: `combatZoneRead(ZONE_POLE_HEAD)` hp 0,
dmgMul 140, bodyShare 100, no break types; a top-band hit resolves
COMBAT_ZONE_HEAD at 140% (dmg 14), the pool never breaks (`zoneBroken` stays
0); a hit below the 8-px head band resolves the body at 100%.

Quest E2E (device `test_quests`): take row 8 (param 4) -> kill one real pole
through the death hook (progress 1) -> turn-in row 9 (param 20) pays 0 zenny,
sets the done bit, clears the active slot.

## Deviations

- The two-pass gen was required because the new quest card pages shift every
  post-card cart address (equip/art/zone headers). Reported above.
- `tst/fxdatatest/cards_test.hpp` `ITEM_COUNT` pin 18 -> 19 (the new quest card
  shifts `WEAPON_BASE`; `WEAPON_BASE == QUEST_BASE + QUEST_COUNT` already
  derives).

No commit/push (orchestrator's job).
