// Host-side dumper for the core table dimensions that the art pipeline needs
// (bead monhun-ardu-42n.2). Prints JSON on stdout from the same src/core/game.hpp
// tables the firmware uses, so tools/gen-art.py never duplicates a number:
// attack hw/hh/reach (main attacks, special and attack branches), monster
// attack hw/hh, monster hurt box size, whirl radii.
//
// Deterministic: fixed traversal order, little text, no timestamps.
//
// Usage: fxdump   (stdout JSON)

#include "../src/core/game.hpp"

#include <stdint.h>
#include <stdio.h>

using namespace mh;

namespace {

void putNum(const char *key, long v) {
    printf("\"%s\": %ld", key, v);
}

// Full attack record (docs/weapon-art.md): the weapon art derives each move's
// pose from these fields, never from copied literals. Same table the firmware
// and the sim read.
void putAttackFull(const Attack *a) {
    putNum("startup", attackStartup(a));
    printf(", ");
    putNum("active", attackActive(a));
    printf(", ");
    putNum("recover", attackRecover(a));
    printf(", ");
    putNum("dmg", attackDmg(a));
    printf(", ");
    putNum("reach", attackReach(a));
    printf(", ");
    putNum("hw", attackHw(a));
    printf(", ");
    putNum("hh", attackHh(a));
    printf(", ");
    putNum("stam", attackStam(a));
    printf(", ");
    putNum("lunge", attackLunge(a));
    printf(", ");
    putNum("push", attackPush(a));
    printf(", ");
    putNum("effect", attackEffect(a));
    printf(", ");
    putNum("shell", attackShell(a) ? 1 : 0);
    printf(", ");
    putNum("id", attackId(a));
}

}   // namespace

int main() {
    const char *weaponNames[3] = {"sword", "flail", "gunshield"};

    printf("{\n");
    printf("  \"weapons\": [\n");
    for (int w = 0; w < 3; w++) {
        const WeaponDef *d = &WEAPON_DEFS[w];
        printf("    {\n");
        printf("      \"name\": \"%s\",\n", weaponNames[w]);
        printf("      \"attacks\": [\n");
        for (int a = 0; a < 3; a++) {
            const Attack *atk = weaponAttack(d, a);
            printf("        {");
            putAttackFull(atk);
            printf("}%s\n", a < 2 ? "," : "");
        }
        printf("      ],\n");
        const Attack *sp = weaponSpecial(d);
        printf("      \"special\": {");
        putAttackFull(sp);
        printf("},\n");
        // Branch attacks (combo follow-ups): same fields plus the AtkId so the
        // art pipeline can tell an attack branch (id != ATK_NONE) from a stance
        // branch (id == ATK_NONE, all-zero box).
        printf("      \"branches\": [\n");
        for (int b = 0; b < 3; b++) {
            const Attack *br = branchAtk(weaponBranch(d, b));
            printf("        {");
            putAttackFull(br);
            printf("}%s\n", b < 2 ? "," : "");
        }
        printf("      ],\n");
        // Roll out of evade, direction+A alt opener and the charge melee slots:
        // the render picks their art rows too (docs/weapon-art.md slots 7..9).
        const Attack *roll = weaponRoll(d);
        printf("      \"roll\": {");
        putAttackFull(roll);
        printf("},\n");
        const Attack *alt = weaponAlt(d);
        printf("      \"alt\": {");
        putAttackFull(alt);
        printf("},\n");
        printf("      \"charge\": [\n");
        for (int c = 0; c < 2; c++) {
            const Attack *ca = weaponCharge(d, c);
            printf("        {");
            putAttackFull(ca);
            printf("}%s\n", c < 1 ? "," : "");
        }
        printf("      ]\n");
        printf("    }%s\n", w < 2 ? "," : "");
    }
    printf("  ],\n");
    printf("  \"monsterAttacks\": {\n");
    for (int m = 0; m < 2; m++) {
        const MonsterAttack *a = &MONSTER_ATTACKS[m];
        printf("    \"%s\": {", m == 0 ? "lunge" : "sweep");
        putNum("hw", monsterAttackHw(a));
        printf(", ");
        putNum("hh", monsterAttackHh(a));
        printf(", ");
        putNum("reach", monsterAttackReach(a));
        printf("}%s\n", m == 0 ? "," : "");
    }
    printf("  },\n");
    printf("  \"monster\": {");
    putNum("w", monsterDefW(&MONSTER_DEFS[MON_LUNGE]));
    printf(", ");
    putNum("h", monsterDefH(&MONSTER_DEFS[MON_LUNGE]));
    printf("},\n");
    // Same numbers as src/render.hpp draws (whirl ring orbit 20/14) and the
    // sim's circleRectOverlap radius; documented here so the sheet layout in
    // gen-art.py consumes them instead of repeating literals.
    printf("  \"whirl\": {");
    putNum("rx", 20);
    printf(", ");
    putNum("ry", 14);
    printf(", ");
    putNum("r", 24);
    printf("}\n");
    printf("}\n");
    return 0;
}
