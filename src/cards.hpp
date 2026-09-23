#pragma once
// Detail-card cart reader + renderer (bead monhun-ardu-5co.3, docs/ui-design.md).
// Device-only (like src/screens.hpp): reads the mhCards records through
// core/fxmem.hpp during the run/render window and blits the prebaked 128x64
// page image (src/render.hpp cardBlit) plus the fixed overlay slots. The pure
// page machine lives in src/card_state.hpp so the host suite exercises it
// without the cart.
//
// Dynamic bits only: the hint line and the meta overlay slots (live have-counts
// on PARTS, the quest progress bar + number). Everything else is baked into the
// page image. All FX reads happen between ArduboyG plane blits.

#include "render.hpp"
#include "card_state.hpp"
#include "forge.hpp"
#include "generated/card_meta.hpp"
#include "core/fxmem.hpp"

namespace mh {

constexpr int16_t CARD_HINT_Y = 56;

static_assert(sizeof(CardItem) == cards::ITEM_SIZE, "CardItem must match the packed mhCards record");

// Fake cart pointer for a byte offset into the mhCards raw_t section.
inline const uint8_t *cardsCart(uint16_t off) {
    return reinterpret_cast<const uint8_t *>(static_cast<uint16_t>(static_cast<uint16_t>(mhCards) + off));
}

// Read item record `index` into the cache. CardItem is byte-identical to the
// packed record, so this is one bulk mhFxReadBytes with no field decoding;
// drawCard bounds the overlay loop by OVERLAY_MAX. An out-of-range index yields
// an empty record (no pages, no overlays).
inline void cardReadItem(uint8_t index, CardItem &it) {
    if (index >= cards::ITEM_COUNT) {
        for (uint8_t i = 0; i < cards::ITEM_SIZE; i++)
            reinterpret_cast<uint8_t *>(&it)[i] = 0;
        return;
    }
    mhFxReadBytes(cardsCart(static_cast<uint16_t>(cards::HEADER_SIZE + cards::ITEM_SIZE * index)), reinterpret_cast<uint8_t *>(&it), cards::ITEM_SIZE);
}

// Page image FX address cached in the item record. `page` must be < PAGE_MAX
// (DetailState only ever holds a page present in the mask).
inline uint32_t cardPageOffset(const CardItem &it, uint8_t page) {
    const uint8_t *p = &it.pages[static_cast<uint8_t>(page * cards::ITEM_PAGE_STRIDE)];
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16);
}

// Open (reopen=false) or refresh (reopen=true) the card for an item index into
// `cache`, applying the crafted-armor PARTS trim so an already-crafted piece
// never shows the craft page. A refresh clamps the current page onto a page
// still present. The raw record is cached so drawCard does not re-read the cart
// every frame.
MH_NOINLINE inline void cardLoad(DetailState &s, CardItem &cache, uint8_t index, const SaveBlock &save, bool reopen) {
    cardReadItem(index, cache);
    uint8_t mask = cache.pageMask;
    if (cache.kind == cards::KIND_ARMOR)
        mask = cardArmorMask(mask, saveCrafted(save, index));
    else if (cache.kind == cards::KIND_WEAPON)
        // Weapon cards carry the node id; cache the node table record so the
        // hint line + the A action do not re-read the cart every frame. An
        // out-of-range index reads the sentinel (zeroed) record.
        forgeReadNode(index >= cards::WEAPON_BASE ? static_cast<uint8_t>(index - cards::WEAPON_BASE) : forge::NODE_COUNT, s.node);
    if (reopen)
        cardSetMask(s, mask);
    else
        cardOpen(s, cache.kind, index, mask);
}

// Dynamic hint strings, one flash-resident blob (MH_PROGMEM: a plain string
// literal lands in SRAM on AVR, see core/progmem.hpp) plus the per-hint byte
// offsets. drawCardHint draws the selected line live at y=56.
static const char MH_PROGMEM CARD_HINTS[] = "A CRAFT\0"
                                            "A EQUIP\0"
                                            "A UNEQUIP\0"
                                            "A ACCEPT\0"
                                            "A TURN IN\0"
                                            "NEED PARTS\0"
                                            "NEED ZENNY\0"
                                            "A FORGE\0";
// Indexed by CardHint (src/card_state.hpp): NONE, CRAFT, EQUIP, UNEQUIP,
// NEED_PARTS, NEED_ZENNY, ACCEPT, TURN_IN, FORGE. NONE points at the literal's
// terminating NUL (75), so it draws nothing.
static const uint8_t MH_PROGMEM CARD_HINT_OFF[9] = {
    75, 0, 8, 16, 45, 56, 26, 35, 67,
};

// Draw the hint line at y=56 (white). Null-terminated; at most 11 chars.
inline void drawCardHint(CardHint hint) {
    const uint8_t off = mhPgmReadU8(&CARD_HINT_OFF[hint]);
    uint8_t x = 2;
    for (uint8_t i = 0; i < 12; i++) {
        const char c = static_cast<char>(mhPgmReadU8(reinterpret_cast<const uint8_t *>(&CARD_HINTS[off + i])));
        if (c == '\0')
            break;
        x = static_cast<uint8_t>(textPut(fxfontw, x, CARD_HINT_Y, c));
    }
}

// Draw one plane of the open card: page image, this page's overlays, cached
// hint. Read-only: never mutates the save. The hint was classified by
// cardSetHint() when the card opened/refreshed.
MH_NOINLINE inline void drawCard(const DetailState &s, const CardItem &it, const SaveBlock &save) {
    cardBlit(cardPageOffset(it, s.page));
    for (uint8_t i = 0; i < it.overlayCount && i < cards::OVERLAY_MAX; i++) {
        const CardOverlay &ov = it.overlays[i];
        if (ov.page != s.page)
            continue;
        if (ov.kind == cards::OVERLAY_HAVE) {
            if (ov.arg0 < item::ITEM_COUNT)
                drawNumber(ov.x, ov.y, save.items[ov.arg0], 3);
        } else if (ov.kind == cards::OVERLAY_PROG && ov.arg0 != 0) {
            uint16_t w = static_cast<uint16_t>(static_cast<uint16_t>(save.progress) * ov.arg1) / ov.arg0;
            if (w > ov.arg1)
                w = ov.arg1;
            if (w > 0)
                hudBlk(ov.x, ov.y, static_cast<int16_t>(w), 6, 2);
        }
    }
    drawCardHint(static_cast<CardHint>(s.hint));
}

}   // namespace mh
