#pragma once
// Hitbox reachability guard (epic monhun-ardu-ryh, bead ryh.3).
//
// The mask converter derives every zone box from the painted overlay. This host
// suite proves two invariants that the masks must preserve:
//
//   1. every zone is hittable from at least one legal hunter stance: a scan of
//      positions x DIR8 facings x every weapon's attack boxes through the real
//      meleeHitbox() math (src/core/player.hpp) must find a melee rect that
//      overlaps the zone's face-relative world rect. No escape-hatch flag: the
//      test only proves a zone is not geometrically impossible, not that a
//      particular stance reaches it.
//   2. the derived hit rect == the part-art rect (the `cey` invariant): for every
//      zone that draws a breakable-part overlay, the overlay sheet's frame width
//      equals the zone box width and its (multiple-of-8 padded) height matches
//      the zone box height. The overlay anchor is the zone box origin
//      (drawZonePart, src/render.hpp), so cropping the part sheet to the mask
//      bbox keeps the painted part on the exact rect the hit test uses.
#include "test.hpp"
#include "../src/core/world.hpp"             // meleeHitbox, WEAPON_DEFS, combat loader
#include "../src/generated/art_dims.hpp"     // part-sheet frame dims
#include "../src/generated/art_sheets.hpp"   // 1-based zone partSheet ids

#include <cstdio>

using namespace mh;

namespace hitboxreach {

static bool overlaps(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

// The zone's world rect for a monster parked at the origin facing `fx`
// (2-facing: east at the authored box, west at the cell mirror).
static void zoneRect(int8_t fx, const CombatBox &b, int &x, int &y, int &w, int &h) {
    const int ox = fx < 0 ? static_cast<int>(ZONE_CELL_W - b.ox - b.w) : b.ox;
    x = ox;
    y = b.oy;
    w = b.w;
    h = b.h;
}

static bool attackHasBox(const Attack *a) {
    return a != nullptr && attackHw(a) > 0 && attackHh(a) > 0;
}

// Every melee attack box the hunter can swing with (combo x3, special, the three
// branches, roll/alt openers, the two charge releases). Stance branches with an
// all-zero box are skipped.
static int collectAttacks(const WeaponDef *d, const Attack *out[12]) {
    int n = 0;
    for (int i = 0; i < 3; i++)
        if (attackHasBox(&d->attacks[i]))
            out[n++] = &d->attacks[i];
    if (attackHasBox(&d->special))
        out[n++] = &d->special;
    for (int i = 0; i < 3; i++)
        if (attackHasBox(&d->branches[i].atk))
            out[n++] = &d->branches[i].atk;
    if (attackHasBox(&d->roll))
        out[n++] = &d->roll;
    if (attackHasBox(&d->alt))
        out[n++] = &d->alt;
    for (int i = 0; i < 2; i++)
        if (attackHasBox(&d->charge[i]))
            out[n++] = &d->charge[i];
    return n;
}

static bool zoneReachable(const CombatBox &box) {
    const Attack *atks[12];
    for (int wi = 0; wi < 3; wi++) {
        const WeaponDef *d = &WEAPON_DEFS[wi];
        const int n = collectAttacks(d, atks);
        for (int sign = 0; sign < 2; sign++) {
            int zx, zy, zw, zh;
            zoneRect(sign == 0 ? fp::FP : -fp::FP, box, zx, zy, zw, zh);
            for (int ai = 0; ai < n; ai++) {
                for (int dy = -48; dy <= 48; dy++) {
                    for (int dx = -48; dx <= 48; dx++) {
                        for (int dir = 0; dir < 8; dir++) {
                            Player p{};
                            p.x = static_cast<int16_t>(dx);
                            p.y = static_cast<int16_t>(dy);
                            p.w = 16;
                            p.h = 16;
                            p.fx = static_cast<int8_t>(fp::dir8X(dir));
                            p.fy = static_cast<int8_t>(fp::dir8Y(dir));
                            const Rect hit = meleeHitbox(p, atks[ai]);
                            if (overlaps(hit.x, hit.y, hit.w, hit.h, zx, zy, zw, zh))
                                return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

// The breakable-part overlay sheet's frame dims for a packed partSheet id, or
// 0x0 when the zone ships no overlay.
static void partFrame(uint8_t partSheet, uint8_t &fw, uint8_t &fh) {
    fw = fh = 0;
    switch (partSheet) {
    case art_sheets::ART_SHEET_FXHEAD_CHICKEN:
        fw = art_dims::head_chicken_frame_w;
        fh = art_dims::head_chicken_frame_h;
        break;
    case art_sheets::ART_SHEET_FXLEGS_CHICKEN:
        fw = art_dims::legs_chicken_frame_w;
        fh = art_dims::legs_chicken_frame_h;
        break;
    case art_sheets::ART_SHEET_FXHEAD_BULL:
        fw = art_dims::head_bull_frame_w;
        fh = art_dims::head_bull_frame_h;
        break;
    case art_sheets::ART_SHEET_FXHOOVES_BULL:
        fw = art_dims::hooves_bull_frame_w;
        fh = art_dims::hooves_bull_frame_h;
        break;
    case art_sheets::ART_SHEET_FXTAIL_HEAVY:
        fw = art_dims::tail_heavy_frame_w;
        fh = art_dims::tail_heavy_frame_h;
        break;
    default:
        break;
    }
}

void HitboxReachSuite(TestRunner &runner) {
    TestSuite suite("Hitbox masks (epic ryh): every zone reachable + hit rect == part-art rect");

    {
        Test t("every creature zone is hittable from at least one legal stance");
        int zonesChecked = 0;
        for (uint8_t i = 0; i < combat::CREATURES_COUNT; i++) {
            const uint8_t headIdx = combatCreatureHeadZone(i);
            const uint8_t appendIdx = combatCreatureAppendZone(i);
            for (int slot = 0; slot < 2; slot++) {
                const uint8_t idx = slot == 0 ? headIdx : appendIdx;
                if (idx == COMBAT_NO_ZONE)
                    continue;
                const CombatZone z = combatZoneRead(idx);
                char msg[96];
                std::snprintf(msg, sizeof(msg), "creature %u zone %s reachable", static_cast<unsigned>(i), slot == 0 ? "head" : "appendage");
                t.assert(zoneReachable(z.box) ? 1 : 0, 1, msg);
                zonesChecked++;
            }
        }
        t.assert(zonesChecked >= 8 ? 1 : 0, 1, "all 8 shipped zones checked");
        suite.addTest(t);
    }

    {
        Test t("derived hit rect == part-art rect (cey invariant)");
        int partsChecked = 0;
        for (uint8_t i = 0; i < combat::CREATURES_COUNT; i++) {
            const uint8_t idxs[2] = {combatCreatureHeadZone(i), combatCreatureAppendZone(i)};
            for (int slot = 0; slot < 2; slot++) {
                if (idxs[slot] == COMBAT_NO_ZONE)
                    continue;
                const CombatZone z = combatZoneRead(idxs[slot]);
                uint8_t fw, fh;
                partFrame(z.partSheet, fw, fh);
                if (fw == 0)
                    continue;
                char msg[96];
                std::snprintf(msg, sizeof(msg), "creature %u zone %s part frame w", static_cast<unsigned>(i), slot == 0 ? "head" : "appendage");
                t.assert(fw, z.box.w, msg);
                std::snprintf(msg, sizeof(msg), "creature %u zone %s part frame h (pad8)", static_cast<unsigned>(i), slot == 0 ? "head" : "appendage");
                t.assert(fh, ((z.box.h + 7) / 8) * 8, msg);
                partsChecked++;
            }
        }
        t.assert(partsChecked >= 5 ? 1 : 0, 1, "every breakable zone part checked");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}

}   // namespace hitboxreach
