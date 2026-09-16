#include "harness/fx_globals.hpp"
#include "data_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    data::test_data(test);
    test.report(F("data_test"));
}
void loop() {
    exit(0);
}
