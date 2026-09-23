#pragma once
// Prebaked detail-card navigation + action hint (bead monhun-ardu-5co.3,
// docs/ui-design.md).
//
// Host-testable, no Arduino/cart: the DetailState page machine (open on the
// first present page, LEFT/RIGHT cycles only pages set in the mask, B backs to
// the list) and the dynamic hint-line rule (A CRAFT / A EQUIP / A UNEQUIP /
// A ACCEPT / A TURN IN / NEED PARTS / NEED ZENNY). The cart read side
// (mhCards record -> CardItem) and the blit live in src/cards.hpp so this
// header compiles on the host with the generated constants only.
//
// The list A opens the card for the cursor row; the card A performs the row's
// context action by reusing screenApplyAction() with the stored row (the sketch
// keeps the ScreenRow). The armor PARTS page is dropped from the effective mask
// once the piece is crafted (cardArmorMask), so a craft immediately loses the
// page -- the meta mask is the static set of generated pages.

#include <stdint.h>
#include "core/input.hpp"
#include "core/save.hpp"
#include "screen_state.hpp"
#include "generated/card_meta.hpp"

namespace mh {

using cards::KIND_ARMOR;
using cards::KIND_QUEST;
using cards::OVERLAY_HAVE;
using cards::OVERLAY_MAX;
using cards::OVERLAY_PROG;
using cards::PAGE_MAX;

// One page's dynamic overlay slot, decoded from the mhCards record.
struct CardOverlay {
    uint8_t kind;   // OVERLAY_*
    uint8_t page;   // page id the slot belongs to
    uint8_t x, y;   // screen position
    uint8_t arg0;   // HAVE: item index; PROG: need
    uint8_t arg1;   // PROG: bar width
};

// One item's card record, laid out exactly like the packed mhCards record
// (src/cards.hpp static_asserts sizeof(CardItem) == cards::ITEM_SIZE) so the
// device reader is one bulk mhFxReadBytes with no field decoding. `pages` is
// the raw PAGE_MAX x u24 page-image address table (little-endian, 0 = absent);
// the page count is the mask popcount, so it is not stored.
struct CardItem {
    uint8_t kind;       // KIND_*
    uint8_t pageMask;   // static generated page set
    uint8_t overlayCount;
    uint8_t pages[PAGE_MAX * 3];
    CardOverlay overlays[OVERLAY_MAX];
};

// Card page machine. `pageMask` is the effective mask (the static mask trimmed
// by the caller, e.g. crafted armor drops PARTS).
struct DetailState {
    uint8_t kind = 0;
    uint8_t index = 0;
    uint8_t page = 0;
    uint8_t pageCount = 0;
    uint8_t pageMask = 0;
    bool active = false;
    bool prevA = false;
    bool prevB = false;
    int8_t navX = 0;
};

enum DetailEvent : int8_t {
    DETAIL_NONE = 0,
    DETAIL_ACTION,   // A rising edge: run the stored row's action
    DETAIL_BACK      // B rising edge: back to the list
};

// Page bit for the mask (PAGE_MAX is 4, so the shift is a constant &3).
inline uint8_t cardPageBit(uint8_t page) {
    return static_cast<uint8_t>(1u << (page & 7));
}

inline bool cardMaskHas(uint8_t mask, uint8_t page) {
    return (mask & cardPageBit(page)) != 0;
}

// Number of present pages (the DetailState page count shown as n/m).
inline uint8_t cardPopcount(uint8_t mask) {
    uint8_t n = 0;
    for (uint8_t p = 0; p < PAGE_MAX; p++) {
        if (cardMaskHas(mask, p))
            n++;
    }
    return n;
}

// First page present in the mask (0 when the mask is empty).
inline uint8_t cardFirstPage(uint8_t mask) {
    for (uint8_t p = 0; p < PAGE_MAX; p++) {
        if (cardMaskHas(mask, p))
            return p;
    }
    return 0;
}

// Effective armor mask: the PARTS page is only live while the piece is
// uncrafted (a crafted piece cannot be crafted again).
inline uint8_t cardArmorMask(uint8_t staticMask, bool crafted) {
    return crafted ? static_cast<uint8_t>(staticMask & ~cardPageBit(cards::PAGE_PARTS)) : staticMask;
}

inline void cardOpen(DetailState &s, uint8_t kind, uint8_t index, uint8_t mask) {
    s.kind = kind;
    s.index = index;
    s.pageMask = mask;
    s.pageCount = cardPopcount(mask);
    s.page = cardFirstPage(mask);
    s.active = true;
    s.prevA = false;
    s.prevB = false;
    s.navX = 0;
}

inline void cardClose(DetailState &s) {
    s.active = false;
}

// Replace the effective mask (e.g. after a craft) and clamp the current page
// onto the first page still present.
inline void cardSetMask(DetailState &s, uint8_t mask) {
    s.pageMask = mask;
    s.pageCount = cardPopcount(mask);
    if (!cardMaskHas(mask, s.page))
        s.page = cardFirstPage(mask);
}

// Cycle to the next/previous page present in the mask (wrapping).
inline void cardNav(DetailState &s, int8_t delta) {
    if (s.pageMask == 0)
        return;
    uint8_t p = s.page;
    for (uint8_t i = 0; i < PAGE_MAX; i++) {
        p = delta >= 0 ? static_cast<uint8_t>((p + 1) & (PAGE_MAX - 1)) : static_cast<uint8_t>((p + PAGE_MAX - 1) & (PAGE_MAX - 1));
        if (cardMaskHas(s.pageMask, p)) {
            s.page = p;
            return;
        }
    }
}

// One input tick: LEFT/RIGHT page nav (fresh direction only) + A/B edges.
inline DetailEvent detailStep(DetailState &s, const Input &in) {
    if (in.mx == 0)
        s.navX = 0;
    else if (in.mx != s.navX) {
        cardNav(s, in.mx);
        s.navX = in.mx;
    }
    bool aP, bP, bR;
    inputEdges(in, s.prevA, s.prevB, aP, bP, bR);
    (void)bR;
    if (aP)
        return DETAIL_ACTION;
    if (bP)
        return DETAIL_BACK;
    return DETAIL_NONE;
}

// Does the list row open a detail card? Armor (craft or equip) and quest
// (take/turn-in) rows do; a GEAR weapon row keeps its direct-equip action
// (ui.3 scope split -- the weapon card lands with the ui.4 forge trees).
inline bool cardRowOpens(const ScreenRow &row) {
    switch (row.action) {
    case screens::ACTION_CRAFT_ARMOR:
    case screens::ACTION_EQUIP_ARMOR:
    case screens::ACTION_TAKE_QUEST:
    case screens::ACTION_TURN_IN_QUEST:
        return true;
    default:
        return false;
    }
}

// The card kind a row opens (armor vs quest).
inline uint8_t cardRowKind(const ScreenRow &row) {
    return (row.action == screens::ACTION_CRAFT_ARMOR || row.action == screens::ACTION_EQUIP_ARMOR) ? KIND_ARMOR : KIND_QUEST;
}

// The card item index a row opens: armor pieces are packed (slot<<5)|piece and
// are the first card table entries (index == piece); quest rows pack the quest
// id in the low nibble and the card table stores quests sorted by (id, name)
// after the armor, so the global index is QUEST_BASE + quest id. gen-cards
// rejects non-dense quest ids so the two stay in lockstep.
inline uint8_t cardRowIndex(const ScreenRow &row) {
    if (row.action == screens::ACTION_CRAFT_ARMOR || row.action == screens::ACTION_EQUIP_ARMOR)
        return screenArmorPiece(row.param);
    return static_cast<uint8_t>(static_cast<uint8_t>(row.param & 15) + cards::QUEST_BASE);
}

// Dynamic hint-line rule (the only dynamic text on a card). Returns the string
// the device draws; the enum keeps the host test cart-free.
enum CardHint : uint8_t {
    HINT_NONE = 0,
    HINT_CRAFT,
    HINT_EQUIP,
    HINT_UNEQUIP,
    HINT_ACCEPT,
    HINT_TURN_IN,
    HINT_NEED_PARTS,
    HINT_NEED_ZENNY
};

inline bool armorSlotEquipped(const SaveBlock &save, uint8_t piece, uint8_t slot) {
    return slot < SAVE_EQUIP_COUNT && save.equip[slot] == static_cast<uint8_t>(piece + 1);
}

inline CardHint cardHint(const SaveBlock &save, const ScreenRow &row) {
    // Reuse screenCondOk for the live/blocked decision (it already encodes the
    // craft bill, the crafted gate, the quest chain and the turn-in readiness),
    // then only disambiguate equip-vs-unequip and the blocked reason.
    const bool live = screenCondOk(save, row);
    switch (row.action) {
    case screens::ACTION_CRAFT_ARMOR:
    case screens::ACTION_EQUIP_ARMOR: {
        const uint8_t piece = screenArmorPiece(row.param);
        const uint8_t slot = screenArmorSlot(row.param);
        if (!live)
            return row.action == screens::ACTION_CRAFT_ARMOR ? (save.zenny < row.cost ? HINT_NEED_ZENNY : HINT_NEED_PARTS) : HINT_NONE;
        if (saveCrafted(save, piece))
            return armorSlotEquipped(save, piece, slot) ? HINT_UNEQUIP : HINT_EQUIP;
        return HINT_CRAFT;
    }
    case screens::ACTION_TAKE_QUEST:
        return live ? HINT_ACCEPT : HINT_NONE;
    case screens::ACTION_TURN_IN_QUEST:
        return live ? HINT_TURN_IN : HINT_NONE;
    default:
        return HINT_NONE;
    }
}

}   // namespace mh
