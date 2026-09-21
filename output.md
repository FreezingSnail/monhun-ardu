# monhun-ardu-arm.1 — armor data: pieces + skill table

Status: DONE. Data + tooling only; shipping image unchanged (flash 0 delta).
Repo left dirty on purpose (orchestrator commits between waves); no commit/push.

## What changed

- `data/armor.json` (new): 5 pieces — `hunter_helm`, `bone_cap` (head),
  `hunter_mail`, `bone_mail` (body), `evade_charm` (charm). Each has
  `slot/defense/resist{fire,water,ice,thunder}/skills[]/recipe{materials,zenny}`
  and an optional placeholder `sheet`. Materials use hunt mats (ore/scale/shell/
  fang/tail) + zenny. All five skill kinds covered.
- `data/skills.json` (new): fixed kind enum (`ATTACK_UP`, `DEFENSE_UP`,
  `HEALTH_UP`, `STAMINA_UP`, `EVADE_WINDOW`), `maxPoints`, `perPoint`, and the
  shared thresholds `{ "s": 10, "m": 15 }` (points below `s` inert; `>= s`
  active; `m` = max useful). Documented in `docs/equipment-framework.md`.
- `tools/gen-armor.py` (new, +x): validates ids/ranges/int-only/slot enum/
  resist i8/item refs/sheet symbol, rejects per-skill totals over `maxPoints`,
  packs `fxdata/tables/armor.bin`, emits
  `src/generated/armor_{data,meta,expect}.hpp` (`ARMOR_*`, `SKILL_*`, `SLOT_*`,
  `KIND_*`, `sheet::*`, `mat::*`, `THRESHOLD_S/M`, spot values + sha256).
  Blob: 8 B header + 18 B piece + 3 B skill, magic `0x5241`, v1, 113 B.
- `tools/gen-smith.py`: v2 -> v3. Reads `data/armor.json` and appends one 8 B
  armor recipe per piece after the weapon records (`armorIdx u8, cost u16,
  unlockFlag u8, mat[2] x (itemIdx+1,count)`); header reserved byte is now
  `armorCount`. Weapon record offsets unchanged. Emits `ARMOR_RECIPE_SIZE/COUNT/
  OFF`, `AREC_*`. Blob 74 -> 114 B, 5 armor recipes. Same recipe debit path.
- `tools/gen.sh`: runs `gen-armor.py` before `gen-smith.py`.
- `fxdata/fxdata.txt`: `raw_t mhArmor = "tables/armor.bin"` after `mhItems`.
- `tools/fxdata_manifest.py`: `fxdata/tables/armor.bin` in OUTPUT_PATHS.
- `tst/armor_test.hpp` (new, host) + wired into `tst/main.cpp`: compiles the
  generated mirror and pins sizes/ids/spot values/thresholds.
- Tests: `tools/tests/test_gen_armor.py` (new, 31 cases), `test_gen_smith.py`
  extended (armor list + errors) with `with_armor` fixture; manifest fixture +
  expectations updated.
- Docs: `docs/equipment-framework.md` ("Armor data" schema/ABI/thresholds +
  arm epic beads), `docs/quests-shops.md` (armor recipes through the smith
  recipe path). `src/upgrade_state.hpp` stale comment refreshed (comment only).

## Interfaces

- `armor::MAGIC=0x5241`, `SIZE=113`, `PIECE_SIZE=18`, `SKILL_SIZE=3`,
  `PIECE_COUNT=5`, `SKILL_COUNT=5`, `PIECES_OFF=8`, `SKILLS_OFF=98`,
  `SLOT_HEAD/BODY/CHARM`, `KIND_*`, `THRESHOLD_S=10`, `THRESHOLD_M=15`,
  `ARMOR_<ID>[_OFF]`, `SKILL_<ID>[_OFF]`, `sheet::*`, `mat::*`.
- `smith::VERSION=3`, `SIZE=114`, `ARMOR_RECIPE_SIZE=8`, `ARMOR_RECIPE_COUNT=5`,
  `ARMOR_RECIPES_OFF=74`, `AREC_ARMOR/COST/UNLOCK/MAT_OFF`, `AREC_<ID>[_OFF]`.

## Verification (tails)

`make gen` x2, then `make gen-check`:
```
gen.sh: FX data + src/fxdata.h regenerated
fxdata_manifest: PASS (90 generated artifacts unchanged)
```
(First pass re-bakes the equip/zone sheet offsets after the table grew; second
pass converges — the mandated `make gen` x2.)

`make test`:
```
Total Passed: 6125
Total Failed: 0
```

`make test-tools`:
```
Ran 300 tests in 17.225s
OK
```

`make fxtest-headless` (full, test_parity excluded):
```
B pUs=6348 pHz=157 lHz=52 lTk=480 rMx=3348 rAv=3075 ram=710
perf_test PASSED=5 FAILED=0
test_perf: PASS
... all suites PASS (assets 270, audio 10, boot 4, combat 237, data 343, hub 63,
hud 25, items 35, menu_art 53, menu 60, monster_art 111, perf 5, player_art 111,
quests 50, screens 85, smith 70, tell 14, zones 80)
```

`make size`:
```
size: .text=28254 .data=20 .bss=1590
size: flash=28274/29696 (1422 free)  ram=1610/2560
size: data facts: HAS_CARVE:true HAS_ENRAGE:true ... HAS_ZONES:true
```
Flash delta vs HEAD: **0 B** (28274). Cart grew by 153 B of table data
(armor 113 + smith +40), which shifted the sprite-section offsets (equip/zone
meta regenerated).

## Deviations

- Resist is packed/validated as signed i8 (-128..127) so negative armor
  resists are already representable; the starter set uses a couple.
- Armor recipes are derived directly from `data/armor.json` (single source)
  rather than authored a second time under `data/smith/`. The "armor list"
  option of the bead; smith schema gains the armor record array + header count.
- No runtime reader/UI added (arm.2/arm.3 own the engine); the host suite is the
  compile/pin coverage for the generated data.
