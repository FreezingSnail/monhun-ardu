#pragma once
// Host unit tests for the armor + skill table (bead monhun-ardu-arm.1): the
// generated armor_data.hpp host mirror against the armor_meta.hpp ABI pins and
// the armor_expect.hpp spot values (data/skills.json + data/armor.json ->
// mhArmor). No runtime reads the blob yet (arm.2/arm.3 own the engine), so this
// suite is the compile-and-pin coverage for the generated data.
//
// The generated constants are the source of truth; tests reference the
// armor::ARMOR_* / SKILL_* ids and the expect pins, never literal indices.
#include "test.hpp"
#include "../src/generated/armor_data.hpp"
#include "../src/generated/armor_expect.hpp"
#include "../src/generated/armor_meta.hpp"

namespace armortest {

// Every shipped piece id, in table order, for the table-wide pins.
const uint8_t PIECE_IDS[armor::PIECE_COUNT] = {
    armor::ARMOR_HUNTER_HELM, armor::ARMOR_BONE_CAP, armor::ARMOR_HUNTER_MAIL, armor::ARMOR_BONE_MAIL, armor::ARMOR_EVADE_CHARM,
};

const uint8_t EXPECT_SLOT[armor::PIECE_COUNT] = {
    armor_expect::ARMOR_HUNTER_HELM_SLOT, armor_expect::ARMOR_BONE_CAP_SLOT, armor_expect::ARMOR_HUNTER_MAIL_SLOT, armor_expect::ARMOR_BONE_MAIL_SLOT, armor_expect::ARMOR_EVADE_CHARM_SLOT,
};

const uint8_t EXPECT_DEFENSE[armor::PIECE_COUNT] = {
    armor_expect::ARMOR_HUNTER_HELM_DEFENSE, armor_expect::ARMOR_BONE_CAP_DEFENSE,    armor_expect::ARMOR_HUNTER_MAIL_DEFENSE,
    armor_expect::ARMOR_BONE_MAIL_DEFENSE,   armor_expect::ARMOR_EVADE_CHARM_DEFENSE,
};

const int8_t EXPECT_FIRE[armor::PIECE_COUNT] = {
    armor_expect::ARMOR_HUNTER_HELM_RESIST_FIRE, armor_expect::ARMOR_BONE_CAP_RESIST_FIRE,    armor_expect::ARMOR_HUNTER_MAIL_RESIST_FIRE,
    armor_expect::ARMOR_BONE_MAIL_RESIST_FIRE,   armor_expect::ARMOR_EVADE_CHARM_RESIST_FIRE,
};

const int8_t EXPECT_THUNDER[armor::PIECE_COUNT] = {
    armor_expect::ARMOR_HUNTER_HELM_RESIST_THUNDER, armor_expect::ARMOR_BONE_CAP_RESIST_THUNDER,    armor_expect::ARMOR_HUNTER_MAIL_RESIST_THUNDER,
    armor_expect::ARMOR_BONE_MAIL_RESIST_THUNDER,   armor_expect::ARMOR_EVADE_CHARM_RESIST_THUNDER,
};

const uint8_t EXPECT_SKILL0[armor::PIECE_COUNT] = {
    armor_expect::ARMOR_HUNTER_HELM_SKILL0, armor_expect::ARMOR_BONE_CAP_SKILL0, armor_expect::ARMOR_HUNTER_MAIL_SKILL0, armor_expect::ARMOR_BONE_MAIL_SKILL0, armor_expect::ARMOR_EVADE_CHARM_SKILL0,
};

const uint8_t EXPECT_SKILL0_POINTS[armor::PIECE_COUNT] = {
    armor_expect::ARMOR_HUNTER_HELM_SKILL0_POINTS, armor_expect::ARMOR_BONE_CAP_SKILL0_POINTS,    armor_expect::ARMOR_HUNTER_MAIL_SKILL0_POINTS,
    armor_expect::ARMOR_BONE_MAIL_SKILL0_POINTS,   armor_expect::ARMOR_EVADE_CHARM_SKILL0_POINTS,
};

const uint8_t EXPECT_SKILL1[armor::PIECE_COUNT] = {
    armor_expect::ARMOR_HUNTER_HELM_SKILL1, armor_expect::ARMOR_BONE_CAP_SKILL1, armor_expect::ARMOR_HUNTER_MAIL_SKILL1, armor_expect::ARMOR_BONE_MAIL_SKILL1, armor_expect::ARMOR_EVADE_CHARM_SKILL1,
};

const uint8_t EXPECT_SKILL1_POINTS[armor::PIECE_COUNT] = {
    armor_expect::ARMOR_HUNTER_HELM_SKILL1_POINTS, armor_expect::ARMOR_BONE_CAP_SKILL1_POINTS,    armor_expect::ARMOR_HUNTER_MAIL_SKILL1_POINTS,
    armor_expect::ARMOR_BONE_MAIL_SKILL1_POINTS,   armor_expect::ARMOR_EVADE_CHARM_SKILL1_POINTS,
};

const uint8_t SKILL_KIND[armor::SKILL_COUNT] = {
    armor_expect::SKILL_ATTACK_UP_KIND, armor_expect::SKILL_DEFENSE_UP_KIND, armor_expect::SKILL_HEALTH_UP_KIND, armor_expect::SKILL_STAMINA_UP_KIND, armor_expect::SKILL_EVADE_WINDOW_KIND,
};

const uint8_t SKILL_PER_POINT[armor::SKILL_COUNT] = {
    armor_expect::SKILL_ATTACK_UP_PER_POINT,  armor_expect::SKILL_DEFENSE_UP_PER_POINT,   armor_expect::SKILL_HEALTH_UP_PER_POINT,
    armor_expect::SKILL_STAMINA_UP_PER_POINT, armor_expect::SKILL_EVADE_WINDOW_PER_POINT,
};

}   // namespace armortest

using namespace armortest;

void ArmorSuite(TestRunner &runner) {
    TestSuite suite("Armor: pieces + skill table (monhun-ardu-arm.1)");

    {
        Test t("table size + blob header match the generated pins");
        t.assert(armor::PIECE_COUNT, armor_expect::PIECE_COUNT, "piece count matches expect");
        t.assert(armor::SKILL_COUNT, armor_expect::SKILL_COUNT, "skill count matches expect");
        t.assert(armor::PIECE_COUNT, 5, "five shipped pieces");
        t.assert(armor::SKILL_COUNT, 5, "five shipped skills");
        t.assert(armor::PIECE_SIZE, armor_expect::PIECE_SIZE, "piece record size pin");
        t.assert(armor::PIECE_SIZE, 18, "piece is 18 B");
        t.assert(armor::SKILL_SIZE, armor_expect::SKILL_SIZE, "skill record size pin");
        t.assert(armor::SKILL_SIZE, 3, "skill is 3 B");
        t.assert(armor::SIZE, armor_expect::BLOB_SIZE, "blob size pin");
        t.assert(armor::SIZE, 8 + 18 * 5 + 3 * 5, "header + pieces + skills");
        t.assert(armor::MAGIC, 0x5241, "magic");
        t.assert(armor::VERSION, 1, "version");
        t.assert(armor_data::BLOB_SIZE, armor::SIZE, "host mirror blob size");
        suite.addTest(t);
    }

    {
        Test t("skills carry kind/maxPoints/perPoint from the data");
        for (uint8_t i = 0; i < armor::SKILL_COUNT; i++) {
            t.assert(armor_data::SKILLS[i].kind, SKILL_KIND[i], "kind pin");
            t.assert(armor_data::SKILLS[i].perPoint, SKILL_PER_POINT[i], "perPoint pin");
            t.assert(armor_data::SKILLS[i].maxPoints, armor::THRESHOLD_M, "maxPoints == M threshold");
        }
        t.assert(armor_data::SKILLS[armor::SKILL_ATTACK_UP].kind, armor::KIND_ATTACK_UP, "attack_up kind");
        t.assert(armor_data::SKILLS[armor::SKILL_DEFENSE_UP].kind, armor::KIND_DEFENSE_UP, "defense_up kind");
        t.assert(armor_data::SKILLS[armor::SKILL_HEALTH_UP].kind, armor::KIND_HEALTH_UP, "health_up kind");
        t.assert(armor_data::SKILLS[armor::SKILL_STAMINA_UP].kind, armor::KIND_STAMINA_UP, "stamina_up kind");
        t.assert(armor_data::SKILLS[armor::SKILL_EVADE_WINDOW].kind, armor::KIND_EVADE_WINDOW, "evade_window kind");
        suite.addTest(t);
    }

    {
        Test t("every piece reads back slot/defense/resist/skills from the table");
        for (uint8_t i = 0; i < armor::PIECE_COUNT; i++) {
            const armor_data::Piece &p = armor_data::PIECES[PIECE_IDS[i]];
            t.assert(p.slot, EXPECT_SLOT[i], "slot pin");
            t.assert(p.defense, EXPECT_DEFENSE[i], "defense pin");
            t.assert(p.resist[0], EXPECT_FIRE[i], "fire pin");
            t.assert(p.resist[3], EXPECT_THUNDER[i], "thunder pin");
            t.assert(p.skillCount, 2, "two skills per shipped piece");
            t.assert(p.skills[0].skill, EXPECT_SKILL0[i], "skill0 code pin");
            t.assert(p.skills[0].points, EXPECT_SKILL0_POINTS[i], "skill0 points pin");
            t.assert(p.skills[1].skill, EXPECT_SKILL1[i], "skill1 code pin");
            t.assert(p.skills[1].points, EXPECT_SKILL1_POINTS[i], "skill1 points pin");
            t.assert(p.mat[0].item > 0, true, "first recipe slot filled");
        }
        suite.addTest(t);
    }

    {
        Test t("skill codes are (index + 1) so 0 stays the empty slot");
        t.assert(armor_data::PIECES[armor::ARMOR_HUNTER_HELM].skills[0].skill, armor::SKILL_ATTACK_UP + 1, "helm attack_up");
        t.assert(armor_data::PIECES[armor::ARMOR_HUNTER_HELM].skills[1].skill, armor::SKILL_DEFENSE_UP + 1, "helm defense_up");
        t.assert(armor_data::PIECES[armor::ARMOR_HUNTER_MAIL].skills[0].skill, armor::SKILL_ATTACK_UP + 1, "mail attack_up");
        t.assert(armor_data::PIECES[armor::ARMOR_HUNTER_MAIL].skills[1].skill, armor::SKILL_HEALTH_UP + 1, "mail health_up");
        t.assert(armor_data::PIECES[armor::ARMOR_BONE_MAIL].skills[0].skill, armor::SKILL_STAMINA_UP + 1, "bone_mail stamina_up");
        t.assert(armor_data::PIECES[armor::ARMOR_BONE_MAIL].skills[1].skill, armor::SKILL_DEFENSE_UP + 1, "bone_mail defense_up");
        t.assert(armor_data::PIECES[armor::ARMOR_EVADE_CHARM].skills[0].skill, armor::SKILL_EVADE_WINDOW + 1, "charm evade_window");
        t.assert(armor_data::PIECES[armor::ARMOR_EVADE_CHARM].skills[1].skill, armor::SKILL_ATTACK_UP + 1, "charm attack_up");
        t.assert(armor::SHEET_NONE, 0, "no sheet is 0");
        suite.addTest(t);
    }

    {
        Test t("recipe material codes are (item index + 1) against the item table");
        // hunter_helm: ore x3 + scale x2.
        const armor_data::Piece &helm = armor_data::PIECES[armor::ARMOR_HUNTER_HELM];
        t.assert(helm.mat[0].item, armor::mat::ORE + 1, "ore code");
        t.assert(helm.mat[0].count, 3, "ore count");
        t.assert(helm.mat[1].item, armor::mat::SCALE + 1, "scale code");
        t.assert(helm.mat[1].count, 2, "scale count");
        t.assert(helm.zenny, armor_expect::ARMOR_HUNTER_HELM_ZENNY, "zenny pin");
        suite.addTest(t);
    }

    {
        Test t("thresholds are authored data (S below M)");
        t.assert(armor::THRESHOLD_S, 10, "S threshold");
        t.assert(armor::THRESHOLD_M, 15, "M threshold");
        t.assert(armor::THRESHOLD_S < armor::THRESHOLD_M, true, "S activates before M");
        suite.addTest(t);
    }

    {
        Test t("slots cover head/body/charm");
        t.assert(armor_data::PIECES[armor::ARMOR_HUNTER_HELM].slot, armor::SLOT_HEAD, "helm head");
        t.assert(armor_data::PIECES[armor::ARMOR_BONE_CAP].slot, armor::SLOT_HEAD, "cap head");
        t.assert(armor_data::PIECES[armor::ARMOR_HUNTER_MAIL].slot, armor::SLOT_BODY, "mail body");
        t.assert(armor_data::PIECES[armor::ARMOR_BONE_MAIL].slot, armor::SLOT_BODY, "bone body");
        t.assert(armor_data::PIECES[armor::ARMOR_EVADE_CHARM].slot, armor::SLOT_CHARM, "charm");
        suite.addTest(t);
    }

    {
        Test t("every skill id is granted by at least one piece");
        for (uint8_t skill = 0; skill < armor::SKILL_COUNT; skill++) {
            bool granted = false;
            for (uint8_t i = 0; i < armor::PIECE_COUNT; i++) {
                const armor_data::Piece &p = armor_data::PIECES[i];
                for (uint8_t s = 0; s < p.skillCount; s++) {
                    if (p.skills[s].skill == skill + 1)
                        granted = true;
                }
            }
            t.assert(granted, true, "skill is reachable");
        }
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
