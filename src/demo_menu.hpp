#pragma once
// Demo playtest picker (bead monhun-ardu-1du). Compiled into the demo build
// only (`make demo`, -DMH_DEMO=1), which boots into one picker screen -- a
// weapon cycle row, a beast cycle row and a GO row -- and compiles the
// hub/quests/gear/forge/cards/EEPROM flows out.
//
// Host-testable and Arduino-free: it depends only on the shared core (Game,
// Input, newGame/loadRoom) and the generated zone constants, so `make test` runs
// the picker state machine and the launch directly. Drawing lives in
// src/render.hpp under #if MH_DEMO; this header has no render code.

#include <stdint.h>
#include "core/world.hpp"
#include "generated/zone_meta.hpp"

namespace mh {

// Picker rows, top to bottom.
enum DemoRow : uint8_t {
    DEMO_ROW_WEAPON = 0,
    DEMO_ROW_BEAST = 1,
    DEMO_ROW_GO = 2,
    DEMO_ROW_COUNT = 3,
};

// Options per row: three weapons (WeaponId order: sword / flail / gun) and the
// four demo beasts (MonsterKind MON_LUNGE..MON_RAVAGER, 1:1).
constexpr uint8_t DEMO_WEAPON_COUNT = 3;
constexpr uint8_t DEMO_BEAST_COUNT = 4;

// Picker state. `weapon` / `beast` are the picked ids (WeaponId / MonsterKind
// values); prevA/prevB/prevMy are this screen's own edge flags.
struct DemoMenu {
    uint8_t row;
    uint8_t weapon;
    uint8_t beast;
    bool prevA;
    bool prevB;
    int8_t prevMy;
};

enum DemoEvent : int8_t {
    DEMO_NONE = 0,
    DEMO_LAUNCH,   // A on the GO row
};

// Wrap a pick by delta within [0, count). Two compares beat a runtime modulo
// (the same rule as screenCycle in src/screen_state.hpp).
inline uint8_t demoWrap(uint8_t v, int8_t delta, uint8_t count) {
    if (count == 0)
        return 0;
    int16_t n = static_cast<int16_t>(v) + delta;
    if (n < 0)
        n = static_cast<int16_t>(n + count);
    else if (n >= count)
        n = static_cast<int16_t>(n - count);
    return static_cast<uint8_t>(n);
}

// Fresh picker (boot): default picks (sword / lunge), cursor on WEAPON, edges
// clear.
inline void demoInit(DemoMenu &m) {
    m.row = DEMO_ROW_WEAPON;
    m.weapon = static_cast<uint8_t>(W_SWORD);
    m.beast = static_cast<uint8_t>(MON_LUNGE);
    m.prevA = false;
    m.prevB = false;
    m.prevMy = 0;
}

// Re-enter the picker (hunt end / camp hold-B). Keeps the picked loadout, resets
// the cursor, and seeds the edges from the held buttons so the press that
// returned cannot re-fire on the entry tick (the same held-button guard as
// appNavApply).
inline void demoEnter(DemoMenu &m, const Input &in) {
    m.row = DEMO_ROW_WEAPON;
    m.prevA = in.a;
    m.prevB = in.b;
    m.prevMy = 0;
}

// One picker tick. UP/DOWN moves the cursor one row per fresh direction (wrap);
// A cycles the row under the cursor (weapon wrap 3 / beast wrap 4) or launches
// from GO. B is inert. Returns DEMO_LAUNCH exactly once, on the A rising edge of
// the GO row.
inline DemoEvent demoStep(DemoMenu &m, const Input &in) {
    if (in.my != 0 && in.my != m.prevMy)
        m.row = demoWrap(m.row, in.my, DEMO_ROW_COUNT);
    m.prevMy = in.my;

    bool aP, bP, bR;
    inputEdges(in, m.prevA, m.prevB, aP, bP, bR);
    (void)bP;
    (void)bR;
    if (!aP)
        return DEMO_NONE;
    switch (m.row) {
    case DEMO_ROW_WEAPON:
        m.weapon = demoWrap(m.weapon, 1, DEMO_WEAPON_COUNT);
        return DEMO_NONE;
    case DEMO_ROW_BEAST:
        m.beast = demoWrap(m.beast, 1, DEMO_BEAST_COUNT);
        return DEMO_NONE;
    default:
        return DEMO_LAUNCH;
    }
}

// Hunt-end A edge: the demo's own latch (src/app_state.hpp's appOverReturnStep
// is compiled out of the demo build). True on the A rising edge only while
// `over`, exactly once per press; `prevA` stays current on every tick so the
// release is not seen as a fresh press.
inline bool demoOverReturnStep(bool over, const Input &in, bool &prevA) {
    const bool aP = in.a && !prevA;
    prevA = in.a;
    return over && aP;
}

// Home-room start spawn (design C): the demo lands the hunter on the home
// room's start spawn, not the room's beast spawn. beastHomeRoom() owns the
// beast -> home mapping; this is the home room -> its start <-> entrance spawn.
// Values are the generated zone symbolic constants.
inline uint8_t demoHomeSpawn(uint8_t homeRoom) {
    switch (homeRoom) {
    case zone::ROOM_AREA:
        return zone::SPAWN_AREA_START;
    case zone::ROOM_RIDGE:
        return zone::SPAWN_RIDGE_START;
    case zone::ROOM_CAMP:
        return zone::SPAWN_CAMP_ENTRY;
    case zone::ROOM_CAVERN:
        return zone::SPAWN_CAVERN_FROM_AREA;
    default:
        return zone::SPAWN_AREA_START;
    }
}

// Launch the picked hunt (design C): a fresh world for the picked weapon and
// beast (fresh Game defaults -- class default sheet, identity multipliers,
// empty inventory; no quest/save/armor/items arming), the beast moved to its
// home room's monster spawn (mirrors huntStart/beastHomeSpawn) and the hunter
// dropped 28 px west of it so the fight starts in view.
//
// Owner report (demo playtest): picks other than HEAVY looked empty because the
// beast stayed at the creature record's spawn coords (200,40), off-screen east
// of the room's start spawn (320,72). beastHomeSpawn + the adjacent drop make
// every pick open on the beast; the placement is clamped to the room bounds.
inline void demoLaunch(Game &g, const DemoMenu &m) {
    const int8_t beast = static_cast<int8_t>(m.beast);
    newGame(g, static_cast<int8_t>(m.weapon), MODE_HUNT, beast);
    beastHomeSpawn(g, beast);   // home room's monster spawn (huntStart parity)
    const uint8_t home = beastHomeRoom(beast);
    loadRoom(g, home, demoHomeSpawn(home));
    int16_t px = static_cast<int16_t>(g.monster.x - 28);
    if (px < 0)
        px = 0;
    const int16_t maxX = static_cast<int16_t>(roomBoundW(g) - g.player.w);
    if (px > maxX)
        px = maxX;
    g.player.x = px;
    g.player.y = g.monster.y;
    updateCamera(g);   // first frame already follows the drop point
}

}   // namespace mh
