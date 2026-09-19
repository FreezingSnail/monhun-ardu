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

    // Sword slash: 32x32 frames (page_count 4), one frame per distinct sword
    // box, box centred in the frame with the white 4x4 core at (14,14). Body
    // offset = ((frame*3 + pass) * 4 + page) * 64 + col * 2.
    if (blobHeader(fxslash, 32, 32, test, F("slash w/h"))) {
        // frame 0 (combo box 12x10 at (10,11)): pass 0 page 1 (rows 11..15) is
        // data/mask 248 at cols 10/11, page 2 (rows 16..20) is 31.
        static const uint8_t box_hi[4] = {248, 248, 248, 248};
        blobBytes(fxslash, 84, box_hi, sizeof(box_hi), test, F("slash f0 box rows11-15"));
        static const uint8_t box_lo[4] = {31, 31, 31, 31};
        blobBytes(fxslash, 148, box_lo, sizeof(box_lo), test, F("slash f0 box rows16-20"));
        // white 4x4 core at (14,14): pass 2 page 1 cols 14..17 = 192/248.
        static const uint8_t core[8] = {192, 248, 192, 248, 192, 248, 192, 248};
        blobBytes(fxslash, 604, core, sizeof(core), test, F("slash f0 core"));
        // frame 0 col 0 is outside the box: fully transparent.
        static const uint8_t clear8[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        blobBytes(fxslash, 0, clear8, sizeof(clear8), test, F("slash f0 outside"));
        // frame 2 (special box 20x16 at (6,8)): the light box clears plane 2
        // (data 0 / mask 255 at cols 6..9 rows 8..15).
        static const uint8_t light_p2[8] = {0, 255, 0, 255, 0, 255, 0, 255};
        blobBytes(fxslash, 2124, light_p2, sizeof(light_p2), test, F("slash f2 plane2 eraser"));
        // frame 4 (spin-cut box 28x26 at (2,3)): page 0 rows 3..7 = 248 at
        // cols 2/3, page 3 rows 24..28 = 31.
        blobBytes(fxslash, 3076, box_hi, sizeof(box_hi), test, F("slash f4 box rows3-7"));
        blobBytes(fxslash, 3268, box_lo, sizeof(box_lo), test, F("slash f4 box rows24-28"));
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

    // Whirl: 8x4 (page_count 1); frame 0 the 2x2 light orbit dot (passes 0,1),
    // frame 1 the 4x4 white ball (all passes), frame 2 the 1x1 light chain dot
    // (cleared plane 2), frame 3 the 2x2 white stun sparkle. Body offsets:
    // frame*48 + pass*16 + col*2.
    if (blobHeader(fxwhirl, 8, 4, test, F("whirl w/h"))) {
        static const uint8_t dot_whirl[6] = {3, 3, 3, 3, 0, 0};
        blobBytes(fxwhirl, 0, dot_whirl, sizeof(dot_whirl), test, F("whirl dot frame"));
        static const uint8_t ball_whirl[8] = {15, 15, 15, 15, 15, 15, 15, 15};
        blobBytes(fxwhirl, 80, ball_whirl, sizeof(ball_whirl), test, F("whirl ball frame"));
        static const uint8_t chain_dot[4] = {1, 1, 0, 0};
        blobBytes(fxwhirl, 96, chain_dot, sizeof(chain_dot), test, F("whirl chain dot frame"));
        static const uint8_t chain_p2[4] = {0, 1, 0, 0};
        blobBytes(fxwhirl, 128, chain_p2, sizeof(chain_p2), test, F("whirl chain plane2 eraser"));
        static const uint8_t stun_dot[4] = {3, 3, 3, 3};
        blobBytes(fxwhirl, 176, stun_dot, sizeof(stun_dot), test, F("whirl stun frame"));
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

    // Chip: 8x8 (page_count 1); frame 0 the 3x3 white idle/aim chip, frame 1
    // the 4x4 white ball. Body 0 / 80 = frame 0/1 pass 2 col 0.
    if (blobHeader(fxchip, 8, 8, test, F("chip w/h"))) {
        static const uint8_t idle[8] = {7, 7, 7, 7, 7, 7, 0, 0};
        blobBytes(fxchip, 32, idle, sizeof(idle), test, F("chip idle frame"));
        static const uint8_t ball[8] = {15, 15, 15, 15, 15, 15, 15, 15};
        blobBytes(fxchip, 80, ball, sizeof(ball), test, F("chip ball frame"));
    }

    // Breakable-part tail (ljj.8): 18x10 (page_count 2), 4 frames in
    // combatPartArtFrame() order. Frame bytes = 3 passes * 2 pages * 18 * 2 =
    // 216. Body 0 = frame 0 (east intact) pass 0 page 0 col 0: the light 6-row
    // bar is rows 2..7 -> data/mask 0b11111100 = 252. Frame 1 (east broken)
    // pass 0 col 9 = dark stub rows 3..6 -> 0b01111000 = 120.
    if (blobHeader(fxtail, 18, 10, test, F("tail w/h"))) {
        static const uint8_t light_col[2] = {252, 252};
        blobBytes(fxtail, 0, light_col, sizeof(light_col), test, F("tail f0 col0 light"));
        static const uint8_t stub_col[2] = {120, 120};
        blobBytes(fxtail, 216 + 9 * 2, stub_col, sizeof(stub_col), test, F("tail f1 broken stub"));
        static const uint8_t clear_col[2] = {0, 0};
        blobBytes(fxtail, 216 + 0 * 2, clear_col, sizeof(clear_col), test, F("tail f1 clears the tip"));
    }

    // Heavy long-tail overlay (bead monhun-ardu-4t4): 24x16, 4 frames in
    // combatPartArtFrame() order, matching heavy.json's appendage box. Header
    // identity only here; the host pixel suite (tst/art_dims_test.hpp) checks
    // the tip/root ink and the west mirror.
    blobHeader(fxtail_heavy, 24, 16, test, F("heavy tail w/h"));

    // Heavy tail-spin overlay (bead monhun-ardu-nch.1): 24x24, 4 frames =
    // world W / N / E / S, frame origin the body centre. Header identity only
    // here; tst/art_dims_test.hpp checks the tip/cap pixels.
    blobHeader(fxtail_spin, 24, 24, test, F("tail spin w/h"));

    // Heavy rotating spin body (bead monhun-ardu-nch.3): the whole longtail in
    // an 8-frame 40x40 sheet, frame i = east silhouette rotated i*45 deg
    // clockwise about the body centre. Header identity only here; the host
    // pixel suite (tst/art_dims_test.hpp) checks the rotation bands and shades.
    blobHeader(fxtailspin, 40, 40, test, F("tail spin sheet w/h"));

    // Chicken attack sheet (bead monhun-ardu-nch.8): 4 frames 32x24 in
    // [peck E, peck W, leap E, leap W] order, drawn during the peck/leap
    // windup+attack. Header identity only here; the host pixel suite
    // (tst/art_dims_test.hpp) checks the poses/mirrors and
    // tst/fxdatatest/monster_art_test.hpp checks the render frame pick.
    blobHeader(fxchickenatk, 32, 24, test, F("chicken attack w/h"));

    // Bull attack sheet (bead monhun-ardu-nch.10): 4 frames 32x24 in
    // [stomp E, stomp W, gore E, gore W] order, drawn during the stomp/gore
    // windup+attack. Header identity only here; the host pixel suite
    // (tst/art_dims_test.hpp) checks the poses/mirrors and
    // tst/fxdatatest/monster_art_test.hpp checks the render frame pick.
    blobHeader(fxbullatk, 32, 24, test, F("bull attack w/h"));

    // Breakable-zone part overlays (bead monhun-ardu-kt7.6): one 4-frame
    // combatPartArtFrame() sheet per breakable demo-roster zone, matching the
    // data box sizes (lunge.json head 11x7 -> 11x8, appendage 9x24; sweep.json
    // head 12x10 -> 12x16, appendage 20x10 -> 20x16; heights padded to a
    // multiple of 8 for the SpritesU page stride). Header identity only here;
    // tst/art_dims_test.hpp pins the frames/mirrors/erases and
    // tst/fxdatatest/monster_art_test.hpp the world-rect frame pick.
    blobHeader(fxhead_chicken, 11, 8, test, F("chicken head part w/h"));
    blobHeader(fxlegs_chicken, 9, 24, test, F("chicken legs part w/h"));
    blobHeader(fxhead_bull, 12, 16, test, F("bull head part w/h"));
    blobHeader(fxhooves_bull, 20, 16, test, F("bull hooves part w/h"));

    // Demo beast sheets (epic monhun-ardu-nch): one 32x24x8 sheet per roster
    // beast -- LUNGE chicken, SWEEP bull, HEAVY longtail -- and RAVAGER keeps
    // the legacy fxmonster. Header identity only here; the host pixel suite
    // (tst/art_dims_test.hpp) checks the silhouettes.
    blobHeader(fxmonster, 32, 24, test, F("ravager monster sheet w/h"));
    blobHeader(fxmonster_lunge, 32, 24, test, F("chicken monster sheet w/h"));
    blobHeader(fxmonster_sweep, 32, 24, test, F("bull monster sheet w/h"));
    blobHeader(fxmonster_heavy, 32, 24, test, F("longtail monster sheet w/h"));

    // Breakable pole variants (beads monhun-ardu-6zb.5 / 6zb.7 / 6zb.10): all
    // three are 24x40 (the additive cap/horn/collar spans the full 24 px so it
    // protrudes 4 px beyond the 16 px DARK post on each side) and carry SIX
    // frames in stage*2 + flash order. PLAIN keeps the legacy 20x40 2-frame
    // fxpole. Header identity only here: the FX block order follows
    // os.listdir, so a next-symbol byte distance is not stable; the host pixel
    // suite (tst/art_dims_test.hpp) parses the generated PNGs and pins the six
    // frames + per-stage ink, and gen-check guarantees PNG <-> blob sync.
    blobHeader(fxpole_sever, 24, 40, test, F("sever pole w/h"));
    blobHeader(fxpole_break, 24, 40, test, F("break pole w/h"));
    blobHeader(fxpole_crack, 24, 40, test, F("crack pole w/h"));

    // Opening menu v2 (beads 2u8 / 4t4): bg + per-row selection tiles, now
    // name-only (no icons). Header identity only here; the device pixel oracle
    // (tst/fxdatatest/menu_art_test.hpp) pins names/frame/cursor and the clear
    // icon slots.
    blobHeader(mh_menu_bg, 128, 64, test, F("menu bg w/h"));
    blobHeader(mh_menu_wsel, 32, 8, test, F("menu weapon sel w/h"));
    blobHeader(mh_menu_msel, 64, 8, test, F("menu monster sel w/h"));
}
