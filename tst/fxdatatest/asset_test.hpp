#pragma once

#include "harness/fxtest.hpp"
#include "src/fxdata.h"

// Device asset smoke test: confirm the flashed FX image really carries the
// generated sprite blobs (not just that the offsets compile). The OLED owns the
// shared SPI bus while a plane is displayed, so the FX read happens through the
// same enableOLED/waitForNextPlane/disableOLED bracket the render loop uses.
//
// The scatter pellet blob is checked byte-for-byte against the convert-sprite
// layout: a 2-byte width/height header followed by one masked pass per shade
// plane. The body is opaque light gray (set on planes 0 and 1 => shade 2), so
// both early passes are all ink/mask 0x0F; the 2x2 white nose (all planes) sits
// on the last pass.
inline void test_assets(FxTest &test) {
    FX::enableOLED();
    arduboy.waitForNextPlane();
    FX::disableOLED();

    FX::seekData(fxscatter);
    const uint8_t w = FX::readPendingUInt8();
    const uint8_t h = FX::readEnd();
    test.expectEq(w, 4, F("scatter width"));
    test.expectEq(h, 8, F("scatter height"));

    static const uint8_t expected[24] = {
        15, 15, 15, 15, 15, 15, 15, 15,
        15, 15, 15, 15, 15, 15, 15, 15,
         0, 15,  6, 15,  6, 15,  0, 15,
    };
    uint8_t blob[24];
    FX::seekData(fxscatter + 2);
    FX::readBytesEnd(blob, sizeof(blob));
    for (uint8_t i = 0; i < sizeof(blob); i++)
        test.expectEqIdx(blob[i], expected[i], F("scatter blob"), i);

    // Font sheet header: 4x8 ASCII-ordered tiles (128 frames x 24 bytes each).
    FX::seekData(fxfontw);
    const uint8_t fw = FX::readPendingUInt8();
    const uint8_t fh = FX::readEnd();
    test.expectEq(fw, 4, F("font glyph width"));
    test.expectEq(fh, 8, F("font glyph height"));

    // Glyph '0' (ASCII 48) starts at 2 + 48 * 24. Its left column is inked rows
    // 0..4 (0b00011111) and fully opaque, on the first plane pass.
    FX::seekData(fxfontw + 2 + 48u * 24u);
    const uint8_t d0 = FX::readPendingUInt8();
    const uint8_t m0 = FX::readEnd();
    test.expectEq(d0, 0b00011111, F("glyph0 data col0"));
    test.expectEq(m0, 0b00011111, F("glyph0 mask col0"));
}
