#pragma once
// Host pack-parity suite for the combat blob (beads monhun-ardu-ljj.2, cgk).
//
// Reads fxdata/tables/combat.bin from disk (make test runs from the repo root),
// verifies the pinned sha256 from src/generated/combat_expect.hpp, the header
// (magic/version/flags/counts), section tiling, that every per-record offset in
// combat_meta.hpp lands on the right record, and decodes every record from the
// packed bytes and compares it against the generated host structs. This is the
// host half of the "one loader" chain: blob == host structs == loader values.
#include "test.hpp"
#include "../src/core/combat.hpp"
#include "../src/generated/combat_expect.hpp"

#include <fstream>
#include <iterator>
#include <stdint.h>
#include <string.h>
#include <vector>

using namespace mh;

namespace {

// ------------------------------------------------------------- sha256
// Small self-contained SHA-256 (FIPS 180-4). The test self-checks it against
// the published "abc" vector before trusting it for the blob hash.
struct Sha256 {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    uint32_t datalen;

    static uint32_t rotr(uint32_t x, uint32_t n) {
        return (x >> n) | (x << (32 - n));
    }

    void init() {
        state[0] = 0x6a09e667;
        state[1] = 0xbb67ae85;
        state[2] = 0x3c6ef372;
        state[3] = 0xa54ff53a;
        state[4] = 0x510e527f;
        state[5] = 0x9b05688c;
        state[6] = 0x1f83d9ab;
        state[7] = 0x5be0cd19;
        bitlen = 0;
        datalen = 0;
    }

    void transform(const uint8_t *chunk) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74,
            0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d,
            0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e,
            0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
            0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
        };
        uint32_t m[64];
        for (uint32_t i = 0, j = 0; i < 16; i++, j += 4)
            m[i] = static_cast<uint32_t>(chunk[j] << 24) | (chunk[j + 1] << 16) | (chunk[j + 2] << 8) | chunk[j + 3];
        for (uint32_t i = 16; i < 64; i++) {
            const uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
            const uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
            m[i] = m[i - 16] + s0 + m[i - 7] + s1;
        }
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (uint32_t i = 0; i < 64; i++) {
            const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = h + s1 + ch + k[i] + m[i];
            const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = s0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    void update(const uint8_t *input, size_t len) {
        for (size_t i = 0; i < len; i++) {
            data[datalen++] = input[i];
            if (datalen == 64) {
                transform(data);
                bitlen += 512;
                datalen = 0;
            }
        }
    }

    void final(uint8_t hash[32]) {
        uint32_t i = datalen;
        if (datalen < 56) {
            data[i++] = 0x80;
            while (i < 56)
                data[i++] = 0;
        } else {
            data[i++] = 0x80;
            while (i < 64)
                data[i++] = 0;
            transform(data);
            memset(data, 0, 56);
        }
        bitlen += static_cast<uint64_t>(datalen) * 8;
        for (uint32_t j = 0; j < 8; j++)
            data[63 - j] = static_cast<uint8_t>(bitlen >> (j * 8));
        transform(data);
        for (i = 0; i < 4; i++) {
            for (uint32_t j = 0; j < 8; j++)
                hash[i + j * 4] = static_cast<uint8_t>((state[j] >> (24 - i * 8)) & 0xFF);
        }
    }
};

inline uint8_t b8(const std::vector<uint8_t> &v, size_t off) {
    return v[off];
}
inline int8_t bi8(const std::vector<uint8_t> &v, size_t off) {
    return static_cast<int8_t>(v[off]);
}
inline uint16_t b16(const std::vector<uint8_t> &v, size_t off) {
    return static_cast<uint16_t>(v[off] | (static_cast<uint16_t>(v[off + 1]) << 8));
}

// Section + named-offset table (mirrors combat_meta.hpp names).
struct MetaRecord {
    const char *name;
    uint16_t off;
    uint16_t sectionOff;
    uint8_t idx;
    uint8_t size;
    uint16_t count;
};

}   // namespace

void CombatPackSuite(TestRunner &runner) {
    TestSuite suite("Combat blob pack parity (fxdata/tables/combat.bin vs generated headers)");

    {
        Test t("sha256 reference vector");
        Sha256 sha;
        sha.init();
        sha.update(reinterpret_cast<const uint8_t *>("abc"), 3);
        uint8_t digest[32];
        sha.final(digest);
        static const uint8_t want[32] = {
            0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
            0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
        };
        t.assert(memcmp(digest, want, sizeof(want)), 0, "sha256('abc')");
        suite.addTest(t);
    }

    std::ifstream in("fxdata/tables/combat.bin", std::ios::binary);
    if (!in) {
        Test t("fxdata/tables/combat.bin opens from repo root");
        t.assert(0, 1, "combat.bin readable");
        suite.addTest(t);
        runner.addTestSuite(suite);
        return;
    }
    const std::vector<uint8_t> blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    {
        Test t("blob size + header (magic/version/flags/counts) + sha256");
        t.assert(blob.size(), combat_expect::BLOB_SIZE, "file size vs expect");
        t.assert(blob.size(), combat::SIZE, "file size vs meta");
        t.assert(blob.size(), combat_data::BLOB_SIZE, "file size vs host data");
        t.assert(b8(blob, 0), 0x43, "magic lo ('C')");
        t.assert(b8(blob, 1), 0x4D, "magic hi ('M')");
        t.assert(b16(blob, 0), 0x4D43, "magic u16");
        t.assert(b8(blob, 2), combat::VERSION, "version");
        t.assert(b8(blob, 3), combat::FLAGS, "flags");
        t.assert(combat_expect::BLOB_SIZE, combat::SIZE, "expect blob size");
        t.assert(combat_data::VERSION, combat::VERSION, "host data version");

        // Header counts: 10 little-endian u16 after magic/version/flags, then 4
        // reserved (0).
        t.assert(b16(blob, 4), combat::CREATURES_COUNT, "count creatures");
        t.assert(b16(blob, 6), combat::PROFILES_COUNT, "count profiles");
        t.assert(b16(blob, 8), combat::SKELETONS_COUNT, "count skeletons");
        t.assert(b16(blob, 10), combat::ZONES_COUNT, "count zones");
        t.assert(b16(blob, 12), combat::ANCHORS_COUNT, "count anchors");
        t.assert(b16(blob, 14), combat::ATTACKS_COUNT, "count attacks");
        t.assert(b16(blob, 16), combat::WINDOWS_COUNT, "count windows");
        t.assert(b16(blob, 18), combat::PATTERNS_COUNT, "count patterns");
        t.assert(b16(blob, 20), combat::GUARDS_COUNT, "count guards");
        t.assert(b16(blob, 22), combat::STEPS_COUNT, "count steps");
        for (uint8_t i = 0; i < 4; i++)
            t.assert(b16(blob, 24 + i * 2), 0, "reserved header count");

        // Sections tile the blob with no gaps or padding.
        t.assert(combat::CREATURES_OFF, combat::HEADER_SIZE, "creatures start after header");
        t.assert(combat::PROFILES_OFF, combat::CREATURES_OFF + combat::CREATURE_SIZE * combat::CREATURES_COUNT, "creatures tile");
        t.assert(combat::SKELETONS_OFF, combat::PROFILES_OFF + combat::PROFILE_SIZE * combat::PROFILES_COUNT, "profiles tile");
        t.assert(combat::ZONES_OFF, combat::SKELETONS_OFF + combat::SKELETON_SIZE * combat::SKELETONS_COUNT, "skeletons tile");
        t.assert(combat::ANCHORS_OFF, combat::ZONES_OFF + combat::ZONE_SIZE * combat::ZONES_COUNT, "zones tile");
        t.assert(combat::ATTACKS_OFF, combat::ANCHORS_OFF + combat::ANCHOR_SIZE * combat::ANCHORS_COUNT, "anchors tile");
        t.assert(combat::WINDOWS_OFF, combat::ATTACKS_OFF + combat::ATTACK_SIZE * combat::ATTACKS_COUNT, "attacks tile");
        t.assert(combat::PATTERNS_OFF, combat::WINDOWS_OFF + combat::WINDOW_SIZE * combat::WINDOWS_COUNT, "windows tile");
        t.assert(combat::GUARDS_OFF, combat::PATTERNS_OFF + combat::PATTERN_SIZE * combat::PATTERNS_COUNT, "patterns tile");
        t.assert(combat::STEPS_OFF, combat::GUARDS_OFF + combat::GUARD_SIZE * combat::GUARDS_COUNT, "guards tile");
        t.assert(combat::STEPS_OFF + combat::STEP_SIZE * combat::STEPS_COUNT, combat::SIZE, "steps end at blob size");

        Sha256 sha;
        sha.init();
        sha.update(blob.data(), blob.size());
        uint8_t digest[32];
        sha.final(digest);
        t.assert(memcmp(digest, combat_expect::BLOB_SHA256, sizeof(digest)), 0, "blob sha256");
        suite.addTest(t);
    }

    {
        Test t("every generated record offset lands on the right record");
        static const MetaRecord records[] = {
            {"CREATURE_HEAVY", combat::CREATURE_HEAVY_OFF, combat::CREATURES_OFF, combat::CREATURE_HEAVY, combat::CREATURE_SIZE, combat::CREATURES_COUNT},
            {"CREATURE_LUNGE", combat::CREATURE_LUNGE_OFF, combat::CREATURES_OFF, combat::CREATURE_LUNGE, combat::CREATURE_SIZE, combat::CREATURES_COUNT},
            {"CREATURE_RAVAGER", combat::CREATURE_RAVAGER_OFF, combat::CREATURES_OFF, combat::CREATURE_RAVAGER, combat::CREATURE_SIZE, combat::CREATURES_COUNT},
            {"CREATURE_SWEEP", combat::CREATURE_SWEEP_OFF, combat::CREATURES_OFF, combat::CREATURE_SWEEP, combat::CREATURE_SIZE, combat::CREATURES_COUNT},
            {"PROFILE_HEAVY", combat::PROFILE_HEAVY_OFF, combat::PROFILES_OFF, combat::CREATURE_HEAVY, combat::PROFILE_SIZE, combat::PROFILES_COUNT},
            {"PROFILE_LUNGE", combat::PROFILE_LUNGE_OFF, combat::PROFILES_OFF, combat::CREATURE_LUNGE, combat::PROFILE_SIZE, combat::PROFILES_COUNT},
            {"PROFILE_RAVAGER", combat::PROFILE_RAVAGER_OFF, combat::PROFILES_OFF, combat::CREATURE_RAVAGER, combat::PROFILE_SIZE, combat::PROFILES_COUNT},
            {"PROFILE_SWEEP", combat::PROFILE_SWEEP_OFF, combat::PROFILES_OFF, combat::CREATURE_SWEEP, combat::PROFILE_SIZE, combat::PROFILES_COUNT},
            {"PROFILE_POLE", combat::PROFILE_POLE_OFF, combat::PROFILES_OFF, combat::CREATURE_POLE, combat::PROFILE_SIZE, combat::PROFILES_COUNT},
            {"PROFILE_POLE_SEVER", combat::PROFILE_POLE_SEVER_OFF, combat::PROFILES_OFF, combat::CREATURE_POLE_SEVER, combat::PROFILE_SIZE, combat::PROFILES_COUNT},
            {"SKELETON_BULL", combat::SKELETON_BULL_OFF, combat::SKELETONS_OFF, combat::SKELETON_BULL, combat::SKELETON_SIZE, combat::SKELETONS_COUNT},
            {"SKELETON_CHICKEN", combat::SKELETON_CHICKEN_OFF, combat::SKELETONS_OFF, combat::SKELETON_CHICKEN, combat::SKELETON_SIZE, combat::SKELETONS_COUNT},
            {"SKELETON_LONGTAIL", combat::SKELETON_LONGTAIL_OFF, combat::SKELETONS_OFF, combat::SKELETON_LONGTAIL, combat::SKELETON_SIZE, combat::SKELETONS_COUNT},
            {"SKELETON_POLE", combat::SKELETON_POLE_OFF, combat::SKELETONS_OFF, combat::SKELETON_POLE, combat::SKELETON_SIZE, combat::SKELETONS_COUNT},
            {"SKELETON_QUAD_32X24", combat::SKELETON_QUAD_32X24_OFF, combat::SKELETONS_OFF, combat::SKELETON_QUAD_32X24, combat::SKELETON_SIZE, combat::SKELETONS_COUNT},
            {"ZONE_RAVAGER_HEAD", combat::ZONE_RAVAGER_HEAD_OFF, combat::ZONES_OFF, combat::ZONE_RAVAGER_HEAD, combat::ZONE_SIZE, combat::ZONES_COUNT},
            {"ZONE_RAVAGER_APPENDAGE", combat::ZONE_RAVAGER_APPENDAGE_OFF, combat::ZONES_OFF, combat::ZONE_RAVAGER_APPENDAGE, combat::ZONE_SIZE, combat::ZONES_COUNT},
            {"ZONE_SWEEP_HEAD", combat::ZONE_SWEEP_HEAD_OFF, combat::ZONES_OFF, combat::ZONE_SWEEP_HEAD, combat::ZONE_SIZE, combat::ZONES_COUNT},
            {"ZONE_SWEEP_APPENDAGE", combat::ZONE_SWEEP_APPENDAGE_OFF, combat::ZONES_OFF, combat::ZONE_SWEEP_APPENDAGE, combat::ZONE_SIZE, combat::ZONES_COUNT},
            {"ZONE_POLE_HEAD", combat::ZONE_POLE_HEAD_OFF, combat::ZONES_OFF, combat::ZONE_POLE_HEAD, combat::ZONE_SIZE, combat::ZONES_COUNT},
            {"ZONE_POLE_BREAK_APPENDAGE", combat::ZONE_POLE_BREAK_APPENDAGE_OFF, combat::ZONES_OFF, combat::ZONE_POLE_BREAK_APPENDAGE, combat::ZONE_SIZE, combat::ZONES_COUNT},
            {"ZONE_POLE_CRACK_APPENDAGE", combat::ZONE_POLE_CRACK_APPENDAGE_OFF, combat::ZONES_OFF, combat::ZONE_POLE_CRACK_APPENDAGE, combat::ZONE_SIZE, combat::ZONES_COUNT},
            {"ZONE_POLE_SEVER_APPENDAGE", combat::ZONE_POLE_SEVER_APPENDAGE_OFF, combat::ZONES_OFF, combat::ZONE_POLE_SEVER_APPENDAGE, combat::ZONE_SIZE, combat::ZONES_COUNT},
            {"ANCHOR_BULL_ORIGIN", combat::ANCHOR_BULL_ORIGIN_OFF, combat::ANCHORS_OFF, 0, combat::ANCHOR_SIZE, combat::ANCHORS_COUNT},
            {"ANCHOR_BULL_HEAD", combat::ANCHOR_BULL_HEAD_OFF, combat::ANCHORS_OFF, 1, combat::ANCHOR_SIZE, combat::ANCHORS_COUNT},
            {"ANCHOR_CHICKEN_ORIGIN", combat::ANCHOR_CHICKEN_ORIGIN_OFF, combat::ANCHORS_OFF, 2, combat::ANCHOR_SIZE, combat::ANCHORS_COUNT},
            {"ANCHOR_CHICKEN_HEAD", combat::ANCHOR_CHICKEN_HEAD_OFF, combat::ANCHORS_OFF, 3, combat::ANCHOR_SIZE, combat::ANCHORS_COUNT},
            {"ANCHOR_LONGTAIL_ORIGIN", combat::ANCHOR_LONGTAIL_ORIGIN_OFF, combat::ANCHORS_OFF, 4, combat::ANCHOR_SIZE, combat::ANCHORS_COUNT},
            {"ANCHOR_LONGTAIL_HEAD", combat::ANCHOR_LONGTAIL_HEAD_OFF, combat::ANCHORS_OFF, 5, combat::ANCHOR_SIZE, combat::ANCHORS_COUNT},
            {"ANCHOR_POLE_ORIGIN", combat::ANCHOR_POLE_ORIGIN_OFF, combat::ANCHORS_OFF, 6, combat::ANCHOR_SIZE, combat::ANCHORS_COUNT},
            {"ANCHOR_POLE_HEAD", combat::ANCHOR_POLE_HEAD_OFF, combat::ANCHORS_OFF, 7, combat::ANCHOR_SIZE, combat::ANCHORS_COUNT},
            {"ANCHOR_QUAD_32X24_ORIGIN", combat::ANCHOR_QUAD_32X24_ORIGIN_OFF, combat::ANCHORS_OFF, 8, combat::ANCHOR_SIZE, combat::ANCHORS_COUNT},
            {"ANCHOR_QUAD_32X24_HEAD", combat::ANCHOR_QUAD_32X24_HEAD_OFF, combat::ANCHORS_OFF, 9, combat::ANCHOR_SIZE, combat::ANCHORS_COUNT},
            {"ATTACK_HEAVY_BITE", combat::ATTACK_HEAVY_BITE_OFF, combat::ATTACKS_OFF, combat::ATTACK_HEAVY_BITE, combat::ATTACK_SIZE, combat::ATTACKS_COUNT},
            {"ATTACK_HEAVY_TAIL_SPIN", combat::ATTACK_HEAVY_TAIL_SPIN_OFF, combat::ATTACKS_OFF, combat::ATTACK_HEAVY_TAIL_SPIN, combat::ATTACK_SIZE, combat::ATTACKS_COUNT},
            {"ATTACK_HEAVY_TAIL_SLAM", combat::ATTACK_HEAVY_TAIL_SLAM_OFF, combat::ATTACKS_OFF, combat::ATTACK_HEAVY_TAIL_SLAM, combat::ATTACK_SIZE, combat::ATTACKS_COUNT},
            {"ATTACK_LUNGE_PECK", combat::ATTACK_LUNGE_PECK_OFF, combat::ATTACKS_OFF, combat::ATTACK_LUNGE_PECK, combat::ATTACK_SIZE, combat::ATTACKS_COUNT},
            {"ATTACK_LUNGE_LEAP", combat::ATTACK_LUNGE_LEAP_OFF, combat::ATTACKS_OFF, combat::ATTACK_LUNGE_LEAP, combat::ATTACK_SIZE, combat::ATTACKS_COUNT},
            {"ATTACK_LUNGE_WING_BEAT", combat::ATTACK_LUNGE_WING_BEAT_OFF, combat::ATTACKS_OFF, combat::ATTACK_LUNGE_WING_BEAT, combat::ATTACK_SIZE, combat::ATTACKS_COUNT},
            {"ATTACK_RAVAGER_BITE", combat::ATTACK_RAVAGER_BITE_OFF, combat::ATTACKS_OFF, combat::ATTACK_RAVAGER_BITE, combat::ATTACK_SIZE, combat::ATTACKS_COUNT},
            {"ATTACK_RAVAGER_TAIL_SWEEP", combat::ATTACK_RAVAGER_TAIL_SWEEP_OFF, combat::ATTACKS_OFF, combat::ATTACK_RAVAGER_TAIL_SWEEP, combat::ATTACK_SIZE, combat::ATTACKS_COUNT},
            {"ATTACK_SWEEP_STOMP", combat::ATTACK_SWEEP_STOMP_OFF, combat::ATTACKS_OFF, combat::ATTACK_SWEEP_STOMP, combat::ATTACK_SIZE, combat::ATTACKS_COUNT},
            {"ATTACK_SWEEP_GORE", combat::ATTACK_SWEEP_GORE_OFF, combat::ATTACKS_OFF, combat::ATTACK_SWEEP_GORE, combat::ATTACK_SIZE, combat::ATTACKS_COUNT},
            {"ATTACK_SWEEP_REAR_KICK", combat::ATTACK_SWEEP_REAR_KICK_OFF, combat::ATTACKS_OFF, combat::ATTACK_SWEEP_REAR_KICK, combat::ATTACK_SIZE, combat::ATTACKS_COUNT},
            {"WINDOW_HEAVY_BITE_0", combat::WINDOW_HEAVY_BITE_0_OFF, combat::WINDOWS_OFF, combat::WINDOW_HEAVY_BITE_0, combat::WINDOW_SIZE, combat::WINDOWS_COUNT},
            {"WINDOW_HEAVY_TAIL_SPIN_0", combat::WINDOW_HEAVY_TAIL_SPIN_0_OFF, combat::WINDOWS_OFF, combat::WINDOW_HEAVY_TAIL_SPIN_0, combat::WINDOW_SIZE, combat::WINDOWS_COUNT},
            {"WINDOW_HEAVY_TAIL_SPIN_1", combat::WINDOW_HEAVY_TAIL_SPIN_1_OFF, combat::WINDOWS_OFF, combat::WINDOW_HEAVY_TAIL_SPIN_1, combat::WINDOW_SIZE, combat::WINDOWS_COUNT},
            {"WINDOW_HEAVY_TAIL_SPIN_2", combat::WINDOW_HEAVY_TAIL_SPIN_2_OFF, combat::WINDOWS_OFF, combat::WINDOW_HEAVY_TAIL_SPIN_2, combat::WINDOW_SIZE, combat::WINDOWS_COUNT},
            {"WINDOW_HEAVY_TAIL_SPIN_3", combat::WINDOW_HEAVY_TAIL_SPIN_3_OFF, combat::WINDOWS_OFF, combat::WINDOW_HEAVY_TAIL_SPIN_3, combat::WINDOW_SIZE, combat::WINDOWS_COUNT},
            {"WINDOW_HEAVY_TAIL_SLAM_0", combat::WINDOW_HEAVY_TAIL_SLAM_0_OFF, combat::WINDOWS_OFF, combat::WINDOW_HEAVY_TAIL_SLAM_0, combat::WINDOW_SIZE, combat::WINDOWS_COUNT},
            {"WINDOW_LUNGE_PECK_0", combat::WINDOW_LUNGE_PECK_0_OFF, combat::WINDOWS_OFF, combat::WINDOW_LUNGE_PECK_0, combat::WINDOW_SIZE, combat::WINDOWS_COUNT},
            {"WINDOW_LUNGE_LEAP_0", combat::WINDOW_LUNGE_LEAP_0_OFF, combat::WINDOWS_OFF, combat::WINDOW_LUNGE_LEAP_0, combat::WINDOW_SIZE, combat::WINDOWS_COUNT},
            {"WINDOW_LUNGE_WING_BEAT_0", combat::WINDOW_LUNGE_WING_BEAT_0_OFF, combat::WINDOWS_OFF, combat::WINDOW_LUNGE_WING_BEAT_0, combat::WINDOW_SIZE, combat::WINDOWS_COUNT},
            {"WINDOW_RAVAGER_BITE_0", combat::WINDOW_RAVAGER_BITE_0_OFF, combat::WINDOWS_OFF, combat::WINDOW_RAVAGER_BITE_0, combat::WINDOW_SIZE, combat::WINDOWS_COUNT},
            {"WINDOW_RAVAGER_TAIL_SWEEP_0", combat::WINDOW_RAVAGER_TAIL_SWEEP_0_OFF, combat::WINDOWS_OFF, combat::WINDOW_RAVAGER_TAIL_SWEEP_0, combat::WINDOW_SIZE, combat::WINDOWS_COUNT},
            {"WINDOW_RAVAGER_TAIL_SWEEP_1", combat::WINDOW_RAVAGER_TAIL_SWEEP_1_OFF, combat::WINDOWS_OFF, combat::WINDOW_RAVAGER_TAIL_SWEEP_1, combat::WINDOW_SIZE, combat::WINDOWS_COUNT},
            {"WINDOW_SWEEP_STOMP_0", combat::WINDOW_SWEEP_STOMP_0_OFF, combat::WINDOWS_OFF, combat::WINDOW_SWEEP_STOMP_0, combat::WINDOW_SIZE, combat::WINDOWS_COUNT},
            {"WINDOW_SWEEP_GORE_0", combat::WINDOW_SWEEP_GORE_0_OFF, combat::WINDOWS_OFF, combat::WINDOW_SWEEP_GORE_0, combat::WINDOW_SIZE, combat::WINDOWS_COUNT},
            {"WINDOW_SWEEP_GORE_1", combat::WINDOW_SWEEP_GORE_1_OFF, combat::WINDOWS_OFF, combat::WINDOW_SWEEP_GORE_1, combat::WINDOW_SIZE, combat::WINDOWS_COUNT},
            {"WINDOW_SWEEP_REAR_KICK_0", combat::WINDOW_SWEEP_REAR_KICK_0_OFF, combat::WINDOWS_OFF, combat::WINDOW_SWEEP_REAR_KICK_0, combat::WINDOW_SIZE, combat::WINDOWS_COUNT},
            {"PATTERN_HEAVY_P_SPIN", combat::PATTERN_HEAVY_P_SPIN_OFF, combat::PATTERNS_OFF, combat::PATTERN_HEAVY_P_SPIN, combat::PATTERN_SIZE, combat::PATTERNS_COUNT},
            {"PATTERN_HEAVY_P_BITE", combat::PATTERN_HEAVY_P_BITE_OFF, combat::PATTERNS_OFF, combat::PATTERN_HEAVY_P_BITE, combat::PATTERN_SIZE, combat::PATTERNS_COUNT},
            {"PATTERN_HEAVY_P_TAIL_SLAM", combat::PATTERN_HEAVY_P_TAIL_SLAM_OFF, combat::PATTERNS_OFF, combat::PATTERN_HEAVY_P_TAIL_SLAM, combat::PATTERN_SIZE, combat::PATTERNS_COUNT},
            {"PATTERN_HEAVY_P_BITE_SPIN", combat::PATTERN_HEAVY_P_BITE_SPIN_OFF, combat::PATTERNS_OFF, combat::PATTERN_HEAVY_P_BITE_SPIN, combat::PATTERN_SIZE, combat::PATTERNS_COUNT},
            {"PATTERN_LUNGE_P_PECK", combat::PATTERN_LUNGE_P_PECK_OFF, combat::PATTERNS_OFF, combat::PATTERN_LUNGE_P_PECK, combat::PATTERN_SIZE, combat::PATTERNS_COUNT},
            {"PATTERN_LUNGE_P_LEAP", combat::PATTERN_LUNGE_P_LEAP_OFF, combat::PATTERNS_OFF, combat::PATTERN_LUNGE_P_LEAP, combat::PATTERN_SIZE, combat::PATTERNS_COUNT},
            {"PATTERN_LUNGE_P_FLANK", combat::PATTERN_LUNGE_P_FLANK_OFF, combat::PATTERNS_OFF, combat::PATTERN_LUNGE_P_FLANK, combat::PATTERN_SIZE, combat::PATTERNS_COUNT},
            {"PATTERN_LUNGE_P_LEAP2", combat::PATTERN_LUNGE_P_LEAP2_OFF, combat::PATTERNS_OFF, combat::PATTERN_LUNGE_P_LEAP2, combat::PATTERN_SIZE, combat::PATTERNS_COUNT},
            {"PATTERN_RAVAGER_P_BITE", combat::PATTERN_RAVAGER_P_BITE_OFF, combat::PATTERNS_OFF, combat::PATTERN_RAVAGER_P_BITE, combat::PATTERN_SIZE, combat::PATTERNS_COUNT},
            {"PATTERN_RAVAGER_P_ENRAGED", combat::PATTERN_RAVAGER_P_ENRAGED_OFF, combat::PATTERNS_OFF, combat::PATTERN_RAVAGER_P_ENRAGED, combat::PATTERN_SIZE, combat::PATTERNS_COUNT},
            {"PATTERN_RAVAGER_P_SWEEP", combat::PATTERN_RAVAGER_P_SWEEP_OFF, combat::PATTERNS_OFF, combat::PATTERN_RAVAGER_P_SWEEP, combat::PATTERN_SIZE, combat::PATTERNS_COUNT},
            {"PATTERN_SWEEP_P_REAR_KICK", combat::PATTERN_SWEEP_P_REAR_KICK_OFF, combat::PATTERNS_OFF, combat::PATTERN_SWEEP_P_REAR_KICK, combat::PATTERN_SIZE, combat::PATTERNS_COUNT},
            {"PATTERN_SWEEP_P_GORE2", combat::PATTERN_SWEEP_P_GORE2_OFF, combat::PATTERNS_OFF, combat::PATTERN_SWEEP_P_GORE2, combat::PATTERN_SIZE, combat::PATTERNS_COUNT},
            {"PATTERN_SWEEP_P_STOMP", combat::PATTERN_SWEEP_P_STOMP_OFF, combat::PATTERNS_OFF, combat::PATTERN_SWEEP_P_STOMP, combat::PATTERN_SIZE, combat::PATTERNS_COUNT},
            {"PATTERN_SWEEP_P_GORE", combat::PATTERN_SWEEP_P_GORE_OFF, combat::PATTERNS_OFF, combat::PATTERN_SWEEP_P_GORE, combat::PATTERN_SIZE, combat::PATTERNS_COUNT},
            {"GUARD_HEAVY_P_SPIN", combat::GUARD_HEAVY_P_SPIN_OFF, combat::GUARDS_OFF, combat::GUARD_HEAVY_P_SPIN, combat::GUARD_SIZE, combat::GUARDS_COUNT},
            {"GUARD_HEAVY_P_BITE", combat::GUARD_HEAVY_P_BITE_OFF, combat::GUARDS_OFF, combat::GUARD_HEAVY_P_BITE, combat::GUARD_SIZE, combat::GUARDS_COUNT},
            {"GUARD_HEAVY_P_TAIL_SLAM", combat::GUARD_HEAVY_P_TAIL_SLAM_OFF, combat::GUARDS_OFF, combat::GUARD_HEAVY_P_TAIL_SLAM, combat::GUARD_SIZE, combat::GUARDS_COUNT},
            {"GUARD_HEAVY_P_BITE_SPIN", combat::GUARD_HEAVY_P_BITE_SPIN_OFF, combat::GUARDS_OFF, combat::GUARD_HEAVY_P_BITE_SPIN, combat::GUARD_SIZE, combat::GUARDS_COUNT},
            {"GUARD_LUNGE_P_PECK", combat::GUARD_LUNGE_P_PECK_OFF, combat::GUARDS_OFF, combat::GUARD_LUNGE_P_PECK, combat::GUARD_SIZE, combat::GUARDS_COUNT},
            {"GUARD_LUNGE_P_LEAP", combat::GUARD_LUNGE_P_LEAP_OFF, combat::GUARDS_OFF, combat::GUARD_LUNGE_P_LEAP, combat::GUARD_SIZE, combat::GUARDS_COUNT},
            {"GUARD_LUNGE_P_FLANK", combat::GUARD_LUNGE_P_FLANK_OFF, combat::GUARDS_OFF, combat::GUARD_LUNGE_P_FLANK, combat::GUARD_SIZE, combat::GUARDS_COUNT},
            {"GUARD_LUNGE_P_LEAP2", combat::GUARD_LUNGE_P_LEAP2_OFF, combat::GUARDS_OFF, combat::GUARD_LUNGE_P_LEAP2, combat::GUARD_SIZE, combat::GUARDS_COUNT},
            {"GUARD_RAVAGER_P_BITE", combat::GUARD_RAVAGER_P_BITE_OFF, combat::GUARDS_OFF, combat::GUARD_RAVAGER_P_BITE, combat::GUARD_SIZE, combat::GUARDS_COUNT},
            {"GUARD_RAVAGER_P_ENRAGED", combat::GUARD_RAVAGER_P_ENRAGED_OFF, combat::GUARDS_OFF, combat::GUARD_RAVAGER_P_ENRAGED, combat::GUARD_SIZE, combat::GUARDS_COUNT},
            {"GUARD_RAVAGER_P_SWEEP", combat::GUARD_RAVAGER_P_SWEEP_OFF, combat::GUARDS_OFF, combat::GUARD_RAVAGER_P_SWEEP, combat::GUARD_SIZE, combat::GUARDS_COUNT},
            {"GUARD_SWEEP_P_REAR_KICK", combat::GUARD_SWEEP_P_REAR_KICK_OFF, combat::GUARDS_OFF, combat::GUARD_SWEEP_P_REAR_KICK, combat::GUARD_SIZE, combat::GUARDS_COUNT},
            {"GUARD_SWEEP_P_GORE2", combat::GUARD_SWEEP_P_GORE2_OFF, combat::GUARDS_OFF, combat::GUARD_SWEEP_P_GORE2, combat::GUARD_SIZE, combat::GUARDS_COUNT},
            {"GUARD_SWEEP_P_STOMP", combat::GUARD_SWEEP_P_STOMP_OFF, combat::GUARDS_OFF, combat::GUARD_SWEEP_P_STOMP, combat::GUARD_SIZE, combat::GUARDS_COUNT},
            {"GUARD_SWEEP_P_GORE", combat::GUARD_SWEEP_P_GORE_OFF, combat::GUARDS_OFF, combat::GUARD_SWEEP_P_GORE, combat::GUARD_SIZE, combat::GUARDS_COUNT},
            {"STEP_HEAVY_P_SPIN_0", combat::STEP_HEAVY_P_SPIN_0_OFF, combat::STEPS_OFF, combat::STEP_HEAVY_P_SPIN_0, combat::STEP_SIZE, combat::STEPS_COUNT},
            {"STEP_HEAVY_P_BITE_0", combat::STEP_HEAVY_P_BITE_0_OFF, combat::STEPS_OFF, combat::STEP_HEAVY_P_BITE_0, combat::STEP_SIZE, combat::STEPS_COUNT},
            {"STEP_HEAVY_P_TAIL_SLAM_0", combat::STEP_HEAVY_P_TAIL_SLAM_0_OFF, combat::STEPS_OFF, combat::STEP_HEAVY_P_TAIL_SLAM_0, combat::STEP_SIZE, combat::STEPS_COUNT},
            {"STEP_HEAVY_P_BITE_SPIN_0", combat::STEP_HEAVY_P_BITE_SPIN_0_OFF, combat::STEPS_OFF, combat::STEP_HEAVY_P_BITE_SPIN_0, combat::STEP_SIZE, combat::STEPS_COUNT},
            {"STEP_HEAVY_P_BITE_SPIN_1", combat::STEP_HEAVY_P_BITE_SPIN_1_OFF, combat::STEPS_OFF, combat::STEP_HEAVY_P_BITE_SPIN_1, combat::STEP_SIZE, combat::STEPS_COUNT},
            {"STEP_HEAVY_P_BITE_SPIN_2", combat::STEP_HEAVY_P_BITE_SPIN_2_OFF, combat::STEPS_OFF, combat::STEP_HEAVY_P_BITE_SPIN_2, combat::STEP_SIZE, combat::STEPS_COUNT},
            {"STEP_LUNGE_P_PECK_0", combat::STEP_LUNGE_P_PECK_0_OFF, combat::STEPS_OFF, combat::STEP_LUNGE_P_PECK_0, combat::STEP_SIZE, combat::STEPS_COUNT},
            {"STEP_LUNGE_P_LEAP_0", combat::STEP_LUNGE_P_LEAP_0_OFF, combat::STEPS_OFF, combat::STEP_LUNGE_P_LEAP_0, combat::STEP_SIZE, combat::STEPS_COUNT},
            {"STEP_LUNGE_P_FLANK_0", combat::STEP_LUNGE_P_FLANK_0_OFF, combat::STEPS_OFF, combat::STEP_LUNGE_P_FLANK_0, combat::STEP_SIZE, combat::STEPS_COUNT},
            {"STEP_LUNGE_P_LEAP2_0", combat::STEP_LUNGE_P_LEAP2_0_OFF, combat::STEPS_OFF, combat::STEP_LUNGE_P_LEAP2_0, combat::STEP_SIZE, combat::STEPS_COUNT},
            {"STEP_LUNGE_P_LEAP2_1", combat::STEP_LUNGE_P_LEAP2_1_OFF, combat::STEPS_OFF, combat::STEP_LUNGE_P_LEAP2_1, combat::STEP_SIZE, combat::STEPS_COUNT},
            {"STEP_RAVAGER_P_BITE_0", combat::STEP_RAVAGER_P_BITE_0_OFF, combat::STEPS_OFF, combat::STEP_RAVAGER_P_BITE_0, combat::STEP_SIZE, combat::STEPS_COUNT},
            {"STEP_RAVAGER_P_ENRAGED_0", combat::STEP_RAVAGER_P_ENRAGED_0_OFF, combat::STEPS_OFF, combat::STEP_RAVAGER_P_ENRAGED_0, combat::STEP_SIZE, combat::STEPS_COUNT},
            {"STEP_RAVAGER_P_SWEEP_0", combat::STEP_RAVAGER_P_SWEEP_0_OFF, combat::STEPS_OFF, combat::STEP_RAVAGER_P_SWEEP_0, combat::STEP_SIZE, combat::STEPS_COUNT},
            {"STEP_SWEEP_P_REAR_KICK_0", combat::STEP_SWEEP_P_REAR_KICK_0_OFF, combat::STEPS_OFF, combat::STEP_SWEEP_P_REAR_KICK_0, combat::STEP_SIZE, combat::STEPS_COUNT},
            {"STEP_SWEEP_P_GORE2_0", combat::STEP_SWEEP_P_GORE2_0_OFF, combat::STEPS_OFF, combat::STEP_SWEEP_P_GORE2_0, combat::STEP_SIZE, combat::STEPS_COUNT},
            {"STEP_SWEEP_P_GORE2_1", combat::STEP_SWEEP_P_GORE2_1_OFF, combat::STEPS_OFF, combat::STEP_SWEEP_P_GORE2_1, combat::STEP_SIZE, combat::STEPS_COUNT},
            {"STEP_SWEEP_P_STOMP_0", combat::STEP_SWEEP_P_STOMP_0_OFF, combat::STEPS_OFF, combat::STEP_SWEEP_P_STOMP_0, combat::STEP_SIZE, combat::STEPS_COUNT},
            {"STEP_SWEEP_P_GORE_0", combat::STEP_SWEEP_P_GORE_0_OFF, combat::STEPS_OFF, combat::STEP_SWEEP_P_GORE_0, combat::STEP_SIZE, combat::STEPS_COUNT},
        };
        for (const MetaRecord &r : records) {
            t.assert(r.off, static_cast<int>(r.sectionOff) + r.idx * r.size, std::string("offset ") + r.name);
            t.assert((r.off - r.sectionOff) % r.size, 0, std::string("align ") + r.name);
            t.assertLessThan(r.off, static_cast<int>(r.sectionOff) + r.count * r.size, std::string("inside ") + r.name);
        }
        suite.addTest(t);
    }

    {
        Test t("packed records decode to the generated host structs");
        for (uint8_t i = 0; i < combat::CREATURES_COUNT; i++) {
            const size_t o = static_cast<size_t>(combat::CREATURES_OFF) + i * combat::CREATURE_SIZE;
            const combat_data::Creature &h = combat_data::CREATURES[i];
            t.assert(b8(blob, o + 0), h.skeletonIdx, "blob creature skeletonIdx");
            t.assert(b8(blob, o + 1), h.profileIdx, "blob creature profileIdx");
            t.assert(b8(blob, o + 2), h.headZone, "blob creature headZone");
            t.assert(b8(blob, o + 3), h.appendZone, "blob creature appendZone");
            t.assert(b8(blob, o + 4), h.firstAttack, "blob creature firstAttack");
            t.assert(b8(blob, o + 5), h.attackCount, "blob creature attackCount");
            t.assert(b8(blob, o + 6), h.firstPattern, "blob creature firstPattern");
            t.assert(b8(blob, o + 7), h.patternCount, "blob creature patternCount");
            t.assert(b8(blob, o + 8), h.w, "blob creature w");
            t.assert(b8(blob, o + 9), h.h, "blob creature h");
            t.assert(b8(blob, o + 10), h.spd, "blob creature spd");
            t.assert(bi8(blob, o + 11), h.collide.ox, "blob creature collide.ox");
            t.assert(bi8(blob, o + 12), h.collide.oy, "blob creature collide.oy");
            t.assert(b8(blob, o + 13), h.collide.w, "blob creature collide.w");
            t.assert(b8(blob, o + 14), h.collide.h, "blob creature collide.h");
            t.assert(b16(blob, o + 15), h.hp, "blob creature hp");
            t.assert(b16(blob, o + 17), h.spawnX, "blob creature spawnX");
            t.assert(b16(blob, o + 19), h.spawnY, "blob creature spawnY");
            t.assert(b8(blob, o + 21), h.flags, "blob creature flags");
            t.assert(b8(blob, o + 22), h.sheet, "blob creature sheet");
            t.assert(b8(blob, o + 23), h.brokenW, "blob creature brokenW");
            t.assert(b8(blob, o + 24), h.brokenH, "blob creature brokenH");
            t.assert(b8(blob, o + 25), h.enrageHpPct, "blob creature enrageHpPct");
            t.assert(b8(blob, o + 26), h.enrageSpdMul, "blob creature enrageSpdMul");
            t.assert(b8(blob, o + 27), h.enrageFaceHold, "blob creature enrageFaceHold");
            t.assert(b8(blob, o + 28), h.enrageCue, "blob creature enrageCue");
        }
        for (uint8_t i = 0; i < combat::PROFILES_COUNT; i++) {
            const size_t o = static_cast<size_t>(combat::PROFILES_OFF) + i * combat::PROFILE_SIZE;
            const combat_data::Profile &h = combat_data::PROFILES[i];
            t.assert(b8(blob, o + 0), h.engageDist, "blob profile engageDist");
            t.assert(b8(blob, o + 1), h.keepDist, "blob profile keepDist");
            t.assert(b8(blob, o + 2), h.attackDist, "blob profile attackDist");
            t.assert(b8(blob, o + 3), h.circleNum, "blob profile circleNum");
            t.assert(b8(blob, o + 4), h.circleDen, "blob profile circleDen");
            t.assert(b8(blob, o + 5), h.retreatNum, "blob profile retreatNum");
            t.assert(b8(blob, o + 6), h.retreatDen, "blob profile retreatDen");
            t.assert(b8(blob, o + 7), h.staggerMax, "blob profile staggerMax");
            t.assert(b8(blob, o + 8), h.staggerDecay, "blob profile staggerDecay");
            t.assert(b8(blob, o + 9), h.zoneFlags, "blob profile zoneFlags");
            t.assert(b8(blob, o + 10), h.faceHold, "blob profile faceHold");
            t.assert(b16(blob, o + 11), h.cdBase, "blob profile cdBase");
            t.assert(b16(blob, o + 13), h.cdJitter, "blob profile cdJitter");
            t.assert(b16(blob, o + 15), h.spawnT, "blob profile spawnT");
            t.assert(b16(blob, o + 17), h.spawnCd, "blob profile spawnCd");
            t.assert(b16(blob, o + 19), h.stunRecoverT, "blob profile stunRecoverT");
            t.assert(b16(blob, o + 21), h.staggerRecoverT, "blob profile staggerRecoverT");
        }
        for (uint8_t i = 0; i < combat::SKELETONS_COUNT; i++) {
            const size_t o = static_cast<size_t>(combat::SKELETONS_OFF) + i * combat::SKELETON_SIZE;
            const combat_data::Skeleton &h = combat_data::SKELETONS[i];
            t.assert(b8(blob, o + 0), h.firstAnchor, "blob skeleton firstAnchor");
            t.assert(b8(blob, o + 1), h.anchorCount, "blob skeleton anchorCount");
        }
        for (uint8_t i = 0; i < combat::ZONES_COUNT; i++) {
            const size_t o = static_cast<size_t>(combat::ZONES_OFF) + i * combat::ZONE_SIZE;
            const combat_data::Zone &h = combat_data::ZONES[i];
            t.assert(bi8(blob, o + 0), h.box.ox, "blob zone box.ox");
            t.assert(bi8(blob, o + 1), h.box.oy, "blob zone box.oy");
            t.assert(b8(blob, o + 2), h.box.w, "blob zone box.w");
            t.assert(b8(blob, o + 3), h.box.h, "blob zone box.h");
            t.assert(b8(blob, o + 4), h.hp, "blob zone hp");
            t.assert(b8(blob, o + 5), h.dmgMul, "blob zone dmgMul");
            t.assert(b8(blob, o + 6), h.bodyShare, "blob zone bodyShare");
            t.assert(b8(blob, o + 7), h.breakTypes, "blob zone breakTypes");
            t.assert(b8(blob, o + 8), h.staggerOnHit, "blob zone staggerOnHit");
            t.assert(b8(blob, o + 9), h.brokenDmgMul, "blob zone brokenDmgMul");
            t.assert(b8(blob, o + 10), h.brokenFlags, "blob zone brokenFlags");
            t.assert(b8(blob, o + 11), h.unlockMaskLo, "blob zone unlockMaskLo");
            t.assert(b8(blob, o + 12), h.unlockMaskHi, "blob zone unlockMaskHi");
        }
        for (uint8_t i = 0; i < combat::ATTACKS_COUNT; i++) {
            const size_t o = static_cast<size_t>(combat::ATTACKS_OFF) + i * combat::ATTACK_SIZE;
            const combat_data::Attack &h = combat_data::ATTACKS[i];
            t.assert(b8(blob, o + 0), h.moveType, "blob attack moveType");
            t.assert(b8(blob, o + 1), h.moveSpeedF, "blob attack moveSpeedF");
            t.assert(bi8(blob, o + 2), h.moveDx, "blob attack moveDx");
            t.assert(bi8(blob, o + 3), h.moveDy, "blob attack moveDy");
            t.assert(b8(blob, o + 4), h.facing, "blob attack facing");
            t.assert(b8(blob, o + 5), h.phys, "blob attack phys");
            t.assert(b8(blob, o + 6), h.elem, "blob attack elem");
            t.assert(b8(blob, o + 7), h.onHitEffect, "blob attack onHitEffect");
            t.assert(bi8(blob, o + 8), h.onHitPush, "blob attack onHitPush");
            t.assert(b8(blob, o + 9), h.onHitStun, "blob attack onHitStun");
            t.assert(b8(blob, o + 10), h.stagger, "blob attack stagger");
            t.assert(b8(blob, o + 11), h.cue, "blob attack cue");
            t.assert(b8(blob, o + 12), h.wallStun, "blob attack wallStun");
            t.assert(b8(blob, o + 13), h.firstWindow, "blob attack firstWindow");
            t.assert(b8(blob, o + 14), h.windowCount, "blob attack windowCount");
            t.assert(b16(blob, o + 15), h.windup, "blob attack windup");
            t.assert(b16(blob, o + 17), h.active, "blob attack active");
            t.assert(b16(blob, o + 19), h.recover, "blob attack recover");
            t.assert(b16(blob, o + 21), h.dmg, "blob attack dmg");
            t.assert(b8(blob, o + 23), h.tell, "blob attack tell");
        }
        for (uint8_t i = 0; i < combat::WINDOWS_COUNT; i++) {
            const size_t o = static_cast<size_t>(combat::WINDOWS_OFF) + i * combat::WINDOW_SIZE;
            const combat_data::Window &h = combat_data::WINDOWS[i];
            t.assert(b16(blob, o + 0), h.t0, "blob window t0");
            t.assert(b16(blob, o + 2), h.t1, "blob window t1");
            t.assert(bi8(blob, o + 4), h.box.ox, "blob window box.ox");
            t.assert(bi8(blob, o + 5), h.box.oy, "blob window box.oy");
            t.assert(b8(blob, o + 6), h.box.w, "blob window box.w");
            t.assert(b8(blob, o + 7), h.box.h, "blob window box.h");
            t.assert(b8(blob, o + 8), h.dmgMul, "blob window dmgMul");
            t.assert(b8(blob, o + 9), 0, "blob window reserved flags");
        }
        for (uint8_t i = 0; i < combat::PATTERNS_COUNT; i++) {
            const size_t o = static_cast<size_t>(combat::PATTERNS_OFF) + i * combat::PATTERN_SIZE;
            const combat_data::Pattern &h = combat_data::PATTERNS[i];
            t.assert(b8(blob, o + 0), h.firstStep, "blob pattern firstStep");
            t.assert(b8(blob, o + 1), h.stepCount, "blob pattern stepCount");
            t.assert(b8(blob, o + 2), h.guardIdx, "blob pattern guardIdx");
        }
        for (uint8_t i = 0; i < combat::GUARDS_COUNT; i++) {
            const size_t o = static_cast<size_t>(combat::GUARDS_OFF) + i * combat::GUARD_SIZE;
            const combat_data::Guard &h = combat_data::GUARDS[i];
            t.assert(b8(blob, o + 0), h.minDist, "blob guard minDist");
            t.assert(b8(blob, o + 1), h.maxDist, "blob guard maxDist");
            t.assert(b8(blob, o + 2), h.hpLo, "blob guard hpLo");
            t.assert(b8(blob, o + 3), h.hpHi, "blob guard hpHi");
            t.assert(b8(blob, o + 4), h.playerFlags, "blob guard playerFlags");
            t.assert(b8(blob, o + 5), h.cooldown, "blob guard cooldown");
            t.assert(b8(blob, o + 6), h.chance, "blob guard chance");
            t.assert(b8(blob, o + 7), h.zonesBroken, "blob guard zonesBroken");
            t.assert(b8(blob, o + 8), h.facing, "blob guard facing");
        }
        for (uint8_t i = 0; i < combat::STEPS_COUNT; i++) {
            const size_t o = static_cast<size_t>(combat::STEPS_OFF) + i * combat::STEP_SIZE;
            const combat_data::Step &h = combat_data::STEPS[i];
            t.assert(b8(blob, o + 0), h.kind, "blob step kind");
            t.assert(b8(blob, o + 1), h.ref, "blob step ref");
            t.assert(b8(blob, o + 2), h.after, "blob step after");
            t.assert(b8(blob, o + 3), h.chance, "blob step chance");
        }
        for (uint8_t i = 0; i < combat::ANCHORS_COUNT; i++) {
            const size_t o = static_cast<size_t>(combat::ANCHORS_OFF) + i * combat::ANCHOR_SIZE;
            t.assert(bi8(blob, o + 0), combat_data::ANCHORS[i].ox, "blob anchor ox");
            t.assert(bi8(blob, o + 1), combat_data::ANCHORS[i].oy, "blob anchor oy");
        }
        suite.addTest(t);
    }

    {
        Test t("blob spot values match combat_expect.hpp");
        t.assert(b16(blob, static_cast<size_t>(combat::CREATURE_HEAVY_OFF) + 15), combat_expect::CREATURE_HEAVY_HP, "expect heavy hp");
        t.assert(b8(blob, static_cast<size_t>(combat::CREATURE_HEAVY_OFF) + 10), combat_expect::CREATURE_HEAVY_SPD, "expect heavy spd");
        t.assert(b8(blob, static_cast<size_t>(combat::CREATURE_HEAVY_OFF) + 8), combat_expect::CREATURE_HEAVY_W, "expect heavy w");
        t.assert(b8(blob, static_cast<size_t>(combat::CREATURE_HEAVY_OFF) + 9), combat_expect::CREATURE_HEAVY_H, "expect heavy h");
        t.assert(b16(blob, static_cast<size_t>(combat::CREATURE_LUNGE_OFF) + 15), combat_expect::CREATURE_LUNGE_HP, "expect lunge hp");
        t.assert(b8(blob, static_cast<size_t>(combat::CREATURE_LUNGE_OFF) + 10), combat_expect::CREATURE_LUNGE_SPD, "expect lunge spd");
        t.assert(b16(blob, static_cast<size_t>(combat::CREATURE_SWEEP_OFF) + 15), combat_expect::CREATURE_SWEEP_HP, "expect sweep hp");
        t.assert(b8(blob, static_cast<size_t>(combat::CREATURE_SWEEP_OFF) + 10), combat_expect::CREATURE_SWEEP_SPD, "expect sweep spd");
        // feel.6: the enrage quad is the creature record's last four bytes and is
        // disabled (halves 0) on every shipped creature. The decode loop above
        // pins it against the host struct; no expect constants exist while no
        // data authors the phase.
        t.assert(b8(blob, static_cast<size_t>(combat::CREATURE_LUNGE_OFF) + 25), 0, "shipped lunge enrage hpPct 0");
        t.assert(b8(blob, static_cast<size_t>(combat::CREATURE_LUNGE_OFF) + 26), 0, "shipped lunge enrage spdMul 0");
        t.assert(b8(blob, static_cast<size_t>(combat::CREATURE_LUNGE_OFF) + 27), 0, "shipped lunge enrage faceHold 0");
        t.assert(b8(blob, static_cast<size_t>(combat::CREATURE_LUNGE_OFF) + 28), 0, "shipped lunge enrage cue 0");
        t.assert(b8(blob, static_cast<size_t>(combat::ZONE_RAVAGER_HEAD_OFF) + 4), combat_expect::ZONE_RAVAGER_HEAD_HP, "expect head hp");
        t.assert(b8(blob, static_cast<size_t>(combat::ZONE_RAVAGER_HEAD_OFF) + 5), combat_expect::ZONE_RAVAGER_HEAD_DMG_MUL, "expect head dmgMul");
        t.assert(b8(blob, static_cast<size_t>(combat::ZONE_RAVAGER_APPENDAGE_OFF) + 4), combat_expect::ZONE_RAVAGER_APPENDAGE_HP, "expect tail hp");
        t.assert(b8(blob, static_cast<size_t>(combat::ZONE_RAVAGER_APPENDAGE_OFF) + 5), combat_expect::ZONE_RAVAGER_APPENDAGE_DMG_MUL, "expect tail dmgMul");
        t.assert(b16(blob, static_cast<size_t>(combat::ATTACK_HEAVY_BITE_OFF) + 15), combat_expect::ATTACK_HEAVY_BITE_WINDUP, "expect heavy bite windup");
        t.assert(b16(blob, static_cast<size_t>(combat::ATTACK_HEAVY_BITE_OFF) + 17), combat_expect::ATTACK_HEAVY_BITE_ACTIVE, "expect heavy bite active");
        t.assert(b16(blob, static_cast<size_t>(combat::ATTACK_HEAVY_BITE_OFF) + 19), combat_expect::ATTACK_HEAVY_BITE_RECOVER, "expect heavy bite recover");
        t.assert(b16(blob, static_cast<size_t>(combat::ATTACK_HEAVY_BITE_OFF) + 21), combat_expect::ATTACK_HEAVY_BITE_DMG, "expect heavy bite dmg");
        t.assert(b8(blob, static_cast<size_t>(combat::ATTACK_HEAVY_BITE_OFF) + 12), combat_expect::ATTACK_HEAVY_BITE_WALLSTUN, "expect heavy bite wallStun");
        t.assert(b8(blob, static_cast<size_t>(combat::ATTACK_HEAVY_BITE_OFF) + 23), combat_expect::ATTACK_HEAVY_BITE_TELL, "expect heavy bite tell");
        // feel.10: bite TELL is now LINE (1); tail_spin retimes to 34/18/62 with
        // ARC (2); tail_slam is the hop pounce with RING (3) and signed dx -56.
        t.assert(b8(blob, static_cast<size_t>(combat::ATTACK_HEAVY_BITE_OFF) + 23), 1, "expect heavy bite line tell");
        t.assert(b16(blob, static_cast<size_t>(combat::ATTACK_HEAVY_TAIL_SPIN_OFF) + 15), 34, "expect heavy spin windup");
        t.assert(b16(blob, static_cast<size_t>(combat::ATTACK_HEAVY_TAIL_SPIN_OFF) + 17), 18, "expect heavy spin active");
        t.assert(b16(blob, static_cast<size_t>(combat::ATTACK_HEAVY_TAIL_SPIN_OFF) + 19), 62, "expect heavy spin recover");
        t.assert(b8(blob, static_cast<size_t>(combat::ATTACK_HEAVY_TAIL_SPIN_OFF) + 23), 2, "expect heavy spin arc tell");
        t.assert(b8(blob, static_cast<size_t>(combat::ATTACK_HEAVY_TAIL_SLAM_OFF) + 0), 3, "expect tail_slam hop moveType");
        t.assert(bi8(blob, static_cast<size_t>(combat::ATTACK_HEAVY_TAIL_SLAM_OFF) + 2), -56, "expect tail_slam hop dx");
        t.assert(bi8(blob, static_cast<size_t>(combat::ATTACK_HEAVY_TAIL_SLAM_OFF) + 3), 0, "expect tail_slam hop dy");
        t.assert(b8(blob, static_cast<size_t>(combat::ATTACK_HEAVY_TAIL_SLAM_OFF) + 4), COMBAT_FACING_LOCK, "expect tail_slam lock-at-windup");
        t.assert(b16(blob, static_cast<size_t>(combat::ATTACK_HEAVY_TAIL_SLAM_OFF) + 15), 26, "expect tail_slam windup");
        t.assert(b16(blob, static_cast<size_t>(combat::ATTACK_HEAVY_TAIL_SLAM_OFF) + 17), 8, "expect tail_slam active");
        t.assert(b16(blob, static_cast<size_t>(combat::ATTACK_HEAVY_TAIL_SLAM_OFF) + 19), 44, "expect tail_slam recover");
        t.assert(b16(blob, static_cast<size_t>(combat::ATTACK_HEAVY_TAIL_SLAM_OFF) + 21), 12, "expect tail_slam dmg");
        t.assert(b8(blob, static_cast<size_t>(combat::ATTACK_HEAVY_TAIL_SLAM_OFF) + 23), 3, "expect tail_slam ring tell");
        t.assert(b16(blob, static_cast<size_t>(combat::ATTACK_LUNGE_PECK_OFF) + 15), combat_expect::ATTACK_LUNGE_PECK_WINDUP, "expect lunge windup");
        t.assert(b16(blob, static_cast<size_t>(combat::ATTACK_LUNGE_PECK_OFF) + 17), combat_expect::ATTACK_LUNGE_PECK_ACTIVE, "expect lunge active");
        t.assert(b16(blob, static_cast<size_t>(combat::ATTACK_LUNGE_PECK_OFF) + 19), combat_expect::ATTACK_LUNGE_PECK_RECOVER, "expect lunge recover");
        t.assert(b16(blob, static_cast<size_t>(combat::ATTACK_LUNGE_PECK_OFF) + 21), combat_expect::ATTACK_LUNGE_PECK_DMG, "expect lunge dmg");
        t.assert(b8(blob, static_cast<size_t>(combat::ATTACK_LUNGE_PECK_OFF) + 12), combat_expect::ATTACK_LUNGE_PECK_WALLSTUN, "expect lunge wallStun");
        // feel.10: p_tail_slam is now the heavy's first pattern (behind, 20..60);
        // p_bite_spin (<=20), p_spin (21..30) and p_bite (31..255) are no longer
        // first, so their reaches are pinned literally.
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_HEAVY_P_TAIL_SLAM_OFF) + 0), combat_expect::PATTERN_HEAVY_P_TAIL_SLAM_MIN_DIST, "expect heavy tail_slam minDist");
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_HEAVY_P_TAIL_SLAM_OFF) + 1), combat_expect::PATTERN_HEAVY_P_TAIL_SLAM_MAX_DIST, "expect heavy tail_slam maxDist");
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_HEAVY_P_TAIL_SLAM_OFF) + 8), GUARD_FACING_BEHIND, "expect heavy tail_slam behind facing");
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_HEAVY_P_BITE_SPIN_OFF) + 0), 0, "expect heavy bite_spin minDist");
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_HEAVY_P_BITE_SPIN_OFF) + 1), 20, "expect heavy bite_spin maxDist");
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_HEAVY_P_SPIN_OFF) + 0), 21, "expect heavy spin minDist");
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_HEAVY_P_SPIN_OFF) + 1), 30, "expect heavy spin maxDist");
        // p_bite is the fourth heavy pattern: minDist 31 .. maxDist 255.
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_HEAVY_P_BITE_OFF) + 0), 31, "expect heavy bite minDist");
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_HEAVY_P_BITE_OFF) + 1), 255, "expect heavy bite maxDist");
        // feel.8: p_flank is the chicken's first pattern (behind guard, <=26).
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_LUNGE_P_FLANK_OFF) + 0), combat_expect::PATTERN_LUNGE_P_FLANK_MIN_DIST, "expect lunge flank minDist");
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_LUNGE_P_FLANK_OFF) + 1), combat_expect::PATTERN_LUNGE_P_FLANK_MAX_DIST, "expect lunge flank maxDist");
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_LUNGE_P_FLANK_OFF) + 8), GUARD_FACING_BEHIND, "expect lunge flank behind facing");
        // feel.9: p_rear_kick is now the bull's first pattern (behind, <=24); the
        // p_stomp guard is no longer the first, so its reach is pinned literally.
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_SWEEP_P_REAR_KICK_OFF) + 0), combat_expect::PATTERN_SWEEP_P_REAR_KICK_MIN_DIST, "expect sweep rear_kick minDist");
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_SWEEP_P_REAR_KICK_OFF) + 1), combat_expect::PATTERN_SWEEP_P_REAR_KICK_MAX_DIST, "expect sweep rear_kick maxDist");
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_SWEEP_P_REAR_KICK_OFF) + 8), GUARD_FACING_BEHIND, "expect sweep rear_kick behind facing");
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_SWEEP_P_STOMP_OFF) + 0), 0, "expect sweep stomp minDist");
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_SWEEP_P_STOMP_OFF) + 1), 24, "expect sweep stomp maxDist");
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_SWEEP_P_GORE2_OFF) + 2), 0, "expect sweep gore2 hpLo");
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_SWEEP_P_GORE2_OFF) + 3), 40, "expect sweep gore2 hpHi");
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_SWEEP_P_GORE_OFF) + 2), 41, "expect sweep gore hpLo");
        t.assert(b8(blob, static_cast<size_t>(combat::GUARD_SWEEP_P_GORE_OFF) + 3), 100, "expect sweep gore hpHi");
        // feel.9: stomp tell RING (3), gore tell LINE (1), rear_kick tell ARC (2);
        // gore's wallStun byte 70.
        t.assert(b8(blob, static_cast<size_t>(combat::ATTACK_SWEEP_STOMP_OFF) + 23), combat_expect::ATTACK_SWEEP_STOMP_TELL, "expect stomp ring tell");
        t.assert(b8(blob, static_cast<size_t>(combat::ATTACK_SWEEP_GORE_OFF) + 23), 1, "expect gore line tell");
        t.assert(b8(blob, static_cast<size_t>(combat::ATTACK_SWEEP_GORE_OFF) + 12), 70, "expect gore wallStun");
        t.assert(b8(blob, static_cast<size_t>(combat::ATTACK_SWEEP_REAR_KICK_OFF) + 23), 2, "expect rear_kick arc tell");
        t.assert(b8(blob, static_cast<size_t>(combat::CREATURE_SWEEP_OFF) + 25), combat_expect::CREATURE_SWEEP_ENRAGE_HP_PCT, "expect sweep enrage hpPct");
        t.assert(b8(blob, static_cast<size_t>(combat::CREATURE_SWEEP_OFF) + 26), combat_expect::CREATURE_SWEEP_ENRAGE_SPD_MUL, "expect sweep enrage spdMul");
        t.assert(b8(blob, static_cast<size_t>(combat::CREATURE_SWEEP_OFF) + 27), combat_expect::CREATURE_SWEEP_ENRAGE_FACE_HOLD, "expect sweep enrage faceHold");
        suite.addTest(t);
    }

    {
        // feel.7/feel.10: move.type hop carries a face-relative dx/dy vector in
        // the attack record's signed bytes 2/3. feel.10 makes heavy.tail_slam the
        // first shipped hop (dx -56, dy 0); every other attack keeps the signed
        // zeros so an inserted field cannot silently shift moveDx. ATTACK_SIZE is
        // 24 since feel.5 appended the tell byte (the hop dx/dy offsets 2/3 are
        // unchanged).
        Test t("attack move delta: hop dx/dy decode signed at offset 2/3");
        for (uint8_t i = 0; i < combat::ATTACKS_COUNT; i++) {
            const size_t o = static_cast<size_t>(combat::ATTACKS_OFF) + i * combat::ATTACK_SIZE;
            const combat_data::Attack &h = combat_data::ATTACKS[i];
            t.assert(bi8(blob, o + 2), h.moveDx, "signed moveDx decode");
            t.assert(bi8(blob, o + 3), h.moveDy, "signed moveDy decode");
            if (i == combat_data::ATTACK_HEAVY_TAIL_SLAM) {
                t.assert(h.moveType, 3, "tail_slam is the shipped hop");
                t.assert(h.moveDx, -56, "tail_slam hop dx -56");
                t.assert(h.moveDy, 0, "tail_slam hop dy 0");
            } else {
                t.assert(h.moveDx, 0, "shipped moveDx zero (none/lunge)");
                t.assert(h.moveDy, 0, "shipped moveDy zero (none/lunge)");
            }
        }
        t.assert(combat::ATTACK_SIZE, 24, "ATTACK_SIZE grew for the tell byte");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
