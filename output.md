# monhun-ardu-feel.5 — engine+render: per-attack telegraph shapes

Baseline: HEAD `3f19ff9`, clean tree. No commit/push (orchestrator commits).
Blob-record change (`ATTACK_SIZE 23 -> 24`) shifts the FX image offsets, so
`make gen` was run twice and the generated set stamped together.

## Result

| metric | baseline (provided) | now | delta |
| --- | --- | --- | --- |
| shipping flash | 27474 / 29696 (2222 free) | **27920 / 29696 (1776 free)** | **+446 B** |
| RAM | 1740 / 2560 | **1741 / 2560** | **+1 B** |
| `.text` / `.data` / `.bss` | — | 27880 / 40 / 1701 | — |
| combat blob | 1068 B | **1076 B** | +8 B (8 attacks x 1) |
| `ATTACK_SIZE` | 23 | **24** | +1 (tell byte, appended) |
| `CombatAttackCache` | 24 B | **25 B** | +1 |
| `CombatState` | 91 B | **92 B** | +1 |

**All five shapes shipped (DOT/LINE/ARC/RING/ZONE) at +446 B — under the ~600 B
target**, leaving 1776 B >= the 1500 B reserve for the kit data. ARC did not
need to be deferred.

Perf (`make fxtest-headless FXTEST_ONLY=test_perf`): **`B pUs=6369 pHz=157
lHz=52 lTk=456 rMx=4772 rAv=4587 ram=588`**, `perf_test PASSED=5 FAILED=0`.
Render max is **4772 us vs the 7407 us floor — unchanged from baseline** (the
shipped attacks are all tell 0, so the perf scene still takes the legacy 2x2
path; the tell branch is one early-out compare).

## What changed

### Data/schema (`tools/gen-combat.py`)
- `TELLS = {dot:0, line:1, arc:2, ring:3, zone:4}`; `tell` is an optional attack
  key (default 0) validated with `read_enum`.
- Packed as the attack record's 24th/last byte (after the `windup..dmg` quad, so
  the hop `moveDx/moveDy` offsets 2/3 and the timing quad are unmoved).
- `--dump` prints `tell dot|line|arc|ring|zone`; `combat_expect.hpp` gains
  `ATTACK_<creature>_<first>_TELL` for each creature's first attack; the host
  `Attack` struct gains `uint8_t tell;`.

### ABI (`src/core/combat.hpp`, `src/core/game.hpp`)
- New `enum Tell` (mirrors the generator).
- `PkAttack` / `CombatAttackValue` append `tell`; `combatAttackRead`,
  `combatAttackTell` and `attackLoad` (AVR read + host) load it into the cache.
- `CombatAttackCache` appends `tell`; static asserts updated (cache 25 B,
  `CombatState` 92 B, new `offsetof(PkAttack, tell) == dmg + 2`).

### Render (`src/render.hpp`, `src/render_math.hpp`)
- The windup/attack telegraph moved out of `drawMonster` into `drawMonsterTell`,
  which reads only the cached window (`g.combat.attack.win.box`) and
  `g.combat.attack.tell` — **no cart read during paint**, same window the hit
  test uses. The attack-phase 4x4 shade-3 marker is unchanged.
- tell 0 keeps the legacy 2x2 shade-2 core (byte-identical pixels).
- LINE: three 2x2 dashes at Q2 fractions of the body-centre -> window-centre ray
  (`tellLineDash`). ARC: three 4x2 segments across the box width with the centre
  dropped 2 px (`tellArcSeg`). RING: expanding outline, half-extent grows ~1 px
  per 2 windup ticks from 2, clamped to the window half (`tellRingHalf`). ZONE:
  static window-bound outline. RING/ZONE share `tellOutline` +
  `tellRectOrigin`.
- The pure geometry lives in `src/render_math.hpp` (Arduino-free) so the host
  suite pins it; the device suite pins the resulting framebuffer bytes. This
  split is required because `src/render.hpp` is device-only (ArduboyG/SpritesU)
  and cannot be compiled into `make test`.

## Tests (permanent, native)

- **Host** `tst/render_math_test.hpp` — `per-attack telegraph geometry`: pins
  `tellNeedsWindow` per shape with the tell-0 negative control, the LINE dash
  coordinates (axis-aligned, diagonal, negative floor), the RING half-extent
  growth + clamp, the ARC segment offsets, and the shared rect origin. The
  device-only render means the host pins the exact geometry the draw consumes.
- **Device** `tst/fxdatatest/tell_test.hpp` + `test_tell.ino` (new suite) —
  calls `drawMonsterTell` after clearing plane 0 and pins `arduboy.getBuffer()`
  exact page bytes for DOT, LINE, RING (empty + full windup), ZONE, ARC and the
  attack-phase 4x4 marker, following the `test_hud` exact-byte pattern inside the
  FX/OLED bracket.
- `tst/combat_pack_test.hpp` — attack decode now pins tell at byte 23 + the
  `ATTACK_HEAVY_BITE_TELL` spot value; the hop test names `ATTACK_SIZE == 24`.
- `tst/combat_test.hpp` — attack record + accessor tell match; `attackLoad`
  cache tell pin.
- `tst/fxdatatest/combat_test.hpp` — `kAttacks` rows carry the tell field and
  the cache pins `ATTACK_LUNGE_PECK_TELL`.
- `tools/tests/test_gen_combat.py` — tell default/emit/dump, enum rejection, and
  the updated 24 B attack payload; `combat_expect.hpp` synced.

## Verification tails

```
# make gen (x2) + gen-check
gen-combat: 8 creatures, 8 attacks, 13 windows, 9 patterns, 9 steps, 5 skeletons, 11 zones, 1076 B, sha256 fdcd0897176bb8bdfaf56b5ba8df1ba60bf0732ea7e1f72b2ab85e50c89efe28
fxdata_manifest: PASS (82 generated artifacts unchanged)

# make test
Total Passed: 5671
Total Failed: 0

# make test-tools
Ran 197 tests in 11.203s
OK

# make size
size: .text=27880 .data=40 .bss=1701
size: flash=27920/29696 (1776 free)  ram=1741/2560
size: data facts: HAS_ENRAGE:false HAS_GUARD_CHANCE:false HAS_GUARD_COOLDOWN:false HAS_GUARD_FACING:false HAS_GUARD_HP:false HAS_GUARD_PLAYER:false HAS_GUARD_ZONES:true HAS_HIT_STAGGER:false HAS_MULTI_STEP:false HAS_MULTI_WINDOW:true HAS_SIMPLE_GUARDS:false HAS_STAGGER:true HAS_STEP_AFTER:false HAS_STEP_CHANCE:false HAS_WAIT_STEPS:false HAS_ZONES:true

# make fxtest-headless (full)
asset_test PASSED=270 FAILED=0
test_audio PASSED=17 FAILED=0
test_boot PASSED=4 FAILED=0
combat_test PASSED=177 FAILED=0
data_test PASSED=368 FAILED=0
test_hub PASSED=57 FAILED=0
test_hud PASSED=17 FAILED=0
test_menu_art PASSED=81 FAILED=0
menu_test PASSED=80 FAILED=0
test_monster_art PASSED=111 FAILED=0
parity_test PASSED=660 FAILED=0
B pUs=6369 pHz=157 lHz=52 lTk=456 rMx=4772 rAv=4587 ram=588
perf_test PASSED=5 FAILED=0
test_player_art PASSED=111 FAILED=0
test_quests PASSED=50 FAILED=0
test_screens PASSED=78 FAILED=0
test_smith PASSED=66 FAILED=0
test_tell PASSED=17 FAILED=0
zones_test PASSED=69 FAILED=0
```

## Notes / deviations

- No shipped attack authors a non-zero tell yet (all shipped `tell` are 0), so
  the shipped look is byte-identical and the tell machinery is compiled but
  inert until the kit-data beads author shapes. The device `test_tell` drives
  the cache directly to exercise all shapes.
- The task's "host render test pinning framebuffer bytes" is not literally
  possible: `src/render.hpp` needs ArduboyG/SpritesU and is device-only. The
  host suite pins the pure tell geometry instead; the new Ardens `test_tell`
  pins the actual framebuffer bytes for every shape. Documented above.
