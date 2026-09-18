# monhun-ardu-rud — types: audit int16 usage, flip provably-narrow fields/params (measured)

Status: **DONE** (all gates green; no commit per worker protocol).
**Net flash 26868 -> 23342 (-3526 B, 2828 -> 6354 free). Net RAM 2005 -> 1829 (-176 B).**
Parity fixtures, player_art and hud goldens byte-identical (no regen). No `HAS_*`
fact flipped. Perf improved: `rMx 5256 -> 4968 (-288)`, `rAv 4910 -> 4756 (-154)`,
`pUs 6377 -> 6375 (-2)`.

## Per-group deltas (measured sequentially, whole-image `make build`)

| group | change | flash | delta | RAM | delta |
|---|---|---|---|---|---|
| baseline (HEAD 9831b19) | — | 26868 | — | 2005 | — |
| G1a | Projectile/Effect/Pole/TrainStats fields + Game counters | 26572 | **-296** | 1903 | -102 |
| G1b | Player/Monster width/velocity/timer fields + FpBody subX/subY | 25316 | **-1256** | 1842 | -61 |
| G2a | `fp::addMove`/`addVel`/`dirIndexFromInput` + `movePlayer`/`applyDrift` params | 25180 | **-136** | 1842 | 0 |
| G2b | render `int32`->`int16` locals/params (`mulQ4`, `textPut`, `partDraw`, draw fns) | 24304 | **-876** | 1842 | 0 |
| G2c | HUD division narrowing (`hudDigits`/`hudNum`/drawHud totals) | 24184 | **-120** | 1842 | 0 |
| G1c | Player hp/hpMax/stam + FpStam + clamp adaptations | 23856 | **-328** | 1838 | -4 |
| G1b revert | Monster `t`/`cd` back to int16 (parity fixture sentinel) | 23984 | **+128** | 1840 | +2 |
| G2d | Game camX/camY/lastShot* + AudioState + pattern dist + pushApart + spawnShot dirs | 23628 | **-356** | 1829 | -11 |
| G2e | Target callback signatures + handlers + `damagePole` | 23342 | **-286** | 1829 | 0 |
| G1d | TrainEvent.dmg/TrainStats.last/Effect.text -> uint8 | 23340 | -2 | 1792 | -37 | **REVERTED (<40 B)** |
| G2f | `combatFacePoint` int16 offsets/outputs | 23314 | -28 | 1829 | 0 | **REVERTED (<40 B)** |
| G2g | uint8 loop counters (projectiles/effects) | 23424 | +82 | 1829 | 0 | **REVERTED (regression)** |
| **total** | | **23342** | **-3526** | **1829** | **-176** |

The G1b -1256 includes 128 B from Monster `t`/`cd` that was later reverted (they
are set to a 30000 sentinel by the parity fixture setup; `g.monster.t = 30000`
cannot be stored in a byte). Reported honestly above.

## Audit table (field/param -> current -> proposed -> domain proof -> measured)

### Flipped to `uint8_t`

| symbol | was | now | proof | group |
|---|---|---|---|---|
| `Game::freeze` | int16 | uint8 | hitstop set to 12 max (`damageMonster`), 3/5/6/8 elsewhere | G1a |
| `Game::projN` | int16 | uint8 | 0..`MAX_PROJECTILES` (12) | G1a |
| `Game::fxN` | int16 | uint8 | 0..`MAX_EFFECTS` (12) | G1a |
| `Projectile::w,h` | int16 | uint8 | generated shell w/h <= 7 | G1a |
| `Projectile::dmg` | int16 | uint8 | generated shell dmg <= 28 | G1a |
| `Projectile::life` | int16 | uint8 | spawn `PROJ_LIFE` (90), culled at 0 | G1a |
| `Effect::t`, `Effect::life` | int16 | uint8 | life <= 26 (addEffect call sites), t ages to life | G1a |
| `Pole::hitFlash` | int16 | uint8 | set 4, decays to 0 | G1a |
| `TrainStats::head`, `::count` | int16 | uint8 | 0..`MAX_TRAIN_EVENTS` (24) | G1a |
| `Player::w,h` | int16 | uint8 | spawn 16x16 | G1b |
| `Player::hp,hpMax` | int16 | uint8 | 0..100; clamp-on-subtract preserves mock end-of-tick 0 (`p.hp<=0` -> `==0`) | G1c |
| `FpStam::stam` | int16 | uint8 | 0..stamMax 100; `drainStam` clamps at 0 (FSM exits the stance the same tick it empties) | G1c |
| `FpStam::stamSub` | int16 | uint8 | 1/16 accumulator, always 0..15 | G1c |
| `Player::stamMax` | int16 | uint8 | spawn 100 | G1b |
| `Player::t` | int16 | uint8 | state timers <= 60 (dodge 16, stun 45, attack totals) | G1b |
| `Player::chain,chainWin,aBuffer` | int16 | uint8 | chain 0..2; windows `CHAIN_WIN` 14 / `A_BUFFER` 10 | G1b |
| `Player::stanceT,stanceAuto,whirlTick` | int16 | uint8 | parry exits >34; whirl <= 2*stam <= 200; `branchAutoT` <= 50 | G1b |
| `Player::throwCd,riposteT` | int16 | uint8 | 50 / 90 | G1b |
| `Player::iT` | int16 | uint8 | 34 | G1b |
| `Player::reload` | int16 | uint8 | generated `shellReload` <= 70 | G1b |
| `Player::shells[2]` | int16 | uint8 | generated `shellCount` <= 5 | G1b |
| `Monster::w,h` | int16 | uint8 | generated creature w/h <= 40 | G1b |
| `Monster::windupMax` | int16 | uint8 | generated attack windup <= 48 | G1b |
| `Monster::hitFlash` | int16 | uint8 | set 4 | G1b |
| `Monster::stun` | int16 | uint8 | onStun 28/60, trip 70 | G1b |
| `Monster::spd` | int16 | uint8 | generated creature spd <= 7 | G1b |
| `Game::camX,camY` | int16 | uint8 | 0..`CAM_MAX_X`(128)/`CAM_MAX_Y`(56) | G2d |
| `Game::lastShotX,Y` | int16 | uint8 | player centre 8..248 | G2d |
| `AudioState::playerHp/reload/monsterStun/projN/riposteT` | int16 | uint8 | mirrors of the fields above | G2d |
| `Target::onHit` push/effect, `onShove` amount/freeze, `onStun` ticks | int | uint8 | push <= 12, effect 0/1, freeze <= 12, ticks <= 70 | G2e |
| `Target::onHit` dmg | int | uint8 | weapon dmg <= 27, riposte x2 -> 54 | G2e |
| `damagePole` dmg | int | uint8 | same as onHit; tests pass 100 | G2e |
| `fp::addMove` spd, `movePlayer` spd, `applyDrift` mult, `addEffect` life, `drainStam` amount | int16 | uint8 | spd <= 18, mult 13/14, life <= 26, amount 1/2/8 | G2a/G1b |

### Flipped to `int8_t` (signed)

| symbol | was | now | proof | group |
|---|---|---|---|---|
| `FpBody::subX,subY` | int16 | int8 | 1/16 remainder in -15..15 (`%16`) | G1b |
| `Projectile::vx,vy` | int16 | int8 | `(dir*speedF)>>4`, dir<=16, speedF<=42 -> |v|<=42 | G1a |
| `Player::fx,fy` | int16 | int8 | DIR8 components -16..16 | G1b |
| `Player::vx,vy` | int16 | int8 | dodge 54, knockback 35, lunge 42 max | G1b |
| `Monster::fx,fy` | int16 | int8 | DIR8 components | G1b |
| `Monster::lvx,lvy` | int16 | int8 | `(fx*speedF)>>4`, speedF<=34 | G1b |
| `Monster::circleDir` | int16 | int8 | 1 / -1 | G1b |
| `Game::lastShotFx,Fy` | int16 | int8 | copy of player facing | G2d |
| `Target::onShove` dirX/dirY | int | int8 | DIR8 components | G2e |
| `fp::dirIndexFromInput` mx/my | int16 | int8 | Input mx/my are -1/0/1 | G2a |
| `fp::addVel` vx/vy | int16 | int8 | all callers pass the int8 fields above | G2a |
| `fp::addMove` dx/dy | int16 | int8 | DIR8 components -16..16 | G2a |
| `movePlayer` mx/my | int16 | int8 | -1/0/1 | G2a |
| `spawnShot` dirX/dirY[3] | int16 | int8 | DIR8 / `rotFp` results | G2d |

### Flipped `int32_t` -> `int16_t` (render/HUD)

| symbol | was | now | proof | group |
|---|---|---|---|---|
| `mulQ4(a,b)` return | int32 | int16 | |a|<=16, radius |b|<=20 -> product <= 320 | G2b |
| `textPut` x,y | int32 | int16 | screen coords / `hudPut` lane | G2b |
| `partDraw`/`partVariantDraw` rx,ry | int32 | int16 | screen coords <= ~512 | G2b |
| `drawPole` x,y; `drawMonster` x,y,w,h,ax,ay; `drawPlayer` x,y,cx,cy,reach,hx,hy,hw,hh,shx,shy; `drawProjectiles` x,y,bx,by,hw,hh; `drawEffects` x,y; `drawArena` lx,ly | int32 | int16 | world <= 256 + cam/shake, sizes <= 128 | G2b |
| `hudDigits`, `hudNum` v | int32 | int16 | totals clamped to 9999 / 999 before the 16-bit div | G2c |
| `monster::patternGuardOk`/`chooseAttack` dist | int32 | int16 | `isqrt` result <= ~360 | G2d |
| `pushApart` ox,oy | int32 | int16 | overlap span <= ~300 | G2d |

### Kept `int16_t`/`int32_t` (signed or wider domain — not flipped)

| symbol | reason |
|---|---|
| `Monster::t`, `Monster::cd` | parity fixture setup writes the 30000 sentinel directly; byte would truncate to 48 |
| `Monster::hp,hpMax` | generated creature hp 320 > 255 |
| `Game::tick` | monotonic, unbounded (`uint16` would not change size) |
| `Player::bHeld` | B can be held indefinitely (compare `== HOLD_TICKS`), hashed |
| `TrainStats::last`, `TrainEvent::tick/dmg`, `Effect::x/y/text` | unbounded/latest fields, hashed; uint8 group measured only -2 B (<40, reverted) |
| `Rect::x,y,w,h`; `Target::onHit` hx/hy; `knockMonsterAway` cx/cy | hit/body coords can be negative (melee reach extends past a clamped player) |
| `Monster::atkIdx`/`winRemain` | already uint8 |
| `Attack`, `Branch`, `ShellDef`, `WeaponDef`, `MonsterAttack`, `MonsterDef` | packed FX-cart blob ABI (G3); changing sizes breaks the static_asserts |
| `combo`/`combat_meta`/`art_dims` generated constants | G3; `rg` counts include `uint16_t` substrings |
| `fp::tdiv` a/b, `fp::isqrt`, `fp::rotFp` | products/intermediates need 16/32 bit |
| `render::sprDraw` x/y, `drawNumber` x/y/value, `hudBar` num/den | screen coords/clamped values, no measured win |
| `combatFacePoint`/`combatFaceOffset` ox/oy/dx/dy | int16 offsets measured -28 B (<40, reverted) |

### Reverts (measured, per the <40 B rule)

- **Monster `t`/`cd` uint8**: parity FAIL 40 ticks; fixture override sets
  `g.monster.t = 30000`. Restored int16 -> +128 B.
- **TrainEvent.dmg/TrainStats.last/Effect.text uint8**: -2 flash / -37 RAM, below
  the 40 B flash bar. Reverted.
- **`combatFacePoint` int16**: -28 B, below 40. Reverted.
- **uint8 loop counters** (`initWorld`, `addEffect`, `updateEffects`, `trainDps`,
  `spawnShot`, `removeProjectile`, `tryBranch`): +82 B regression (AVR promotes
  the counter and adds extend instructions). Reverted.

## G3 note (generated/packed data)

G3 was not attempted: the packed combat blob mirrors (`Pk*` in combat.hpp,
`combat_data.hpp`, `combat_meta.hpp`) are ABI-pinned by static_asserts and read
back as u8/u16 already (no int16 stored values). Narrowing them would change the
blob ABI and require a generator + fixture regen with no flash win expected; the
audit above stays within the hand-written core/render/audio.

## What the bytes actually came from (AVR codegen)

- **Struct fields**: the dominant win. Byte fields lower LDS/STS to one
  instruction and remove the second register of every scalar access. G1b alone
  (-1256 B) is mostly Player/Monster field width; G1a/G1c (-296/-328) the
  projectile/effect/hp/stam structs.
- **Call marshalling**: `Target::onHit`/`onShove`/`onStun` and the fp/render
  helpers now pass single-byte args, so callers no longer set the high register
  of each argument (G2a -136, G2e -286).
- **Render int32->int16 locals**: int16 keeps everything in the native 16-bit
  ALU instead of 32-bit helper calls (`G2b -876`, `G2c -120`).
- **Not loops**: uint8 loop counters regressed (+82) — AVR promotes to `int`
  and pays an extend on every compare.

## Verification (exact tails / numbers)

1. `make gen` (x2) -> `make gen-check`:
   ```
   fxdata_manifest: PASS (53 generated artifacts unchanged)
   ```
   `git status --short` shows only the 8 edited sources; no generated diffs.
2. `make test` -> `Total Passed: 3144  Total Failed: 0`.
   `make test-tools` -> `Ran 82 tests ... OK`.
3. `make fxtest-headless` (full), all suites PASS:
   `test_assets 262/0, test_audio 14/0, test_boot 4/0, test_combat 184/0,
   test_data 221/0, test_hud 17/0, test_menu 59/0, test_parity 660/0,
   test_perf 5/0, test_player_art 111/0`.
   perf tail: `B pUs=6375 pHz=156 lHz=52 lTk=488 rMx=4968 rAv=4756 ram=611`
   vs reference `rMx=5256 rAv=4910 pUs=6377` — **improved, no regression**.
4. `make build` + `make size`:
   ```
   Sketch uses 23342 bytes (78%) of program storage space. Maximum is 29696 bytes.
   Global variables use 1829 bytes (71%) of dynamic memory, leaving 731 bytes ...
   size: .text=23284 .data=58 .bss=1771
   size: flash=23342/29696 (6354 free)  ram=1829/2560
   ```
   Baseline 26868/2005 -> new 23342/1829 = **-3526 flash / -176 RAM**.
5. Parity fixtures / player_art / hud goldens unchanged (no regen, no test
   assertion edits). Only `tst/player_test.hpp` changed: the three test stub
   callbacks' parameter types were aligned to the new `Target` typedef (same
   recorded values, same assertions). Data facts unchanged.

## Files changed

- `src/core/game.hpp` — struct field widths, Target callback typedef, Game counters/camera/lastShot.
- `src/core/fp.hpp` — FpBody subX/subY, FpStam, addVel/addMove/dirIndexFromInput.
- `src/core/player.hpp` — Player field use, clamp-on-subtract for hp/stam, move/drift params.
- `src/core/monster.hpp` — Monster field use, onHit/onShove/onStun handlers, pattern dist, pushApart.
- `src/core/projectiles.hpp` — projectile/effect/pole/train fields, addEffect/trainAdd/damagePole, spawnShot dirs.
- `src/core/combat.hpp` — combatZoneContains (int32 locals untouched; see revert).
- `src/render.hpp` — int32->int16 locals/params, mulQ4/textPut/partDraw, HUD division.
- `src/audio.hpp` — AudioState narrow fields.
- `tst/player_test.hpp` — test stub callback signatures.
