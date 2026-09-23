#pragma once
// On-device end-to-end suite for smith upgrades (bead monhun-ardu-4ug, qs.3).
//
// Covers the shipped cart path the host suite cannot: reading the UpgradeDef
// records out of the mhSmith blob, resolving tier multipliers, the smith screen
// rows (availability / bought / locked conditions), the full buy -> stat
// change flow through the real player damage + move-speed paths, and the
// EEPROM persistence of the purchased tier (insufficient funds and max tier
// rejected).

#include "harness/fxtest.hpp"
#include "src/screens.hpp"
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

    // ------------------------------------------------- smith screen rows
    test.expectEq(screens::SCREEN_COUNT, 3, F("screen count"));
    test.expectEq(screens::SCREEN_SMITH, 2, F("smith screen index"));
    test.expectEq(screenRowCount(screens::SCREEN_SMITH), 12, F("smith row count"));
    ScreenRow t1, t2, leave;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_SMITH, 0), t1);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_SMITH, 1), t2);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_SMITH, 11), leave);
    test.expectEq(t1.cost, 100, F("t1 row cost"));
    test.expectEq(t1.action, screens::ACTION_BUY_UPGRADE, F("t1 row action"));
    test.expectEq(t1.cond, screens::COND_UPGRADE, F("t1 row cond"));
    test.expectEq(t1.param, 1, F("t1 row param (sword t1)"));
    test.expectEq(t2.cost, 250, F("t2 row cost"));
    test.expectEq(t2.param, 2, F("t2 row param (sword t2)"));
    test.expectEq(leave.action, screens::ACTION_LEAVE, F("leave row"));
    // Row 5 is gun tier 2: param = (0 << 4) | (W_GUN << 2) | 2 = 10.
    ScreenRow gun2;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_SMITH, 5), gun2);
    test.expectEq(gun2.param, 10, F("gun t2 row param"));
    test.expectEq(gun2.cost, 220, F("gun t2 row cost"));

    // Armor rows (arm.2): rows 6..10, param = (slot << 5) | piece, cost + bill
    // resolved from the mhSmith armor recipe record.
    ScreenRow helm, mail, charm;
    screenReadRow(screenRowOffsetAt(screens::SCREEN_SMITH, 6), helm);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_SMITH, 8), mail);
    screenReadRow(screenRowOffsetAt(screens::SCREEN_SMITH, 10), charm);
    test.expectEq(helm.action, screens::ACTION_CRAFT_ARMOR, F("helm row action"));
    test.expectEq(helm.cond, screens::COND_ARMOR, F("helm row cond"));
    test.expectEq(helm.param, 0, F("helm row param (head piece 0)"));
    test.expectEq(helm.cost, 300, F("helm row cost from recipe"));
    test.expectEq(helm.recipe[0].item, armor::mat::ORE + 1, F("helm ore code"));
    test.expectEq(helm.recipe[0].count, 3, F("helm ore count"));
    test.expectEq(helm.recipe[1].item, armor::mat::SCALE + 1, F("helm scale code"));
    test.expectEq(helm.recipe[1].count, 2, F("helm scale count"));
    test.expectEq(mail.param, 34, F("mail row param (body piece 2)"));
    test.expectEq(mail.cost, 400, F("mail row cost from recipe"));
    test.expectEq(mail.recipe[0].item, armor::mat::SCALE + 1, F("mail scale code"));
    test.expectEq(mail.recipe[0].count, 3, F("mail scale count"));
    test.expectEq(mail.recipe[1].item, armor::mat::SHELL + 1, F("mail shell code"));
    test.expectEq(charm.param, 68, F("charm row param (charm piece 4)"));
    test.expectEq(charm.cost, 600, F("charm row cost from recipe"));
    test.expectEq(charm.recipe[0].item, armor::mat::TAIL + 1, F("charm tail code"));
    test.expectEq(charm.recipe[1].item, armor::mat::ORE + 1, F("charm ore code"));

    // ------------------------------------------------ smith screen nav
    SaveBlock navSave;
    saveDefaults(navSave);
    ScreenState nav;
    screenEnter(nav, screens::SCREEN_SMITH, navSave);
    test.expectEq(nav.rowCount, 12, F("enter smith row count"));
    test.expectEq(nav.cursor, 0, F("enter smith cursor"));
    const Input down = {0, 1, false, false};
    const Input idle = {0, 0, false, false};
    screenStep(nav, down);
    screenStep(nav, idle);
    test.expectEq(nav.cursor, 1, F("smith nav down 1"));
    for (uint8_t i = 0; i < 10; i++) {
        screenStep(nav, down);
        screenStep(nav, idle);
    }
    test.expectEq(nav.cursor, 11, F("smith nav to last row"));
    test.expectEq(nav.scroll, 6, F("smith second page"));

    // ------------------------------------------------ purchase E2E + EEPROM
    SaveBlock save;
    saveDefaults(save);
    save.zenny = 1000;
    // prg.7 recipes: SWORD T1 = 2 ore + 1 scale, T2 = 3 ore + 1 fang.
    save.items[ITEM_ORE] = 5;
    save.items[ITEM_SCALE] = 1;
    save.items[ITEM_FANG] = 1;
    saveStore(save, REAL_BACKEND);   // clear any previous device run

    test.expectEq(screenCondOk(save, t1), 1, F("t1 available"));
    test.expectEq(screenCondOk(save, t2), 0, F("t2 not next tier"));
    test.expectEq(screenApplyAction(save, t1), 1, F("t1 buy applies"));
    test.expectEq(save.zenny, 900, F("zenny debited"));
    test.expectEq(save.tier[W_SWORD], 1, F("tier 1 stored"));
    test.expectEq(static_cast<uint32_t>(save.items[ITEM_ORE]), 3, F("t1 ore debited"));
    test.expectEq(static_cast<uint32_t>(save.items[ITEM_SCALE]), 0, F("t1 scale debited"));
    saveStore(save, REAL_BACKEND);

    SaveBlock loaded;
    test.expectEq(saveLoad(loaded, REAL_BACKEND), 1, F("tier persisted"));
    test.expectEq(loaded.tier[W_SWORD], 1, F("tier 1 reloaded"));
    test.expectEq(loaded.zenny, 900, F("zenny reloaded"));

    test.expectEq(screenCondOk(loaded, t1), 0, F("t1 bought dead"));
    test.expectEq(screenCondOk(loaded, t2), 1, F("t2 available"));
    test.expectEq(screenApplyAction(loaded, t2), 1, F("t2 buy applies"));
    test.expectEq(loaded.tier[W_SWORD], 2, F("tier 2 stored"));
    test.expectEq(static_cast<uint32_t>(loaded.items[ITEM_ORE]), 0, F("t2 ore debited"));
    test.expectEq(static_cast<uint32_t>(loaded.items[ITEM_FANG]), 0, F("t2 fang debited"));
    saveStore(loaded, REAL_BACKEND);
    test.expectEq(saveLoad(loaded, REAL_BACKEND), 1, F("tier 2 persisted"));
    test.expectEq(loaded.tier[W_SWORD], 2, F("tier 2 reloaded"));
    test.expectEq(screenCondOk(loaded, t2), 0, F("max tier dead"));
    test.expectEq(screenApplyAction(loaded, t2), 0, F("max tier buy rejected"));

    // Insufficient funds.
    SaveBlock poor;
    saveDefaults(poor);
    poor.zenny = 99;
    test.expectEq(screenCondOk(poor, t1), 0, F("insufficient dead"));
    test.expectEq(screenApplyAction(poor, t1), 0, F("insufficient rejected"));
    test.expectEq(poor.tier[W_SWORD], 0, F("tier unchanged when broke"));

    // Locked row: unlockFlag 1 gates on quest 0's done bit.
    SaveBlock lockedSave;
    saveDefaults(lockedSave);
    lockedSave.zenny = 500;
    lockedSave.items[ITEM_ORE] = 2;
    lockedSave.items[ITEM_SCALE] = 1;
    ScreenRow locked = t1;
    locked.param = static_cast<uint8_t>((1 << 4) | (0 << 2) | 1);
    test.expectEq(screenCondOk(lockedSave, locked), 0, F("locked until quest done"));
    saveQuestSet(lockedSave, 0, 1);
    test.expectEq(screenCondOk(lockedSave, locked), 1, F("quest done unlocks"));

    // --------------------------------------- armor craft/equip + EEPROM (arm.2)
    SaveBlock asave;
    saveDefaults(asave);
    asave.zenny = 1000;
    asave.items[ITEM_ORE] = 3;
    asave.items[ITEM_SCALE] = 2;
    saveStore(asave, REAL_BACKEND);
    test.expectEq(screenCondOk(asave, helm), 1, F("helm craftable"));
    test.expectEq(screenApplyAction(asave, helm), 1, F("helm craft+equip applies"));
    test.expectEq(saveCrafted(asave, armor::ARMOR_HUNTER_HELM), 1, F("helm crafted bit"));
    test.expectEq(asave.equip[armor::SLOT_HEAD], armor::ARMOR_HUNTER_HELM + 1, F("helm equipped"));
    test.expectEq(asave.zenny, 700, F("helm zenny debited"));
    test.expectEq(static_cast<uint32_t>(asave.items[ITEM_ORE]), 0, F("helm ore debited"));
    test.expectEq(static_cast<uint32_t>(asave.items[ITEM_SCALE]), 0, F("helm scale debited"));
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
    test.expectEq(screenApplyAction(aloaded, helm), 1, F("second A unequips"));
    test.expectEq(aloaded.equip[armor::SLOT_HEAD], SAVE_EQUIP_NONE, F("unequipped"));
    armorApplyToGame(ag, aloaded);
    test.expectEq(ag.armor.defense, 0, F("unequipped defense 0"));
    test.expectEq(ag.armorHead, 0, F("armorHead cleared"));
    test.expectEq(ag.armorFx.defense, 0, F("unequipped effect defense 0"));

    // ----------------------------------------- buy -> damage/speed change
    test.expectEq(meleeHit(100), 9, F("baseline sword hit 9"));
    test.expectEq(meleeHit(125), 11, F("tier 2 hit 11"));
    const int16_t base = walkDist(100);
    const int16_t fast = walkDist(115);
    test.expectEq(base > 0, 1, F("baseline moves"));
    test.expectEq(fast > base, 1, F("tier 2 speed travels farther"));
}

}   // namespace smithfx
