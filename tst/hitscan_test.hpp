#pragma once
// Host unit tests for the gun hitscan arrowshot (replaces the retired shell /
// projectile suite) — permanent, co-located with repo tests. The special enters
// PS_SPECIAL, resolves instantly through meleeHitbox at the authored reach
// (44 px), spends stamina, spawns the muzzle spark, and never touches the
// retired projectile ring. Holding B keeps the guard up through the shot (the
// nock paces repeat fire); releasing B drops the stance as usual.
#include "test.hpp"
#include "../src/core/world.hpp"

using namespace mh;

namespace hitscantest {

struct HitRec {
    int hits = 0, lastDmg = 0;
    void reset() {
        hits = 0;
        lastDmg = 0;
    }
};
HitRec hrec;

void onHit(Game &, uint8_t dmg, int16_t, int16_t, uint8_t, uint8_t) {
    hrec.hits++;
    hrec.lastDmg = dmg;
}
void onStun(Game &, uint8_t) {
}

// Monster-shaped hurt box (32x24) at (x,y), mirroring player_test's harness.
void harmTarget(Game &g, int16_t x, int16_t y) {
    hrec.reset();
    g.target.alive = true;
    g.target.rect = Rect{x, y, 32, 24};
    g.target.onHit = onHit;
    g.target.onShove = nullptr;
    g.target.onStun = onStun;
}

// zero-initialized Game + the gun player + the cleared world (the retired
// proj ring must start at 0 even though nothing spawns into it).
void hinitGun(Game &g) {
    initGame(g, W_GUN);
    initWorld(g, MODE_HUNT);
}

void hstep(Game &g, int n, Input in = Input{0, 0, false, false}) {
    for (int i = 0; i < n; i++)
        stepPlayer(g, in);
}

// hold B until the gunshield guard stance is up
void hhold(Game &g) {
    hstep(g, 13, Input{0, 0, false, true});
}

// A press while the guard stance is held: the arrowshot fires this tick.
void hfire(Game &g) {
    hstep(g, 1, Input{0, 0, true, true});
}

}   // namespace hitscantest

using namespace hitscantest;

void HitscanSuite(TestRunner &runner) {
    TestSuite suite("Gun hitscan arrowshot: reach hit, stam, muzzle spark (src/core/player.hpp)");

    {
        Test t("arrowshot data: reach 44, dmg 12, stam 14");
        const Attack *a = weaponSpecial(&WEAPON_DEFS[W_GUN]);
        t.assert(attackReach(a), 44, "authored reach");
        t.assert(attackDmg(a), 12, "authored dmg");
        t.assert(attackStam(a), 14, "authored stam");
        suite.addTest(t);
    }

    {
        Test t("A in guard enters PS_SPECIAL, spends stam, muzzle spark, no projectile");
        Game g{};
        hinitGun(g);
        hhold(g);
        const uint8_t stam0 = g.player.stam;
        hfire(g);
        t.assert(g.player.state, PS_SPECIAL, "arrowshot enters PS_SPECIAL");
        t.assert(g.player.atk == weaponSpecial(&WEAPON_DEFS[W_GUN]) ? 1 : 0, 1, "special attack data");
        t.assert(g.player.stam, stam0 - 14, "stam spent");
        t.assert(g.player.reload, ARROW_NOCK_TICKS, "nock timer armed");
        t.assert(g.player.stance, ST_GUARD, "guard held through the shot (B held)");
        t.assert(g.player.bLocked, false, "no re-entry lock: the hold is still live");
        t.assertGreaterThan(g.fxN, 0, "muzzle spark spawned");
        t.assert(g.projN, 0, "no projectile spawned");
        suite.addTest(t);
    }

    {
        Test t("releasing B drops the guard mid-shot, shot still resolves");
        Game g{};
        hinitGun(g);
        hhold(g);
        hfire(g);
        t.assert(g.player.stance, ST_GUARD, "guard up during the shot");
        hstep(g, 1, Input{0, 0, false, false});   // release B
        t.assert(g.player.stance, ST_NONE, "release exits the stance");
        t.assert(g.player.state, PS_SPECIAL, "shot still resolves");
        suite.addTest(t);
    }

    {
        Test t("nock timer blocks a second shot until it expires");
        Game g{};
        hinitGun(g);
        hhold(g);
        hfire(g);
        t.assert(g.player.reload, ARROW_NOCK_TICKS, "nock armed");
        for (int i = 0; i < 4; i++)
            hstep(g, 1, Input{0, 0, false, true});
        t.assert(g.player.reload > 0 ? 1 : 0, 1, "still nocking");
        // The shot no longer drops the stance: hold B through the special and
        // the nock, then a fresh A tap fires again with no release/re-hold.
        for (int i = 0; i < ARROW_NOCK_TICKS + 4; i++)
            hstep(g, 1, Input{0, 0, false, true});
        t.assert(g.player.reload, 0, "nock expired");
        t.assert(g.player.state, PS_IDLE, "special finished");
        t.assert(g.player.stance, ST_GUARD, "guard held through the shot");
        hfire(g);
        t.assert(g.player.state, PS_SPECIAL, "ready again after the nock");
        suite.addTest(t);
    }

    {
        Test t("A tap during the shot buffers: the guard fires again, not a melee swing");
        Game g{};
        hinitGun(g);
        hhold(g);
        hfire(g);
        hstep(g, 11, Input{0, 0, false, true});   // mid-recovery
        hstep(g, 1, Input{0, 0, true, true});     // A tap: PS_SPECIAL is still busy
        t.assertGreaterThan(g.player.aBuffer, 0, "tap buffered, not dropped");
        for (int i = 0; i < 18; i++)
            hstep(g, 1, Input{0, 0, false, true});
        t.assert(g.player.reload, ARROW_NOCK_TICKS - 4, "second shot re-armed the nock");
        t.assert(g.player.state, PS_SPECIAL, "second arrowshot running");
        t.assert(g.player.stance, ST_GUARD, "guard still up");
        t.assert(g.player.aBuffer, 0, "buffer consumed");
        t.assert(g.player.chain, 0, "no melee chain started");
        suite.addTest(t);
    }

    {
        Test t("arrowshot lands at reach 44 on the facing axis");
        Game g{};
        hinitGun(g);
        harmTarget(g, static_cast<int16_t>(g.player.x + 40), g.player.y);
        hhold(g);
        hfire(g);
        hstep(g, 10, Input{0, 0, false, true});
        t.assert(hrec.hits, 1, "one hit at reach");
        t.assert(hrec.lastDmg, 12, "arrowshot dmg");
        suite.addTest(t);
    }

    {
        Test t("beyond reach 44 the arrowshot misses");
        Game g{};
        hinitGun(g);
        harmTarget(g, static_cast<int16_t>(g.player.x + 70), g.player.y);
        hhold(g);
        hfire(g);
        hstep(g, 10, Input{0, 0, false, true});
        t.assert(hrec.hits, 0, "no hit past reach");
        suite.addTest(t);
    }

    {
        Test t("no stamina -> no shot");
        Game g{};
        hinitGun(g);
        hhold(g);
        g.player.stam = 0;
        hfire(g);
        t.assert(g.player.state != PS_SPECIAL ? 1 : 0, 1, "stam gate blocks the shot");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
