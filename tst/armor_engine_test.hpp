#pragma once
// Host unit tests for the armor engine (bead monhun-ardu-arm.2):
// src/armor_state.hpp (equip toggle, crafted bits, stat/skill aggregation with
// thresholds). The armor craft gate + material/zenny debit moved onto the
// detail card (ui.3.1, 5co.6) and is covered by tst/card_state_test.hpp.
// The cart read side (src/armor.hpp) is device-only and is pinned by
// tst/fxdatatest/armor_test.hpp.
//
// The shipped data mirror (armor_data.hpp) is the source of truth for the
// table-wide pins; the threshold/aggregation math also uses synthetic pieces so
// totals above the shipped per-skill 3 can cross S=10 / M=15.
#include "test.hpp"
#include "../src/armor_state.hpp"
#include "../src/core/save.hpp"
#include "../src/generated/armor_data.hpp"
#include "../src/generated/armor_meta.hpp"

using namespace mh;

namespace armorenginetest {

// armor_data::Piece (host mirror) -> the engine's plain ArmorPiece view.
inline ArmorPiece toPiece(const armor_data::Piece &p) {
    ArmorPiece out;
    out.slot = p.slot;
    out.defense = p.defense;
    for (uint8_t i = 0; i < ARMOR_RESIST_COUNT; i++)
        out.resist[i] = p.resist[i];
    out.skillCount = p.skillCount;
    for (uint8_t i = 0; i < ARMOR_SKILL_SLOTS; i++) {
        out.skill[i] = p.skills[i].skill;
        out.points[i] = p.skills[i].points;
    }
    return out;
}

inline void shippedTable(ArmorPiece *out) {
    for (uint8_t i = 0; i < armor::PIECE_COUNT; i++)
        out[i] = toPiece(armor_data::PIECES[i]);
}

// Synthetic high-point piece so aggregation can cross the thresholds.
inline ArmorPiece skillPiece(uint8_t slot, uint8_t skill, uint8_t points, uint8_t defense = 0) {
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

// Craft + equip every listed piece, then aggregate the shipped table. Used by
// the representative-loadout tests below (helm+mail, cap+mail, ...).
inline void equipPieces(const ArmorPiece *table, uint8_t count, const uint8_t *ids, uint8_t n, SaveBlock &s, ArmorAgg &out) {
    saveDefaults(s);
    for (uint8_t i = 0; i < n; i++) {
        saveSetCrafted(s, ids[i]);
        armorEquipToggle(s, ids[i], table[ids[i]].slot);
    }
    armorAggregate(s, table, count, out);
}

}   // namespace armorenginetest

using namespace armorenginetest;

void ArmorEngineSuite(TestRunner &runner) {
    TestSuite suite("Armor engine: slots, aggregation, craft/equip (monhun-ardu-arm.2)");

    // -------------------------------------------------------- equip toggle
    {
        Test t("armorEquipToggle: equip -> unequip, and a second piece replaces the first");
        SaveBlock s;
        saveDefaults(s);
        t.assert(s.equip[armor::SLOT_HEAD], SAVE_EQUIP_NONE, "empty to start");
        // Uncrafted pieces are refused (craft gate at the lowest level).
        t.assert(armorEquipToggle(s, armor::ARMOR_HUNTER_HELM, armor::SLOT_HEAD), false, "uncrafted refused (no change)");
        t.assert(s.equip[armor::SLOT_HEAD], SAVE_EQUIP_NONE, "uncrafted refused");
        saveSetCrafted(s, armor::ARMOR_HUNTER_HELM);
        saveSetCrafted(s, armor::ARMOR_BONE_CAP);
        t.assert(armorEquipToggle(s, armor::ARMOR_HUNTER_HELM, armor::SLOT_HEAD), true, "helm equip changes the slot");
        t.assert(s.equip[armor::SLOT_HEAD], armor::ARMOR_HUNTER_HELM + 1, "helm equipped");
        t.assert(armorEquipToggle(s, armor::ARMOR_HUNTER_HELM, armor::SLOT_HEAD), true, "same piece unequip changes the slot");
        t.assert(s.equip[armor::SLOT_HEAD], SAVE_EQUIP_NONE, "same piece unequips");
        t.assert(armorEquipToggle(s, armor::ARMOR_BONE_CAP, armor::SLOT_HEAD), true, "cap equip changes the slot");
        t.assert(s.equip[armor::SLOT_HEAD], armor::ARMOR_BONE_CAP + 1, "cap equipped");
        t.assert(armorEquipToggle(s, armor::ARMOR_HUNTER_HELM, armor::SLOT_HEAD), true, "helm replaces cap");
        t.assert(s.equip[armor::SLOT_HEAD], armor::ARMOR_HUNTER_HELM + 1, "helm replaces cap");
        // Out-of-range ids are inert and report no change.
        t.assert(armorEquipToggle(s, armor::PIECE_COUNT, armor::SLOT_HEAD), false, "piece past the table no change");
        t.assert(armorEquipToggle(s, armor::ARMOR_HUNTER_MAIL, 9), false, "slot past the array no change");
        t.assert(s.equip[armor::SLOT_HEAD], armor::ARMOR_HUNTER_HELM + 1, "bad toggle leaves slot");
        t.assert(s.equip[armor::SLOT_BODY], SAVE_EQUIP_NONE, "bad slot untouched");
        suite.addTest(t);
    }

    // ----------------------------------------------------- crafted bitmask
    {
        Test t("crafted bits live in the v5 crafted bitset and round-trip");
        SaveBlock s;
        saveDefaults(s);
        t.assert(saveCrafted(s, armor::ARMOR_HUNTER_HELM), false, "default not crafted");
        saveSetCrafted(s, armor::ARMOR_HUNTER_HELM);
        saveSetCrafted(s, armor::ARMOR_EVADE_CHARM);
        t.assert(saveCrafted(s, armor::ARMOR_HUNTER_HELM), true, "helm crafted");
        t.assert(saveCrafted(s, armor::ARMOR_EVADE_CHARM), true, "charm crafted");
        t.assert(saveCrafted(s, armor::ARMOR_BONE_CAP), false, "cap not crafted");
        t.assert(s.flags & SAVE_FLAG_SMITHY_SEEN, 0, "crafted bits do not set smithy-seen");
        // The bitset spans its slot cap; the top slot still round-trips, a slot
        // past the cap is inert (ui.4.1 narrowed it to the armor data).
        saveSetCrafted(s, SAVE_CRAFTED_SLOTS - 1);
        t.assert(saveCrafted(s, SAVE_CRAFTED_SLOTS - 1), true, "top slot crafted");
        saveSetCrafted(s, SAVE_CRAFTED_SLOTS);
        t.assert(saveCrafted(s, SAVE_CRAFTED_SLOTS), false, "slot past the cap inert");
        // Round-trip through the wire record.
        SaveBlock enc;
        saveDefaults(enc);
        saveSetCrafted(enc, armor::ARMOR_BONE_MAIL);
        enc.zenny = 7;
        uint8_t bytes[SAVE_BYTES];
        saveEncode(enc, bytes);
        SaveBlock out;
        t.assert(saveDecode(bytes, out), true, "decode succeeds");
        t.assert(saveCrafted(out, armor::ARMOR_BONE_MAIL), true, "crafted bit round-trips");
        t.assert(out.zenny, 7, "other fields intact");
        suite.addTest(t);
    }

    // -------------------------------------------------- aggregation (shipped)
    {
        Test t("armorAggregate: defense/resist/skill sums across equipped pieces");
        ArmorPiece table[armor::PIECE_COUNT];
        shippedTable(table);
        SaveBlock s;
        saveDefaults(s);
        ArmorAgg agg;
        armorAggregate(s, table, armor::PIECE_COUNT, agg);
        t.assert(agg.defense, 0, "empty slots -> 0 defense");
        for (uint8_t i = 0; i < armor::SKILL_COUNT; i++)
            t.assert(agg.tier[i], 0, "empty slots -> inert skills");

        // helm + mail + charm (indices 0, 2, 4): both skills on every piece.
        saveSetCrafted(s, armor::ARMOR_HUNTER_HELM);
        saveSetCrafted(s, armor::ARMOR_HUNTER_MAIL);
        saveSetCrafted(s, armor::ARMOR_EVADE_CHARM);
        armorEquipToggle(s, armor::ARMOR_HUNTER_HELM, armor::SLOT_HEAD);
        armorEquipToggle(s, armor::ARMOR_HUNTER_MAIL, armor::SLOT_BODY);
        armorEquipToggle(s, armor::ARMOR_EVADE_CHARM, armor::SLOT_CHARM);
        armorAggregate(s, table, armor::PIECE_COUNT, agg);
        t.assert(agg.defense, 10 + 14 + 0, "defense summed");
        t.assert(agg.resist[0], 1 + 1 + 0, "fire summed");
        t.assert(agg.resist[1], 0 + 0 + 0, "water summed");
        t.assert(agg.resist[2], 0 + 0 + 0, "ice summed");
        t.assert(agg.resist[3], -1 + -1 + 0, "thunder signed sum");
        t.assert(agg.points[armor::SKILL_ATTACK_UP], 15, "attack_up 6+6+4 clamps to M");
        t.assert(agg.points[armor::SKILL_DEFENSE_UP], 6, "defense_up 6 on helm");
        t.assert(agg.points[armor::SKILL_HEALTH_UP], 4, "health_up only on mail");
        t.assert(agg.points[armor::SKILL_STAMINA_UP], 0, "stamina_up untouched");
        t.assert(agg.points[armor::SKILL_EVADE_WINDOW], 10, "evade_window 10 on charm");
        t.assert(agg.tier[armor::SKILL_ATTACK_UP], 2, "attack_up 16 -> clamped M");
        t.assert(agg.tier[armor::SKILL_EVADE_WINDOW], 1, "evade 10 >= S -> S");
        // Wrong-slot ids (hand-edited save) are ignored.
        s.equip[armor::SLOT_HEAD] = armor::ARMOR_HUNTER_MAIL + 1;
        armorAggregate(s, table, armor::PIECE_COUNT, agg);
        t.assert(agg.defense, 14 + 0, "wrong-slot head ignored (mail stays in body)");
        t.assert(agg.points[armor::SKILL_ATTACK_UP], 10, "mail 6 + charm 4 = 10");
        t.assert(agg.tier[armor::SKILL_ATTACK_UP], 1, "10 >= S -> S without the helm");
        suite.addTest(t);
    }

    // ------------------------------------- representative loadout tiers (shipped)
    // Every piece carries 2 skills; the shipped stacks cross S=10 (attack_up,
    // health_up, stamina_up) and clamp at M=15 (attack_up + charm). Keep these
    // as the data-level balance pins.
    {
        Test t("loadout tiers: attack_up S with helm+mail, M with charm added");
        ArmorPiece table[armor::PIECE_COUNT];
        shippedTable(table);
        SaveBlock s;
        ArmorAgg agg;

        const uint8_t helmMail[] = {armor::ARMOR_HUNTER_HELM, armor::ARMOR_HUNTER_MAIL};
        equipPieces(table, armor::PIECE_COUNT, helmMail, 2, s, agg);
        t.assert(agg.points[armor::SKILL_ATTACK_UP], 12, "helm 6 + mail 6");
        t.assert(agg.tier[armor::SKILL_ATTACK_UP], 1, "12 >= S -> S (attack_up)");

        const uint8_t helmMailCharm[] = {armor::ARMOR_HUNTER_HELM, armor::ARMOR_HUNTER_MAIL, armor::ARMOR_EVADE_CHARM};
        equipPieces(table, armor::PIECE_COUNT, helmMailCharm, 3, s, agg);
        t.assert(agg.points[armor::SKILL_ATTACK_UP], armor::THRESHOLD_M, "6+6+4 = 16 clamps to 15");
        t.assert(agg.tier[armor::SKILL_ATTACK_UP], 2, "clamped total is M (attack_up)");
        suite.addTest(t);
    }

    {
        Test t("loadout tiers: health_up S with bone_cap+hunter_mail, stamina_up S with bone_mail+bone_cap");
        ArmorPiece table[armor::PIECE_COUNT];
        shippedTable(table);
        SaveBlock s;
        ArmorAgg agg;

        const uint8_t capMail[] = {armor::ARMOR_BONE_CAP, armor::ARMOR_HUNTER_MAIL};
        equipPieces(table, armor::PIECE_COUNT, capMail, 2, s, agg);
        t.assert(agg.points[armor::SKILL_HEALTH_UP], 10, "bone_cap 6 + mail 4");
        t.assert(agg.tier[armor::SKILL_HEALTH_UP], 1, "10 >= S -> S (health_up)");

        const uint8_t mailCap[] = {armor::ARMOR_BONE_MAIL, armor::ARMOR_BONE_CAP};
        equipPieces(table, armor::PIECE_COUNT, mailCap, 2, s, agg);
        t.assert(agg.points[armor::SKILL_STAMINA_UP], 10, "bone_mail 6 + cap 4");
        t.assert(agg.tier[armor::SKILL_STAMINA_UP], 1, "10 >= S -> S (stamina_up)");
        suite.addTest(t);
    }

    {
        Test t("loadout tiers: defense_up S with helm+bone_mail, evade_window S on the charm");
        // arm.4 balance fix: helm defense_up 4->6 (stack 10 = S) and charm
        // evade_window 6->10 (S alone; the iT bonus is capped in armorEffects).
        ArmorPiece table[armor::PIECE_COUNT];
        shippedTable(table);
        SaveBlock s;
        ArmorAgg agg;

        const uint8_t mailHelm[] = {armor::ARMOR_BONE_MAIL, armor::ARMOR_HUNTER_HELM};
        equipPieces(table, armor::PIECE_COUNT, mailHelm, 2, s, agg);
        t.assert(agg.points[armor::SKILL_DEFENSE_UP], 10, "helm 6 + bone_mail 4");
        t.assert(agg.tier[armor::SKILL_DEFENSE_UP], 1, "10 >= S -> S");

        const uint8_t charm[] = {armor::ARMOR_EVADE_CHARM};
        equipPieces(table, armor::PIECE_COUNT, charm, 1, s, agg);
        t.assert(agg.points[armor::SKILL_EVADE_WINDOW], 10, "charm alone 10");
        t.assert(agg.tier[armor::SKILL_EVADE_WINDOW], 1, "10 >= S -> S");
        suite.addTest(t);
    }

    // --------------------------------------------------- thresholds (synthetic)
    {
        Test t("thresholds: S at 10 activates, M at 15 clamps; empty stays inert");
        ArmorPiece table[2];
        table[0] = skillPiece(armor::SLOT_HEAD, armor::SKILL_ATTACK_UP, 9, 5);
        table[1] = skillPiece(armor::SLOT_BODY, armor::SKILL_ATTACK_UP, 6, 7);
        SaveBlock s;
        saveDefaults(s);
        ArmorAgg agg;
        armorAggregate(s, table, 2, agg);
        t.assert(agg.tier[armor::SKILL_ATTACK_UP], 0, "no pieces -> inert");

        saveSetCrafted(s, 0);
        saveSetCrafted(s, 1);
        armorEquipToggle(s, 0, armor::SLOT_HEAD);
        armorAggregate(s, table, 2, agg);
        t.assert(agg.points[armor::SKILL_ATTACK_UP], 9, "single piece 9 points");
        t.assert(agg.tier[armor::SKILL_ATTACK_UP], 0, "9 < S -> inert");

        armorEquipToggle(s, 1, armor::SLOT_BODY);
        armorAggregate(s, table, 2, agg);
        t.assert(agg.points[armor::SKILL_ATTACK_UP], 15, "9 + 6 = 15 points");
        t.assert(agg.tier[armor::SKILL_ATTACK_UP], 2, "15 >= M -> M tier");
        t.assert(agg.defense, 12, "defense still summed");

        // Clamp above M.
        ArmorAgg big;
        armorClear(big);
        ArmorPiece huge = skillPiece(armor::SLOT_CHARM, armor::SKILL_ATTACK_UP, 20);
        armorAdd(big, huge);
        armorFinalize(big);
        t.assert(big.points[armor::SKILL_ATTACK_UP], armor::THRESHOLD_M, "points clamp to M");
        t.assert(big.tier[armor::SKILL_ATTACK_UP], 2, "clamped total is M");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
