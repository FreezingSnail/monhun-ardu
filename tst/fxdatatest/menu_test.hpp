#pragma once
// On-device suite for src/menu_state.hpp (beads monhun-ardu-6zb.2, 6zb.4): a
// scripted tap + hold nav sequence through menuStep (debounced d-pad), the A
// edge, the pick -> newGame mapping through the real AVR core (FX-cart
// MONSTER_DEFS), and the post-over return edge + nav reset. Same semantics as
// tst/menu_test.hpp, compiled here to pin the device build of the menu glue and
// the cart-backed monster kinds it selects.

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

    // ---- taps: weapon right x3 wraps, one left wraps back to GUN
    menuStep(m, right);
    menuStep(m, idle);   // release, so the next call is a fresh press
    test.expectEq(static_cast<uint32_t>(m.weapon), 1, F("nav right 1"));
    menuStep(m, right);
    menuStep(m, idle);
    test.expectEq(static_cast<uint32_t>(m.weapon), 2, F("nav right 2"));
    menuStep(m, right);
    menuStep(m, idle);
    test.expectEq(static_cast<uint32_t>(m.weapon), 0, F("nav weapon wrap fwd"));
    menuStep(m, left);
    menuStep(m, idle);
    test.expectEq(static_cast<uint32_t>(m.weapon), 2, F("nav weapon wrap back"));

    // ---- taps: target down through all 8, wrap to LUNGE, up wraps back
    menuStep(m, down);
    menuStep(m, idle);
    test.expectEq(static_cast<uint32_t>(m.target), 1, F("nav down 1"));
    menuStep(m, down);
    menuStep(m, idle);
    test.expectEq(static_cast<uint32_t>(m.target), 2, F("nav down 2"));
    menuStep(m, down);
    menuStep(m, idle);
    test.expectEq(static_cast<uint32_t>(m.target), 3, F("nav down 3 ravager"));
    menuStep(m, down);
    menuStep(m, idle);
    test.expectEq(static_cast<uint32_t>(m.target), 4, F("nav down 4 pole"));
    menuStep(m, down);
    menuStep(m, idle);
    test.expectEq(static_cast<uint32_t>(m.target), 5, F("nav down 5 sever"));
    menuStep(m, down);
    menuStep(m, idle);
    test.expectEq(static_cast<uint32_t>(m.target), 6, F("nav down 6 break"));
    menuStep(m, down);
    menuStep(m, idle);
    test.expectEq(static_cast<uint32_t>(m.target), 7, F("nav down 7 crack"));
    menuStep(m, down);
    menuStep(m, idle);
    test.expectEq(static_cast<uint32_t>(m.target), 0, F("nav target wrap fwd"));
    menuStep(m, up);
    menuStep(m, idle);
    test.expectEq(static_cast<uint32_t>(m.target), 7, F("nav target wrap back"));

    // ---- debounce: press steps once, hold waits DELAY, then repeats REPEAT
    menuStep(m, right);   // weapon 2 -> 0 (immediate)
    test.expectEq(static_cast<uint32_t>(m.weapon), 0, F("hold press step"));
    for (uint8_t i = 0; i < MENU_NAV_DELAY - 1; i++)
        menuStep(m, right);
    test.expectEq(static_cast<uint32_t>(m.weapon), 0, F("hold before delay"));
    menuStep(m, right);   // delay expires: second step
    test.expectEq(static_cast<uint32_t>(m.weapon), 1, F("hold at delay step"));
    for (uint8_t i = 0; i < MENU_NAV_REPEAT - 1; i++)
        menuStep(m, right);
    test.expectEq(static_cast<uint32_t>(m.weapon), 1, F("hold between repeats"));
    menuStep(m, right);   // repeat step
    test.expectEq(static_cast<uint32_t>(m.weapon), 2, F("hold repeat step"));
    menuStep(m, idle);   // release resets; pick kept
    test.expectEq(static_cast<uint32_t>(m.weapon), 2, F("hold release keeps"));
    menuStep(m, right);
    test.expectEq(static_cast<uint32_t>(m.weapon), 0, F("tap after release immediate"));

    // ---- reversal is immediate and re-arms the delay
    menuStep(m, left);
    test.expectEq(static_cast<uint32_t>(m.weapon), 2, F("reversal immediate"));
    for (uint8_t i = 0; i < MENU_NAV_DELAY - 1; i++)
        menuStep(m, left);
    test.expectEq(static_cast<uint32_t>(m.weapon), 2, F("reversal restart held"));
    menuStep(m, left);
    test.expectEq(static_cast<uint32_t>(m.weapon), 1, F("reversal after delay"));
    menuStep(m, idle);   // release the axis so later picks start fresh

    // ---- A edge fires START once per press (picks now FLS + POLE)
    m.target = MENU_POLE_TARGET;   // nav above wrapped to CRACK; pick plain
    test.expectEq(static_cast<uint32_t>(menuStep(m, a)), MENU_ACCEPT, F("A edge start"));
    for (uint8_t i = 0; i < 4; i++)
        test.expectEq(static_cast<uint32_t>(menuStep(m, a)), MENU_NONE, F("A held no repeat"));

    // ---- start mapping through the real core: POLE (target 4) -> MODE_TRAIN,
    // no beast, pole kind 0 (plain). One static Game reused for every mapping
    // check: Game is ~700 B and three stack copies overflow the AVR stack.
    static Game g;
    menuStart(g, m);
    test.expectEq(static_cast<uint32_t>(g.weapon), W_FLAIL, F("start pole weapon"));
    test.expectEq(static_cast<uint32_t>(g.mode), MODE_TRAIN, F("start pole mode"));
    test.expectEq(static_cast<uint32_t>(g.monsterKind), MON_LUNGE, F("start pole kind 0"));
    test.expectEq(static_cast<uint32_t>(g.pole.kind), POLE_PLAIN, F("start plain pole"));
    test.expectEq(static_cast<uint32_t>(g.combat.zone[COMBAT_ZONE_HEAD].hp), 0, F("plain pool 0"));

    // ---- target 5 SEVER installs the whole-pole breakable variant (pool 60)
    MenuState sv;
    sv.weapon = W_SWORD;
    sv.target = MENU_POLE_TARGET + POLE_SEVER;
    menuStart(g, sv);
    test.expectEq(static_cast<uint32_t>(g.mode), MODE_TRAIN, F("start sever mode"));
    test.expectEq(static_cast<uint32_t>(g.pole.kind), POLE_SEVER, F("start sever pole"));
    test.expectEq(static_cast<uint32_t>(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp), 60, F("sever pool 60"));
    test.expectEq(static_cast<uint32_t>(g.pole.rect.w), 20, F("sever rect w"));

    // ---- target 6 BREAK installs the horn variant (pool 40, rect 20)
    MenuState bk;
    bk.weapon = W_FLAIL;
    bk.target = MENU_POLE_TARGET + POLE_BREAK;
    menuStart(g, bk);
    test.expectEq(static_cast<uint32_t>(g.pole.kind), POLE_BREAK, F("start break pole"));
    test.expectEq(static_cast<uint32_t>(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp), 40, F("break pool 40"));
    test.expectEq(static_cast<uint32_t>(g.pole.rect.w), 20, F("break rect w 20"));
    test.expectEq(static_cast<uint32_t>(g.target.rect.w), 20, F("break target rect"));

    // ---- target 7 CRACK installs the whole-pole crack variant (pool 30)
    MenuState ck;
    ck.weapon = W_GUN;
    ck.target = MENU_POLE_TARGET + POLE_CRACK;
    menuStart(g, ck);
    test.expectEq(static_cast<uint32_t>(g.pole.kind), POLE_CRACK, F("start crack pole"));
    test.expectEq(static_cast<uint32_t>(g.combat.zone[COMBAT_ZONE_APPENDAGE].hp), 30, F("crack pool 30"));

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

    // ---- RAVAGER -> hunt, cart def 32x24 / 260 hp (ljj.6 target slot 3)
    MenuState rv;
    rv.target = MON_RAVAGER;
    menuStart(g, rv);
    test.expectEq(static_cast<uint32_t>(g.monsterKind), MON_RAVAGER, F("start ravager kind"));
    test.expectEq(static_cast<uint32_t>(g.monster.hpMax), 260, F("ravager hp from cart"));
    test.expectEq(static_cast<uint32_t>(g.monster.w), 32, F("ravager w from cart"));

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

    // ---- return clears the hold state: a held d-pad cannot skip picks
    MenuState n;
    menuStep(n, right);   // SWD -> FLS, timer armed
    for (uint8_t i = 0; i < MENU_NAV_DELAY - 1; i++)
        menuStep(n, right);
    test.expectEq(static_cast<uint32_t>(n.navXTimer), 1, F("nav-reset timer mid-hold"));
    test.expectEq(static_cast<uint32_t>(menuReturnStep(n, true, a)), 1, F("nav-reset returns"));
    test.expectEq(static_cast<uint32_t>(n.navXTimer), 0, F("nav-reset timer clear"));
    menuStep(n, right);   // still held on re-entry: one immediate step only
    test.expectEq(static_cast<uint32_t>(n.weapon), 2, F("nav-reset holds count"));
    for (uint8_t i = 0; i < MENU_NAV_DELAY - 1; i++)
        menuStep(n, right);
    test.expectEq(static_cast<uint32_t>(n.weapon), 2, F("nav-reset fresh delay"));
    menuStep(n, right);
    test.expectEq(static_cast<uint32_t>(n.weapon), 0, F("nav-reset fresh repeat"));
}

}   // namespace menu
