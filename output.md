# monhun-ardu-nup — render: drop the procedural attack/tell markers

Owner: "can remove the amrkers, will be built into the beasts sprite sheets".
Every telegraph is now the beast's own sprite art. No new marker/indicator
added; no fallback invented.

## Changes

- `src/render.hpp`: deleted `drawAttackMarker()` (the MS_WINDUP 2x2 shade-2 and
  MS_ATTACK 4x4 shade-3 window-centre rects) and the MS_WINDUP/MS_ATTACK call
  block at the end of `drawMonster()`. `tellWindupFrame`/`tellSlot` pose select
  untouched (the authored-art path). Updated stale marker/telegraph comments
  (spin tell overlay, drawMonster header).
- `src/render_math.hpp`: tell comments now art-only — tells 1..3 select authored
  windup poses, dot (0) = generic coil, tell 4 keeps the attack's own windup
  frame; no marker fallback. `TELL_WINDUP_NONE`/`TELL_FRAMES_AUTHORED`/
  `tellHasAuthoredFrame`/`tellWindupFrame` kept (still gate the pose select).
- `src/core/combat.hpp`: `Tell` enum comment drops the marker fallback.
- `tst/fxdatatest/tell_test.hpp`, `tst/fxdatatest/test_tell.ino`: deleted (their
  subject was the markers).
- `tst/fxdatatest/monster_art_test.hpp`: added a marker-free pin (body-centre
  4x4 rect clear for an authored arc windup on plane 2 and a dot-tell windup on
  plane 1); fixed the stale marker comments (setupSpinAttack, setupChickenAttack,
  setupBullAttack, setupHeavyAttack, wing_beat windup). Existing pins untouched.
- `tst/render_math_test.hpp`: comment now points at the monster_art suite (host
  selector pins unchanged, still green).
- Docs: `docs/feel-design.md` (~25, ~64, ~94-116, ~320, ~467), 
  `docs/creature-framework.md` (~381, ~396), `README.md` (~32, ~46, ~35),
  `tools/contact_sheet.py` (~100) — core-marker/2x2 wording -> art-only.

## Verification (exact commands, run from repo root)

```
make test                -> Total Passed: 6830  Total Failed: 0
make test-tools          -> Ran 388 tests ... OK
FXTEST_ONLY="test_monster_art" make fxtest-headless
                         -> test_monster_art PASSED=182 FAILED=0 | PASS
make size                -> flash=29118/29696 (578 free)  ram=1814/2560
ARDENS=/usr/bin/true make dev-hitboxes
                         -> dev-hitboxes size: flash=29212/29696 (484 free)  ram=1814/2560
```

`tell_test` removed; no Makefile/docs reference to it beyond the suite
wildcard (verified by grep). No `drawAttackMarker`/telegraph marker call left in
`render.hpp`.

## Size delta

- pre  (HEAD 96907c7): `flash=29316/29696 (380 free)  ram=1814/2560`
- post:                `flash=29118/29696 (578 free)  ram=1814/2560`
- **−198 B flash, +0 B RAM** (198 B reclaimed, left unspent).
- dev-hitboxes still fits: 29212/29696 (484 free).

## Files

- src/render.hpp, src/render_math.hpp, src/core/combat.hpp
- tst/fxdatatest/monster_art_test.hpp (+tell_test.hpp/test_tell.ino deleted)
- tst/render_math_test.hpp
- docs/feel-design.md, docs/creature-framework.md, README.md
- tools/contact_sheet.py

## Wall time

Worker (implement + touched-suite gates + size): ~12 min. Full device gate not
run (orchestrator).
