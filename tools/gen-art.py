#!/usr/bin/env python3
"""Author the 4-shade block-art and font sprite sheets for monhun-ardu.

Source of truth for every silhouette is mock/game.js (drawPlayer, drawMonster,
drawPole, drawProjectiles, drawEffects) and its FONT table. Each PNG is an RGBA
sprite sheet laid out left-to-right, one frame per tile; heights are multiples
of 8 to satisfy the converter. Palette maps 1:1 onto the L4 triplane levels:

    transparent -> mask 0 (nothing drawn)
    black       -> shade 0 (no plane): opaque eraser pixel (mask 1, data 0)
    dark gray   -> shade 1 (plane 0)
    light gray  -> shade 2 (plane 1)
    white       -> shade 3 (plane 2)

tools/convert-sprite.py (shades=4) turns each sheet into the 2-byte-header
plus-mask triplane blob that SpritesU::drawPlusMaskFX reads.

Overlay/effect sheets (bead monhun-ardu-42n.2) are authored from the core table
dimensions dumped by tools/fxdump.cpp: attack hw/hh, monster attack hw/hh and
whirl orbit radii are never duplicated here. Block coordinates come straight
from the current blk() draw calls in src/render.hpp (the exact shapes the mock
paints) with the core dims substituted, so the PNG is pixel-exact by
construction. Every sheet is checked back: sheet dims must match the derived
frame size, every blk rect must land in the typed plane, and every pixel outside
the declared rects must stay transparent. `--dump` prints the ASCII evidence.

The opening-menu sheets (images/menu/, bead monhun-ardu-zza) are authored from
the same GLYPHS table as the font sheets, so the baked pixels are provably the
font glyphs the old textPut(fxfontw/fxfontg) layout drew. check_menu_identity()
cross-compares every menu glyph cell against the authored font sheet. The HUD
marker strips (images/blocks/fxhud, bead monhun-ardu-e4a) are baked the same way
and cross-checked against fxfontw by check_hud_identity(); the font sheets stay
on the MCU-independent side because the HUD still textPuts the gun reload/shell
and train readouts from them.

The same run writes src/generated/art_dims.hpp: per-frame core dimensions and
frame layout for the render bead + the host dims-drift test (tst/art_dims_test.hpp).
"""
import argparse
import json
import os
import re
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

CLEAR = (0, 0, 0, 0)
BLACK = (0, 0, 0, 255)
DARK = (85, 85, 85, 255)
LIGHT = (170, 170, 170, 255)
WHITE = (255, 255, 255, 255)

SHADES = (BLACK, DARK, LIGHT, WHITE)
SHADE_NAMES = ("black", "dark", "light", "white")


# ------------------------------------------------- whirl ring trig source
# The flail whirl ring is 6 dots on the mock's exact ellipse
# (round(cos(a) * rx), round(sin(a) * ry)) at a = tick*0.35 rad + i*60deg. The
# device drew them through the Q4 sine LUT (src/core/sin256.hpp) and mulQ4()
# rounding in src/render.hpp; the baked multi-phase sheet must use the *same*
# table + rounding so each frame is pixel-identical to the old six blits at that
# frame's angle. SIN65 is parsed straight out of the core header (the single
# source gen-fxtables.cpp also packs to the cart), so the bake cannot drift from
# the device table. RING6 is the 42.667-unit (60deg) ring offset set the render
# used; the frame count is the render phase selector
# (art_dims::whirlring_frames).
#
# 24 phases, not 32: chosen when the blocks section was packed *before* the
# raw_t runtime tables in fxdata.txt (a 48x32 frame is 1152 B, so 32 phases =
# 36,864 B pushed mhEquip past the 64 KiB 16-bit fake-pointer window fxmem.hpp
# requires). Bead monhun-ardu-603 moved the raw_t tables first, so the window no
# longer constrains sprite growth; the count stays 24 (256/24 = 10.67 units of
# ring angle per frame vs the 14-unit tick step keeps the motion smooth) rather
# than rebaking a larger sheet. RING6 frames remain pixel-identical to the old
# six blits.
RING6 = (0, 43, 85, 128, 171, 213)
WHIRL_RING_FRAMES = 24


def load_sin65():
    path = os.path.join(ROOT, "src", "core", "sin256.hpp")
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    start = text.index("SIN65[65] = {")
    body = text[start:text.index("};", start)]
    values = [int(v) for v in re.findall(r"-?\d+", body.split("{", 1)[1])]
    if len(values) != 65:
        raise SystemExit("gen-art: SIN65 parse got %d values, want 65" % len(values))
    return values


SIN65 = load_sin65()


def sin256(a):
    a &= 255
    q = a >> 6
    k = a & 63
    i = (64 - k) if (q & 1) else k
    v = SIN65[i]
    return -v if (q & 2) else v


def cos256(a):
    return sin256(a + 64)


def mul_q4(a, b):
    return (a * b + 8) >> 4


def rect(img, x, y, w, h, color):
    if w <= 0 or h <= 0:
        return
    if x < 0 or y < 0 or x + w > img.size[0] or y + h > img.size[1]:
        raise SystemExit("gen-art: block (%d,%d,%d,%d) outside %s canvas" % (x, y, w, h, img.size))
    px = img.load()
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            px[xx, yy] = color


def new(w, h):
    return Image.new("RGBA", (w, h), CLEAR)


def strip(frames, w, h):
    """Lay frames left-to-right into one sheet."""
    sheet = new(w * len(frames), h)
    for i, frame in enumerate(frames):
        sheet.paste(frame, (i * w, 0))
    return sheet


# ------------------------------------------------------------------ dims JSON


class DimsError(Exception):
    pass


class Dims:
    """fxdump JSON with attribute access and cycle-safe resolution of `!ref`."""

    def __init__(self, raw):
        self.raw = raw

    def __getattr__(self, key):
        try:
            value = self.raw[key]
        except KeyError:
            raise AttributeError(key) from None
        return self._resolve(value)

    def _resolve(self, value, seen=()):
        if isinstance(value, Dims):
            return value
        if isinstance(value, dict) and set(value) == {"!ref"}:
            ref = value["!ref"]
            if ref in seen:
                raise DimsError("cyclic !ref chain: %s" % " -> ".join(seen + (ref,)))
            return self._resolve(self.raw[ref], seen + (ref,))
        if isinstance(value, dict):
            return Dims({k: self._resolve(v, seen) for k, v in value.items()})
        if isinstance(value, list):
            return [self._resolve(v, seen) for v in value]
        return value


def load_dims(path):
    with open(path, "r", encoding="utf-8") as f:
        return Dims(json.load(f))


# --------------------------------------------------- fxdump-backed dimensions


def attack_boxes(dims):
    """Per-sheet core dims from fxdump, keyed by sheet id."""
    sword, flail, gun = dims.weapons
    # fxslash carries one frame per distinct sword hit box: the three main
    # combo attacks (12x10 twice), the special (20x16) and the two attack
    # branches (step-slash 14x12, spin-cut 28x26). Order is the frame order.
    slash = [(sword.attacks[i].hw, sword.attacks[i].hh) for i in range(3)]
    slash.append((sword.special.hw, sword.special.hh))
    for b in sword.branches:
        if b.id:
            slash.append((b.hw, b.hh))
    return {
        "slash": list(dict.fromkeys(slash)),
        "ripspecial": (sword.special.hw, sword.special.hh),
        "monster": {
            "lunge": (dims.monsterAttacks.lunge.hw, dims.monsterAttacks.lunge.hh),
            "sweep": (dims.monsterAttacks.sweep.hw, dims.monsterAttacks.sweep.hh),
        },
        "gun_boxes": [(gun.attacks[i].hw, gun.attacks[i].hh) for i in range(3)],
    }


def sheet_size(rects):
    """Smallest frame containing every rect (extent max - origin min per axis)."""
    max_x = max(x + w for _, x, y, w, h, _ in rects)
    max_y = max(y + h for _, x, y, w, h, _ in rects)
    min_x = min(0, min(x for _, x, y, w, h, _ in rects))
    min_y = min(0, min(y for _, x, y, w, h, _ in rects))
    return min_x, min_y, max_x, max_y


# --------------------------------------------------------------- mock shapes
# Each shape is a blk() rect from src/render.hpp / mock/game.js translated to a
# frame-local origin. `box` marks the hit box rect the dims test re-derives
# from the core tables; the others are composite decorations (cores, rims,
# bars). Blocks are (color, dx, dy, w, h). `reaches` records the core reach
# each frame bakes (used by the dims test's reach drift check).
#
# Anchor convention (documented for the render bead):
#   "box top-left"   frame origin == hit box top-left; box drawn at (pad, pad)
#   "box centre"     frame origin == hit box centre; box centred in the frame
#   "player centre"  frame origin == player centre (cx, cy)
#   "player top-left" frame origin == player top-left (x, y)
#   "plate centre"   frame origin == shield centre (shx, shy)
#   "bar centre"     frame origin == reload bar centre (x + 8, y - 2)
#   "dot top-left"   frame origin == the dot's top-left


def whirl_ring_frames(dims):
    """Bake the 6 whirl orbit dots into WHIRL_RING_FRAMES phase frames.

    Frame f represents the device ring angle bin [f*256/N, (f+1)*256/N); its
    dots are placed at the bin-centre angle so the worst-case quantization error
    is 256/(2N) units. Each dot is the old 2x2 LIGHT blit on the exact ellipse,
    translated into the 48x32 frame local space (player centre at 24,16)."""
    rx, ry = dims.whirl.rx, dims.whirl.ry
    frames = []
    for f in range(WHIRL_RING_FRAMES):
        base = ((2 * f + 1) * 256) // (2 * WHIRL_RING_FRAMES)
        blocks = []
        for off in RING6:
            a = (base + off) & 255
            blocks.append((LIGHT, mul_q4(cos256(a), rx) + 24, mul_q4(sin256(a), ry) + 16, 2, 2))
        frames.append(blocks)
    return frames


def icon_defs(dims):
    d = attack_boxes(dims)
    lunge, sweep = d["monster"]["lunge"], d["monster"]["sweep"]

    def slash_frame(hw, hh):
        px = (32 - hw) // 2
        py = (32 - hh) // 2
        # White 4x4 core on the hit-box centre, which is always (14,14) in the
        # uniform 32x32 frame, so the mock's core at (hx-2, hy-2) is exact.
        return [(LIGHT, px, py, hw, hh), (WHITE, 14, 14, 4, 4)]

    return [
        {"id": "slash", "w": 32, "h": 32, "anchor": "box centre",
         "frames": [slash_frame(hw, hh) for hw, hh in d["slash"]]},
        {"id": "ripspecial", "w": 24, "h": 24, "anchor": "box top-left",
         "frames": [[(LIGHT, 0, 0, d["ripspecial"][0] + 4, d["ripspecial"][1] + 4)]]},
        {"id": "parry", "w": 24, "h": 16, "anchor": "player centre",
         "frames": [[(WHITE, 11, 0, 2, 14), (LIGHT, 9, 2, 6, 2)]]},
        {"id": "whirl", "w": 8, "h": 4, "anchor": "dot top-left",
         "frames": [[(LIGHT, 0, 0, 2, 2)], [(WHITE, 0, 0, 4, 4)],
                    [(LIGHT, 0, 0, 1, 1)], [(WHITE, 0, 0, 2, 2)]]},
        {"id": "deflect", "w": 24, "h": 16, "anchor": "player top-left",
         "frames": [[(LIGHT, 2, 2, 1, 12), (LIGHT, 21, 2, 1, 12)]]},
        {"id": "guard", "w": 12, "h": 16, "anchor": "plate centre",
         "frames": [[(LIGHT, 1, 1, 10, 14), (BLACK, 5, 1, 2, 14)],
                    [(WHITE, 1, 1, 10, 14)],
                    [(WHITE, 0, 1, 10, 14)]]},
        {"id": "reload", "w": 10, "h": 8, "anchor": "bar centre",
         "frames": [[(LIGHT, 0, 3, 10, 2)]]},
        {"id": "erase", "w": 4, "h": 16, "anchor": "player top-left",
         "frames": [[(BLACK, 0, 0, 4, 1)]]},
        {"id": "trail", "w": 4, "h": 4, "anchor": "puff top-left",
         "frames": [[(LIGHT, 0, 0, 2, 2)], [(DARK, 0, 0, 2, 2)]]},
        # Frames in order: lunge windup, lunge attack, sweep windup, sweep
        # attack. The mock's windup box is shade 1 with a 2x2 shade-2 core and
        # the attack box shade 2 with a 4x4 shade-3 core, for both attacks.
        {"id": "telegraph", "w": 32, "h": 24, "anchor": "box centre",
         "frames": [[(DARK, (32 - lunge[0]) // 2, (24 - lunge[1]) // 2, lunge[0], lunge[1]), (LIGHT, 15, 11, 2, 2)],
                    [(LIGHT, (32 - lunge[0]) // 2, (24 - lunge[1]) // 2, lunge[0], lunge[1]), (WHITE, 14, 10, 4, 4)],
                    [(DARK, (32 - sweep[0]) // 2, (24 - sweep[1]) // 2, sweep[0], sweep[1]), (LIGHT, 15, 11, 2, 2)],
                    [(LIGHT, (32 - sweep[0]) // 2, (24 - sweep[1]) // 2, sweep[0], sweep[1]), (WHITE, 14, 10, 4, 4)]]},
        {"id": "chip", "w": 8, "h": 8, "anchor": "chip top-left",
         "frames": [[(WHITE, 0, 0, 3, 3)], [(WHITE, 0, 0, 4, 4)]]},
        # Flail whirl ring (bead monhun-ardu-836): the 6 orbit dots of
        # mock/game.js drawPlayer pre-composited into one sprite frame per
        # quantized ring phase. 48x32 holds the full ellipse (rx=20, ry=14 from
        # fxdump) with a 2 px margin; the frame origin is the player centre, so
        # the equipment record anchors at (24,16). Each frame is baked at the
        # bin-centre angle of its phase (see render.hpp for the phase selector);
        # the 6 dots are placed with the device's SIN65/mulQ4 math.
        {"id": "whirlring", "w": 48, "h": 32, "anchor": "player centre",
         "frames": whirl_ring_frames(dims)},
        # Breakable-part overlay (beads monhun-ardu-ljj.6/ljj.8): the ravager
        # tail, one 18x10 frame per (facing, stage) pair, matching the part box
        # in data/creatures/ravager.json ({ox:-14, oy:8, w:18, h:10}). Frame
        # order is the combatPartArtFrame contract (src/core/combat.hpp):
        # east intact, east broken, west intact, west broken. Frame origin is
        # the face-relative part box top-left (rotated at draw time).
        {"id": "tail", "w": 18, "h": 10, "anchor": "part box top-left",
         "frames": tail_defs()},
    ] + hud_defs()


# Existing block sheets (kept byte-stable).
def player_frames():
    """16x16 body+shadow only; weapon overlays live on the overlay sheets."""
    frames = []
    for body in (WHITE, LIGHT):   # normal shade3, dodge shade2
        img = new(16, 16)
        rect(img, 2, 15, 12, 1, DARK)   # shadow
        rect(img, 5, 1, 6, 6, body)     # head
        rect(img, 4, 7, 8, 6, body)     # torso
        rect(img, 5, 13, 2, 2, body)    # legs
        rect(img, 9, 13, 2, 2, body)
        frames.append(img)
    return frames


def monster_frame(body, head, east, dead=False):
    img = new(32, 24)
    if dead:
        rect(img, 0, 16, 32, 8, DARK)
        rect(img, 12, 14, 8, 4, LIGHT)
        return img
    rect(img, 2, 23, 28, 1, BLACK)          # ground shadow
    for i in range(4):
        rect(img, 3 + i * 8, 21, 3, 3, BLACK)   # feet
    rect(img, 2, 4, 28, 14, body)
    rect(img, 6, 1, 20, 6, body)
    head_x = 22 if east else 0
    rect(img, head_x, 6, 10, 12, head)
    rect(img, head_x + (7 if east else 1), 13, 2, 2, BLACK)   # face-side eye
    rect(img, head_x + 4, 9, 2, 2, BLACK)
    return img


# ---- breakable-part overlay (ljj.6 / ljj.8 parts pass). The tail is an 18x10
# frame matching the ravager part box; the broken variants keep only a stub at
# the body end. Frame order is the combatPartArtFrame() contract
# (src/core/combat.hpp): east intact, east broken, west intact, west broken, so
# one index selects the draw. Frame origin is the part box top-left.
def tail_defs():
    return [
        [(LIGHT, 0, 2, 12, 6), (WHITE, 0, 3, 3, 4)],   # east intact
        [(DARK, 9, 3, 9, 4)],                          # east broken stub
        [(LIGHT, 6, 2, 12, 6), (WHITE, 15, 3, 3, 4)],  # west intact
        [(DARK, 0, 3, 9, 4)],                          # west broken stub
    ]


def monster_frames():
    # 0..3 facing east (head right), 4..7 facing west (head left)
    states = [
        (DARK, WHITE),   # idle / attack
        (LIGHT, LIGHT),  # recover
        (WHITE, WHITE),  # windup flash / hit flash
    ]
    frames = []
    for east in (True, False):
        for body, head in states:
            frames.append(monster_frame(body, head, east))
        frames.append(monster_frame(DARK, WHITE, east, dead=True))
    return frames


def pole_frame(flash):
    img = new(20, 40)   # 20x36 art, padded to a multiple of 8
    rect(img, 2, 12, 16, 24, DARK)
    for band in (20, 27, 34):
        rect(img, 2, band, 16, 1, BLACK)
    rect(img, 0, 0, 20, 16, WHITE if flash else LIGHT)
    rect(img, 8, 5, 4, 4, BLACK)
    rect(img, 0, 34, 20, 2, BLACK)
    return img


def ball_frame():
    img = new(7, 8)   # 7x6 art, padded
    rect(img, 0, 0, 7, 6, LIGHT)
    rect(img, 1, 1, 5, 4, WHITE)
    rect(img, 0, 4, 7, 1, BLACK)
    return img


def scatter_frame():
    img = new(4, 8)   # 4x4 art, padded so the tile has transparency (plus-mask)
    rect(img, 0, 0, 4, 4, LIGHT)
    rect(img, 1, 1, 2, 2, WHITE)
    return img


SPARK = [0b0100, 0b1110, 0b0111, 0b0010]


def spark_frame(color):
    img = new(4, 4)
    for row, bits in enumerate(SPARK):
        for col in range(4):
            if bits & (0b1000 >> col):
                rect(img, col, row, 1, 1, color)
    return img


# --------------------------------------------------------------- mock FONT

GLYPHS = {
    " ": [0b000, 0b000, 0b000, 0b000, 0b000],
    "A": [0b010, 0b101, 0b111, 0b101, 0b101],
    "B": [0b110, 0b101, 0b110, 0b101, 0b110],
    "C": [0b011, 0b100, 0b100, 0b100, 0b011],
    "D": [0b110, 0b101, 0b101, 0b101, 0b110],
    "E": [0b111, 0b100, 0b110, 0b100, 0b111],
    "F": [0b111, 0b100, 0b110, 0b100, 0b100],
    "G": [0b011, 0b100, 0b101, 0b101, 0b011],
    "H": [0b101, 0b101, 0b111, 0b101, 0b101],
    "I": [0b111, 0b010, 0b010, 0b010, 0b111],
    "J": [0b001, 0b001, 0b001, 0b101, 0b010],
    "K": [0b101, 0b101, 0b110, 0b101, 0b101],
    "L": [0b100, 0b100, 0b100, 0b100, 0b111],
    "M": [0b101, 0b111, 0b111, 0b101, 0b101],
    "N": [0b101, 0b111, 0b101, 0b101, 0b101],
    "O": [0b010, 0b101, 0b101, 0b101, 0b010],
    "P": [0b110, 0b101, 0b110, 0b100, 0b100],
    "Q": [0b010, 0b101, 0b101, 0b110, 0b011],
    "R": [0b110, 0b101, 0b110, 0b101, 0b101],
    "S": [0b011, 0b100, 0b010, 0b001, 0b110],
    "T": [0b111, 0b010, 0b010, 0b010, 0b010],
    "U": [0b101, 0b101, 0b101, 0b101, 0b011],
    "V": [0b101, 0b101, 0b101, 0b101, 0b010],
    "W": [0b101, 0b101, 0b111, 0b111, 0b101],
    "X": [0b101, 0b101, 0b010, 0b101, 0b101],
    "Y": [0b101, 0b101, 0b010, 0b010, 0b010],
    "Z": [0b111, 0b001, 0b010, 0b100, 0b111],
    "0": [0b111, 0b101, 0b101, 0b101, 0b111],
    "1": [0b010, 0b110, 0b010, 0b010, 0b111],
    "2": [0b111, 0b001, 0b111, 0b100, 0b111],
    "3": [0b111, 0b001, 0b011, 0b001, 0b111],
    "4": [0b101, 0b101, 0b111, 0b001, 0b001],
    "5": [0b111, 0b100, 0b111, 0b001, 0b111],
    "6": [0b111, 0b100, 0b111, 0b101, 0b111],
    "7": [0b111, 0b001, 0b001, 0b001, 0b001],
    "8": [0b111, 0b101, 0b111, 0b101, 0b111],
    "9": [0b111, 0b101, 0b111, 0b001, 0b111],
    ":": [0b000, 0b010, 0b000, 0b010, 0b000],
    "-": [0b000, 0b000, 0b111, 0b000, 0b000],
    ".": [0b000, 0b000, 0b000, 0b000, 0b010],
    "/": [0b001, 0b001, 0b010, 0b100, 0b100],
}


def font_sheet(color):
    """128 ASCII-ordered 4x8 tiles; glyph at cols 0..2, mock 5-row cap."""
    sheet = new(4 * 128, 8)
    for code in range(128):
        ch = chr(code)
        glyph = GLYPHS.get(ch)
        if glyph is None:
            continue
        ox = code * 4
        for row, bits in enumerate(glyph):
            for col in range(3):
                if bits & (4 >> col):
                    rect(sheet, ox + col, row, 1, 1, color)
    return sheet


# ------------------------------------------------------ opening-menu sheets
# Bead monhun-ardu-zza: the old MCU layout (textPut on fxfontw/fxfontg + the
# blk() underline) is baked into two FX sheets. Every glyph pixel comes from
# the same GLYPHS table the font sheets are authored from, so the bake is the
# font glyphs moved, not redrawn. The underlines are the shade-3 blk() rows.

def text_blocks(x, y, text, color):
    """blk() rects for `text` on the 4 px glyph lane: 4x8 tile per char, glyph
    at cols 0..2 / rows 0..4, advance 4 px (mock drawText scale 1)."""
    blocks = []
    for i, ch in enumerate(text):
        for row, bits in enumerate(GLYPHS[ch]):
            for col in range(3):
                if bits & (4 >> col):
                    blocks.append((color, x + i * 4 + col, y + row, 1, 1))
    return blocks


# (x, y, text, color) of every static element, in the old draw order. The
# underlined selected option is NOT here: it comes from the sel tiles.
MENU_ELEMENTS = (
    (42, 10, "MONHUN DEMO", WHITE),   # (128 - 11*4) / 2, title
    (4, 22, "WEAPON", LIGHT),
    (4, 34, "TARGET", LIGHT),
    (36, 22, "SWD", LIGHT),
    (52, 22, "FLS", LIGHT),
    (68, 22, "GUN", LIGHT),
    (36, 34, "LUNGE", LIGHT),
    (60, 34, "SWEEP", LIGHT),
    (84, 34, "HEAVY", LIGHT),
    (36, 44, "RAVAGER", LIGHT),
    (68, 44, "POLE", LIGHT),
    (4, 56, "A START", WHITE),
)

# Selected-option order: weapons 0..2, then targets 0..4 (design order, which
# is also the old wrap order: LUNGE/SWEEP/HEAVY then RAVAGER/POLE).
MENU_OPTIONS = ("SWD", "FLS", "GUN", "LUNGE", "SWEEP", "HEAVY", "RAVAGER", "POLE")


def menu_defs():
    """bg: one 128x64 frame of static content (transparent everywhere else).
    sel: eight 28x16 tiles, glyphs at local (0,0) and the white underline
    (len*4-1 px, the old blk row) at local row 9; the spare rows 10..15 stay
    transparent. Height must be a multiple of 8 for the plus-mask blitter."""
    bg = []
    for x, y, text, color in MENU_ELEMENTS:
        bg += text_blocks(x, y, text, color)
    frames = []
    for name in MENU_OPTIONS:
        # Underline 1 px under the 8 px glyph tile (old MENU_UNDERLINE_DY = 9).
        frames.append(text_blocks(0, 0, name, WHITE) + [(WHITE, 0, 9, len(name) * 4 - 1, 1)])
    return [
        {"id": "menu_bg", "w": 128, "h": 64, "anchor": "screen top-left", "frames": [bg]},
        {"id": "menu_sel", "w": 28, "h": 16, "anchor": "option top-left", "frames": frames},
    ]


# ------------------------------------------------------ HUD marker strips
# Bead monhun-ardu-e4a: drawHud() used to textPut() 4 glyphs/frame for the
# weapon marker (SWD/FLA/GUN) + mode char (H/T) at x=46, each a fixed cart seek.
# The 4 txfa glyphs (3 weapon + 1 mode) are baked into one 16x8 strip, drawn
# with a single blit per plane. Pixels come from the same GLYPHS table the font
# sheets are authored from; check_hud_identity() cross-checks every cell against
# fxfontw, the sheet hudPut() blitted from.
HUD_WEAPONS = ("SWD", "FLA", "GUN")   # W_SWORD, W_FLAIL, W_GUN
HUD_MODES = ("H", "T")                # MODE_HUNT, MODE_TRAIN


def hud_defs():
    """Six 16x8 marker strips, frame = weapon*2 + mode: the drawHud x=46 lane
    (4 glyph cells at 4 px advance, 8 px tall) for each weapon x mode."""
    frames = []
    for wpn in HUD_WEAPONS:
        for mode in HUD_MODES:
            frames.append(text_blocks(0, 0, wpn + mode, WHITE))
    return [{"id": "hud", "w": 16, "h": 8, "anchor": "marker top-left", "frames": frames}]


def check_hud_identity(sheets):
    """Cross-check the HUD bake against the authored white font sheet: each of
    the six 16x8 frames is 4 fxfontw glyph tiles (weapon marker + mode char) and
    must be pixel-identical to the sheet tile for the same character. Same
    pixel-oracle link as check_menu_identity()."""
    fontw = sheets["fontw"].load()
    hud = sheets["hud"].load()
    failures = []
    fi = 0
    for wpn in HUD_WEAPONS:
        for mode in HUD_MODES:
            for i, ch in enumerate(wpn + mode):
                ox = fi * 16 + i * 4
                for row in range(8):
                    for col in range(4):
                        got = hud[ox + col, row]
                        want = fontw[ord(ch) * 4 + col, row]
                        if got != want:
                            failures.append("hud frame %d (%d,%d) %r: got %s want %s" % (fi, ox + col, row, ch, got, want))
            fi += 1
    if failures:
        for f in failures[:20]:
            print("gen-art: HUD IDENTITY FAIL: %s" % f, file=sys.stderr)
        raise SystemExit("gen-art: %d hud identity failures" % len(failures))


def check_menu_identity(sheets):
    """Cross-check the bake against the authored font sheets: every menu glyph
    cell (4x8, the tile textPut() blits) must be pixel-identical to the
    fxfontw/fxfontg sheet tile for the same character, and the cells must be
    transparent outside the glyph. This is the pixel-oracle link between the
    menu sheets and the font source."""
    fontw = sheets["fontw"].load()
    fontg = sheets["fontg"].load()
    bg = sheets["menu_bg"].load()
    sel = sheets["menu_sel"].load()
    failures = []
    for x, y, text, color in MENU_ELEMENTS:
        font = fontw if color == WHITE else fontg
        for i, ch in enumerate(text):
            ox = x + i * 4
            for row in range(8):
                for col in range(4):
                    got = bg[ox + col, y + row]
                    want = font[ord(ch) * 4 + col, row]
                    if got != want:
                        failures.append("menu_bg (%d,%d) %r: got %s want %s" % (ox + col, y + row, ch, got, want))
    for fi, name in enumerate(MENU_OPTIONS):
        for i, ch in enumerate(name):
            ox = fi * 28 + i * 4
            for row in range(8):
                for col in range(4):
                    got = sel[ox + col, row]
                    want = fontw[ord(ch) * 4 + col, row]
                    if got != want:
                        failures.append("menu_sel frame %d (%d,%d) %r: got %s want %s" % (fi, ox + col, row, ch, got, want))
    if failures:
        for f in failures[:20]:
            print("gen-art: MENU IDENTITY FAIL: %s" % f, file=sys.stderr)
        raise SystemExit("gen-art: %d menu identity failures" % len(failures))


# ------------------------------------------------------- authored sheet table


def render_icon(defn):
    frames = []
    for blocks in defn["frames"]:
        img = new(defn["w"], defn["h"])
        for color, dx, dy, w, h in blocks:
            rect(img, dx, dy, w, h, color)
        frames.append(img)
    return strip(frames, defn["w"], defn["h"])


def render_all(dims):
    icons = icon_defs(dims)
    menu = menu_defs()
    sheets = {}
    for d in icons + menu:
        sheets[d["id"]] = render_icon(d)
    sheets["player"] = strip(player_frames(), 16, 16)
    sheets["monster"] = strip(monster_frames(), 32, 24)
    sheets["pole"] = strip([pole_frame(False), pole_frame(True)], 20, 40)
    sheets["ball"] = strip([ball_frame()], 7, 8)
    sheets["scatter"] = strip([scatter_frame()], 4, 8)
    sheets["spark"] = strip([spark_frame(LIGHT), spark_frame(WHITE)], 4, 4)
    sheets["fontw"] = font_sheet(WHITE)
    sheets["fontg"] = font_sheet(LIGHT)
    return icons, menu, sheets


def png_name(fname):
    stem, ext = os.path.splitext(fname)
    body, dims = stem.rsplit("_", 1)
    return body, tuple(int(v) for v in dims.split("x"))


# ------------------------------------------------------------ self-checking


def check_sheets(icons, sheets):
    """Assert every frame is exactly the declared rects on an empty canvas."""
    failures = []
    for d in icons:
        sheet = sheets[d["id"]]
        w, h, n = d["w"], d["h"], len(d["frames"])
        if sheet.size != (w * n, h):
            failures.append("%s: sheet %sx%s, want %sx%s" % (d["id"], sheet.size[0], sheet.size[1], w * n, h))
            continue
        spx = sheet.load()
        for fi, blocks in enumerate(d["frames"]):
            # Reference = the declared blk() calls composited in order (a later
            # rect may paint over an earlier one: the white core sits on the
            # slash box, matching the mock's draw order).
            ref = new(w, h)
            for color, dx, dy, bw, bh in blocks:
                rect(ref, dx, dy, bw, bh, color)
            rpx = ref.load()
            for yy in range(h):
                for xx in range(w):
                    got = spx[fi * w + xx, yy]
                    want = rpx[xx, yy]
                    if got != want:
                        failures.append("%s frame %d (%d,%d): got %s want %s" % (d["id"], fi, xx, yy, got, want))
    if failures:
        for f in failures[:20]:
            print("gen-art: PIXEL CHECK FAIL: %s" % f, file=sys.stderr)
        raise SystemExit("gen-art: %d pixel-check failures" % len(failures))


def sheet_filename(body, img, icons):
    """name_WxH.png where WxH is the FRAME size (convert-sprite's tile), not the
    full strip width: the existing pipeline reads the frame dims from the name."""
    if body == "menu_bg":
        return "mh_menu_bg_128x64.png"
    if body == "menu_sel":
        return "mh_menu_sel_28x16.png"
    by_id = {d["id"]: d for d in icons}
    d = by_id.get(body)
    if d is not None:
        return "fx%s_%dx%d.png" % (body, d["w"], d["h"])
    if body == "player":
        return "fxplayer_16x16.png"
    if body == "monster":
        return "fxmonster_32x24.png"
    if body == "pole":
        return "fxpole_20x40.png"
    if body == "ball":
        return "fxball_7x8.png"
    if body == "scatter":
        return "fxscatter_4x8.png"
    if body == "spark":
        return "fxspark_4x4.png"
    if body == "fontw":
        return "fxfontw_4x8.png"
    if body == "fontg":
        return "fxfontg_4x8.png"
    raise SystemExit("gen-art: no filename rule for sheet %s" % body)


def sheet_kind(body):
    """Generated-PNG directory for one sheet body."""
    if body in ("fontw", "fontg"):
        return "fonts"
    if body in ("menu_bg", "menu_sel"):
        return "menu"
    return "blocks"


def check_disk(sheets, icons):
    """Re-read every written PNG and compare it pixel-for-pixel."""
    for body, img in sheets.items():
        directory = os.path.join(ROOT, "images", sheet_kind(body))
        path = os.path.join(directory, sheet_filename(body, img, icons))
        disk = Image.open(path).convert("RGBA")
        if disk.size != img.size:
            raise SystemExit("gen-art: %s on disk %s, want %s" % (path, disk.size, img.size))
        if disk.tobytes() != img.tobytes():
            raise SystemExit("gen-art: %s on disk differs from authored pixels" % path)


def ascii_dump(sheets, icons):
    """Compact ASCII of every authored sheet (evidence for output.md)."""
    by_id = {d["id"]: d for d in icons}
    chars = {CLEAR: ".", BLACK: "K", DARK: "g", LIGHT: "l", WHITE: "W"}
    lines = []
    for body in sorted(sheets):
        img = sheets[body]
        d = by_id.get(body, {})
        w, h, n = d.get("w", img.size[0]), d.get("h", img.size[1]), len(d.get("frames", [])) or 1
        lines.append("%s  frame %dx%d  frames %d  size %dx%d" %
                     (body, w, h, img.size[0] // w, img.size[0], img.size[1]))
        px = img.load()
        for yy in range(img.size[1]):
            row = "".join(chars[px[xx, yy]] for xx in range(img.size[0]))
            if len(row) > 100:
                row = row[:100] + "..."
            lines.append("  " + row)
        lines.append("")
    return "\n".join(lines)


# ------------------------------------------------------- generated dims header


def emit_dims_header(dims, icons, path):
    d = attack_boxes(dims)
    by_id = {i["id"]: i for i in icons}
    L = []
    L.append("// Generated by tools/gen-art.py from tools/fxdump.cpp (make gen).")
    L.append("// Do not edit: tuning hw/hh/reach in src/core/game.hpp changes this file,")
    L.append("// and tst/art_dims_test.hpp fails until `make gen` regenerates it.")
    L.append("#pragma once")
    L.append("")
    L.append("#include <stdint.h>")
    L.append("")
    L.append("namespace art_dims {")
    L.append("")
    for weapon in dims.weapons:
        name = weapon.name
        for i, atk in enumerate(weapon.attacks):
            L.append("constexpr int16_t %s_atk%d_hw = %d;" % (name, i, atk.hw))
            L.append("constexpr int16_t %s_atk%d_hh = %d;" % (name, i, atk.hh))
            L.append("constexpr int16_t %s_atk%d_reach = %d;" % (name, i, atk.reach))
        sp = weapon.special
        L.append("constexpr int16_t %s_special_hw = %d;" % (name, sp.hw))
        L.append("constexpr int16_t %s_special_hh = %d;" % (name, sp.hh))
        L.append("constexpr int16_t %s_special_reach = %d;" % (name, sp.reach))
        # Branch attacks: stance branches carry an all-zero box (id 0).
        for i, br in enumerate(weapon.branches):
            L.append("constexpr int16_t %s_branch%d_hw = %d;" % (name, i, br.hw))
            L.append("constexpr int16_t %s_branch%d_hh = %d;" % (name, i, br.hh))
            L.append("constexpr int16_t %s_branch%d_reach = %d;" % (name, i, br.reach))
    L.append("")
    lunge = dims.monsterAttacks.lunge
    sweep = dims.monsterAttacks.sweep
    L.append("constexpr int16_t monster_lunge_hw = %d;" % lunge.hw)
    L.append("constexpr int16_t monster_lunge_hh = %d;" % lunge.hh)
    L.append("constexpr int16_t monster_lunge_reach = %d;" % lunge.reach)
    L.append("constexpr int16_t monster_sweep_hw = %d;" % sweep.hw)
    L.append("constexpr int16_t monster_sweep_hh = %d;" % sweep.hh)
    L.append("constexpr int16_t monster_sweep_reach = %d;" % sweep.reach)
    L.append("constexpr int16_t monster_w = %d;" % dims.monster.w)
    L.append("constexpr int16_t monster_h = %d;" % dims.monster.h)
    L.append("")
    L.append("constexpr int16_t whirl_orbit_rx = %d;" % dims.whirl.rx)
    L.append("constexpr int16_t whirl_orbit_ry = %d;" % dims.whirl.ry)
    L.append("constexpr int16_t whirl_radius = %d;" % dims.whirl.r)
    L.append("")

    # Per-sheet frame layout. frame_w/frame_h are the uniform sheet frame size;
    # `anchor` is the blk() origin the frame's top-left maps to (see render.hpp).
    L.append("// Sheet frame layout (uniform frame per sheet, left-to-right strip).")
    for d in icons:
        L.append("constexpr uint8_t %s_frame_w = %d;" % (d["id"], d["w"]))
        L.append("constexpr uint8_t %s_frame_h = %d;" % (d["id"], d["h"]))
        L.append("constexpr uint8_t %s_frames = %d;" % (d["id"], len(d["frames"])))
    L.append("")

    # Slash boxes: the rect (box_w x box_h) centred in the 32x32 slash frame
    # with the white 4x4 core on the hit-box centre. The dims test re-derives
    # these from the core accessors.
    L.append("// Sword slash: 32x32 frame, hit box (hw x hh) centred with the 4x4")
    L.append("// white core at slash_core_x/y (the hit-box centre). Frame order: 12x10")
    L.append("// combo, 18x14 combo, 20x16 special, 14x12 step-slash, 28x26 spin-cut.")
    L.append("constexpr uint8_t slash_core_x = %d;" % ((by_id["slash"]["w"] - 4) // 2))
    L.append("constexpr uint8_t slash_core_y = %d;" % ((by_id["slash"]["h"] - 4) // 2))
    L.append("constexpr uint8_t slash_core_size = 4;")
    L.append("constexpr uint8_t slash_riposte_pad = 2;")
    L.append("")

    # Telegraph boxes: hit box centred in the frame (same size as the monster
    # hurt box) with the flash core at its centre. Frame order: lunge windup,
    # lunge attack, sweep windup, sweep attack.
    L.append("// Telegraph: hit box centred in the frame, core at centre. Frames: lunge")
    L.append("// windup, lunge attack, sweep windup, sweep attack.")
    for name, atk in (("lunge", lunge), ("sweep", sweep)):
        L.append("constexpr uint8_t telegraph_%s_x = %d;" % (name, (32 - atk.hw) // 2))
        L.append("constexpr uint8_t telegraph_%s_y = %d;" % (name, (24 - atk.hh) // 2))
    L.append("")

    L.append("// Whirl frames in fxwhirl: 2x2 light orbit dot, 4x4 white ball, 1x1")
    L.append("// light chain dot, 2x2 white stun sparkle.")
    L.append("constexpr uint8_t whirl_dot_frame = 0;")
    L.append("constexpr uint8_t whirl_ball_frame = 1;")
    L.append("constexpr uint8_t whirl_chain_frame = 2;")
    L.append("constexpr uint8_t whirl_stun_frame = 3;")
    L.append("")
    L.append("// Chip frames in fxchip: 3x3 white idle/aim chip, 4x4 white ball.")
    L.append("constexpr uint8_t chip_idle_frame = 0;")
    L.append("constexpr uint8_t chip_ball_frame = 1;")
    L.append("")
    L.append("}   // namespace art_dims")

    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")


# -------------------------------------------------------------------- main


def clean_stale(directory, expected, prefix):
    """Drop generated <prefix>*_WxH.png sheets that are no longer authored, so a
    renamed/removed sheet cannot linger in images/ (the manifest bead finds
    files by name; orphans would be picked up by the converter)."""
    for name in os.listdir(directory):
        if name in expected:
            continue
        if name.startswith(prefix) and name.endswith(".png"):
            os.remove(os.path.join(directory, name))
            print("gen-art: removed stale %s" % os.path.join(directory, name))


def main():
    parser = argparse.ArgumentParser(description="Author monhun-ardu FX sprite sheets.")
    parser.add_argument("--dims", help="fxdump JSON (default: run build/fxdump)")
    parser.add_argument("--dump", action="store_true", help="print ASCII pixel dump")
    args = parser.parse_args()

    if args.dims:
        dims = load_dims(args.dims)
    else:
        import subprocess

        out = subprocess.run([os.path.join(ROOT, "build", "fxdump")], capture_output=True, text=True, check=True)
        dims = Dims(json.loads(out.stdout))

    dirs = {"blocks": os.path.join(ROOT, "images", "blocks"),
            "fonts": os.path.join(ROOT, "images", "fonts"),
            "menu": os.path.join(ROOT, "images", "menu")}
    gen_dir = os.path.join(ROOT, "src", "generated")
    for directory in dirs.values():
        os.makedirs(directory, exist_ok=True)
    os.makedirs(gen_dir, exist_ok=True)

    icons, menu, sheets = render_all(dims)
    defs = icons + menu
    check_sheets(defs, sheets)
    check_menu_identity(sheets)
    check_hud_identity(sheets)

    names = {kind: set() for kind in dirs}
    for body, img in sheets.items():
        names[sheet_kind(body)].add(sheet_filename(body, img, defs))
    clean_stale(dirs["blocks"], names["blocks"], "fx")
    clean_stale(dirs["fonts"], names["fonts"], "fx")
    clean_stale(dirs["menu"], names["menu"], "mh_menu")

    for body, img in sheets.items():
        img.save(os.path.join(dirs[sheet_kind(body)], sheet_filename(body, img, defs)))
    check_disk(sheets, defs)

    emit_dims_header(dims, icons, os.path.join(gen_dir, "art_dims.hpp"))

    n_blocks = len(sheets) - 2 - len(menu)
    print("gen-art: wrote %d block sheets (%d overlay/effect icons) + 2 font sheets + %d menu sheets" %
          (n_blocks, len(icons), len(menu)))
    print("gen-art: pixel check OK (%d sheets, disk-exact; menu matches the font source)" % len(sheets))
    if args.dump:
        print(ascii_dump(sheets, defs))


if __name__ == "__main__":
    main()
