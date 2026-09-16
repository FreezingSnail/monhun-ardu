#include "harness/fx_globals.hpp"
#include "audio_test.hpp"

void setup() {
    fxTestSetup();
    FxTest test;
    audio_test::test_audio(test);
    test.report(F("test_audio"));
}
void loop() {
    exit(0);
}
