
#define ABG_IMPLEMENTATION
#define SPRITESU_IMPLEMENTATION
#include "src/common.hpp"
#include "src/globals.hpp"
#include "src/fxdata.h"

decltype(arduboy) arduboy;

void setup() {
    // Serial.begin(9600);

    arduboy.boot();
    arduboy.startGray();
    arduboy.initRandomSeed();

    FX::begin(FX_DATA_PAGE);
    FX::setCursorRange(0, 32767);
}

void run() {
}

void render() {
}

void loop() {
    FX::enableOLED();
    arduboy.waitForNextPlane();
    FX::disableOLED();
    if (arduboy.needsUpdate()) {
        arduboy.pollButtons();
        run();
    }
    render();
}
