// Host test runner entry point. Compiles to build/tests/host via `make test`.
#include "test.hpp"
#include "fp_test.hpp"
#include "input_test.hpp"
#include "player_test.hpp"
#include "monster_test.hpp"
#include "shells_test.hpp"
#include "world_test.hpp"
#include "art_dims_test.hpp"
#include "sin_test.hpp"
#include "menu_test.hpp"
#include "screens_test.hpp"
#include "quests_test.hpp"
#include "smith_test.hpp"
#include "combat_test.hpp"
#include "combat_pack_test.hpp"

int main() {
    TestRunner runner;
    FpSuite(runner);
    InputSuite(runner);
    PlayerSuite(runner);
    MonsterSuite(runner);
    ShellSuite(runner);
    WorldSuite(runner);
    artdimstest::ArtDimsSuite(runner);
    SinSuite(runner);
    MenuSuite(runner);
    ScreenSuite(runner);
    QuestSuite(runner);
    SmithSuite(runner);
    CombatSuite(runner);
    CombatPackSuite(runner);
    runner.printSummary();
    return runner.fail() ? 1 : 0;
}