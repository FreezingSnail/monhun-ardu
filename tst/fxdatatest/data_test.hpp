#pragma once
// On-device guard for the FX-cart content tables (bead monhun-ardu-42n.1).
//
// The tables are serialized by tools/gen-fxtables.cpp into the packed AVR
// layout. This suite reads them back through the shipping accessors and
// compares against the mock numbers, then checks struct sizes, strides and
// field offsets via pointer differences on the fake cart pointers, so a
// host-padded blob or a serialization-order slip cannot pass silently.
//
// Reads run here in setup() with no plane blits active, the same window the
// sim uses (between waitForNextPlane and paint).

#include "harness/fxtest.hpp"
#include "src/core/game.hpp"

#include <stdint.h>

namespace data {

using namespace mh;

static inline uint16_t off(const void *base, const void *field) {
    return static_cast<uint16_t>(reinterpret_cast<const uint8_t *>(field) - reinterpret_cast<const uint8_t *>(base));
}

inline void test_data(FxTest &test) {
    // ------------------------------------------------- packed AVR layout
    test.expectEq(sizeof(Attack), 23, F("sizeof Attack"));
    test.expectEq(sizeof(Branch), 27, F("sizeof Branch"));
    test.expectEq(sizeof(ShellDef), 15, F("sizeof ShellDef"));
    test.expectEq(sizeof(WeaponDef), 180, F("sizeof WeaponDef"));
    test.expectEq(sizeof(MonsterAttack), 17, F("sizeof MonsterAttack"));
    test.expectEq(sizeof(MonsterDef), 11, F("sizeof MonsterDef"));

    const WeaponDef *w0 = &WEAPON_DEFS[0];
    const WeaponDef *w1 = &WEAPON_DEFS[1];
    const WeaponDef *w2 = &WEAPON_DEFS[2];
    test.expectEq(off(w0, w1), 180, F("weapon stride 1"));
    test.expectEq(off(w0, w2), 360, F("weapon stride 2"));
    test.expectEq(off(&MONSTER_ATTACKS[0], &MONSTER_ATTACKS[1]), 17, F("monster stride"));
    test.expectEq(off(&MONSTER_DEFS[0], &MONSTER_DEFS[1]), 11, F("monsterdef stride 1"));
    test.expectEq(off(&MONSTER_DEFS[0], &MONSTER_DEFS[2]), 22, F("monsterdef stride 2"));
    test.expectEq(off(&w0->attacks[0], &w0->attacks[1]), 23, F("attack stride"));
    test.expectEq(off(&w0->branches[0], &w0->branches[1]), 27, F("branch stride"));
    test.expectEq(off(&w0->shells[0], &w0->shells[1]), 15, F("shell stride"));

    // ----------------------------------------------- WeaponDef field map
    test.expectEq(off(w0, &w0->id), 0, F("weapon.id off"));
    test.expectEq(off(w0, &w0->spd), 1, F("weapon.spd off"));
    test.expectEq(off(w0, &w0->attacks), 3, F("weapon.attacks off"));
    test.expectEq(off(w0, &w0->special), 72, F("weapon.special off"));
    test.expectEq(off(w0, &w0->branches), 95, F("weapon.branches off"));
    test.expectEq(off(w0, &w0->canCancel), 149, F("weapon.canCancel off"));
    test.expectEq(off(w0, &w0->shells), 150, F("weapon.shells off"));

    const Attack *a = &w0->attacks[0];
    test.expectEq(off(a, &a->lunge), 16, F("attack.lunge off"));
    test.expectEq(off(a, &a->push), 18, F("attack.push off"));
    test.expectEq(off(a, &a->effect), 20, F("attack.effect off"));
    test.expectEq(off(a, &a->shell), 21, F("attack.shell off"));
    test.expectEq(off(a, &a->id), 22, F("attack.id off"));

    const Branch *b = &w0->branches[0];
    test.expectEq(off(b, &b->stage), 0, F("branch.stage off"));
    test.expectEq(off(b, &b->stance), 1, F("branch.stance off"));
    test.expectEq(off(b, &b->autoT), 2, F("branch.autoT off"));
    test.expectEq(off(b, &b->atk), 4, F("branch.atk off"));

    const ShellDef *s = &w2->shells[0];
    test.expectEq(off(s, &s->count), 0, F("shell.count off"));
    test.expectEq(off(s, &s->dmg), 2, F("shell.dmg off"));
    test.expectEq(off(s, &s->speedF), 4, F("shell.speedF off"));
    test.expectEq(off(s, &s->w), 6, F("shell.w off"));
    test.expectEq(off(s, &s->h), 8, F("shell.h off"));
    test.expectEq(off(s, &s->reload), 10, F("shell.reload off"));
    test.expectEq(off(s, &s->stam), 12, F("shell.stam off"));
    test.expectEq(off(s, &s->pellets), 14, F("shell.pellets off"));

    const MonsterAttack *ma = &MONSTER_ATTACKS[0];
    test.expectEq(off(ma, &ma->kind), 0, F("monster.kind off"));
    test.expectEq(off(ma, &ma->windup), 1, F("monster.windup off"));
    test.expectEq(off(ma, &ma->active), 3, F("monster.active off"));
    test.expectEq(off(ma, &ma->recover), 5, F("monster.recover off"));
    test.expectEq(off(ma, &ma->speedF), 7, F("monster.speedF off"));
    test.expectEq(off(ma, &ma->dmg), 9, F("monster.dmg off"));
    test.expectEq(off(ma, &ma->reach), 11, F("monster.reach off"));
    test.expectEq(off(ma, &ma->hw), 13, F("monster.hw off"));
    test.expectEq(off(ma, &ma->hh), 15, F("monster.hh off"));

    const MonsterDef *md = &MONSTER_DEFS[0];
    test.expectEq(off(md, &md->kind), 0, F("monsterdef.kind off"));
    test.expectEq(off(md, &md->w), 1, F("monsterdef.w off"));
    test.expectEq(off(md, &md->h), 3, F("monsterdef.h off"));
    test.expectEq(off(md, &md->hp), 5, F("monsterdef.hp off"));
    test.expectEq(off(md, &md->spd), 7, F("monsterdef.spd off"));
    test.expectEq(off(md, &md->atkDist), 9, F("monsterdef.atkDist off"));

    // ------------------------------------------ sword values (mock/game.js)
    test.expectEq(weaponId(w0), W_SWORD, F("sword id"));
    test.expectEq(weaponSpd(w0), 18, F("sword spd"));
    test.expectEq(weaponCanCancel(w0), 1, F("sword cancel"));

    const Attack *a0 = weaponAttack(w0, 0);
    test.expectEq(attackStartup(a0), 3, F("s0 startup"));
    test.expectEq(attackActive(a0), 5, F("s0 active"));
    test.expectEq(attackRecover(a0), 8, F("s0 recover"));
    test.expectEq(attackDmg(a0), 9, F("s0 dmg"));
    test.expectEq(attackReach(a0), 13, F("s0 reach"));
    test.expectEq(attackHw(a0), 12, F("s0 hw"));
    test.expectEq(attackHh(a0), 10, F("s0 hh"));
    test.expectEq(attackStam(a0), 9, F("s0 stam"));
    test.expectEq(attackLunge(a0), 0, F("s0 lunge"));
    test.expectEq(attackPush(a0), 0, F("s0 push"));
    test.expectEq(attackEffect(a0), 0, F("s0 effect"));
    test.expectEq(attackShell(a0), 0, F("s0 shell"));
    test.expectEq(attackId(a0), ATK_NONE, F("s0 id"));

    const Attack *a2 = weaponAttack(w0, 2);
    test.expectEq(attackStartup(a2), 5, F("s2 startup"));
    test.expectEq(attackActive(a2), 6, F("s2 active"));
    test.expectEq(attackRecover(a2), 14, F("s2 recover"));
    test.expectEq(attackDmg(a2), 17, F("s2 dmg"));
    test.expectEq(attackReach(a2), 16, F("s2 reach"));
    test.expectEq(attackHw(a2), 18, F("s2 hw"));
    test.expectEq(attackHh(a2), 14, F("s2 hh"));
    test.expectEq(attackStam(a2), 15, F("s2 stam"));

    const Attack *as = weaponSpecial(w0);
    test.expectEq(attackStartup(as), 4, F("special startup"));
    test.expectEq(attackActive(as), 6, F("special active"));
    test.expectEq(attackRecover(as), 16, F("special recover"));
    test.expectEq(attackDmg(as), 24, F("special dmg"));
    test.expectEq(attackReach(as), 18, F("special reach"));
    test.expectEq(attackHw(as), 20, F("special hw"));
    test.expectEq(attackHh(as), 16, F("special hh"));
    test.expectEq(attackStam(as), 20, F("special stam"));

    const Branch *b0 = weaponBranch(w0, 0);
    test.expectEq(branchStage(b0), 1, F("stepslash stage"));
    test.expectEq(branchStance(b0), ST_NONE, F("stepslash stance"));
    test.expectEq(branchAutoT(b0), 0, F("stepslash auto"));
    test.expectEq(attackLunge(branchAtk(b0)), 42, F("stepslash lunge"));
    test.expectEq(attackDmg(branchAtk(b0)), 12, F("stepslash dmg"));
    test.expectEq(attackReach(branchAtk(b0)), 18, F("stepslash reach"));
    test.expectEq(attackId(branchAtk(b0)), ATK_STEPSLASH, F("stepslash id"));

    const Branch *b1 = weaponBranch(w0, 1);
    test.expectEq(branchStage(b1), 2, F("spincut stage"));
    test.expectEq(attackDmg(branchAtk(b1)), 20, F("spincut dmg"));
    test.expectEq(attackHw(branchAtk(b1)), 28, F("spincut hw"));
    test.expectEq(attackHh(branchAtk(b1)), 26, F("spincut hh"));
    test.expectEq(attackId(branchAtk(b1)), ATK_SPINCUT, F("spincut id"));

    const ShellDef *ss0 = weaponShell(w0, 0);
    const ShellDef *ss1 = weaponShell(w0, 1);
    test.expectEq(shellCount(ss0), 0, F("sword shell0 count"));
    test.expectEq(shellCount(ss1), 0, F("sword shell1 count"));

    // ------------------------------------------ flail values (mock/game.js)
    test.expectEq(weaponId(w1), W_FLAIL, F("flail id"));
    test.expectEq(weaponSpd(w1), 15, F("flail spd"));
    test.expectEq(weaponCanCancel(w1), 0, F("flail cancel"));

    const Attack *f0 = weaponAttack(w1, 0);
    test.expectEq(attackStartup(f0), 8, F("f0 startup"));
    test.expectEq(attackActive(f0), 6, F("f0 active"));
    test.expectEq(attackRecover(f0), 9, F("f0 recover"));
    test.expectEq(attackDmg(f0), 14, F("f0 dmg"));
    test.expectEq(attackReach(f0), 19, F("f0 reach"));
    test.expectEq(attackHw(f0), 20, F("f0 hw"));
    test.expectEq(attackHh(f0), 16, F("f0 hh"));
    test.expectEq(attackStam(f0), 13, F("f0 stam"));

    const Attack *fs = weaponSpecial(w1);
    test.expectEq(attackDmg(fs), 27, F("flail special dmg"));
    test.expectEq(attackReach(fs), 32, F("flail special reach"));
    test.expectEq(attackHw(fs), 14, F("flail special hw"));
    test.expectEq(attackHh(fs), 18, F("flail special hh"));
    test.expectEq(attackStam(fs), 22, F("flail special stam"));

    const Branch *fb0 = weaponBranch(w1, 0);
    test.expectEq(branchStage(fb0), 1, F("whirl stage"));
    test.expectEq(branchStance(fb0), ST_WHIRL, F("whirl stance"));
    test.expectEq(branchAutoT(fb0), 50, F("whirl auto"));

    const Branch *fb1 = weaponBranch(w1, 1);
    test.expectEq(branchStage(fb1), 2, F("trip stage"));
    test.expectEq(attackStartup(branchAtk(fb1)), 5, F("trip startup"));
    test.expectEq(attackActive(branchAtk(fb1)), 6, F("trip active"));
    test.expectEq(attackRecover(branchAtk(fb1)), 16, F("trip recover"));
    test.expectEq(attackDmg(branchAtk(fb1)), 12, F("trip dmg"));
    test.expectEq(attackReach(branchAtk(fb1)), 22, F("trip reach"));
    test.expectEq(attackHw(branchAtk(fb1)), 22, F("trip hw"));
    test.expectEq(attackHh(branchAtk(fb1)), 14, F("trip hh"));
    test.expectEq(attackStam(branchAtk(fb1)), 14, F("trip stam"));
    test.expectEq(attackEffect(branchAtk(fb1)), 1, F("trip effect"));
    test.expectEq(attackShell(branchAtk(fb1)), 0, F("trip shell"));
    test.expectEq(attackId(branchAtk(fb1)), ATK_TRIP, F("trip id"));

    // ---------------------------------------- gunshield values (mock/game.js)
    test.expectEq(weaponId(w2), W_GUN, F("gun id"));
    test.expectEq(weaponSpd(w2), 9, F("gun spd"));
    test.expectEq(weaponCanCancel(w2), 1, F("gun cancel"));

    const Attack *g0 = weaponAttack(w2, 0);
    test.expectEq(attackStartup(g0), 5, F("g0 startup"));
    test.expectEq(attackActive(g0), 4, F("g0 active"));
    test.expectEq(attackRecover(g0), 11, F("g0 recover"));
    test.expectEq(attackDmg(g0), 6, F("g0 dmg"));
    test.expectEq(attackReach(g0), 11, F("g0 reach"));
    test.expectEq(attackHw(g0), 14, F("g0 hw"));
    test.expectEq(attackHh(g0), 12, F("g0 hh"));
    test.expectEq(attackStam(g0), 8, F("g0 stam"));

    const Attack *gs = weaponSpecial(w2);
    test.expectEq(attackStartup(gs), 0, F("gun special startup"));
    test.expectEq(attackDmg(gs), 0, F("gun special dmg"));
    test.expectEq(attackId(gs), ATK_NONE, F("gun special id"));

    const Branch *gb0 = weaponBranch(w2, 0);
    test.expectEq(branchStage(gb0), 1, F("pointblank stage"));
    test.expectEq(attackStartup(branchAtk(gb0)), 4, F("pointblank startup"));
    test.expectEq(attackActive(branchAtk(gb0)), 5, F("pointblank active"));
    test.expectEq(attackRecover(branchAtk(gb0)), 16, F("pointblank recover"));
    test.expectEq(attackDmg(branchAtk(gb0)), 22, F("pointblank dmg"));
    test.expectEq(attackReach(branchAtk(gb0)), 15, F("pointblank reach"));
    test.expectEq(attackHw(branchAtk(gb0)), 18, F("pointblank hw"));
    test.expectEq(attackHh(branchAtk(gb0)), 16, F("pointblank hh"));
    test.expectEq(attackStam(branchAtk(gb0)), 6, F("pointblank stam"));
    test.expectEq(attackShell(branchAtk(gb0)), 1, F("pointblank shell"));
    test.expectEq(attackId(branchAtk(gb0)), ATK_POINTBLANK, F("pointblank id"));

    const Branch *gb1 = weaponBranch(w2, 1);
    test.expectEq(branchStage(gb1), 2, F("guardbash stage"));
    test.expectEq(attackStartup(branchAtk(gb1)), 4, F("guardbash startup"));
    test.expectEq(attackActive(branchAtk(gb1)), 4, F("guardbash active"));
    test.expectEq(attackRecover(branchAtk(gb1)), 12, F("guardbash recover"));
    test.expectEq(attackDmg(branchAtk(gb1)), 9, F("guardbash dmg"));
    test.expectEq(attackReach(branchAtk(gb1)), 14, F("guardbash reach"));
    test.expectEq(attackHw(branchAtk(gb1)), 16, F("guardbash hw"));
    test.expectEq(attackHh(branchAtk(gb1)), 14, F("guardbash hh"));
    test.expectEq(attackStam(branchAtk(gb1)), 8, F("guardbash stam"));
    test.expectEq(attackLunge(branchAtk(gb1)), 0, F("guardbash lunge"));
    test.expectEq(attackPush(branchAtk(gb1)), 12, F("guardbash push"));
    test.expectEq(attackEffect(branchAtk(gb1)), 0, F("guardbash effect"));
    test.expectEq(attackShell(branchAtk(gb1)), 0, F("guardbash shell"));
    test.expectEq(attackId(branchAtk(gb1)), ATK_GUARDBASH, F("guardbash id"));

    const ShellDef *ball = weaponShell(w2, 0);
    test.expectEq(shellCount(ball), 2, F("ball count"));
    test.expectEq(shellDmg(ball), 28, F("ball dmg"));
    test.expectEq(shellSpeedF(ball), 35, F("ball speedF"));
    test.expectEq(shellW(ball), 7, F("ball w"));
    test.expectEq(shellH(ball), 6, F("ball h"));
    test.expectEq(shellReload(ball), 70, F("ball reload"));
    test.expectEq(shellStam(ball), 6, F("ball stam"));
    test.expectEq(shellPellets(ball), 1, F("ball pellets"));

    const ShellDef *scat = weaponShell(w2, 1);
    test.expectEq(shellCount(scat), 5, F("scatter count"));
    test.expectEq(shellDmg(scat), 7, F("scatter dmg"));
    test.expectEq(shellSpeedF(scat), 42, F("scatter speedF"));
    test.expectEq(shellW(scat), 4, F("scatter w"));
    test.expectEq(shellH(scat), 4, F("scatter h"));
    test.expectEq(shellReload(scat), 30, F("scatter reload"));
    test.expectEq(shellStam(scat), 5, F("scatter stam"));
    test.expectEq(shellPellets(scat), 3, F("scatter pellets"));

    // ------------------------------------ monster values (mock/game.js)
    const MonsterAttack *m0 = &MONSTER_ATTACKS[0];
    test.expectEq(monsterAttackKind(m0), MK_LUNGE, F("lunge kind"));
    test.expectEq(monsterAttackWindup(m0), 40, F("lunge windup"));
    test.expectEq(monsterAttackActive(m0), 10, F("lunge active"));
    test.expectEq(monsterAttackRecover(m0), 55, F("lunge recover"));
    test.expectEq(monsterAttackSpeedF(m0), 34, F("lunge speedF"));
    test.expectEq(monsterAttackDmg(m0), 12, F("lunge dmg"));
    test.expectEq(monsterAttackReach(m0), 12, F("lunge reach"));
    test.expectEq(monsterAttackHw(m0), 24, F("lunge hw"));
    test.expectEq(monsterAttackHh(m0), 22, F("lunge hh"));

    const MonsterAttack *m1 = &MONSTER_ATTACKS[1];
    test.expectEq(monsterAttackKind(m1), MK_SWEEP, F("sweep kind"));
    test.expectEq(monsterAttackWindup(m1), 48, F("sweep windup"));
    test.expectEq(monsterAttackActive(m1), 12, F("sweep active"));
    test.expectEq(monsterAttackRecover(m1), 60, F("sweep recover"));
    test.expectEq(monsterAttackSpeedF(m1), 0, F("sweep speedF"));
    test.expectEq(monsterAttackDmg(m1), 9, F("sweep dmg"));
    test.expectEq(monsterAttackReach(m1), 17, F("sweep reach"));
    test.expectEq(monsterAttackHw(m1), 32, F("sweep hw"));
    test.expectEq(monsterAttackHh(m1), 24, F("sweep hh"));

    // ---------------------------- monster roster (mock/game.js, 6zb.1)
    const MonsterDef *md0 = &MONSTER_DEFS[0];
    test.expectEq(monsterDefKind(md0), MON_LUNGE, F("lunge def kind"));
    test.expectEq(monsterDefW(md0), 32, F("lunge def w"));
    test.expectEq(monsterDefH(md0), 24, F("lunge def h"));
    test.expectEq(monsterDefHp(md0), 200, F("lunge def hp"));
    test.expectEq(monsterDefSpd(md0), 5, F("lunge def spd"));
    test.expectEq(monsterDefAtkDist(md0), 32, F("lunge def atkDist"));

    const MonsterDef *md1 = &MONSTER_DEFS[1];
    test.expectEq(monsterDefKind(md1), MON_SWEEP, F("sweep def kind"));
    test.expectEq(monsterDefW(md1), 28, F("sweep def w"));
    test.expectEq(monsterDefH(md1), 22, F("sweep def h"));
    test.expectEq(monsterDefHp(md1), 150, F("sweep def hp"));
    test.expectEq(monsterDefSpd(md1), 7, F("sweep def spd"));
    test.expectEq(monsterDefAtkDist(md1), -1, F("sweep def atkDist"));

    const MonsterDef *md2 = &MONSTER_DEFS[2];
    test.expectEq(monsterDefKind(md2), MON_HEAVY, F("heavy def kind"));
    test.expectEq(monsterDefW(md2), 40, F("heavy def w"));
    test.expectEq(monsterDefH(md2), 28, F("heavy def h"));
    test.expectEq(monsterDefHp(md2), 320, F("heavy def hp"));
    test.expectEq(monsterDefSpd(md2), 3, F("heavy def spd"));
    test.expectEq(monsterDefAtkDist(md2), 24, F("heavy def atkDist"));
}

}   // namespace data
