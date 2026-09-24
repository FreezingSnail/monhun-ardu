// Host-side serializer for the core content tables that live on the FX cart
// (bead monhun-ardu-42n.1). Includes the same game.hpp the firmware uses and
// emits the packed AVR layout field-by-field in declaration order: the host
// x86 struct layout is padded, so values are written explicitly, never memcpy'd.
//
// Deterministic: fixed traversal, little-endian bytes, no timestamps. Output
// sizes are asserted per struct (WeaponDef 329, Attack 23, Branch 27, ShellDef
// 15, MonsterAttack 17, MonsterDef 11) and per file (987 / 34 / 55).
//
// Usage: gen-fxtables [outdir]   (default: fxdata/tables)

#include "../src/core/game.hpp"
#include "../src/core/sin256.hpp"   // host SIN65 array (monhun-ardu-ept blob source)

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

using namespace mh;

namespace {

constexpr size_t WEAPON_DEFS_BYTES = 987;
constexpr size_t MONSTER_ATTACKS_BYTES = 34;
constexpr size_t MONSTER_DEFS_BYTES = 55;
constexpr size_t SIN65_BYTES = 65;

std::vector<uint8_t> g_bytes;

void require(bool ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "gen-fxtables: layout assert failed: %s\n", what);
        exit(1);
    }
}

void putU8(uint8_t v) {
    g_bytes.push_back(v);
}
void putI8(int8_t v) {
    g_bytes.push_back(static_cast<uint8_t>(v));
}
void putI16(int16_t v) {
    g_bytes.push_back(static_cast<uint8_t>(v) & 0xFF);
    g_bytes.push_back((static_cast<uint16_t>(v) >> 8) & 0xFF);
}

void putAttack(const Attack &a) {
    const size_t start = g_bytes.size();
    putI16(a.startup);
    putI16(a.active);
    putI16(a.recover);
    putI16(a.dmg);
    putI16(a.reach);
    putI16(a.hw);
    putI16(a.hh);
    putI16(a.stam);
    putI16(a.lunge);
    putI16(a.push);
    putI8(a.effect);
    putU8(a.shell ? 1 : 0);
    putI8(a.id);
    require(g_bytes.size() - start == 23, "Attack size");
}

void putBranch(const Branch &b) {
    const size_t start = g_bytes.size();
    putI8(b.stage);
    putI8(b.stance);
    putI16(b.autoT);
    putAttack(b.atk);
    require(g_bytes.size() - start == 27, "Branch size");
}

void putShell(const ShellDef &s) {
    const size_t start = g_bytes.size();
    putI16(s.count);
    putI16(s.dmg);
    putI16(s.speedF);
    putI16(s.w);
    putI16(s.h);
    putI16(s.reload);
    putI16(s.stam);
    putI8(s.pellets);
    require(g_bytes.size() - start == 15, "ShellDef size");
}

void putWeapon(const WeaponDef &d) {
    const size_t start = g_bytes.size();
    putI8(d.id);
    putI16(d.spd);
    for (int i = 0; i < 3; i++)
        putAttack(d.attacks[i]);
    putAttack(d.special);
    for (int i = 0; i < 3; i++)
        putBranch(d.branches[i]);
    putU8(d.canCancel ? 1 : 0);
    for (int i = 0; i < 2; i++)
        putShell(d.shells[i]);
    putAttack(d.roll);
    putAttack(d.alt);
    for (int i = 0; i < 2; i++)
        putAttack(d.charge[i]);
    for (int i = 0; i < 2; i++)
        putShell(d.chargeShells[i]);
    require(g_bytes.size() - start == 329, "WeaponDef size");
}

void putMonsterAttack(const MonsterAttack &a) {
    const size_t start = g_bytes.size();
    putI8(a.kind);
    putI16(a.windup);
    putI16(a.active);
    putI16(a.recover);
    putI16(a.speedF);
    putI16(a.dmg);
    putI16(a.reach);
    putI16(a.hw);
    putI16(a.hh);
    require(g_bytes.size() - start == 17, "MonsterAttack size");
}

void putMonsterDef(const MonsterDef &d) {
    const size_t start = g_bytes.size();
    putI8(d.kind);
    putI16(d.w);
    putI16(d.h);
    putI16(d.hp);
    putI16(d.spd);
    putI16(d.atkDist);
    require(g_bytes.size() - start == 11, "MonsterDef size");
}

void writeFile(const std::string &path, const std::vector<uint8_t> &bytes) {
    FILE *f = fopen(path.c_str(), "wb");
    if (f == nullptr) {
        fprintf(stderr, "gen-fxtables: cannot open %s\n", path.c_str());
        exit(1);
    }
    const size_t written = fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    if (written != bytes.size()) {
        fprintf(stderr, "gen-fxtables: short write to %s\n", path.c_str());
        exit(1);
    }
}

}   // namespace

int main(int argc, char **argv) {
    std::string outDir = argc > 1 ? argv[1] : "fxdata/tables";
    if (!outDir.empty() && outDir.back() != '/')
        outDir += '/';

    g_bytes.clear();
    for (int w = 0; w < 3; w++)
        putWeapon(WEAPON_DEFS[w]);
    require(g_bytes.size() == WEAPON_DEFS_BYTES, "weapondefs total");
    writeFile(outDir + "weapondefs.bin", g_bytes);

    g_bytes.clear();
    for (int m = 0; m < 2; m++)
        putMonsterAttack(MONSTER_ATTACKS[m]);
    require(g_bytes.size() == MONSTER_ATTACKS_BYTES, "monsterattacks total");
    writeFile(outDir + "monsterattacks.bin", g_bytes);

    g_bytes.clear();
    for (int m = 0; m < 5; m++)
        putMonsterDef(MONSTER_DEFS[m]);
    require(g_bytes.size() == MONSTER_DEFS_BYTES, "monsterdefs total");
    writeFile(outDir + "monsterdefs.bin", g_bytes);

    g_bytes.clear();
    for (size_t i = 0; i < SIN65_BYTES; i++)
        g_bytes.push_back(static_cast<uint8_t>(SIN65[i]));
    require(g_bytes.size() == SIN65_BYTES, "sin65 total");
    writeFile(outDir + "sin65.bin", g_bytes);

    printf("gen-fxtables: %sweapondefs.bin (%zu B), %smonsterattacks.bin (%zu B), %smonsterdefs.bin (%zu B), %ssin65.bin (%zu B)\n", outDir.c_str(), WEAPON_DEFS_BYTES, outDir.c_str(),
           MONSTER_ATTACKS_BYTES, outDir.c_str(), MONSTER_DEFS_BYTES, outDir.c_str(), SIN65_BYTES);
    return 0;
}
