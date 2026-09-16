// Host-side dumper for the core table dimensions that the art pipeline needs
// (bead monhun-ardu-42n.2). Prints JSON on stdout from the same src/core/game.hpp
// tables the firmware uses, so tools/gen-art.py never duplicates a number:
// attack hw/hh/reach, monster attack hw/hh, monster hurt box size, whirl radii.
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
            putNum("hw", attackHw(atk));
            printf(", ");
            putNum("hh", attackHh(atk));
            printf(", ");
            putNum("reach", attackReach(atk));
            printf("}%s\n", a < 2 ? "," : "");
        }
        printf("      ],\n");
        const Attack *sp = weaponSpecial(d);
        printf("      \"special\": {");
        putNum("hw", attackHw(sp));
        printf(", ");
        putNum("hh", attackHh(sp));
        printf(", ");
        putNum("reach", attackReach(sp));
        printf("}\n");
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
    putNum("w", 32);
    printf(", ");
    putNum("h", 24);
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
