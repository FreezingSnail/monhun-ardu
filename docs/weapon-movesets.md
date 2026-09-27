# Weapon movesets — demo build (sword / flail / gunshield)

Every move the `make demo` build can perform (same gameplay flags as
shipping). Source of truth is the code: `WEAPON_DEFS` in `src/core/game.hpp`,
the melee boxes in `src/generated/player_boxes.hpp` (generated from
`images/masks/mh_player_base_16x16.png`), and the FSM timings in
`src/core/player.hpp`. Host tests pin every number (`tst/player_test.hpp`,
`tst/hitbox_reach_test.hpp`).

How to read:

- Ticks are logic ticks at 52 Hz. `S/A/R` = startup / active / recover; the
  melee box is live during the active window only.
- Box = `reach` px along the facing vector + `hw x hh` px (rect centred at the
  reach point).
- `Dmg` is base damage; the sim truncates at each step (smith tier, then armor
  ATTACK_UP); the sword riposte special doubles after that fold.
- `Spd` is walk speed in 1/16 px per tick (stowed run is 24 = 1.5 px/t).

## Sword — spd 14 (0.88 px/t), canCancel, parry stance

| Move | Input | S/A/R | Dmg | Box | Stam | Notes |
|---|---|---|---|---|---|---|
| Hit 1 | A | 3/5/8 | 9 | 13 + 12x10 | 14 | chain window 14t after the hit |
| Hit 2 | A,A | 3/5/8 | 10 | 13 + 12x10 | 14 | |
| Hit 3 | A,A,A | 5/6/14 | 17 | 16 + 18x14 | 22 | finisher lock 24t |
| Branch 1 stepslash | B in hit-1 recovery | 3/5/12 | 12 | 18 + 14x12 | 15 | lunge 42 |
| Branch 2 spincut | B in hit-2 recovery | 5/7/15 | 20 | 12 + 28x26 | 24 | |
| Parry | B hold 11t | - | - | - | drain 8/t | cap 34t; `stanceT <= 20` parries: beast stun 60, riposteT 90 |
| Riposte special | A in parry | 4/6/16 | 24 | 18 + 20x16 | 30 | x2 dmg while riposteT is live |
| Dodge | B tap | 16t | - | - | 20 | i-frames 14, 3.4 px/t |

## Flail — spd 12 (0.75 px/t), no cancel, whirl stance

| Move | Input | S/A/R | Dmg | Box | Stam | Notes |
|---|---|---|---|---|---|---|
| Hit 1 | A | 8/6/9 | 14 | 19 + 20x16 | 20 | |
| Hit 2 | A,A | 6/6/9 | 17 | 21 + 22x16 | 18 | |
| Hit 3 | A,A,A | 5/7/15 | 25 | 24 + 24x20 | 26 | finisher lock 24t |
| Branch 1 -> whirl | B in hit-1 recovery | - | - | - | - | stance auto-exits after 50t |
| Branch 2 trip | B in hit-2 recovery | 5/6/16 | 12 | 22 + 22x14 | 21 | trip effect |
| Whirl | B hold 11t | - | 8 / 16t | r24 circle | drain 14/t | push 8 per ring hit |
| Ball throw | A in whirl | 4/8/14 | 27 | 32 + 14x18 | 33 | throw cooldown 50t |
| Deflect | B tap | 9t | - | - | 10 | back-step 1.9 px/t; a hit during it stuns the beast 28 |
| Charge slam | hold A >=14t past a swing, release | 4/6/14 | 24 | 26 + 28x18 | 21 | flail is the only charge weapon |

## Gunshield — spd 7 (0.44 px/t), canCancel, guard stance

| Move | Input | S/A/R | Dmg | Box | Stam | Notes |
|---|---|---|---|---|---|---|
| Hit 1 | A | 5/4/11 | 6 | 11 + 14x12 | 12 | |
| Hit 2 | A,A | 5/4/11 | 7 | 11 + 14x12 | 12 | |
| Hit 3 | A,A,A | 7/5/15 | 11 | 13 + 16x14 | 20 | finisher lock 24t |
| Branch 1 pointblank | B in hit-1 recovery | 4/5/16 | 22 | 15 + 18x16 | 9 | |
| Branch 2 guardbash | B in hit-2 recovery | 4/4/12 | 9 | 14 + 16x14 | 12 | push 12 |
| Guard | B hold 11t | - | - | - | drain 7/t | block: chip 25%, -28 stam, break -> stun 45 |
| Arrowshot | A in guard | 6/4/16 | 12 | 44 + 8x6 | 21 | hitscan, nock 24t, guard stays while B is held |
| Shove | B tap | 10t | - | - | 10 | lean 1.5 px/t; target front and <=38 px -> push 10, stun 2 |

## Shared mechanics

| Mechanic | Rule |
|---|---|
| Combo chain | 3 hits per weapon; chain window 14t opens after a hit; gap lock 9t (24t after the finisher); A buffer 16t |
| Branch (A-B) | B tap in the recovery of hit N (or the chain window from idle) runs branch N; the flail's hit-1 branch enters whirl instead of an attack; B after the finisher is a tap defense |
| Stance | B held 11t, needs >=10 stam, cancels an attack only when the weapon `canCancel` (sword/gun); drains per tick as listed; 0 stam breaks it with `bLocked` |
| Stance special | A inside the stance; the sword exits the stance on fire, the gun keeps guard while B is held, the flail stays in whirl |
| Double-tap d-pad | Universal dodge roll (all weapons, stowed too): 16t, i-frames 14 (+ armor EVADE_WINDOW), 20 stam, 3.4 px/t |
| Tap defense gate | No new tap-defense/roll while an evade/stun/special runs; out of an attack only when `canCancel` |
| Sheathe | Hold A 24t on an armed press (not while a charge release is pending) |
| Draw | A while stowed: rooted windup (sword 6 / flail 10 / gun 16t), then combo hit 1; movement and B are inert during the windup |
| Stowed | Run 24 (1.5 px/t); B tap inert; A = draw / gather node / carve; B hold = herb use |

## Verify

- `make test` — `tst/player_test.hpp` pins every attack/special/branch/charge
  row and the FSM gates; `tst/hitbox_reach_test.hpp` pins the mask boxes.
- `FXTEST_ONLY=test_data` — device suite pins the packed `mhWeaponDefs` blob.
