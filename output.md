# monhun-ardu-hbk.13 — Trim: drop hub quest column + slim UPGRADE (generated tables)

Status: **DONE.** The shipping image links again at **29478 / 29696 (218 free)**.
The bead frees **296 B** vs the hbk.11 overflow (29774 → 29478) and leaves the
UPGRADE UX unchanged: three class rows show the owned tier, `>`, the next tier
and the cost; A opens the next node's card and forges it. No commit/push.

## What changed

1. **Hub quest column deleted**
   - `src/screens.hpp` — removed `drawHubQuestColumn` and its `SCREEN_HUB &&
     i == 0` call site in `drawScreen`. `SCREEN_COST_RIGHT` stays (the header
     zenny uses it); `quest.hpp`/`QuestDef`/`questReadDef` stay (still serve the
     `COND_QUEST` row read in `screenReadRow`).
   - `tst/fxdatatest/screens_test.hpp` — the hub quest-progress pixel block
     (active-quest `p/n` ink, met-progress, no-quest) collapsed to one pin: an
     active quest draws nothing in the row-0 right column. `hub_test.hpp` needed
     no change (its `save.progress` pins are state, not render).

2. **UPGRADE slimmed to generated tables**
   - `tools/gen-forge.py` — emits
     `constexpr uint16_t NODE_UPGRADE_COST[NODE_COUNT] = {...}` (each node's
     `cost`) in `src/generated/forge_meta.hpp`.
   - `src/screens.hpp` — new `screenUpgradeNext(cls, save, next, cost)`: walks
     the class's 3-node spine from `screenClassFirst(cls)`, takes the highest
     owned node, and when it exists and is not the last tier returns
     `next = node + 1` and `cost = NODE_UPGRADE_COST[next]`. `drawScreen`'s
     `ACTION_UPGRADE_ROW` branch calls it per row (draw stays exactly as hbk.11
     shipped: tier digit, `>`, next digit via `textPut`, cost via `drawNumber`).
     Removed `screenUpgradeCache`; `screenEnter` no longer fills a cache.
   - `src/screen_state.hpp` — dropped `ScreenState::nextNode[WEAPON_COUNT]` /
     `cost[WEAPON_COUNT]` and their `screenReset` clearing.
   - `monhun-ardu.ino` — the `ACTION_UPGRADE_ROW` dispatch now calls
     `screenUpgradeNext` and synthesizes the `FORGE_NODE` row from its result
     (nothing upgradeable → `CUE_HURT`); the post-forge cache rebuild calls are
     deleted (the draw reads the save directly).
   - `tools/tests/test_gen_forge.py` — `test_meta_header_constants` pins the new
     `NODE_UPGRADE_COST` table.
   - `tst/fxdatatest/screens_test.hpp` — the UPGRADE block now drives
     `screenUpgradeNext` (no owned → false; root owned → T1/100; T1 → T2/250;
     maxed → false) and pins that a forged node advances with no rebuild.

## Command tails

```
$ make gen
gen-forge: 9 nodes, 161 B blob (magic 0x4647 version 1)
gen-forge: src/generated/forge_meta.hpp
gen.sh: FX data + src/fxdata.h regenerated

$ make gen-check
fxdata_manifest: PASS (161 generated artifacts unchanged)

$ make test
Total Passed: 6355
Total Failed: 0

$ make test-tools
Ran 358 tests in 22.991s
OK

$ FXTEST_ONLY=test_screens_smithy make fxtest-headless
test_screens_smithy PASSED=98 FAILED=0
test_screens_smithy: PASS

$ FXTEST_ONLY=test_screens make fxtest-headless
test_screens PASSED=176 FAILED=0
test_screens: PASS

$ FXTEST_ONLY=test_hub make fxtest-headless
test_hub PASSED=81 FAILED=0
test_hub: PASS

$ FXTEST_ONLY=test_forge make fxtest-headless
test_forge PASSED=64 FAILED=0
test_forge: PASS
```

## Size line + delta

```
$ make size-line
size: flash=29478/29696 (218 free)  ram=1817/2560

hbk.11 (uncommitted):  flash=29774/29696 (-78 free)   (overflow)
this bead:             flash=29478/29696 (218 free)
delta vs hbk.11:       -296 B flash
HEAD 632e951:          flash=29246/29696 (450 free)
net vs HEAD:           +232 B flash  (hbk.11's remaining UPGRADE draw/routing)
```

The design expected ~300+ free; the real landing is 218 free — above the
AGENTS.md ~150 B reserve floor, so the bead is not BLOCKED. The gap is the
hbk.11 draw branch + dispatch glue that the trim (by design) keeps.

## Wall time

Worker (read + edit + 1 gen-check + 2 host/tools + 4 device suites + 2 shipping
builds): ~30 min. Slowest: the 4 serial device suite compiles and the shipping
size build.
