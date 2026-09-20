#pragma once
// Host unit tests for the item table + inventory (bead monhun-ardu-prg.2):
// src/core/items.hpp (itemRead/itemKind/itemHeal/itemSell via the host/AVR
// shim; itemAdd/itemConsume/itemCount) and the generated items_meta.hpp /
// items_expect.hpp pins (data/items.json -> mhItems).
//
// The generated constants are the source of truth; tests reference the
// item::ITEM_* / ITEM_* ids, never literal record indices. The gather code
// mapping (zone::GATHER_<NAME> == item index + 1) is pinned so the zone blob
// can never point at a slot the inventory does not have.
#include "test.hpp"
#include "../src/core/world.hpp"
#include "../src/generated/items_expect.hpp"

using namespace mh;

namespace itemtest {

// Every shipped item id, in table order, for the table-wide pins.
const uint8_t ITEM_IDS[ITEM_COUNT] = {
    ITEM_HERB, ITEM_BLUE_MUSHROOM, ITEM_ORE, ITEM_BUG, ITEM_SCALE, ITEM_SHELL, ITEM_FANG, ITEM_TAIL,
};

const uint8_t EXPECT_KIND[ITEM_COUNT] = {
    item_expect::ITEM_HERB_KIND,  item_expect::ITEM_BLUE_MUSHROOM_KIND, item_expect::ITEM_ORE_KIND,  item_expect::ITEM_BUG_KIND,
    item_expect::ITEM_SCALE_KIND, item_expect::ITEM_SHELL_KIND,         item_expect::ITEM_FANG_KIND, item_expect::ITEM_TAIL_KIND,
};

const uint8_t EXPECT_HEAL[ITEM_COUNT] = {
    item_expect::ITEM_HERB_HEAL,  item_expect::ITEM_BLUE_MUSHROOM_HEAL, item_expect::ITEM_ORE_HEAL,  item_expect::ITEM_BUG_HEAL,
    item_expect::ITEM_SCALE_HEAL, item_expect::ITEM_SHELL_HEAL,         item_expect::ITEM_FANG_HEAL, item_expect::ITEM_TAIL_HEAL,
};

const uint8_t EXPECT_STAM[ITEM_COUNT] = {
    item_expect::ITEM_HERB_STAM,  item_expect::ITEM_BLUE_MUSHROOM_STAM, item_expect::ITEM_ORE_STAM,  item_expect::ITEM_BUG_STAM,
    item_expect::ITEM_SCALE_STAM, item_expect::ITEM_SHELL_STAM,         item_expect::ITEM_FANG_STAM, item_expect::ITEM_TAIL_STAM,
};

const uint16_t EXPECT_SELL[ITEM_COUNT] = {
    item_expect::ITEM_HERB_SELL,  item_expect::ITEM_BLUE_MUSHROOM_SELL, item_expect::ITEM_ORE_SELL,  item_expect::ITEM_BUG_SELL,
    item_expect::ITEM_SCALE_SELL, item_expect::ITEM_SHELL_SELL,         item_expect::ITEM_FANG_SELL, item_expect::ITEM_TAIL_SELL,
};

}   // namespace itemtest

using namespace itemtest;

void ItemsSuite(TestRunner &runner) {
    TestSuite suite("Items: table reader + inventory gain/consume/heal (monhun-ardu-prg.2)");

    {
        Test t("table size + blob header match the generated pins");
        t.assert(ITEM_COUNT, item_expect::ITEM_COUNT, "ITEM_COUNT matches expect");
        t.assert(item_expect::ITEM_COUNT, 8, "eight shipped items");
        t.assert(item::ITEM_SIZE, item_expect::ITEM_SIZE, "record size pin");
        t.assert(item::ITEM_SIZE, 5, "kind/heal/stam/sell is 5 B");
        t.assert(item::ITEM_MAX, 16, "inventory cap is 16");
        suite.addTest(t);
    }

    {
        Test t("every record reads back kind/heal/stam/sell from the table");
        for (uint8_t i = 0; i < ITEM_COUNT; i++) {
            const ItemInfo v = itemRead(ITEM_IDS[i]);
            t.assert(v.kind, EXPECT_KIND[i], "kind pin");
            t.assert(v.heal, EXPECT_HEAL[i], "heal pin");
            t.assert(v.stam, EXPECT_STAM[i], "stam pin");
            t.assert(v.sell, EXPECT_SELL[i], "sell pin");
            t.assert(itemKind(ITEM_IDS[i]), EXPECT_KIND[i], "itemKind matches");
            t.assert(itemHeal(ITEM_IDS[i]), EXPECT_HEAL[i], "itemHeal matches");
            t.assert(itemSell(ITEM_IDS[i]), EXPECT_SELL[i], "itemSell matches");
        }
        suite.addTest(t);
    }

    {
        Test t("herb + blue mushroom are consumables with authored heals");
        t.assert(itemKind(ITEM_HERB), item::KIND_CONSUMABLE, "herb consumable");
        t.assert(itemHeal(ITEM_HERB), 20, "herb heals 20 (feel.22)");
        t.assert(itemKind(ITEM_BLUE_MUSHROOM), item::KIND_CONSUMABLE, "mushroom consumable");
        t.assert(itemHeal(ITEM_BLUE_MUSHROOM), 10, "mushroom heals 10");
        suite.addTest(t);
    }

    {
        Test t("materials carry no heal");
        const uint8_t mats[] = {ITEM_ORE, ITEM_BUG, ITEM_SCALE, ITEM_SHELL, ITEM_FANG, ITEM_TAIL};
        for (uint8_t i = 0; i < 6; i++) {
            t.assert(itemKind(mats[i]), item::KIND_MATERIAL, "material kind");
            t.assert(itemHeal(mats[i]), 0, "material heal 0");
        }
        suite.addTest(t);
    }

    {
        Test t("zone gather codes map to the item table (index + 1)");
        t.assert(zone::GATHER_HERB, ITEM_HERB + 1, "herb gather code");
        t.assert(zone::GATHER_BLUE_MUSHROOM, ITEM_BLUE_MUSHROOM + 1, "mushroom gather code");
        t.assert(zone::GATHER_ORE, ITEM_ORE + 1, "ore gather code");
        t.assert(zone::GATHER_BUG, ITEM_BUG + 1, "bug gather code");
        t.assert(zone::GATHER_NONE, 0, "none is 0");
        suite.addTest(t);
    }

    {
        Test t("itemAdd accumulates and clamps at 255; out-of-range ids are inert");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        itemAdd(g, ITEM_HERB, 2);
        itemAdd(g, ITEM_HERB, 3);
        t.assert(itemCount(g, ITEM_HERB), 5, "2 + 3");
        itemAdd(g, ITEM_ORE, 250);
        itemAdd(g, ITEM_ORE, 100);
        t.assert(itemCount(g, ITEM_ORE), 255, "clamped at 255");
        itemAdd(g, 200, 5);
        t.assert(itemCount(g, 200), 0, "id past the table is ignored");
        suite.addTest(t);
    }

    {
        Test t("itemConsume decrements, reports empty, ignores out-of-range");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        g.items[ITEM_BLUE_MUSHROOM] = 2;
        t.assert(itemConsume(g, ITEM_BLUE_MUSHROOM), 1, "first consume ok");
        t.assert(itemConsume(g, ITEM_BLUE_MUSHROOM), 1, "second consume ok");
        t.assert(itemConsume(g, ITEM_BLUE_MUSHROOM), 0, "empty consume fails");
        t.assert(itemCount(g, ITEM_BLUE_MUSHROOM), 0, "drained");
        t.assert(itemConsume(g, 200), 0, "out-of-range consume fails");
        suite.addTest(t);
    }

    {
        Test t("newGame clears every inventory slot");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        for (uint8_t i = 0; i < ITEM_COUNT; i++)
            g.items[i] = static_cast<uint8_t>(i + 1);
        newGame(g, W_SWORD, MODE_HUNT);
        for (uint8_t i = 0; i < ITEM_COUNT; i++)
            t.assert(g.items[i], 0, "slot reset");
        suite.addTest(t);
    }

    {
        Test t("applyItemUse heals the table value (20) and consumes one herb");
        Game g;
        newGame(g, W_SWORD, MODE_HUNT);
        g.player.hp = 50;
        g.items[ITEM_HERB] = 2;
        applyItemUse(g, g.player);
        t.assert(g.player.hp, 70, "heals itemHeal(herb) == 20");
        t.assert(g.items[ITEM_HERB], 1, "one herb consumed");
        g.player.hp = 95;
        applyItemUse(g, g.player);
        t.assert(g.player.hp, 100, "heal clamps at hpMax");
        t.assert(g.items[ITEM_HERB], 0, "second herb consumed");
        applyItemUse(g, g.player);   // empty: no heal, no underflow
        t.assert(g.player.hp, 100, "empty inventory is a no-op");
        t.assert(g.items[ITEM_HERB], 0, "still empty");
        suite.addTest(t);
    }

    {
        Test t("out-of-range table reads are inert");
        t.assert(itemKind(200), 0, "kind 0");
        t.assert(itemHeal(200), 0, "heal 0");
        t.assert(itemSell(200), 0, "sell 0");
        suite.addTest(t);
    }

    runner.addTestSuite(suite);
}
