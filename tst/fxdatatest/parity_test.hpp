#pragma once
// On-device sim parity suite (bead monhun-ardu-p82).
//
// Replays the deterministic input scripts in parity_fixtures.hpp (generated
// from mock/game.js by tools/gen-parity-fixtures.js) through the real C++ core
// (mh::stepGame) tick by tick and compares:
//   - a 16-bit hash of the FULL sim state every tick        -> catches drift
//   - a packed 20-field snapshot every 16 ticks and final   -> readable fields
//
// Fields: player x/y/hp/stam/state/stance/chain, ball+scatter ammo, reload,
// projectile count, train total/last, monster x/y/state/hp/stun, camera x/y.
// Fixtures live in flash; reads go through the portable mhPgmRead* helpers so
// RAM stays clear of the generated tables. No float: raw ints only.

#include "harness/fxtest.hpp"
#include "src/core/world.hpp"
#include "parity_fixtures.hpp"

#include <stdint.h>

namespace parity {

using namespace mh;

// --------------------------------------------------------------- state hash
// Must match hashState() in tools/gen-parity-fixtures.js exactly (same field
// order, same FNV-1a constants, same final fold).

static inline uint32_t mix(uint32_t h, int32_t v) {
    h ^= static_cast<uint32_t>(v);
    h *= 16777619u;
    return h;
}

static uint16_t hashState(const Game &g) {
    const Player &p = g.player;
    const Monster &m = g.monster;
    uint32_t h = 2166136261u;

    // game scalars
    h = mix(h, g.tick);
    h = mix(h, g.freeze);
    h = mix(h, g.over);
    h = mix(h, g.mode);
    h = mix(h, g.prevA ? 1 : 0);
    h = mix(h, g.prevB ? 1 : 0);
    h = mix(h, g.camX);
    h = mix(h, g.camY);

    // player
    h = mix(h, p.x);
    h = mix(h, p.y);
    h = mix(h, p.subX);
    h = mix(h, p.subY);
    h = mix(h, p.vx);
    h = mix(h, p.vy);
    h = mix(h, p.fx);
    h = mix(h, p.fy);
    h = mix(h, p.hp);
    h = mix(h, p.stam);
    h = mix(h, p.stamSub);
    h = mix(h, p.state);
    h = mix(h, p.t);
    h = mix(h, p.hitDone ? 1 : 0);
    h = mix(h, p.chain);
    h = mix(h, p.chainWin);
    h = mix(h, p.aBuffer);
    h = mix(h, p.stance);
    h = mix(h, p.stanceT);
    h = mix(h, p.stanceAuto);
    h = mix(h, p.whirlTick);
    h = mix(h, p.throwCd);
    h = mix(h, p.riposteT);
    h = mix(h, p.bHeld);
    h = mix(h, p.bReady ? 1 : 0);
    h = mix(h, p.bLocked ? 1 : 0);
    h = mix(h, p.iT);
    h = mix(h, p.shell);
    h = mix(h, p.reload);
    h = mix(h, p.shells[0]);
    h = mix(h, p.shells[1]);
    int32_t aId = 0, aSt = 0, aAc = 0, aRc = 0, aDm = 0, aRe = 0, aSm = 0;
    if (p.atk) {
        aId = attackId(p.atk);
        aSt = attackStartup(p.atk);
        aAc = attackActive(p.atk);
        aRc = attackRecover(p.atk);
        aDm = attackDmg(p.atk);
        aRe = attackReach(p.atk);
        aSm = attackStam(p.atk);
    }
    h = mix(h, aId);
    h = mix(h, aSt);
    h = mix(h, aAc);
    h = mix(h, aRc);
    h = mix(h, aDm);
    h = mix(h, aRe);
    h = mix(h, aSm);

    // monster
    h = mix(h, m.x);
    h = mix(h, m.y);
    h = mix(h, m.subX);
    h = mix(h, m.subY);
    h = mix(h, m.hp);
    h = mix(h, m.state);
    h = mix(h, m.t);
    h = mix(h, m.cd);
    h = mix(h, m.fx);
    h = mix(h, m.fy);
    h = mix(h, m.lvx);
    h = mix(h, m.lvy);
    h = mix(h, m.windupMax);
    h = mix(h, m.hitFlash);
    h = mix(h, m.stun);
    h = mix(h, m.circleDir);
    h = mix(h, m.spd);
    // Monster attack scalars: migration A reads them from the RAM cache
    // (identity = Monster::atkIdx), which mirrors the fixture's m.atk fields.
    // kind is the fixture's MODO (0 lunge / 1 sweep); the cache exposes the
    // move type, so the LUNGE move type maps back to kind 0.
    int32_t mKd = 0, mWu = 0, mAc = 0, mRc = 0, mDm = 0, mRe = 0, mHw = 0, mHh = 0;
    if (m.atkIdx != COMBAT_NO_ATTACK) {
        mKd = (g.combat.attack.moveType == MOVE_LUNGE) ? MK_LUNGE : MK_SWEEP;
        mWu = g.combat.attack.windup;
        mAc = g.combat.attack.active;
        mRc = g.combat.attack.recover;
        mDm = g.combat.attack.dmg;
        mRe = g.combat.attack.win.box.ox;
        mHw = g.combat.attack.win.box.w;
        mHh = g.combat.attack.win.box.h;
    }
    h = mix(h, mKd);
    h = mix(h, mWu);
    h = mix(h, mAc);
    h = mix(h, mRc);
    h = mix(h, mDm);
    h = mix(h, mRe);
    h = mix(h, mHw);
    h = mix(h, mHh);

    // pole / train
    h = mix(h, g.pole.hitFlash);
    h = mix(h, g.train.total);
    h = mix(h, g.train.last);
    h = mix(h, trainDps(g));

    // projectiles (x*16 + subX reconstructs the mock's 1/16 px position)
    h = mix(h, g.projN);
    for (int16_t i = 0; i < g.projN; i++) {
        const Projectile &pr = g.proj[i];
        h = mix(h, pr.x * 16 + pr.subX);
        h = mix(h, pr.y * 16 + pr.subY);
        h = mix(h, pr.vx);
        h = mix(h, pr.vy);
        h = mix(h, pr.w);
        h = mix(h, pr.h);
        h = mix(h, pr.dmg);
        h = mix(h, pr.life);
        h = mix(h, pr.heavy ? 1 : 0);
    }

    // effects
    h = mix(h, g.fxN);
    for (int16_t i = 0; i < g.fxN; i++) {
        const Effect &e = g.fx[i];
        h = mix(h, e.x);
        h = mix(h, e.y);
        h = mix(h, e.t);
        h = mix(h, e.life);
        h = mix(h, e.crit ? 1 : 0);
        h = mix(h, e.text);
    }

    // sheathe + combo debounce/buffer state (udb; appended after effects, mirror
    // of tools/gen-parity-fixtures.js hashState in the same order)
    h = mix(h, p.sheathed ? 1 : 0);
    h = mix(h, p.chainLock);
    h = mix(h, p.bBuffer);

    return static_cast<uint16_t>((h ^ (h >> 16)) & 0xffffu);
}

// --------------------------------------------------------------- snapshot

static int32_t snapField(const Game &g, uint8_t f) {
    const Player &p = g.player;
    const Monster &m = g.monster;
    switch (f) {
    case 0:
        return p.x;
    case 1:
        return p.y;
    case 2:
        return p.hp;
    case 3:
        return p.stam;
    case 4:
        return p.state;
    case 5:
        return p.stance;
    case 6:
        return p.chain;
    case 7:
        return p.shells[0];
    case 8:
        return p.shells[1];
    case 9:
        return p.reload;
    case 10:
        return g.projN;
    case 11:
        return g.train.total;
    case 12:
        return g.train.last;
    case 13:
        return m.x;
    case 14:
        return m.y;
    case 15:
        return m.state;
    case 16:
        return m.hp;
    case 17:
        return m.stun;
    case 18:
        return g.camX;
    case 19:
        return g.camY;
    default:
        return 0;
    }
}

// --------------------------------------------------------------- reporting

static void parCheck(FxTest &test, int32_t got, int32_t want, uint16_t scene, const __FlashStringHelper *what) {
    if (got == want) {
        ++test.passCount;
        return;
    }
    ++test.failCount;
    Serial.print(F("FAIL s="));
    Serial.print(scene);
    Serial.print(' ');
    Serial.print(what);
    Serial.print(F(" got="));
    Serial.print(got);
    Serial.print(F(" want="));
    Serial.println(want);
}

// Print a compact human-readable dump only when a hash/first mismatch lands.
static void dumpState(const Game &g) {
    Serial.print(F("  p("));
    Serial.print(g.player.x);
    Serial.print(',');
    Serial.print(g.player.y);
    Serial.print(F(") st="));
    Serial.print(g.player.state);
    Serial.print(F(" hp="));
    Serial.print(g.player.hp);
    Serial.print(F(" sm="));
    Serial.print(g.player.stam);
    Serial.print(F(" frz="));
    Serial.print(g.freeze);
    Serial.print(F(" pn="));
    Serial.print(g.projN);
    Serial.print(F(" m("));
    Serial.print(g.monster.x);
    Serial.print(',');
    Serial.print(g.monster.y);
    Serial.print(F(") ms="));
    Serial.print(g.monster.state);
    Serial.print(F(" mhp="));
    Serial.println(g.monster.hp);
}

// --------------------------------------------------------------- scenarios

static void test_parity(FxTest &test) {
    static Game g;

    uint16_t inOff = 0, hashOff = 0, snapOff = 0, overOff = 0;
    uint16_t n = 0, ncp = 0;
    uint8_t weapon = 0, mode = 0;

    for (uint16_t s = 0; s < parity_fx::SCENE_COUNT; s++) {
        const uint16_t base = static_cast<uint16_t>(s * parity_fx::META_FIELDS);
        inOff = mhPgmReadU16(&parity_fx::meta[base + 0]);
        hashOff = mhPgmReadU16(&parity_fx::meta[base + 1]);
        snapOff = mhPgmReadU16(&parity_fx::meta[base + 2]);
        overOff = mhPgmReadU16(&parity_fx::meta[base + 3]);
        n = mhPgmReadU16(&parity_fx::meta[base + 4]);
        ncp = mhPgmReadU16(&parity_fx::meta[base + 5]);
        weapon = static_cast<uint8_t>(mhPgmReadU16(&parity_fx::meta[base + 6]));
        mode = static_cast<uint8_t>(mhPgmReadU16(&parity_fx::meta[base + 7]));

        newGame(g, weapon, mode);

        // Reproduce the mock scenario's setup mutations.
        const uint16_t o = overOff;
        g.monster.x = mhPgmReadI16(&parity_fx::overrides[o + 0]);
        g.monster.y = mhPgmReadI16(&parity_fx::overrides[o + 1]);
        g.monster.state = static_cast<MState>(mhPgmReadI16(&parity_fx::overrides[o + 2]));
        g.monster.t = mhPgmReadI16(&parity_fx::overrides[o + 3]);
        g.monster.cd = mhPgmReadI16(&parity_fx::overrides[o + 4]);
        g.monster.fx = mhPgmReadI16(&parity_fx::overrides[o + 5]);
        g.monster.fy = mhPgmReadI16(&parity_fx::overrides[o + 6]);
        const int16_t kind = mhPgmReadI16(&parity_fx::overrides[o + 7]);
        // Fixture records the mock's attack kind (MODO) only; every scene runs
        // the legacy LUNGE creature, whose authored list is [lunge, sweep].
        if (kind >= 0)
            monsterAttackSet(g, kind == MK_LUNGE ? combat::ATTACK_LUNGE_LUNGE : combat::ATTACK_LUNGE_SWEEP);
        else
            g.monster.atkIdx = COMBAT_NO_ATTACK;
        g.pole.rect.x = mhPgmReadI16(&parity_fx::overrides[o + 8]);
        g.pole.rect.y = mhPgmReadI16(&parity_fx::overrides[o + 9]);
        updateActiveTarget(g);

        uint16_t hashFails = 0;
        uint16_t cp = 0;

        for (uint16_t t = 0; t < n; t++) {
            const uint8_t raw = mhPgmReadU8(&parity_fx::inputs[inOff + t]);
            Input in;
            in.mx = static_cast<int8_t>(((raw >> 2) & 3) - 1);
            in.my = static_cast<int8_t>(((raw >> 4) & 3) - 1);
            in.a = (raw & 1) != 0;
            in.b = (raw & 2) != 0;

            stepGame(g, in);

            const uint16_t got = hashState(g);
            const uint16_t want = mhPgmReadU16(&parity_fx::hashes[hashOff + t]);
            if (got != want) {
                if (hashFails == 0) {
                    Serial.print(F("MISMATCH scene="));
                    Serial.print(s);
                    Serial.print(F(" tick="));
                    Serial.println(t + 1);
                    dumpState(g);
                }
                ++hashFails;
            }

            const uint16_t tick = static_cast<uint16_t>(t + 1);
            if (tick % parity_fx::CP_STRIDE == 0 || tick == n) {
                const uint16_t sb = static_cast<uint16_t>(snapOff + cp * parity_fx::CP_FIELDS);
                for (uint8_t f = 0; f < parity_fx::CP_FIELDS; f++) {
                    parCheck(test, snapField(g, f), mhPgmReadI16(&parity_fx::snaps[sb + f]), s, F("snap"));
                }
                ++cp;
            }
        }

        parCheck(test, hashFails, 0, s, F("ticks"));
    }
}

}   // namespace parity
