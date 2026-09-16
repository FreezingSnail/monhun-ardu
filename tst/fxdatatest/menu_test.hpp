#pragma once
// On-device suite for src/menu_state.hpp (bead monhun-ardu-6zb.2): a scripted
// nav + A-edge sequence through menuStep, the pick -> newGame mapping through
// the real AVR core (FX-cart MONSTER_DEFS), and the post-over return edge. Same
// semantics as tst/menu_test.hpp, compiled here to pin the device build of the
// menu glue and the cart-backed monster kinds it selects.

#include "harness/fxtest.hpp"
#include "src/menu_state.hpp"

#include <stdint.h>

namespace menu {

using namespace mh;

inline void test_menu(FxTest &test) {
    const Input idle = {0, 0, false, false};
    const Input right = {1, 0, false, false};
    const Input left = {-1, 0, false, false};
    const Input up = {0, -1, false, false};
    const Input down = {0, 1, false, false};
    const Input a = {0, 0, true, false};

    // ---- boot state: SWD / LUNGE, active, idle tick silent
    MenuState m;
    test.expectEq(static_cast<uint32_t>(m.weapon), 0, F("menu boot weapon"));
    test.expectEq(static_cast<uint32_t>(m.target), 0, F("menu boot target"));
    test.expectEq(static_cast<uint32_t>(m.active), 1, F("menu boot active"));
    test.expectEq(static_cast<uint32_t>(menuStep(m, idle)), MENU_NONE, F("menu idle silent"));

    // ---- scripted nav: weapon right x3 wraps, one left wraps back to GUN
    menuStep(m, right);
    test.expectEq(static_cast<uint32_t>(m.weapon), 1, F("nav right 1"));
    menuStep(m, right);
    test.expectEq(static_cast<uint32_t>(m.weapon), 2, F("nav right 2"));
    menuStep(m, right);
    test.expectEq(static_cast<uint32_t>(m.weapon), 0, F("nav weapon wrap fwd"));
    menuStep(m, left);
    test.expectEq(static_cast<uint32_t>(m.weapon), 2, F("nav weapon wrap back"));

    // ---- target down x3 reaches POLE, x4 wraps to LUNGE, up wraps back
    menuStep(m, down);
    test.expectEq(static_cast<uint32_t>(m.target), 1, F("nav down 1"));
    menuStep(m, down);
    test.expectEq(static_cast<uint32_t>(m.target), 2, F("nav down 2"));
    menuStep(m, down);
    test.expectEq(static_cast<uint32_t>(m.target), 3, F("nav down 3 pole"));
    menuStep(m, down);
    test.expectEq(static_cast<uint32_t>(m.target), 0, F("nav target wrap fwd"));
    menuStep(m, up);
    test.expectEq(static_cast<uint32_t>(m.target), 3, F("nav target wrap back"));

    // ---- A edge fires START once per press (picks now GUN + POLE)
    test.expectEq(static_cast<uint32_t>(menuStep(m, a)), MENU_START, F("A edge start"));
    for (uint8_t i = 0; i < 4; i++)
        test.expectEq(static_cast<uint32_t>(menuStep(m, a)), MENU_NONE, F("A held no repeat"));

    // ---- start mapping through the real core: POLE -> MODE_TRAIN, no beast
    // One static Game reused for every mapping check: Game is ~700 B and three
    // stack copies overflow the AVR stack in setup().
    static Game g;
    menuStart(g, m);
    test.expectEq(static_cast<uint32_t>(g.weapon), W_GUN, F("start pole weapon"));
    test.expectEq(static_cast<uint32_t>(g.mode), MODE_TRAIN, F("start pole mode"));
    test.expectEq(static_cast<uint32_t>(g.monsterKind), MON_LUNGE, F("start pole kind 0"));

    // ---- HEAVY -> hunt, cart def drives size/hp (40x28, 320 hp)
    MenuState h;
    h.weapon = W_FLAIL;
    h.target = MON_HEAVY;
    menuStart(g, h);
    test.expectEq(static_cast<uint32_t>(g.weapon), W_FLAIL, F("start heavy weapon"));
    test.expectEq(static_cast<uint32_t>(g.mode), MODE_HUNT, F("start heavy mode"));
    test.expectEq(static_cast<uint32_t>(g.monsterKind), MON_HEAVY, F("start heavy kind"));
    test.expectEq(static_cast<uint32_t>(g.monster.hpMax), 320, F("heavy hp from cart"));
    test.expectEq(static_cast<uint32_t>(g.monster.w), 40, F("heavy w from cart"));
    test.expectEq(static_cast<uint32_t>(g.monster.h), 28, F("heavy h from cart"));

    // ---- SWEEP -> hunt, cart def 28x22 / 150 hp
    MenuState s;
    s.target = MON_SWEEP;
    menuStart(g, s);
    test.expectEq(static_cast<uint32_t>(g.monsterKind), MON_SWEEP, F("start sweep kind"));
    test.expectEq(static_cast<uint32_t>(g.monster.hpMax), 150, F("sweep hp from cart"));

    // ---- return edge: over+ A only, exactly once per press
    MenuState r;
    test.expectEq(static_cast<uint32_t>(menuReturnStep(r, false, a)), 0, F("return pre-over none"));
    test.expectEq(static_cast<uint32_t>(menuReturnStep(r, true, a)), 0, F("return held-a no edge"));
    test.expectEq(static_cast<uint32_t>(menuReturnStep(r, true, idle)), 0, F("return release none"));
    test.expectEq(static_cast<uint32_t>(menuReturnStep(r, true, a)), 1, F("return over+A fires"));
    test.expectEq(static_cast<uint32_t>(menuReturnStep(r, true, a)), 0, F("return edge once"));

    // ---- picks survive the round trip; held return A does not restart
    MenuState p;
    p.weapon = W_FLAIL;
    p.target = MON_HEAVY;
    test.expectEq(static_cast<uint32_t>(menuStep(p, idle)), MENU_NONE, F("kept pick idle"));
    menuReturnStep(p, true, idle);   // sim ticks keep flags current
    test.expectEq(static_cast<uint32_t>(menuReturnStep(p, true, a)), 1, F("kept pick returns"));
    test.expectEq(static_cast<uint32_t>(p.weapon), W_FLAIL, F("kept pick weapon"));
    test.expectEq(static_cast<uint32_t>(p.target), MON_HEAVY, F("kept pick target"));
    test.expectEq(static_cast<uint32_t>(menuStep(p, a)), MENU_NONE, F("return A not restarted"));
}

}   // namespace menu
