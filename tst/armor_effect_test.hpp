#pragma once
// Host unit tests for armor combat effects (bead monhun-ardu-arm.3):
// src/armor_state.hpp armorEffects() / armorReduce() / armorRestoreStats(), the
// smith+armor damage fold (upgrade_state.hpp attackMulFold), and the live
// playerHurt / dodge paths in src/core/player.hpp.
//
// The shipped data now activates attack_up / health_up / stamina_up at S/M
// (arm.4), while defense_up (best stack 8) and evade_window (6) stay below
// THRESHOLD_S. The magnitude tests drive synthetic ArmorAgg blocks and synthetic
// ArmorSkill tables so every branch (including the evade iT cap) is exercised;
// the shipped skill table (armor_data::SKILLS) pins the real perPoint values.
#include "test.hpp"
#include "../src/armor_state.hpp"
#include "../src/upgrade_state.hpp"
#include "../src/core/player.hpp"
#include "../src/core/projectiles.hpp"   // addEffect: playerHurt hit sparks
#include "../src/generated/armor_data.hpp"
#include "../src/generated/armor_meta.hpp"

using namespace mh;

namespace armoreffecttest {

// armor_data::Skill (host mirror) -> the engine's plain ArmorSkill view.
inline ArmorSkill toSkill(const armor_data::Skill &s) {
    ArmorSkill out;
    out.kind = s.kind;
    out.maxPoints = s.maxPoints;
    out.perPoint = s.perPoint;
    out.pad = 0;
    return out;
}

inline void shippedSkills(ArmorSkill *out) {
    for (uint8_t i = 0; i < armor::SKILL_COUNT; i++)
        out[i] = toSkill(armor_data::SKILLS[i]);
}

// Direct agg with one skill's points/tier set (bypasses armorAggregate so a
// total can sit at S / M without a synthetic piece table).
inline ArmorAgg aggWith(uint8_t skill, uint8_t points, uint8_t tier) {
    ArmorAgg a;
    armorClear(a);
    a.points[skill] = points;
    a.tier[skill] = tier;
    return a;
}

inline ArmorSkill customSkill(uint8_t kind, uint8_t maxPoints, uint8_t perPoint) {
    ArmorSkill s;
    s.kind = kind;
    s.maxPoints = maxPoints;
    s.perPoint = perPoint;
    s.pad = 0;
    return s;
}

// Synthetic high-point piece so armorAggregate can cross the thresholds.
inline ArmorPiece effectPiece(uint8_t slot, uint8_t skill, uint8_t points, uint8_t defense = 0) {
    ArmorPiece p;
    p.slot = slot;
    p.defense = defense;
    for (uint8_t i = 0; i < ARMOR_RESIST_COUNT; i++)
        p.resist[i] = 0;
    p.skillCount = 1;
    p.skill[0] = static_cast<uint8_t>(skill + 1);
    p.points[0] = points;
    p.skill[1] = 0;
    p.points[1] = 0;
    return p;
}

// Base-effect identity for the shipped empty armor.
inline ArmorEffects baseEffects() {
    ArmorEffects fx;
    armorEffectsBase(fx);
    return fx;
}

}   // namespace armoreffecttest

using namespace armoreffecttest;

void ArmorEffectSuite(TestRunner &runner) {
    TestSuite suite("Armor effects: defense + skills in combat (monhun-ardu-arm.3)");
    ArmorSkill skills[armor::SKILL_COUNT];
    shippedSkills(skills);

    // ------------------------------------------------------ defense reduction
    {
        Test t("armorReduce: dmg*100/(100+def), floor 1, def 0 identity");
        t.assert(armorReduce(30, 0), 30, "def 0 identity");
        t.assert(armorReduce(30, 50), 20, "def 50: 3000/150");
        t.assert(armorReduce(30, 100), 15, "def 100: 3000/200");
        t.assert(armorReduce(9, 50), 6, "truncates: 900/150");
        t.assert(armorReduce(100, 50), 66, "truncates: 10000/150");
        t.assert(armorReduce(2, 100), 1, "200/200 = 1");
        t.assert(armorReduce(1, 100), 1, "0 rounds up to the floor 1");
        t.assert(armorReduce(0, 100), 0, "zero damage stays zero");
        t.assert(armorReduce(255, 255), 71, "25500/355 truncated");
        t.assert(armorReduce(30, 0xFFFF), 1, "absurd def still floors at 1");
        suite.addTest(t);
    }

    // ------------------------------------------------------------- base block
    {
        Test t("empty armor -> identity effects (no-op)");
        ArmorAgg agg;
        armorClear(agg);
        ArmorEffects fx;
        armorEffects(agg, skills, fx);
        t.assert(fx.defense, 0, "0 defense");
        t.assert(fx.hpMax, 100, "hpMax 100");
        t.assert(fx.stamMax, 100, "stamMax 100");
        t.assert(fx.dmgMul, 100, "dmgMul 100");
        t.assert(fx.iT, 0, "no iT bonus");
        const ArmorEffects b = baseEffects();
        t.assert(fx.defense == b.defense && fx.hpMax == b.hpMax && fx.stamMax == b.stamMax && fx.dmgMul == b.dmgMul && fx.iT == b.iT, true, "matches base");
        suite.addTest(t);
    }

    // -------------------------------------------------------- ATTACK_UP fold
    {
        Test t("ATTACK_UP -> dmgMul 100 + points*perPoint");
        ArmorEffects fx;
        armorEffects(aggWith(armor::SKILL_ATTACK_UP, 15, 2), skills, fx);
        t.assert(fx.dmgMul, 130, "15 points * 2 = +30");
        armorEffects(aggWith(armor::SKILL_ATTACK_UP, 10, 1), skills, fx);
        t.assert(fx.dmgMul, 120, "S tier 10 points * 2 = +20");
        armorEffects(aggWith(armor::SKILL_ATTACK_UP, 9, 0), skills, fx);
        t.assert(fx.dmgMul, 100, "inert tier leaves identity");
        suite.addTest(t);
    }

    {
        Test t("attackMulFold: smith tier then armor tier, truncating each step");
        t.assert(attackMulFold(9, 100, 100), 9, "both identity");
        t.assert(attackMulFold(9, 125, 100), 11, "smith only: 1125/100");
        t.assert(attackMulFold(9, 100, 130), 11, "armor only: 1170/100");
        t.assert(attackMulFold(9, 125, 130), 14, "9->11 then 11*130/100");
        t.assert(attackMulFold(17, 110, 120), 21, "17->18 then 18*120/100");
        t.assert(attackMulFold(8, 100, 100), 8, "whirl base unchanged");
        suite.addTest(t);
    }

    // ------------------------------------------------------ HEALTH/STAMINA_UP
    {
        Test t("HEALTH_UP / STAMINA_UP -> hpMax / stamMax");
        ArmorEffects fx;
        armorEffects(aggWith(armor::SKILL_HEALTH_UP, 15, 2), skills, fx);
        t.assert(fx.hpMax, 115, "health +15");
        t.assert(fx.stamMax, 100, "stamina untouched");
        armorEffects(aggWith(armor::SKILL_STAMINA_UP, 15, 2), skills, fx);
        t.assert(fx.stamMax, 115, "stamina +15");
        t.assert(fx.hpMax, 100, "health untouched");
        suite.addTest(t);
    }

    {
        Test t("hpMax / stamMax / dmgMul clamp at 255");
        ArmorSkill big[armor::SKILL_COUNT];
        shippedSkills(big);
        big[armor::SKILL_HEALTH_UP] = customSkill(armor::KIND_HEALTH_UP, 15, 255);
        big[armor::SKILL_STAMINA_UP] = customSkill(armor::KIND_STAMINA_UP, 15, 255);
        big[armor::SKILL_ATTACK_UP] = customSkill(armor::KIND_ATTACK_UP, 15, 255);
        ArmorAgg agg;
        armorClear(agg);
        agg.points[armor::SKILL_HEALTH_UP] = 15;
        agg.tier[armor::SKILL_HEALTH_UP] = 2;
        agg.points[armor::SKILL_STAMINA_UP] = 15;
        agg.tier[armor::SKILL_STAMINA_UP] = 2;
        agg.points[armor::SKILL_ATTACK_UP] = 15;
        agg.tier[armor::SKILL_ATTACK_UP] = 2;
        ArmorEffects fx;
        armorEffects(agg, big, fx);
        t.assert(fx.hpMax, 255, "hpMax clamps");
        t.assert(fx.stamMax, 255, "stamMax clamps");
        t.assert(fx.dmgMul, 255, "dmgMul clamps");
        suite.addTest(t);
    }

    // ---------------------------------------------------- DEFENSE/EVADE skills
    {
        Test t("DEFENSE_UP -> defense, EVADE_WINDOW -> capped iT bonus");
        ArmorAgg agg = aggWith(armor::SKILL_DEFENSE_UP, 15, 2);
        agg.defense = 10;
        ArmorEffects fx;
        armorEffects(agg, skills, fx);
        t.assert(fx.defense, 10 + 30, "base defense + +30");
        // The raw M magnitude would be +15 i-frames on a 14-tick dodge roll;
        // armorEffects caps EVADE_WINDOW iT at ARMOR_EVADE_IT_CAP (4).
        t.assert(ARMOR_EVADE_IT_CAP, 4, "evade iT cap is 4");
        armorEffects(aggWith(armor::SKILL_EVADE_WINDOW, 15, 2), skills, fx);
        t.assert(fx.iT, ARMOR_EVADE_IT_CAP, "15-point evade clamps to +4");
        armorEffects(aggWith(armor::SKILL_EVADE_WINDOW, 10, 1), skills, fx);
        t.assert(fx.iT, ARMOR_EVADE_IT_CAP, "S-tier 10-point evade clamps to +4");
        armorEffects(aggWith(armor::SKILL_EVADE_WINDOW, 3, 1), skills, fx);
        t.assert(fx.iT, 3, "sub-cap bonus passes through");
        armorEffects(aggWith(armor::SKILL_EVADE_WINDOW, 9, 0), skills, fx);
        t.assert(fx.iT, 0, "inert evade leaves 0");
        suite.addTest(t);
    }

    // ------------------------------------------- thresholds through aggregate
    {
        Test t("aggregate S/M thresholds feed the effect magnitudes");
        ArmorPiece table[2];
        table[0] = effectPiece(armor::SLOT_HEAD, armor::SKILL_ATTACK_UP, 9);
        table[1] = effectPiece(armor::SLOT_BODY, armor::SKILL_ATTACK_UP, 6);
        SaveBlock s;
        saveDefaults(s);
        saveSetCrafted(s, 0);
        saveSetCrafted(s, 1);
        ArmorAgg agg;
        ArmorEffects fx;

        armorAggregate(s, table, 2, agg);
        armorEffects(agg, skills, fx);
        t.assert(fx.dmgMul, 100, "no equipment -> inert");

        armorEquipToggle(s, 0, armor::SLOT_HEAD);
        armorAggregate(s, table, 2, agg);
        armorEffects(agg, skills, fx);
        t.assert(fx.dmgMul, 100, "9 < S inert");

        armorEquipToggle(s, 1, armor::SLOT_BODY);
        armorAggregate(s, table, 2, agg);
        armorEffects(agg, skills, fx);
        t.assert(fx.dmgMul, 130, "15 >= M -> +30");
        suite.addTest(t);
    }

    // ------------------------------------------------ hp/stam restore (max)
    {
        Test t("armorRestoreStats: tops up and clamps to the resolved maxes");
        ArmorEffects fx;
        armorEffectsBase(fx);
        fx.hpMax = 130;
        fx.stamMax = 90;
        uint8_t hp = 40, hpMax = 100, stam = 70, stamMax = 100;
        armorRestoreStats(fx, hp, hpMax, stam, stamMax);
        t.assert(hpMax, 130, "hpMax armed");
        t.assert(stamMax, 90, "stamMax armed");
        t.assert(hp, 130, "hp topped to max");
        t.assert(stam, 90, "stam topped to max");
        fx.hpMax = 255;
        armorRestoreStats(fx, hp, hpMax, stam, stamMax);
        t.assert(hpMax, 255, "clamped max holds 255");
        t.assert(hp, 255, "hp follows the clamp");
        suite.addTest(t);
    }

    // --------------------------------------------------- live player paths
    {
        Test t("playerHurt: armor reduces incoming damage, empty armor is a no-op");
        Game g;
        initGame(g, W_SWORD);
        g.player.hp = 100;
        g.player.iT = 0;
        g.player.state = PS_IDLE;
        playerHurt(g, 30, 0, 0);
        t.assert(g.player.hp, 70, "def 0 -> full 30 damage");
        t.assert(g.player.iT, 34, "hit i-frames set");

        g.player.hp = 100;
        g.player.iT = 0;
        g.player.stance = ST_NONE;
        g.player.state = PS_IDLE;
        g.armorFx.defense = 50;
        playerHurt(g, 30, 0, 0);
        t.assert(g.player.hp, 80, "def 50 -> 20 damage");
        g.player.hp = 100;
        g.player.iT = 0;
        g.armorFx.defense = 65535;
        playerHurt(g, 30, 0, 0);
        t.assert(g.player.hp, 99, "absurd def still lands the floor-1 chip");
        suite.addTest(t);
    }

    {
        Test t("guard chip uses the reduced damage and never underflows hp");
        Game g;
        initGame(g, W_GUN);
        g.armorFx.defense = 50;
        g.player.hp = 100;
        g.player.stam = 100;
        g.player.stance = ST_GUARD;
        g.player.stanceT = 30;   // past the parry window; plain guard
        g.player.state = PS_IDLE;
        g.player.iT = 0;
        playerHurt(g, 30, 0, 0);   // reduced to 20 -> chip 5
        t.assert(g.player.hp, 95, "chip = reduced*25/100");
        t.assert(g.player.stam, 78, "guard drains 22 stamina");
        t.assert(g.player.stance, ST_GUARD, "guard holds while stamina remains");

        // A floor-1 reduced hit still chips exactly 1 and cannot wrap hp.
        g.player.hp = 1;
        g.player.stam = 100;
        g.player.stance = ST_GUARD;
        g.player.stanceT = 30;
        g.player.iT = 0;
        g.armorFx.defense = 65535;
        playerHurt(g, 3, 0, 0);
        t.assert(g.player.hp, 0, "chip floors and clamps at 0");
        suite.addTest(t);
    }

    {
        Test t("EVADE_WINDOW extends the dodge i-frames");
        Game g;
        initGame(g, W_SWORD);
        g.player.stam = 100;
        armorEffectsBase(g.armorFx);
        startDodgeRoll(g, 1, 0);
        t.assert(g.player.iT, 14, "base dodge 14 frames");
        g.player.stam = 100;
        g.armorFx.iT = 5;
        startDodgeRoll(g, 1, 0);
        t.assert(g.player.iT, 19, "evade window +5");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
