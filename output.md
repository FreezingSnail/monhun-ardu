# monhun-ardu-feel.19 — data: hunt length (raise HP / zone pools / stagger)

HEAD at start: `b2f1524` (feel.18), clean tree. No commit/push (orchestrator commits).
Owner goal: 3-4 minute hunts (was ~15-25 s at effective 11-15 dmg/s).

## Data changes (`data/creatures/*.json`, sim source)

| Field | chicken `lunge` | bull `sweep` | heavy `heavy` | ravager `ravager` |
|---|---:|---:|---:|---:|
| `stats.hp` | 200 -> **1800** | 150 -> **1500** | 320 -> **2800** | 260 -> **2400** |
| head zone `hp` | 40 -> **160** | 40 -> **160** | — | 40 -> **160** |
| appendage zone `hp` | 60 -> **240** | 60 -> **240** | 60 -> **240** | 60 -> **240** |
| `profile.staggerMax` | 30 -> **60** | 40 -> **80** | 0 (unchanged) | 60 (unchanged) |

Pole variants (`pole*.json`) untouched. `MONSTER_DEFS` (legacy `src/core/game.hpp`)
untouched — the sim reads the combat blob.

## Dump review

`python3 tools/gen-combat.py --dump` is blocked by the runtime bash permission
(`python*` deny), so the same generated values were read from the authoritative
`src/generated/combat_expect.hpp` / `combat_data.hpp` (both emitted by
`gen-combat.py`, `gen-check` verified byte-stable):

```
CREATURE_LUNGE_HP          = 1800   ZONE_LUNGE_HEAD_HP        = 160   ZONE_LUNGE_APPENDAGE_HP        = 240
CREATURE_SWEEP_HP          = 1500   ZONE_SWEEP_HEAD_HP        = 160   ZONE_SWEEP_APPENDAGE_HP        = 240
CREATURE_HEAVY_HP          = 2800   ZONE_HEAVY_APPENDAGE_HP   = 240
CREATURE_RAVAGER_HP        = 2400   ZONE_RAVAGER_HEAD_HP      = 160   ZONE_RAVAGER_APPENDAGE_HP      = 240
PROFILE_LUNGE    staggerMax = 60 (decay 2, recover 30)
PROFILE_SWEEP    staggerMax = 80 (decay 1, recover 24)
PROFILE_HEAVY    staggerMax = 0
PROFILE_RAVAGER  staggerMax = 60
GUARD_LUNGE_P_LEAP  = hp 51..100    GUARD_LUNGE_P_LEAP2 = hp 0..50
GUARD_SWEEP_P_GORE  = hp 41..100    GUARD_SWEEP_P_GORE2 = hp 0..40
```

Band splits still land where intended (percent guards, so unchanged by the
scale-up): bull gore/gore2 at 40% (now 600/1500 HP), chicken leap/leap2 at 50%
(now 900/1800 HP). Boxes/windows/timings unchanged.

## TTK from the bead model (52 Hz, 11-15 effective dmg/s)

| Beast | HP | TTK |
|---|---:|---:|
| chicken | 1800 | ~120-164 s (2.0-2.7 min) |
| bull | 1500 | ~100-136 s (1.7-2.3 min) |
| heavy | 2800 | ~187-255 s (3.1-4.3 min) |
| ravager | 2400 | ~160-218 s (2.7-3.6 min) |

Overall ~100-255 s; the heavy/ravager reach the 3-4 min owner window, the fast
chicken/bull stay shorter to keep the fast/weak vs slow/tank identity. On-device
tuning belongs to **monhun-ardu-1to**.

## Unplanned engine fix (required by the data)

Increasing `m.hpMax` exposed a real AVR int16 overflow in `hudBar`
(`src/render.hpp`): `(w-2)*num` with `num=1800` wraps on the 16-bit int target
(44*2800 = 123200 > 32767), so the hunt monster HP bar rendered ~6/42 at full
health (device `test_hud` caught it: `hud mon fill plane1 got=6 want=42`). The
fill is now computed with a 16x16->32 multiply + 32/16 divide; host/device bar
counts are exact again. Cost: **+4 B flash** (budget line below).

The device hub/quest kill hooks also used `damageMonster(g, 9999, ...)`, which
relies on `dmg*14` wrapping on int16 (the engine contract caps dmg < 2340).
Now 2000 (crit -> 2800, lethal for every shipped pool) in
`tst/fxdatatest/hub_test.hpp`, `tst/fxdatatest/quests_test.hpp`,
`tst/quests_test.hpp`.

## Pins updated intentionally (no coverage weakened)

- `tst/monster_test.hpp`: spawn hp/hpMax 1800/1500/2800; legacy-default hp
  1800; bull 40% band 1500/600/615; enrage 601/600; heavy swap/reset 2800;
  melee crit 188 -> 1788.
- `tst/combat_test.hpp`: staggerMax 60/80; ravager pools 160/240; drain
  147/225; tail-break loop 4 -> 16 hits (240/15); heavy tail hp 240.
- `tst/fxdatatest/menu_test.hpp`: heavy/sweep/ravager cart hp 2800/1500/2400.
- `tst/fxdatatest/data_test.hpp`, `MONSTER_DEFS` pins: unchanged (legacy table).
- `src/generated/combat_expect.hpp`: regenerated.

## Documentation

- `README.md` target roster HP column: 1800 / 1500 / 2800.
- `docs/feel-design.md`: chicken/bull/heavy stat blocks (hp, zone pools,
  staggerMax) + the diagnosis-table stagger revision note.

## Gate

```
make gen (x2)           OK
make gen-check          exit 0  fxdata_manifest: PASS (82 generated artifacts unchanged)
make test               exit 0  Total Passed: 6580  Total Failed: 0
make test-tools         exit 0  Ran 202 tests ... OK
make fxtest-headless    exit 0  all 17 suites PASS
  test_combat PASSED=237 FAILED=0
  test_hud PASSED=17 FAILED=0
  test_quests PASSED=50 FAILED=0
  test_perf PASSED=5 FAILED=0  (B pUs=6370 pHz=156 lHz=52 lTk=456 rMx=4824 rAv=4649 ram=584)
make size
  size: flash=28724/29696 (972 free)  ram=1743/2560
  size: .text=28684 .data=40 .bss=1703
```

Baseline `28720` (976 free) -> `28724` (972 free): **+4 B flash / +0 B RAM**, all
from the `hudBar` 32-bit fill fix; the HP/pool/stagger data itself is +0 B
(records keep their packed width; `HAS_*` facts unchanged). PARITY untouched.
