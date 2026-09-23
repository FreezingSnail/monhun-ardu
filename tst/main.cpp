// Host test runner entry point. Compiles to build/tests/host via `make test`.
#include "test.hpp"
#include "fp_test.hpp"
#include "input_test.hpp"
#include "player_test.hpp"
#include "monster_test.hpp"
#include "hitscan_test.hpp"
#include "world_test.hpp"
#include "art_dims_test.hpp"
#include "sin_test.hpp"
#include "screens_test.hpp"
#include "app_state_test.hpp"
#include "quests_test.hpp"
#include "smith_test.hpp"
#include "combat_test.hpp"
#include "combat_pack_test.hpp"
#include "render_math_test.hpp"
#include "zone_test.hpp"
#include "gather_test.hpp"
#include "items_test.hpp"
#include "armor_test.hpp"
#include "armor_engine_test.hpp"
#include "armor_effect_test.hpp"
#include "carve_test.hpp"
#include "card_state_test.hpp"
#include "forge_state_test.hpp"

int main() {
    TestRunner runner;
    FpSuite(runner);
    InputSuite(runner);
    PlayerSuite(runner);
    MonsterSuite(runner);
    HitscanSuite(runner);
    WorldSuite(runner);
    artdimstest::ArtDimsSuite(runner);
    SinSuite(runner);
    ScreenSuite(runner);
    AppSuite(runner);
    QuestSuite(runner);
    SmithSuite(runner);
    CombatSuite(runner);
    CombatPackSuite(runner);
    rendermathtest::RenderMathSuite(runner);
    ZoneSuite(runner);
    GatherSuite(runner);
    ItemsSuite(runner);
    ArmorSuite(runner);
    ArmorEngineSuite(runner);
    ArmorEffectSuite(runner);
    CarveSuite(runner);
    cardstatetest::CardStateSuite(runner);
    ForgeSuite(runner);
    runner.printSummary();
    return runner.fail() ? 1 : 0;
}