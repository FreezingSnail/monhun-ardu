#pragma once
// Host tests for src/core/sin256.hpp (monhun-ardu-42n.7).
//
// REF256 is a verbatim copy of the original 256-entry mh::SIN256 table (the
// pre-shrink render LUT). The suite walks all 256 inputs of both sin256 and
// cos256 and requires bit-identical results from the 65-entry quarter-wave
// table + sign folding, then pins SIN65 itself to the first 65 reference bytes.
// The reference copy is deliberately kept here, not derived from SIN65, so a
// folding regression in the production header fails loudly.

#include "test.hpp"
#include "../src/core/sin256.hpp"

static const int8_t REF256[256] = {
    0,   0,   1,   1,   2,   2,   2,   3,   3,   4,   4,   4,   5,   5,   5,   6,   6,   6,   7,   7,   8,   8,   8,   9,   9,   9,   10,  10,  10,  10,  11,  11,  11,  12,  12,  12,  12,
    13,  13,  13,  13,  14,  14,  14,  14,  14,  14,  15,  15,  15,  15,  15,  15,  15,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,
    16,  15,  15,  15,  15,  15,  15,  15,  14,  14,  14,  14,  14,  14,  13,  13,  13,  13,  12,  12,  12,  12,  11,  11,  11,  10,  10,  10,  10,  9,   9,   9,   8,   8,   8,   7,   7,
    6,   6,   6,   5,   5,   5,   4,   4,   4,   3,   3,   2,   2,   2,   1,   1,   0,   0,   0,   -1,  -1,  -2,  -2,  -2,  -3,  -3,  -4,  -4,  -4,  -5,  -5,  -5,  -6,  -6,  -6,  -7,  -7,
    -8,  -8,  -8,  -9,  -9,  -9,  -10, -10, -10, -10, -11, -11, -11, -12, -12, -12, -12, -13, -13, -13, -13, -14, -14, -14, -14, -14, -14, -15, -15, -15, -15, -15, -15, -15, -16, -16, -16,
    -16, -16, -16, -16, -16, -16, -16, -16, -16, -16, -16, -16, -16, -16, -16, -16, -16, -16, -15, -15, -15, -15, -15, -15, -15, -14, -14, -14, -14, -14, -14, -13, -13, -13, -13, -12, -12,
    -12, -12, -11, -11, -11, -10, -10, -10, -10, -9,  -9,  -9,  -8,  -8,  -8,  -7,  -7,  -6,  -6,  -6,  -5,  -5,  -5,  -4,  -4,  -4,  -3,  -3,  -2,  -2,  -2,  -1,  -1,  0,
};

void SinSuite(TestRunner &runner) {
    TestSuite suite("Quarter-wave sine (src/core/sin256.hpp)");

    {
        Test t("sin256 bit-identical to the original 256-entry table");
        for (int a = 0; a < 256; a++) {
            t.assert(mh::sin256(static_cast<uint8_t>(a)), REF256[a], "sin256(" + std::to_string(a) + ")");
        }
        t.assert(t.passCount, 256, "all 256 inputs checked");
        suite.addTest(t);
    }

    {
        Test t("cos256 bit-identical to the original 256-entry table");
        for (int a = 0; a < 256; a++) {
            t.assert(mh::cos256(static_cast<uint8_t>(a)), REF256[(a + 64) & 255], "cos256(" + std::to_string(a) + ")");
        }
        t.assert(t.passCount, 256, "all 256 inputs checked");
        suite.addTest(t);
    }

    {
        Test t("SIN65 quarter table holds the first 65 reference sine bytes");
        for (int i = 0; i < 65; i++) {
            t.assert(mh::SIN65[i], REF256[i], "SIN65[" + std::to_string(i) + "]");
        }
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
