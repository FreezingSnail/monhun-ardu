# monhun-ardu-qrv — bash step forward with the gun shove (feel.24)

Follow-up to z5i (shield thrust animation): the animation alone read as static,
so the bash now also carries the hunter forward a small step.

## What changed

`src/core/player.hpp`

- `tapDefense()` gun branch arms `vx/vy = (dx * 24) >> 4` (1.5 px/t along the
  bash direction) when entering `PS_SHOVE`, before `exitStance`.
- `PS_SHOVE` joins the `PS_DODGE`/`PS_DEFLECT` case, so the step decays with the
  evade drift (`applyDrift(p, 14)`) over the 10 shove ticks: ~12 px total.
  `PS_STUN` gets its own case (no drift, as before).
- Render unchanged: the z5i shield thrust (10 px -> 4 px retract) is relative to
  the moving body, so the plate now travels with the hunter and thrusts.

`tst/player_test.hpp`

- New "shove bash: small forward step, shield thrust stays (feel.24)": the B tap
  arms a positive vx, the shove ends at idle, the hunter moved forward, and the
  step stays under 20 px.

## Gate (exact)

1. `make test`: `Total Passed: 6339  Total Failed: 0`
2. `make gen-check`: `fxdata_manifest: PASS (91 generated artifacts unchanged)`
3. `make fxtest-headless`: 18/18 suites PASS
4. `make size`: `size: flash=29032/29696 (664 free)  ram=1708/2560`
   — +50 B vs the 28982 (714 free) pre-change baseline. The first variant (a
   separate PS_SHOVE case with 13/16 drift, vx 30) measured +62 B and was
   dropped for the shared evade-drift case with vx 24.

Implemented inline by the orchestrator; the bead was opened retroactively so the
commit carries the id.
