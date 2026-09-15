#include "harness/fx_globals.hpp"
#include "boot_test.hpp"

void setup() { fxTestSetup(); FxTest test; test_boot(test); test.report(F("test_boot")); }
void loop() { exit(0); }