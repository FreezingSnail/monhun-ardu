# Weapon movesets — sword / flail / gunshield (sim reference)

Hand-kept reference for the three player movesets as shipped. Source of truth
is the code: `WEAPON_DEFS` in `src/core/game.hpp` (ported byte-for-byte from
`mock/game.js`), the melee boxes in `src/generated/player_boxes.hpp` (generated
by `tools/gen-hitboxes.py` from `images/masks/mh_player_base_16x16.png`), and
the FSM timings in `src/core/player.hpp`. Host tests pin every number
(`tst/player_test.hpp`, `tst/hitbox_reach_test.hpp`).

How to read:

- Ticks are logic ticks at the shipping cadence (52 Hz logic, 3 planes/tick).
  `S/A/R` = startup / active / recover ticks. The melee box is live during the
  active window only.
- Box = `reach` px along the facing vector + `hw x hh` px (the rect centred at
  the reach point). Reach/hw/hh come from the authored mask columns, index
  order `attacks[0..2], special, roll, alt, charge[0..1], branches[0..2]`.
- `Dmg` is base damage. The sim truncates at each step: smith tier (`dmgMul`)
  then armor ATTACK_UP (`armorFx.dmgMul`); the sword riposte special doubles
  after that fold.
- `Spd` is walk speed in 1/16 px per tick (stowed run is 24 = 1.5 px/t).
- Shipping/demo flags: `MH_STAGE3=0` (no stage-3 branch), `MH_ROLL_ALT=0` (no
  roll attack, no direction+A opener), `MH_B_BRANCH_BUFFER=0` (no A-A-B queue
  through recovery), `MH_CHARGE=1` (flail charge L1). Host tests force the
  carves back on; rows marked *(carved)* are test-only in the shipped image.

## Sword — spd 14 (0.88 px/t), canCancel, parry stance

| Move | Input | S/A/R | Dmg | Box | Stam | Notes |
|---|---|---|---|---|---|---|
| Hit 1 | A | 3/5/8 | 9 | 13 + 12x10 | 14 | chain window 14t after the hit |
| Hit 2 | A,A | 3/5/8 | 10 | 13 + 12x10 | 14 | |
| Hit 3 | A,A,A | 5/6/14 | 17 | 16 + 18x14 | 22 | finisher lock 24t |
| Branch 1 stepslash | B in hit-1 recovery | 3/5/12 | 12 | 18 + 14x12 | 15 | lunge 42 |
| Branch 2 spincut | B in hit-2 recovery | 5/7/15 | 20 | 12 + 28x26 | 24 | |
| Branch 3 *(carved)* | B after the finisher | 8/4/20 | 26 | 16 + 20x22 | 27 | needs `-DMH_STAGE3=1` |
| Parry | B hold 11t | - | - | - | drain 8/t | cap 34t; `stanceT <= 20` parries: beast stun 60, riposteT 90 |
| Riposte special | A in parry | 4/6/16 | 24 | 18 + 20x16 | 30 | x2 dmg while riposteT is live |
| Dodge | B tap | 16t | - | - | 20 | i-frames 14, 3.4 px/t |
| Roll attack *(carved)* | A in dodge | 4/5/10 | 12 | 15 + 16x14 | 15 | needs `-DMH_ROLL_ALT=1` |
| Thrust *(carved)* | dir + A | 6/4/12 | 14 | 22 + 10x10 | 18 | lunge 20, needs `-DMH_ROLL_ALT=1` |

## Flail — spd 12 (0.75 px/t), no cancel, whirl stance

| Move | Input | S/A/R | Dmg | Box | Stam | Notes |
|---|---|---|---|---|---|---|
| Hit 1 | A | 8/6/9 | 14 | 19 + 20x16 | 20 | |
| Hit 2 | A,A | 6/6/9 | 17 | 21 + 22x16 | 18 | |
| Hit 3 | A,A,A | 5/7/15 | 25 | 24 + 24x20 | 26 | finisher lock 24t |
| Branch 1 -> whirl | B in hit-1 recovery | - | - | - | - | stance auto-exits after 50t |
| Branch 2 trip | B in hit-2 recovery | 5/6/16 | 12 | 22 + 22x14 | 21 | trip effect |
| Branch 3 *(carved)* | B after the finisher | 10/6/24 | 32 | 24 + 32x24 | 36 | push 12 + trip, needs `-DMH_STAGE3=1` |
| Whirl | B hold 11t | - | 8 / 16t | r24 circle | drain 14/t | push 8 per ring hit |
| Ball throw | A in whirl | 4/8/14 | 27 | 32 + 14x18 | 33 | throw cooldown 50t |
| Deflect | B tap | 9t | - | - | 10 | back-step 1.9 px/t; a hit during it stuns the beast 28 |
| Charge slam 1 | hold A >=14t past a swing, release | 4/6/14 | 24 | 26 + 28x18 | 21 | flail is the only charge weapon (L2 carved) |

## Gunshield — spd 7 (0.44 px/t), canCancel, guard stance

| Move | Input | S/A/R | Dmg | Box | Stam | Notes |
|---|---|---|---|---|---|---|
| Hit 1 | A | 5/4/11 | 6 | 11 + 14x12 | 12 | |
| Hit 2 | A,A | 5/4/11 | 7 | 11 + 14x12 | 12 | |
| Hit 3 | A,A,A | 7/5/15 | 11 | 13 + 16x14 | 20 | finisher lock 24t |
| Branch 1 pointblank | B in hit-1 recovery | 4/5/16 | 22 | 15 + 18x16 | 9 | shell gate retired |
| Branch 2 guardbash | B in hit-2 recovery | 4/4/12 | 9 | 14 + 16x14 | 12 | push 12 |
| Branch 3 *(carved)* | B after the finisher | 6/3/20 | 30 | 16 + 24x18 | 24 | push 16, needs `-DMH_STAGE3=1` |
| Guard | B hold 11t | - | - | - | drain 7/t | block: chip 25%, -28 stam, break -> stun 45 |
| Arrowshot | A in guard | 6/4/16 | 12 | 44 + 8x6 | 21 | hitscan, nock 24t, guard stays while B is held |
| Shove | B tap | 10t | - | - | 10 | lean 1.5 px/t; target front and <=38 px -> push 10, stun 2 |
| Roll attack *(carved)* | A in shove | 3/4/12 | 8 | 14 + 16x14 | 12 | lunge 30, push 10, needs `-DMH_ROLL_ALT=1` |
| Shieldcharge *(carved)* | dir + A | 4/5/14 | 10 | 15 + 18x16 | 14 | lunge 18, push 14, needs `-DMH_ROLL_ALT=1` |

## Shared mechanics

| Mechanic | Rule |
|---|---|
| Combo chain | 3 hits per weapon; chain window 14t opens after a hit; gap lock 9t (24t after the finisher); A buffer 16t |
| Branch (A-B) | B tap in the recovery of hit N (or the chain window from idle) runs branch N; flail stage 1 enters whirl instead of an attack |
| Stance | B held 11t, needs >=10 stam, cancels an attack only when the weapon `canCancel` (sword/gun); drains per tick as listed; 0 stam breaks it with `bLocked` |
| Stance special | A inside the stance; the sword exits the stance on fire, the gun keeps guard while B is held, the flail stays in whirl |
| Double-tap d-pad | Universal dodge roll (all weapons, stowed too): 16t, i-frames 14 (+ armor EVADE_WINDOW), 20 stam, 3.4 px/t |
| Tap defense gate | No new tap-defense/roll while an evade/stun/special runs; out of an attack only when `canCancel` |
| Sheathe | Hold A 24t on an armed press (not while a charge release is pending) |
| Draw | A while stowed: rooted windup (sword 6 / flail 10 / gun 16t), then combo hit 1; movement and B are inert during the windup |
| Stowed | Run 24 (1.5 px/t); B tap inert; A = draw / gather node / carve; B hold = herb use |

Dead data: the gun's `shells[2]` rows (ball `2 x 28 dmg`, scatter `5 x 7 dmg`)
and `chargeShells[2]` are unreferenced since the hitscan arrowshot rework
(prg.11 removed the charged ball); only the weapon blob keeps them for the
packed ABI.

## Verify / regenerate

- `make test` — `tst/player_test.hpp` pins every attack/special/branch/charge
  row and the FSM gates; `tst/hitbox_reach_test.hpp` pins the mask boxes.
- `make gen` — regenerates `src/generated/player_boxes.hpp` from
  `images/masks/mh_player_base_16x16.png`; `make gen-check` gates the result.
- `FXTEST_ONLY=test_data` — device suite pins the packed `mhWeaponDefs` blob.
