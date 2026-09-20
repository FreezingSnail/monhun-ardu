#pragma once
// On-device item-table suite (bead monhun-ardu-prg.2).
//
// Reads the mhItems blob straight off the cart (items_meta.hpp symbolic
// offsets) and pins the header + the herb/blue-mushroom/material records, then
// drives the shipping src/core/items.hpp readers (itemKind/itemHeal/itemSell)
// and the inventory verbs (itemAdd/itemConsume/itemCount) so a stale blob or a
// host-padded record cannot pass silently.
//
// Reads run here in setup() with no plane blits active, the same window the sim
// uses (between waitForNextPlane and paint), mirroring data_test.

#include "harness/fxtest.hpp"
#include "src/core/world.hpp"
#include "src/generated/items_expect.hpp"

#include <stdint.h>

namespace itemsfx {

using namespace mh;

inline void test_items(FxTest &test) {
    // -------------------------------------------------- blob header + count
    FX::seekData(mhItems);
    const uint8_t magicLo = FX::readPendingUInt8();
    const uint8_t magicHi = FX::readPendingUInt8();
    const uint8_t version = FX::readPendingUInt8();
    const uint8_t flags = FX::readEnd();
    test.expectEq(magicLo, item::MAGIC & 0xFF, F("items magic lo"));
    test.expectEq(magicHi, item::MAGIC >> 8, F("items magic hi"));
    test.expectEq(version, item::VERSION, F("items version"));
    test.expectEq(flags, item::FLAGS, F("items flags"));

    FX::seekData(mhItems + 4);
    const uint8_t count = FX::readEnd();
    test.expectEq(count, item::ITEM_COUNT, F("items count"));
    test.expectEq(count, item_expect::ITEM_COUNT, F("items count pin"));

    // ------------------------------------------------ raw herb record bytes
    FX::seekData(mhItems + item::ITEM_HERB_OFF);
    const uint8_t herbKind = FX::readPendingUInt8();
    const uint8_t herbHeal = FX::readPendingUInt8();
    const uint8_t herbStam = FX::readPendingUInt8();
    const uint16_t herbSell = static_cast<uint16_t>(FX::readPendingUInt8()) | static_cast<uint16_t>(FX::readEnd()) << 8;
    test.expectEq(herbKind, item_expect::ITEM_HERB_KIND, F("herb kind byte"));
    test.expectEq(herbHeal, item_expect::ITEM_HERB_HEAL, F("herb heal byte"));
    test.expectEq(herbStam, item_expect::ITEM_HERB_STAM, F("herb stam byte"));
    test.expectEq(herbSell, item_expect::ITEM_HERB_SELL, F("herb sell byte"));

    // Blue mushroom + a material record (kind byte boundary).
    FX::seekData(mhItems + item::ITEM_BLUE_MUSHROOM_OFF);
    const uint8_t mushroomKind = FX::readPendingUInt8();
    const uint8_t mushroomHeal = FX::readEnd();
    test.expectEq(mushroomKind, item::KIND_CONSUMABLE, F("mushroom kind byte"));
    test.expectEq(mushroomHeal, item_expect::ITEM_BLUE_MUSHROOM_HEAL, F("mushroom heal byte"));
    FX::seekData(mhItems + item::ITEM_ORE_OFF);
    const uint8_t oreKind = FX::readPendingUInt8();
    test.expectEq(oreKind, item::KIND_MATERIAL, F("ore kind byte"));

    // ------------------------------------------- shipping table readers
    test.expectEq(itemKind(ITEM_HERB), item_expect::ITEM_HERB_KIND, F("itemKind herb"));
    test.expectEq(itemHeal(ITEM_HERB), 20, F("itemHeal herb"));
    test.expectEq(itemKind(ITEM_BLUE_MUSHROOM), item::KIND_CONSUMABLE, F("itemKind mushroom"));
    test.expectEq(itemHeal(ITEM_BLUE_MUSHROOM), item_expect::ITEM_BLUE_MUSHROOM_HEAL, F("itemHeal mushroom"));
    test.expectEq(itemKind(ITEM_ORE), item::KIND_MATERIAL, F("itemKind ore"));
    test.expectEq(itemHeal(ITEM_ORE), 0, F("itemHeal ore"));
    test.expectEq(itemSell(ITEM_TAIL), item_expect::ITEM_TAIL_SELL, F("itemSell tail"));
    test.expectEq(itemHeal(200), 0, F("itemHeal out of range"));

    // -------------------------------------------------- inventory verbs
    static Game g;
    newGame(g, W_SWORD, MODE_HUNT);
    test.expectEq(itemCount(g, ITEM_HERB), 0, F("newGame inventory empty"));
    itemAdd(g, ITEM_HERB, 3);
    test.expectEq(itemCount(g, ITEM_HERB), 3, F("itemAdd accumulates"));
    test.expectEq(itemConsume(g, ITEM_HERB), 1, F("itemConsume ok"));
    test.expectEq(itemCount(g, ITEM_HERB), 2, F("itemConsume decrements"));
}

}   // namespace itemsfx
