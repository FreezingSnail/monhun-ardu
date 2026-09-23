#pragma once
// On-device end-to-end suite for the smith upgrade table (bead monhun-ardu-4ug,
// qs.3). ui.2 trimmed the weapon-tier purchase rows and ui.3.1 (5co.6) removed
// the SMITH screen entirely (FORGE replaces it in ui.4), so this suite now
// covers only the surviving cart path: reading the UpgradeDef records out of
// the mhSmith blob, resolving tier multipliers, and the cart armor records'
// stat/skill aggregation (armorApplyToGame). The armor craft/equip card E2E is
// pinned by tst/fxdatatest/cards_test.hpp.

#include "harness/fxtest.hpp"
#include "src/smith.hpp"
#include "src/armor.hpp"
#include "src/core/world.hpp"

#include <stdint.h>

namespace smithfx {

using namespace mh;

static const SaveBackend REAL_BACKEND = {saveEepromRead, saveEepromWrite};

static Game g_smith;
static uint8_t g_hitDmg = 0;

static void captureHit(Game &, uint8_t dmg, int16_t, int16_t, uint8_t, uint8_t) {
    g_hitDmg = dmg;
}

// One sword attack against a world-covering target; returns the damage the
// real melee path handed to Target::onHit.
static uint8_t meleeHit(uint8_t dmgMul) {
    newGame(g_smith, W_SWORD, MODE_HUNT);
    g_smith.dmgMul = dmgMul;
    g_smith.target.alive = true;
    g_smith.target.rect = mh::Rect{0, 0, WORLD_W, WORLD_H};
    g_smith.target.onHit = captureHit;
    g_hitDmg = 0xFF;
    const Input a = {0, 0, true, false};
    const Input idle = {0, 0, false, false};
    stepPlayer(g_smith, a);
    for (uint8_t i = 0; i < 12; i++)
        stepPlayer(g_smith, idle);
    return g_hitDmg;
}

// Walk right for 40 ticks; returns the travelled pixels at this tier speed.
static int16_t walkDist(uint8_t spdMul) {
    newGame(g_smith, W_SWORD, MODE_HUNT);
    g_smith.spdMul = spdMul;
    const int16_t x0 = g_smith.player.x;
    const Input right = {1, 0, false, false};
    for (uint8_t i = 0; i < 40; i++)
        stepPlayer(g_smith, right);
    return static_cast<int16_t>(g_smith.player.x - x0);
}

inline void test_smith(FxTest &test) {
    // ------------------------------------------------------ cart records
    test.expectEq(smith::UPGRADE_COUNT, 6, F("upgrade count"));
    test.expectEq(smith::WEAPON_COUNT, 3, F("weapon count"));
    test.expectEq(smith::WEAPON_SWORD, W_SWORD, F("weapon sword == roster"));
    test.expectEq(smith::WEAPON_FLAIL, W_FLAIL, F("weapon flail == roster"));
    test.expectEq(smith::WEAPON_GUN, W_GUN, F("weapon gun == roster"));

    UpgradeDef d0, d1, d5;
    smithReadDef(smith::UPG_SWORD_T1, d0);
    smithReadDef(smith::UPG_SWORD_T2, d1);
    smithReadDef(smith::UPG_GUN_T2, d5);
    test.expectEq(d0.weaponIdx, W_SWORD, F("sword t1 weapon"));
    test.expectEq(d0.tier, 1, F("sword t1 tier"));
    test.expectEq(d0.cost, 100, F("sword t1 cost"));
    test.expectEq(d0.dmgMul, 110, F("sword t1 dmgMul"));
    test.expectEq(d0.spdMul, 105, F("sword t1 spdMul"));
    test.expectEq(d0.unlockFlag, 0, F("sword t1 unlock"));
    test.expectEq(d1.tier, 2, F("sword t2 tier"));
    test.expectEq(d1.dmgMul, 125, F("sword t2 dmgMul"));
    test.expectEq(d5.weaponIdx, W_GUN, F("gun t2 weapon"));
    test.expectEq(d5.tier, 2, F("gun t2 tier"));
    test.expectEq(d5.spdMul, 105, F("gun t2 spdMul"));

    // ------------------------------------------------- multiplier resolve
    uint8_t dmg = 0, spd = 0;
    smithResolve(W_SWORD, 0, dmg, spd);
    test.expectEq(dmg, 100, F("tier 0 dmg identity"));
    test.expectEq(spd, 100, F("tier 0 spd identity"));
    smithResolve(W_SWORD, 1, dmg, spd);
    test.expectEq(dmg, 110, F("sword t1 dmg"));
    test.expectEq(spd, 105, F("sword t1 spd"));
    smithResolve(W_FLAIL, 2, dmg, spd);
    test.expectEq(dmg, 130, F("flail t2 dmg"));
    test.expectEq(spd, 110, F("flail t2 spd"));
    test.expectEq(upgradeMul(9, 110), 9, F("upgradeMul truncates"));
    test.expectEq(upgradeMul(18, 115), 20, F("upgradeMul rounds down"));

    // ------------------------------- armor cart aggregation + EEPROM (arm.2)
    SaveBlock asave;
    saveDefaults(asave);
    saveSetCrafted(asave, armor::ARMOR_HUNTER_HELM);
    test.expectEq(armorEquipToggle(asave, armor::ARMOR_HUNTER_HELM, armor::SLOT_HEAD), 1, F("helm equip toggles"));
    test.expectEq(asave.equip[armor::SLOT_HEAD], armor::ARMOR_HUNTER_HELM + 1, F("helm equipped"));
    saveStore(asave, REAL_BACKEND);

    SaveBlock aloaded;
    test.expectEq(saveLoad(aloaded, REAL_BACKEND), 1, F("armor save loads"));
    test.expectEq(saveCrafted(aloaded, armor::ARMOR_HUNTER_HELM), 1, F("crafted reloaded"));
    test.expectEq(aloaded.equip[armor::SLOT_HEAD], armor::ARMOR_HUNTER_HELM + 1, F("equipped id reloaded"));

    // Cart aggregation: helm defense 10, attack_up 6 + defense_up 4 (arm.4
    // two-skills-per-piece); both stay below S=10 alone -> inert, so the flat
    // helm defense is the only active effect.
    // Reuse the file-scope Game instead of a second 650 B stack frame: the
    // ATmega32u4 sim has ~845 B of stack left after globals, and this suite's
    // frame already sat within 7 B of that limit (dlp.2 grew ScreenRow by one
    // byte and tipped it). g_smith is re-initialized by the melee/walk helpers
    // right after, so its state here is transient.
    Game &ag = g_smith;
    newGame(ag, W_SWORD, MODE_HUNT);
    armorApplyToGame(ag, aloaded);
    test.expectEq(ag.armor.defense, 10, F("cart armor defense"));
    test.expectEq(ag.armor.points[armor::SKILL_ATTACK_UP], 6, F("cart attack_up points"));
    test.expectEq(ag.armor.tier[armor::SKILL_ATTACK_UP], 0, F("6 points inert"));
    test.expectEq(ag.armor.points[armor::SKILL_DEFENSE_UP], 6, F("cart defense_up points"));
    test.expectEq(ag.armor.tier[armor::SKILL_DEFENSE_UP], 0, F("4 points inert"));
    test.expectEq(ag.armorHead, armor::ARMOR_HUNTER_HELM + 1, F("armorHead set"));
    // arm.3: the resolved effect cache comes off the same cart read. The shipped
    // attack_up points are below S alone, so the damage fold stays identity (100)
    // while the 10 helm defense lands in the combat reduction.
    test.expectEq(ag.armorFx.defense, 10, F("cart effect defense"));
    test.expectEq(ag.armorFx.dmgMul, 100, F("inert attack_up identity mul"));
    test.expectEq(ag.armorFx.hpMax, 100, F("no health_up -> hpMax 100"));
    test.expectEq(ag.armorFx.stamMax, 100, F("no stamina_up -> stamMax 100"));
    test.expectEq(ag.armorFx.iT, 0, F("no evade_window -> no iT bonus"));
    test.expectEq(ag.player.hpMax, 100, F("live hpMax armed"));
    test.expectEq(ag.player.hp, 100, F("live hp topped to max"));
    test.expectEq(armorEquipToggle(aloaded, armor::ARMOR_HUNTER_HELM, armor::SLOT_HEAD), 1, F("toggle unequips"));
    test.expectEq(aloaded.equip[armor::SLOT_HEAD], SAVE_EQUIP_NONE, F("unequipped"));
    armorApplyToGame(ag, aloaded);
    test.expectEq(ag.armor.defense, 0, F("unequipped defense 0"));
    test.expectEq(ag.armorHead, 0, F("armorHead cleared"));
    test.expectEq(ag.armorFx.defense, 0, F("unequipped effect defense 0"));

    // ----------------------------- tier multiplier -> damage/speed change
    // (the multiplier still resolves off the cart; only the purchase UI is gone)
    test.expectEq(meleeHit(100), 9, F("baseline sword hit 9"));
    test.expectEq(meleeHit(125), 11, F("tier 2 hit 11"));
    const int16_t base = walkDist(100);
    const int16_t fast = walkDist(115);
    test.expectEq(base > 0, 1, F("baseline moves"));
    test.expectEq(fast > base, 1, F("tier 2 speed travels farther"));
}

}   // namespace smithfx
