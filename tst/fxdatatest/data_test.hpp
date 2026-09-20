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
    test.expectEq(sizeof(WeaponDef), 329, F("sizeof WeaponDef"));
    test.expectEq(sizeof(MonsterAttack), 17, F("sizeof MonsterAttack"));
    test.expectEq(sizeof(MonsterDef), 11, F("sizeof MonsterDef"));

    const WeaponDef *w0 = &WEAPON_DEFS[0];
    const WeaponDef *w1 = &WEAPON_DEFS[1];
    const WeaponDef *w2 = &WEAPON_DEFS[2];
    test.expectEq(off(w0, w1), 329, F("weapon stride 1"));
    test.expectEq(off(w0, w2), 658, F("weapon stride 2"));
    test.expectEq(off(&MONSTER_ATTACKS[0], &MONSTER_ATTACKS[1]), 17, F("monster stride"));
    test.expectEq(off(&MONSTER_DEFS[0], &MONSTER_DEFS[1]), 11, F("monsterdef stride 1"));
    test.expectEq(off(&MONSTER_DEFS[0], &MONSTER_DEFS[2]), 22, F("monsterdef stride 2"));
    test.expectEq(off(&w0->attacks[0], &w0->attacks[1]), 23, F("attack stride"));
    test.expectEq(off(&w0->branches[0], &w0->branches[1]), 27, F("branch stride"));
    test.expectEq(off(&w0->branches[1], &w0->branches[2]), 27, F("branch stride 2"));
    test.expectEq(off(&w0->shells[0], &w0->shells[1]), 15, F("shell stride"));
    test.expectEq(off(&w0->charge[0], &w0->charge[1]), 23, F("charge stride"));
    test.expectEq(off(&w0->chargeShells[0], &w0->chargeShells[1]), 15, F("chargeShell stride"));

    // ----------------------------------------------- WeaponDef field map
    test.expectEq(off(w0, &w0->id), 0, F("weapon.id off"));
    test.expectEq(off(w0, &w0->spd), 1, F("weapon.spd off"));
    test.expectEq(off(w0, &w0->attacks), 3, F("weapon.attacks off"));
    test.expectEq(off(w0, &w0->special), 72, F("weapon.special off"));
    test.expectEq(off(w0, &w0->branches), 95, F("weapon.branches off"));
    test.expectEq(off(w0, &w0->canCancel), 176, F("weapon.canCancel off"));
    test.expectEq(off(w0, &w0->shells), 177, F("weapon.shells off"));
    test.expectEq(off(w0, &w0->roll), 207, F("weapon.roll off"));
    test.expectEq(off(w0, &w0->alt), 230, F("weapon.alt off"));
    test.expectEq(off(w0, &w0->charge), 253, F("weapon.charge off"));
    test.expectEq(off(w0, &w0->chargeShells), 299, F("weapon.chargeShells off"));

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
    test.expectEq(weaponSpd(w0), 14, F("sword spd"));
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

    // stage 3 (7pw): no device AtkId for the mock names, so ATK_NONE like roll/alt
    const Branch *b2 = weaponBranch(w0, 2);
    test.expectEq(branchStage(b2), 3, F("helmsplit stage"));
    test.expectEq(branchStance(b2), ST_NONE, F("helmsplit stance"));
    test.expectEq(branchAutoT(b2), 0, F("helmsplit auto"));
    test.expectEq(attackStartup(branchAtk(b2)), 8, F("helmsplit startup"));
    test.expectEq(attackActive(branchAtk(b2)), 4, F("helmsplit active"));
    test.expectEq(attackRecover(branchAtk(b2)), 20, F("helmsplit recover"));
    test.expectEq(attackDmg(branchAtk(b2)), 26, F("helmsplit dmg"));
    test.expectEq(attackReach(branchAtk(b2)), 16, F("helmsplit reach"));
    test.expectEq(attackHw(branchAtk(b2)), 20, F("helmsplit hw"));
    test.expectEq(attackHh(branchAtk(b2)), 22, F("helmsplit hh"));
    test.expectEq(attackStam(branchAtk(b2)), 18, F("helmsplit stam"));
    test.expectEq(attackLunge(branchAtk(b2)), 0, F("helmsplit lunge"));
    test.expectEq(attackPush(branchAtk(b2)), 0, F("helmsplit push"));
    test.expectEq(attackEffect(branchAtk(b2)), 0, F("helmsplit effect"));
    test.expectEq(attackShell(branchAtk(b2)), 0, F("helmsplit shell"));
    test.expectEq(attackId(branchAtk(b2)), ATK_NONE, F("helmsplit id"));

    const ShellDef *ss0 = weaponShell(w0, 0);
    const ShellDef *ss1 = weaponShell(w0, 1);
    test.expectEq(shellCount(ss0), 0, F("sword shell0 count"));
    test.expectEq(shellCount(ss1), 0, F("sword shell1 count"));

    // ------------------------------------------ flail values (mock/game.js)
    test.expectEq(weaponId(w1), W_FLAIL, F("flail id"));
    test.expectEq(weaponSpd(w1), 12, F("flail spd"));
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

    const Branch *fb2 = weaponBranch(w1, 2);
    test.expectEq(branchStage(fb2), 3, F("earthslam stage"));
    test.expectEq(branchStance(fb2), ST_NONE, F("earthslam stance"));
    test.expectEq(attackStartup(branchAtk(fb2)), 10, F("earthslam startup"));
    test.expectEq(attackActive(branchAtk(fb2)), 6, F("earthslam active"));
    test.expectEq(attackRecover(branchAtk(fb2)), 24, F("earthslam recover"));
    test.expectEq(attackDmg(branchAtk(fb2)), 32, F("earthslam dmg"));
    test.expectEq(attackReach(branchAtk(fb2)), 24, F("earthslam reach"));
    test.expectEq(attackHw(branchAtk(fb2)), 32, F("earthslam hw"));
    test.expectEq(attackHh(branchAtk(fb2)), 24, F("earthslam hh"));
    test.expectEq(attackStam(branchAtk(fb2)), 24, F("earthslam stam"));
    test.expectEq(attackPush(branchAtk(fb2)), 12, F("earthslam push"));
    test.expectEq(attackEffect(branchAtk(fb2)), 1, F("earthslam effect"));
    test.expectEq(attackShell(branchAtk(fb2)), 0, F("earthslam shell"));
    test.expectEq(attackId(branchAtk(fb2)), ATK_NONE, F("earthslam id"));

    // ---------------------------------------- gunshield values (mock/game.js)
    test.expectEq(weaponId(w2), W_GUN, F("gun id"));
    test.expectEq(weaponSpd(w2), 7, F("gun spd"));
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

    const Branch *gb2 = weaponBranch(w2, 2);
    test.expectEq(branchStage(gb2), 3, F("cannonblast stage"));
    test.expectEq(branchStance(gb2), ST_NONE, F("cannonblast stance"));
    test.expectEq(attackStartup(branchAtk(gb2)), 6, F("cannonblast startup"));
    test.expectEq(attackActive(branchAtk(gb2)), 3, F("cannonblast active"));
    test.expectEq(attackRecover(branchAtk(gb2)), 20, F("cannonblast recover"));
    test.expectEq(attackDmg(branchAtk(gb2)), 30, F("cannonblast dmg"));
    test.expectEq(attackReach(branchAtk(gb2)), 16, F("cannonblast reach"));
    test.expectEq(attackHw(branchAtk(gb2)), 24, F("cannonblast hw"));
    test.expectEq(attackHh(branchAtk(gb2)), 18, F("cannonblast hh"));
    test.expectEq(attackStam(branchAtk(gb2)), 16, F("cannonblast stam"));
    test.expectEq(attackLunge(branchAtk(gb2)), 0, F("cannonblast lunge"));
    test.expectEq(attackPush(branchAtk(gb2)), 16, F("cannonblast push"));
    test.expectEq(attackEffect(branchAtk(gb2)), 0, F("cannonblast effect"));
    test.expectEq(attackShell(branchAtk(gb2)), 0, F("cannonblast shell"));
    test.expectEq(attackId(branchAtk(gb2)), ATK_NONE, F("cannonblast id"));

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

    // --------------------------- roll attack + direction+A alt (monhun-ardu-8xx)
    const Attack *sroll = weaponRoll(w0);
    test.expectEq(attackStartup(sroll), 4, F("sword roll startup"));
    test.expectEq(attackActive(sroll), 5, F("sword roll active"));
    test.expectEq(attackRecover(sroll), 10, F("sword roll recover"));
    test.expectEq(attackDmg(sroll), 12, F("sword roll dmg"));
    test.expectEq(attackReach(sroll), 15, F("sword roll reach"));
    test.expectEq(attackHw(sroll), 16, F("sword roll hw"));
    test.expectEq(attackHh(sroll), 14, F("sword roll hh"));
    test.expectEq(attackStam(sroll), 10, F("sword roll stam"));
    test.expectEq(attackId(sroll), ATK_NONE, F("sword roll id"));

    const Attack *salt = weaponAlt(w0);
    test.expectEq(attackStartup(salt), 6, F("sword alt startup"));
    test.expectEq(attackActive(salt), 4, F("sword alt active"));
    test.expectEq(attackRecover(salt), 12, F("sword alt recover"));
    test.expectEq(attackDmg(salt), 14, F("sword alt dmg"));
    test.expectEq(attackReach(salt), 22, F("sword alt reach"));
    test.expectEq(attackHw(salt), 10, F("sword alt hw"));
    test.expectEq(attackHh(salt), 10, F("sword alt hh"));
    test.expectEq(attackStam(salt), 12, F("sword alt stam"));
    test.expectEq(attackLunge(salt), 20, F("sword alt lunge"));
    test.expectEq(attackId(salt), ATK_NONE, F("sword alt id"));

    const Attack *froll = weaponRoll(w1);
    test.expectEq(attackStartup(froll), 4, F("flail roll startup"));
    test.expectEq(attackActive(froll), 6, F("flail roll active"));
    test.expectEq(attackRecover(froll), 13, F("flail roll recover"));
    test.expectEq(attackDmg(froll), 15, F("flail roll dmg"));
    test.expectEq(attackReach(froll), 20, F("flail roll reach"));
    test.expectEq(attackHw(froll), 24, F("flail roll hw"));
    test.expectEq(attackHh(froll), 16, F("flail roll hh"));
    test.expectEq(attackStam(froll), 10, F("flail roll stam"));

    const Attack *falt = weaponAlt(w1);
    test.expectEq(attackStartup(falt), 6, F("flail alt startup"));
    test.expectEq(attackActive(falt), 6, F("flail alt active"));
    test.expectEq(attackRecover(falt), 14, F("flail alt recover"));
    test.expectEq(attackDmg(falt), 18, F("flail alt dmg"));
    test.expectEq(attackReach(falt), 22, F("flail alt reach"));
    test.expectEq(attackHw(falt), 30, F("flail alt hw"));
    test.expectEq(attackHh(falt), 14, F("flail alt hh"));
    test.expectEq(attackStam(falt), 14, F("flail alt stam"));

    const Attack *groll = weaponRoll(w2);
    test.expectEq(attackStartup(groll), 3, F("gun roll startup"));
    test.expectEq(attackActive(groll), 4, F("gun roll active"));
    test.expectEq(attackRecover(groll), 12, F("gun roll recover"));
    test.expectEq(attackDmg(groll), 8, F("gun roll dmg"));
    test.expectEq(attackReach(groll), 14, F("gun roll reach"));
    test.expectEq(attackHw(groll), 16, F("gun roll hw"));
    test.expectEq(attackHh(groll), 14, F("gun roll hh"));
    test.expectEq(attackStam(groll), 8, F("gun roll stam"));
    test.expectEq(attackLunge(groll), 30, F("gun roll lunge"));
    test.expectEq(attackPush(groll), 10, F("gun roll push"));
    test.expectEq(attackId(groll), ATK_NONE, F("gun roll id"));

    const Attack *galt = weaponAlt(w2);
    test.expectEq(attackStartup(galt), 4, F("gun alt startup"));
    test.expectEq(attackActive(galt), 5, F("gun alt active"));
    test.expectEq(attackRecover(galt), 14, F("gun alt recover"));
    test.expectEq(attackDmg(galt), 10, F("gun alt dmg"));
    test.expectEq(attackReach(galt), 15, F("gun alt reach"));
    test.expectEq(attackHw(galt), 18, F("gun alt hw"));
    test.expectEq(attackHh(galt), 16, F("gun alt hh"));
    test.expectEq(attackStam(galt), 9, F("gun alt stam"));
    test.expectEq(attackLunge(galt), 18, F("gun alt lunge"));
    test.expectEq(attackPush(galt), 14, F("gun alt push"));
    test.expectEq(attackId(galt), ATK_NONE, F("gun alt id"));

    // ------------------------------- charge attacks + charge shells (ynb)
    // Sword has no charge data: both slots zero (weaponHasCharge false).
    test.expectEq(weaponHasCharge(w0), 0, F("sword has no charge"));
    test.expectEq(weaponHasChargeShells(w0), 0, F("sword has no charge shells"));

    const Attack *fc0 = weaponCharge(w1, 0);
    test.expectEq(attackStartup(fc0), 4, F("chargeslam1 startup"));
    test.expectEq(attackActive(fc0), 6, F("chargeslam1 active"));
    test.expectEq(attackRecover(fc0), 14, F("chargeslam1 recover"));
    test.expectEq(attackDmg(fc0), 24, F("chargeslam1 dmg"));
    test.expectEq(attackReach(fc0), 26, F("chargeslam1 reach"));
    test.expectEq(attackHw(fc0), 28, F("chargeslam1 hw"));
    test.expectEq(attackHh(fc0), 18, F("chargeslam1 hh"));
    test.expectEq(attackStam(fc0), 14, F("chargeslam1 stam"));
    test.expectEq(attackEffect(fc0), 0, F("chargeslam1 effect"));
    test.expectEq(attackId(fc0), ATK_NONE, F("chargeslam1 id"));

    const Attack *fc1 = weaponCharge(w1, 1);
    test.expectEq(attackStartup(fc1), 5, F("chargeslam2 startup"));
    test.expectEq(attackActive(fc1), 8, F("chargeslam2 active"));
    test.expectEq(attackRecover(fc1), 20, F("chargeslam2 recover"));
    test.expectEq(attackDmg(fc1), 36, F("chargeslam2 dmg"));
    test.expectEq(attackReach(fc1), 28, F("chargeslam2 reach"));
    test.expectEq(attackHw(fc1), 34, F("chargeslam2 hw"));
    test.expectEq(attackHh(fc1), 24, F("chargeslam2 hh"));
    test.expectEq(attackStam(fc1), 22, F("chargeslam2 stam"));
    test.expectEq(attackEffect(fc1), 1, F("chargeslam2 trip"));
    test.expectEq(weaponHasCharge(w1), 1, F("flail has charge"));

    test.expectEq(weaponHasCharge(w2), 0, F("gun has no melee charge"));
    const ShellDef *gcs0 = weaponChargeShell(w2, 0);
    test.expectEq(shellDmg(gcs0), 34, F("charge ball L1 dmg"));
    test.expectEq(shellSpeedF(gcs0), 45, F("charge ball L1 speedF"));
    test.expectEq(shellW(gcs0), 7, F("charge ball L1 w"));
    test.expectEq(shellH(gcs0), 6, F("charge ball L1 h"));
    test.expectEq(shellReload(gcs0), 70, F("charge ball L1 reload"));
    test.expectEq(shellStam(gcs0), 12, F("charge ball L1 stam"));
    test.expectEq(shellPellets(gcs0), 1, F("charge ball L1 pellets"));

    const ShellDef *gcs1 = weaponChargeShell(w2, 1);
    test.expectEq(shellDmg(gcs1), 46, F("charge ball L2 dmg"));
    test.expectEq(shellSpeedF(gcs1), 55, F("charge ball L2 speedF"));
    test.expectEq(shellW(gcs1), 8, F("charge ball L2 w"));
    test.expectEq(shellH(gcs1), 8, F("charge ball L2 h"));
    test.expectEq(shellReload(gcs1), 70, F("charge ball L2 reload"));
    test.expectEq(shellStam(gcs1), 18, F("charge ball L2 stam"));
    test.expectEq(shellPellets(gcs1), 1, F("charge ball L2 pellets"));
    test.expectEq(weaponHasChargeShells(w2), 1, F("gun has charge shells"));

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
