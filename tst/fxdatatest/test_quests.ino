#include "harness/fx_globals.hpp"
#include "quests_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    questfx::test_quests(test);
    test.report(F("test_quests"));
}
void loop() {
    exit(0);
}
