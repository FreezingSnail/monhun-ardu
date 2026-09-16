#pragma once

#include "harness/fxtest.hpp"
#include "src/fxdata.h"

// Device asset smoke test: confirm the flashed FX image really carries the
// generated sprite blobs (not just that the offsets compile). The OLED owns the
// shared SPI bus while a plane is displayed, so the FX read happens through the
// same enableOLED/waitForNextPlane/disableOLED bracket the render loop uses.
//
// Blob layout (tools/convert-sprite.py): a 2-byte width/height header followed
// by, per frame, per shade pass (3 passes), per 8-px page, per column a
// (data, mask) pair. The pinned bytes below were dumped from the flashed image
// (see output.md) and cross-checked against the PNGs host-side.
namespace assetcheck {

// Read a blob's 2-byte header.
bool blobHeader(uint24_t sheet, uint8_t want_w, uint8_t want_h, FxTest &test, const __FlashStringHelper *label) {
    FX::seekData(sheet);
    const uint8_t w = FX::readPendingUInt8();
    const uint8_t h = FX::readEnd();
    test.expectEq(w, want_w, label);
    test.expectEq(h, want_h, label);
    return w == want_w && h == want_h;
}

// Compare `n` body bytes (after the 2-byte header) at `offset` against `want`.
// Reads one byte at a time (readPendingUInt8 / readEnd) rather than
// readBytesEnd: the inline-asm bulk read is fragile here and per-byte reads
// match the render path's access pattern.
void blobBytes(uint24_t sheet, uint16_t offset, const uint8_t *want, uint8_t n, FxTest &test, const __FlashStringHelper *label) {
    FX::seekData(sheet + 2 + offset);
    for (uint8_t i = 0; i < n; i++) {
        const uint8_t got = (i + 1 < n) ? FX::readPendingUInt8() : FX::readEnd();
        test.expectEqIdx(got, want[i], label, i);
    }
}

}   // namespace assetcheck

inline void test_assets(FxTest &test) {
    using namespace assetcheck;

    FX::enableOLED();
    arduboy.waitForNextPlane();
    FX::disableOLED();

    FX::seekData(fxscatter);
    const uint8_t w = FX::readPendingUInt8();
    const uint8_t h = FX::readEnd();
    test.expectEq(w, 4, F("scatter width"));
    test.expectEq(h, 8, F("scatter height"));

    static const uint8_t expected[24] = {
        15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 0, 15, 6, 15, 6, 15, 0, 15,
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

    // ------------------------------------------------- overlay/effect sheets
    // Body offsets: frame*3*page_count*w*2 + pass*page_count*w*2 + page*w*2 +
    // col*2. Light gray fills passes 0+1, white all three, black erasers mask
    // only.

    // Sword slash: 24x24 frames (page_count 3), light box (6,7,12,10) with the
    // white 4x4 core at (8,9). Body offset 14 = col 7 rows 6..13 (pass 0).
    if (blobHeader(fxslash, 24, 24, test, F("slash w/h"))) {
        // frame 0 (box 12x10 at (6,7)): cols 6..15 rows 7..14 pass 0 are all
        // light (data 128 / mask 128), starting at body 12.
        static const uint8_t box_cols[20] = {128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128};
        blobBytes(fxslash, 12, box_cols, sizeof(box_cols), test, F("slash f0 box"));
        // frame 3 (special, box 20x16 at (2,4)): same light pattern from body 444.
        static const uint8_t special_box[12] = {128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128};
        blobBytes(fxslash, 444, special_box, sizeof(special_box), test, F("slash f3 box"));
    }

    // Riposte rim: 24x24 (page_count 3), light 24x20 rect covering the special
    // slash. Body 0 = col 0 rows 0..17 all light; body 118 = col 16 rows 0..5
    // pass 2 ... the rim interior there is the eraser hole (mask, planes 0).
    if (blobHeader(fxripspecial, 24, 24, test, F("riposte w/h"))) {
        // Upper rows are painted on all planes (white); the rect ends at row
        // 19, so the last pages carry the plane-2 ink (15) only.
        static const uint8_t top[8] = {255, 255, 255, 255, 255, 255, 255, 255};
        blobBytes(fxripspecial, 0, top, sizeof(top), test, F("riposte f0 col0"));
        static const uint8_t lower[8] = {15, 15, 15, 15, 15, 15, 15, 15};
        blobBytes(fxripspecial, 118, lower, sizeof(lower), test, F("riposte f0 pass2"));
    }

    // Parry: 24x16 (page_count 2), white blade 2x14 at col 11 rows 0..13, light
    // 6x2 cap at col 9 y=2. Blade col 11 rows 0..7 pass 0 -> data/mask 255;
    // cap col 10 rows 8..15 pass 0 -> rows 8,9 light (0b00000011).
    if (blobHeader(fxparry, 24, 16, test, F("parry w/h"))) {
        // cols 8..15 rows 0..7 pass 0: cap rows 2,3 (data 12), blade cols
        // 11,12 rows 0..7 (255), transparent on both sides.
        static const uint8_t cols[16] = {0, 0, 12, 12, 12, 12, 255, 255, 255, 255, 12, 12, 12, 12, 0, 0};
        blobBytes(fxparry, 16, cols, sizeof(cols), test, F("parry f0 cols8-15"));
        // blade page 1 (rows 8..13) still white.
        static const uint8_t blade_low[4] = {63, 63, 63, 63};
        blobBytes(fxparry, 70, blade_low, sizeof(blade_low), test, F("parry f0 blade page1"));
    }

    // Flail chain: 40x16 (page_count 2), dark 1 px dots. Frame 0 reach 19 puts
    // dots at cols 24/29/34; frame 1 (reach 21) dots at 25/30/35. Body 48 =
    // frame 0 page 0 col 24 pass 0; body 52 = frame 0 col 25 pass 0 (dot at
    // row 8 => bits 1+2).
    if (blobHeader(fxchain, 40, 16, test, F("chain w/h"))) {
        // dot 1 (col 24, row 8) lives in page 1: its (data, mask) pair starts
        // at body 50 (mask bit 0). Dot 2 (col 29) starts at body 60.
        static const uint8_t dot1[4] = {0, 0, 0, 0};
        blobBytes(fxchain, 48, dot1, sizeof(dot1), test, F("chain f0 page1 col24"));
        static const uint8_t dot2[4] = {0, 0, 1, 1};
        blobBytes(fxchain, 146, dot2, sizeof(dot2), test, F("chain f1 page1 col29"));
    }

    // Whirl: 8x4 (page_count 1); frame 0 the 2x2 light orbit dot (passes 0,1),
    // frame 1 the 4x4 white ball (all passes). Body 48 = frame 1 pass 0 col 0.
    if (blobHeader(fxwhirl, 8, 4, test, F("whirl w/h"))) {
        static const uint8_t dot_whirl[6] = {3, 3, 3, 3, 0, 0};
        blobBytes(fxwhirl, 0, dot_whirl, sizeof(dot_whirl), test, F("whirl dot frame"));
        static const uint8_t ball_whirl[4] = {15, 15, 15, 15};
        blobBytes(fxwhirl, 48, ball_whirl, sizeof(ball_whirl), test, F("whirl ball frame"));
    }

    // Deflect: 24x16 (page_count 2); light 1x12 bars at cols 2 and 21 rows
    // 2..13. Body 4 = col 2 rows 0..3 pass 0 (rows 2,3 light = 0b00001100).
    if (blobHeader(fxdeflect, 24, 16, test, F("deflect w/h"))) {
        static const uint8_t bar[4] = {252, 252, 0, 0};
        blobBytes(fxdeflect, 4, bar, sizeof(bar), test, F("deflect f0 col2"));
    }

    // Gun shield: 12x16 (page_count 2). Body 0 = plate frame col 0 rows 0..7
    // pass 0 (rows 1..7 light = 254); body 30 = notch col 5 pass 0 (rows 1..6
    // light, row 7 black = 0 => 127); frames 1/2 start at 96/192 body offset.
    if (blobHeader(fxguard, 12, 16, test, F("guard w/h"))) {
        static const uint8_t plate[10] = {0, 0, 254, 254, 254, 254, 254, 254, 254, 254};
        blobBytes(fxguard, 0, plate, sizeof(plate), test, F("guard f0 col0"));
        static const uint8_t notch[8] = {127, 127, 127, 127, 0, 127, 0, 127};
        blobBytes(fxguard, 30, notch, sizeof(notch), test, F("guard f0 notch col5"));
        static const uint8_t guard_white[2] = {254, 254};
        blobBytes(fxguard, 196, guard_white, sizeof(guard_white), test, F("guard f1 col0"));
        static const uint8_t shove_white[2] = {254, 254};
        blobBytes(fxguard, 388, shove_white, sizeof(shove_white), test, F("shove f2 col0"));
    }

    // Reload bar: 10x8 (page_count 1); light 10x2 row at y=3 (bits 3,4 => 24).
    // Body 14 = col 2 rows 0..7 pass 0.
    if (blobHeader(fxreload, 10, 8, test, F("reload w/h"))) {
        static const uint8_t bar[6] = {24, 24, 24, 24, 24, 24};
        blobBytes(fxreload, 14, bar, sizeof(bar), test, F("reload f0 bar col2"));
    }

    // I-frame erase: 4x16 (page_count 2); black 4x1 eraser row 0 (mask only,
    // planes off) then rows 1..15 transparent. Body 0 = col 0 rows 0..7.
    if (blobHeader(fxerase, 4, 16, test, F("erase w/h"))) {
        static const uint8_t page0[8] = {0, 1, 0, 1, 0, 1, 0, 1};
        blobBytes(fxerase, 0, page0, sizeof(page0), test, F("erase row0"));
        static const uint8_t clear[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        blobBytes(fxerase, 8, clear, sizeof(clear), test, F("erase row1 clear"));
    }

    // Trail puffs: 4x4 (page_count 1); frame 0 light, frame 1 dark. Body 24 =
    // frame 1 pass 0 col 0.
    if (blobHeader(fxtrail, 4, 4, test, F("trail w/h"))) {
        static const uint8_t puff0[4] = {3, 3, 3, 3};
        blobBytes(fxtrail, 0, puff0, sizeof(puff0), test, F("trail light puff"));
        static const uint8_t puff1[4] = {3, 3, 3, 3};
        blobBytes(fxtrail, 24, puff1, sizeof(puff1), test, F("trail dark puff"));
    }

    // Telegraph: 32x24 (page_count 3). Frame 0 lunge box (4,1,24,22) dark with
    // the light 2x2 core; frame 1 sweep box (0,0,32,24) light with white core.
    // Body 0 = f0 col 0 rows 0..3 pass 0 (0 here, box starts row 1 => 254s at
    // body 10 = page 0 col 1). Body 194 = f1 pass 1 col 0 rows 0..3 (light).
    if (blobHeader(fxtelegraph, 32, 24, test, F("telegraph w/h"))) {
        static const uint8_t lunge_lead[12] = {0, 0, 0, 0, 0, 0, 0, 0, 254, 254, 254, 254};
        blobBytes(fxtelegraph, 0, lunge_lead, sizeof(lunge_lead), test, F("lunge f0 col0"));
        static const uint8_t lunge_core[6] = {254, 254, 254, 254, 254, 254};
        blobBytes(fxtelegraph, 10, lunge_core, sizeof(lunge_core), test, F("lunge f0 col1"));
        static const uint8_t sweep_lead[12] = {0, 0, 0, 0, 0, 0, 0, 254, 0, 254, 0, 254};
        blobBytes(fxtelegraph, 194, sweep_lead, sizeof(sweep_lead), test, F("sweep f1 pass1"));
    }

    // Chip: 8x8 (page_count 1), white 4x4 head at the frame origin. Body 0 =
    // col 0 rows 0..7 pass 0 (rows 0..3 white, below transparent).
    if (blobHeader(fxchip, 8, 8, test, F("chip w/h"))) {
        static const uint8_t chip[8] = {15, 15, 15, 15, 15, 15, 15, 15};
        blobBytes(fxchip, 0, chip, sizeof(chip), test, F("chip f0 col0"));
    }
}
