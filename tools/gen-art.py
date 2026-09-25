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

The HUD marker strips (images/blocks/fxhud, bead monhun-ardu-e4a) are authored
from the same GLYPHS table as the font sheets and cross-checked against fxfontw
by check_hud_identity(); the font sheets stay on the MCU-independent side
because the HUD still textPuts the gun reload/shell and train readouts from
them. The opening-menu sheets (images/menu/, beads zza/2u8/4t4) were deleted
with the menu (`hml.2`).

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


def flip(img):
    """The west twin of an east-authored frame: a horizontal mirror."""
    return img.transpose(Image.FLIP_LEFT_RIGHT)


def compose(blocks, w, h):
    """Composite one frame's blk() rects onto an empty w x h canvas."""
    img = new(w, h)
    for color, dx, dy, bw, bh in blocks:
        rect(img, dx, dy, bw, bh, color)
    return img


# Mirror-derived sheet registry (bead monhun-ardu-ryh.2). Keyed by the packed
# PNG symbol ("fx<id>"); value = (east-only source image, packed frame plan). The
# plan lists the shipped frames as (source_frame_index, mirror), so the packer
# (tools/convert-sprite.py, fed images/blocks/layout.json) rebuilds the shipped
# sheet from the one-facing source. Frames that are NOT the mirror of an east
# frame stay authored explicitly and are not registered here: the ravager's
# asymmetric feet (all four west frames are drawn) and the facing-independent
# dead heap (a symmetric frame mirrors to itself; an asymmetric one is reused
# unflipped). See the exception notes on monster_frames() and _beast_sheet().
MIRROR_SOURCES = {}


# Mask-derived part-art anchors (epic monhun-ardu-ryh, bead ryh.3). tools/
# gen-hitboxes.py derives each zone's cell-relative box from the painted mask;
# gen-art crops the breakable-part overlay sheet to that bbox so the hit rect and
# the drawn part stay the same rectangle (the cey invariant). Keyed
# (creature_id, zone_name); absent (art-dump without a regen) falls back to the
# shipped per-sheet default.
PART_BOXES = {}


def load_part_boxes(path):
    """Read build/hitboxes.json into PART_BOXES (best effort)."""
    if not os.path.isfile(path):
        return
    with open(path, encoding="utf-8") as handle:
        doc = json.load(handle)
    for cid, entry in doc.get("creatures", {}).items():
        for name, box in entry.get("zones", {}).items():
            PART_BOXES[(cid, name)] = (box["ox"], box["oy"], box["w"], box["h"])


def _part_box(cid, zone, fallback):
    return PART_BOXES.get((cid, zone), fallback)



def register_mirror(symbol, src_frames, w, h, plan):
    MIRROR_SOURCES[symbol] = (strip(src_frames, w, h), plan)


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
        # Heavy long-tail overlay (bead monhun-ardu-4t4): 24x16 frames matching
        # heavy.json's appendage box, same combatPartArtFrame() order. Height is
        # a multiple of 8 so SpritesU's plus-mask page stride is exact.
        {"id": "tail_heavy", "w": 24, "h": 16, "anchor": "part box top-left",
         "frames": tail_heavy_defs()},
        # HEAVY tail-spin overlay (bead monhun-ardu-nch.1): 24x24 frames, tail
        # rooted at the body centre pointing world W/N/E/S, drawn during the
        # locked tail_spin WINDUP tell. Frame origin is the body centre.
        {"id": "tail_spin", "w": 24, "h": 24, "anchor": "body centre",
         "frames": tail_spin_defs()},
        # HEAVY real spin sheet (bead monhun-ardu-nch.3): the whole longtail
        # silhouette, 8 frames 40x40, rotated 45 deg clockwise per frame about
        # the body centre. Frame 0 is the east idle beast centred in the cell
        # (32x24 padded to 40x40); frame i is frame 0 rotated i*45 deg with
        # nearest-neighbour sampling and the 4-shade palette preserved (no
        # interpolation). Drawn instead of the normal beast sheet during the
        # locked tail_spin attack. Frame origin is the body centre.
        {"id": "tailspin", "w": 40, "h": 40, "anchor": "body centre",
         "frames": tailspin_frames()},
        # Chicken attack sheet (bead monhun-ardu-nch.8): 4 frames 32x24 in the
        # order [peck E, peck W, leap E, leap W]. Drawn instead of the generic
        # BEAST_POSES frame during the chicken's peck/leap WINDUP+ATTACK. Frame
        # origin is the body top-left like the normal 32x24 beast sheet; height
        # 24 is a multiple of 8 so the SpritesU plus-mask page stride is exact.
        {"id": "chickenatk", "w": 32, "h": 24, "anchor": "body top-left",
         "frames": chickenatk_frames()},
        # Bull attack sheet (bead monhun-ardu-nch.10): 4 frames 32x24 in the
        # order [stomp E, stomp W, gore E, gore W]. Drawn instead of the generic
        # BEAST_POSES frame during the bull's stomp/gore WINDUP+ATTACK. Frame
        # origin is the body top-left like the normal 32x24 beast sheet; height
        # 24 is a multiple of 8 so the SpritesU plus-mask page stride is exact.
        {"id": "bullatk", "w": 32, "h": 24, "anchor": "body top-left",
         "frames": bullatk_frames()},
        # Heavy attack sheet (bead monhun-ardu-prg.12): 8 frames 32x24 in ordinal
        # order [bite, bite-windup, tail_spin-windup, tail_slam-windup] x facing.
        # The tell selector (prg.11) indexes this sheet by windup slot: line(1) =
        # bite windup, arc(2) / ring(3) are carried by the fxtailspin sheet for
        # the locked spin/slam (the spin branch wins), so only ordinal 0 (bite
        # release, from the attack order) and ordinal 1 (bite windup) draw. Frame
        # origin is the body top-left; height 24 is a multiple of 8.
        {"id": "heavyatk", "w": 32, "h": 24, "anchor": "body top-left",
         "frames": heavyatk_frames()},
        # Breakable-zone part overlays (bead monhun-ardu-kt7.6). One 4-frame
        # combatPartArtFrame() sheet per breakable demo-roster zone, drawn at the
        # face-relative zone box origin (the same world rect the hit test uses).
        # Frame sizes are the mask-derived zone bbox (head 11x7 / appendage 9x9
        # after ryh.3), height padded up to a multiple of 8 for the SpritesU
        # plus-mask page stride. Frames are authored facing east and mirrored
        # west, exactly like the heavy tail_heavy sheet.
        {"id": "head_chicken", "w": 11, "h": 8, "anchor": "part box top-left",
         "frames": head_chicken_defs()},
        {"id": "legs_chicken", "w": 9, "h": 16, "anchor": "part box top-left",
         "frames": legs_chicken_defs()},
        {"id": "head_bull", "w": 12, "h": 16, "anchor": "part box top-left",
         "frames": head_bull_defs()},
        {"id": "hooves_bull", "w": 20, "h": 16, "anchor": "part box top-left",
         "frames": hooves_bull_defs()},
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


# ---- HEAVY long-tail overlay (bead monhun-ardu-4t4). The demo longtail's
# in-cell tail read as a stub, so the real tail is a 24x16 overlay sheet drawn
# at the appendage zone box (data/creatures/heavy.json {ox:-24, oy:0, w:24,
# h:16}). oy is 0 on purpose: the zone origin rotates with the facing vector
# (combatFacePoint), and a non-zero oy would flip vertically when the beast
# turns around. Height 16 is a multiple of 8 because SpritesU's plus-mask frame
# stride is (h >> 3) pages (the legacy 18x10 fxtail is never drawn). Frame order
# is the combatPartArtFrame() contract: east intact, east broken, west intact,
# west broken; the broken frames keep only the root stub. Only the east pair is
# authored (ryh.2): the packer mirrors each column into the west twin.


# ---- HEAVY tail-spin overlay (bead monhun-ardu-nch.1). The spin attack whips
# the tail 360 around the body; this 24x24 sheet holds one frame per world
# direction (W / N / E / S) with the tail rooted at the frame centre (12,12),
# so render.hpp draws it at body centre - (12,12). Frames are the east tail
# rotated 90 deg about the centre; W turns=2, N turns=1, E turns=0, S turns=3
# (clockwise quarter turns), matching spr::SPIN_WEST/NORTH/EAST/SOUTH.
SPIN_CANON = [
    (LIGHT, 12, 9, 6, 7),    # root segment off the body centre
    (DARK, 13, 14, 5, 2),    # root underside
    (LIGHT, 18, 10, 4, 5),   # mid segment
    (DARK, 18, 13, 4, 2),    # mid underside
    (LIGHT, 22, 11, 2, 3),   # tip stub reaching the frame edge
    (WHITE, 22, 11, 2, 2),   # bright tip cap
]


def _cw_rect(block, size=24):
    """Rotate one rect 90 deg clockwise about the frame centre (size/2)."""
    c, x, y, w, h = block
    return (c, y, size - x - w, h, w)


def _spin_frame(turns):
    blocks = list(SPIN_CANON)
    for _ in range(turns):
        blocks = [_cw_rect(b) for b in blocks]
    return blocks


def tail_spin_defs():
    return [_spin_frame(2), _spin_frame(1), _spin_frame(0), _spin_frame(3)]


def tail_heavy_defs():
    east_intact = [
        (LIGHT, 0, 7, 7, 5),     # tapered tip
        (WHITE, 0, 8, 3, 3),     # bright tip cap
        (LIGHT, 6, 5, 9, 9),     # mid segment
        (DARK, 7, 12, 8, 2),     # mid underside
        (LIGHT, 14, 4, 10, 11),  # root at the body
        (DARK, 15, 13, 9, 2),    # root underside
        (WHITE, 21, 4, 3, 3),    # root top highlight
    ]
    east_broken = [
        (DARK, 17, 5, 7, 9),     # root stub only
        (LIGHT, 20, 4, 4, 2),    # stump highlight
    ]
    return _zone_part_defs("tail_heavy", east_intact, east_broken, 24, 16)


def monster_frames():
    # Ravager (legacy fxmonster sheet). EXCEPTION to one-facing/mirror (ryh.2):
    # its alive frames carry four identical feet rects (x = 3,11,19,27) that are
    # NOT x-mirror-symmetric, so each west frame is deliberately NOT the mirror
    # of its east twin and must stay authored explicitly. Both facings are drawn
    # here; the packer writes the file unchanged (no mirror plan is registered).
    # 0..3 facing east (head right), 4..7 facing west (head left).
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


# ---- demo beast silhouettes (epic monhun-ardu-nch). One 32x24 sheet per demo
# creature so LUNGE/SWEEP/HEAVY read as distinct at 1x; RAVAGER keeps the legacy
# fxmonster sheet (its tail part overlays it). The state shade rules are the
# legacy ones (idle dark body + white head, recover light, windup/hit flash
# white) and the dead heap stays on the same dark/light palette. Shapes are
# authored facing east only (ryh.2): the packer mirrors each column into the
# west twin, so both facings come from one source. The ground shadow row and the
# body/head/eye layers all stay inside the 32x24 cell.
def _beast_frame(draw, dead_draw, body, head, dead=False, dx=0, dy=0):
    img = new(32, 24)
    if dead:
        dead_draw(img)
        return img

    def put(x, y, w, h, color):
        # dx/dy are the animation pose offsets (bob/coil/lunge); the ground
        # shadow stays planted. Poses that push ink past the cell edge are
        # clipped (placeholder poses only).
        xs = x + dx
        ys = y + dy
        px = img.load()
        for yy in range(max(0, ys), min(24, ys + h)):
            for xx in range(max(0, xs), min(32, xs + w)):
                px[xx, yy] = color

    rect(img, 2, 23, 28, 1, BLACK)   # ground shadow
    draw(put, body, head)
    return img


def _beast_tone(body):
    """Highlight/shadow accents for a state body shade, preserving the legacy
    mapping (idle body dark + white head, recover light, flash white): the bulk
    fill stays the state shade, the accents only sculpt it."""
    hi = WHITE if body in (LIGHT, WHITE) else LIGHT
    lo = DARK if body == LIGHT else BLACK
    return hi, lo


def _chicken_east(put, body, head):
    # Tall wader (epic monhun-ardu-nch, bead 76y): the body is raised into the
    # upper half of the 32x24 cell on two long legs that reach the ground rows,
    # so the silhouette reads as a chicken the hunter can walk under. Layered
    # tail plumes on the left, a rounded raised body with a scalloped wing, a
    # comb on a short neck, beak/wattle, and two bird legs with a knee bump,
    # shank and splayed feet. Legs run rows 13..22; the body bulk sits rows 4..13
    # (vs the old rows 8..20), leaving the leg gap and outer clearance readable
    # at 1x.
    hi, lo = _beast_tone(body)

    put(0, 3, 6, 3, body)             # tail plume, upper
    put(0, 6, 8, 4, body)             # tail plume, middle
    put(1, 10, 6, 3, body)            # tail plume, lower
    put(0, 3, 3, 1, hi)               # upper plume tip
    put(0, 6, 3, 1, hi)               # middle plume tip
    put(1, 10, 3, 1, hi)              # lower plume tip

    put(6, 4, 15, 10, body)           # raised body
    put(15, 5, 5, 7, hi)              # chest highlight
    put(7, 12, 12, 2, lo)             # belly shadow
    put(7, 6, 10, 6, body)            # wing panel
    put(7, 6, 9, 1, hi)               # wing top
    put(8, 9, 9, 1, lo)               # wing feather row 1
    put(8, 11, 8, 1, lo)              # wing feather row 2

    put(17, 1, 5, 4, body)            # neck
    put(18, 0, 11, 6, head)           # head
    put(20, 0, 3, 1, head)            # comb front
    put(24, 0, 2, 1, head)            # comb back
    put(28, 3, 4, 3, lo)              # beak
    put(28, 6, 2, 2, lo)              # wattle
    put(23, 2, 2, 2, BLACK)           # eye

    # Legs in DARK, not the tone `lo`: for the idle/flash bodies lo is BLACK,
    # the shade-0 eraser, so the legs vanished on the black arena floor. DARK
    # renders on the floor and the 1 px LIGHT shank/foot highlights keep the
    # thin limbs readable at 1x. BLACK stays for the eye/beak/wattle (against
    # the white head) and the ground shadow row.
    put(11, 13, 2, 4, DARK)           # near thigh
    put(10, 16, 4, 2, DARK)           # near knee
    put(11, 18, 2, 3, DARK)           # near shank
    put(11, 18, 1, 3, LIGHT)          # near shank highlight
    put(9, 21, 5, 1, DARK)            # near foot
    put(9, 20, 1, 1, DARK)            # near rear toe
    put(10, 21, 2, 1, LIGHT)          # near foot front highlight

    put(16, 13, 2, 4, DARK)           # far thigh
    put(15, 16, 4, 2, DARK)           # far knee
    put(16, 18, 2, 3, DARK)           # far shank
    put(16, 18, 1, 3, LIGHT)          # far shank highlight
    put(15, 21, 5, 1, DARK)           # far foot
    put(19, 20, 1, 1, DARK)           # far rear toe
    put(18, 21, 2, 1, LIGHT)          # far foot front highlight


def _bull_east(put, body, head):
    # Barrel-chested body with muscle highlights and a dark underside, four
    # hoofed legs, a hanging tail, low head with a muzzle, and two horns
    # stepping up and inward so the curve reads at 1x.
    hi, lo = _beast_tone(body)

    put(1, 7, 2, 9, body)             # tail
    put(0, 5, 3, 2, lo)               # tail tuft
    put(3, 8, 22, 12, body)           # barrel body
    put(6, 6, 12, 3, body)            # shoulder hump
    put(4, 8, 15, 2, hi)              # back highlight
    put(5, 11, 9, 3, hi)              # rib highlight
    put(4, 18, 19, 2, lo)             # belly shadow

    put(21, 10, 10, 9, head)          # low head
    put(22, 9, 2, 2, head)            # ear
    put(28, 14, 4, 4, lo)             # muzzle
    put(28, 13, 4, 1, hi)             # muzzle bridge
    put(25, 12, 2, 2, BLACK)          # eye

    put(22, 8, 2, 3, head)            # near horn base
    put(23, 5, 2, 3, head)            # near horn mid
    put(24, 4, 3, 2, head)            # near horn tip
    put(29, 8, 2, 3, head)            # far horn base
    put(29, 5, 2, 3, head)            # far horn mid
    put(28, 4, 3, 2, head)            # far horn tip

    for lx in (5, 10, 18, 23):
        put(lx, 18, 3, 4, body)       # leg
        put(lx, 20, 1, 2, hi)         # shank highlight
        put(lx - 1, 22, 4, 1, BLACK)  # hoof


def _longtail_east(put, body, head):
    # Thick segmented tail filling the left of the cell with ridge spikes, a
    # forward-leaning bulk (high chest, heavy belly), and a jawed head pushed
    # forward of the shoulders.
    hi, lo = _beast_tone(body)

    put(0, 7, 8, 11, body)            # thick tail base
    put(3, 8, 1, 9, lo)               # tail segment 1
    put(5, 8, 1, 9, lo)               # tail segment 2
    put(1, 5, 2, 2, body)             # ridge spike
    put(3, 4, 2, 2, body)             # ridge spike
    put(5, 5, 2, 2, body)             # ridge spike
    put(7, 4, 2, 2, body)             # ridge spike
    put(0, 15, 6, 2, lo)              # tail underside

    put(7, 5, 16, 14, body)           # forward-leaning bulk
    put(16, 7, 5, 10, hi)             # chest highlight
    put(8, 17, 13, 2, lo)             # belly shadow

    put(19, 3, 11, 10, head)          # head forward
    put(28, 8, 4, 4, head)            # snout
    put(27, 11, 4, 2, lo)             # jaw
    put(30, 9, 1, 1, BLACK)           # nostril
    put(24, 6, 2, 2, BLACK)           # eye

    for lx in (9, 14, 19, 24):
        put(lx, 19, 3, 3, body)       # leg
        put(lx - 1, 22, 4, 1, BLACK)  # hoof


def _dead_heap(img):
    rect(img, 0, 16, 32, 8, DARK)
    rect(img, 12, 14, 8, 4, LIGHT)


def _chicken_dead(img):
    rect(img, 8, 17, 16, 7, DARK)
    rect(img, 12, 15, 7, 3, LIGHT)
    rect(img, 21, 14, 3, 2, LIGHT)   # crest stub


def _bull_dead(img):
    rect(img, 3, 17, 26, 7, DARK)
    rect(img, 10, 15, 12, 3, LIGHT)
    rect(img, 4, 15, 3, 2, LIGHT)    # horn
    rect(img, 25, 15, 3, 2, LIGHT)   # horn


def _longtail_dead(img):
    rect(img, 9, 17, 18, 7, DARK)
    rect(img, 13, 15, 9, 3, LIGHT)
    rect(img, 1, 19, 8, 5, DARK)     # collapsed tail


# Animated demo-beast layout (epic monhun-ardu-nch): per facing, seven frames
# in this order. The pose offsets are the placeholder animation: idle bob,
# windup coil, attack lunge. art_dims emits the matching base indices and the
# render frame map consumes them (BEAST_* below).
BEAST_POSES = (
    ("idle0", DARK, WHITE, 0, 0),      # rest
    ("idle1", DARK, WHITE, 0, 1),      # bob down 1
    ("windup", DARK, WHITE, -1, 1),    # coil back + down
    ("attack", DARK, WHITE, 1, -1),    # lunge forward + up
    ("recover", LIGHT, LIGHT, 0, 0),
    ("flash", WHITE, WHITE, 0, 0),
    ("dead", DARK, WHITE, 0, 0),       # dead heap (pose ignored)
)
BEAST_FRAMES = len(BEAST_POSES)
BEAST_STRIDE = BEAST_FRAMES   # west frames start at +stride


def _beast_east(draw, dead_draw):
    """The seven east frames (head right) in BEAST_POSES order."""
    return [_beast_frame(draw, dead_draw, body, head, dead=(name == "dead"), dx=dx, dy=dy)
            for name, body, head, dx, dy in BEAST_POSES]


def _beast_sheet(draw, dead_draw):
    """East frames + packed frame plan (ryh.2).

    The shipped sheet is 14 frames: BEAST_POSES east (0..6), then the mirrored
    west twins of poses 0..5, then the dead heap. The dead heap is
    facing-independent (drawn once, identical for both facings) so it is NOT the
    mirror of the east dead frame: it is reused unflipped (plan source index 6,
    mirror False). Returns (source frames, shipped frames, plan); the caller
    writes the 7-frame one-facing source and registers the plan for the packer.
    """
    east = _beast_east(draw, dead_draw)
    ship = list(east) + [flip(east[i]) for i in range(6)] + [east[6]]
    plan = [(i, False) for i in range(7)] + [(i, True) for i in range(6)] + [(6, False)]
    return east, ship, plan


# ---- Chicken attack sheet (bead monhun-ardu-nch.8; prg.12 windup frames). The
# whole chicken is drawn from this 6-frame 32x24 sheet during the peck/leap/
# wing_beat windup+attack instead of the generic BEAST_POSES coil/lunge frame, so
# every attack reads as bespoke art. Frame order is [peck E, peck W, leap E,
# leap W, wing E, wing W]: peck and leap LOWER the head into the target (peck to
# rows 4..9 with the body leaned 1 px, leap to rows 3..8 off the raised body);
# wing_beat crouches and sweeps the near wing out behind the body as a wide
# horizontal panel while the head stays level, so the arc sweep reads apart from
# the two jabs. Frames are authored east only (ryh.2) and the west twins are
# produced by the packer's mirror, exactly like the idle/windup/attack frames.
# Windup and attack share
# the pose: the overlay has no windup-flash frame, so the telegraph window +
# tell carry the timing (same trade as fxtailspin).
def _chicken_attack_east(put, body, head, mode):
    hi, lo = _beast_tone(body)

    if mode == 2:
        # wing_beat (arc): crouched body, near wing swept out behind as a wide
        # horizontal panel with feather rows, head level (rows 3..8) rather than
        # lowered, plumes fanned over the wing.
        put(0, 2, 6, 3, body)         # upper plume fanned
        put(0, 5, 9, 3, body)         # middle plume fanned
        put(1, 8, 8, 3, body)         # lower plume fanned
        put(0, 2, 3, 1, hi)
        put(0, 5, 3, 1, hi)
        put(1, 8, 3, 1, hi)

        put(7, 7, 14, 9, body)        # crouched body
        put(15, 8, 5, 6, hi)          # chest highlight
        put(8, 14, 12, 2, lo)         # belly shadow

        put(0, 9, 14, 5, body)        # near wing swept out behind
        put(0, 9, 12, 1, hi)          # wing leading edge
        put(1, 11, 12, 1, lo)         # wing feather row 1
        put(2, 13, 10, 1, lo)         # wing feather row 2
        put(12, 8, 6, 3, body)        # wing shoulder

        put(18, 5, 5, 4, body)        # neck level
        put(19, 3, 10, 6, head)       # head level (rows 3..8), not lowered
        put(21, 2, 3, 1, head)        # comb front
        put(25, 2, 2, 1, head)        # comb back
        put(28, 6, 2, 2, lo)          # beak at the level head's front edge
        put(24, 5, 2, 2, BLACK)       # eye

        put(12, 13, 2, 4, DARK)       # near thigh
        put(11, 16, 4, 2, DARK)       # near knee
        put(12, 18, 2, 3, DARK)
        put(12, 18, 1, 3, LIGHT)
        put(10, 21, 5, 1, DARK)
        put(10, 20, 1, 1, DARK)
        put(11, 21, 2, 1, LIGHT)
        put(17, 13, 2, 4, DARK)       # far thigh
        put(16, 16, 4, 2, DARK)
        put(17, 18, 2, 3, DARK)
        put(17, 18, 1, 3, LIGHT)
        put(16, 21, 5, 1, DARK)
        put(20, 20, 1, 1, DARK)
        put(19, 21, 2, 1, LIGHT)
        return

    if mode == 1:
        # Plumes/wing raised with the body (~2 px). The head LOWERS into the
        # charge (head-down posture) instead of holding the idle head high: the
        # neck angles down off the raised body, the head drops to rows 3..8 and
        # the beak sits at the lowered head's front edge -- no beak/wattle wedge
        # is added below the head.
        put(0, 1, 6, 3, body)         # tail plume, upper
        put(0, 4, 8, 4, body)         # tail plume, middle
        put(1, 8, 6, 3, body)         # tail plume, lower
        put(0, 1, 3, 1, hi)
        put(0, 4, 3, 1, hi)
        put(1, 8, 3, 1, hi)

        put(6, 2, 15, 10, body)       # raised body
        put(15, 3, 5, 7, hi)          # chest highlight
        put(7, 10, 12, 2, lo)         # belly shadow
        put(7, 4, 10, 6, body)        # wing panel raised
        put(7, 4, 9, 1, hi)           # wing top
        put(8, 7, 9, 1, lo)           # wing feather row 1
        put(8, 9, 8, 1, lo)           # wing feather row 2

        put(17, 3, 5, 4, body)        # neck angled down into the charge
        put(20, 3, 10, 6, head)       # head lowered (rows 3..8, was 0..5)
        put(22, 2, 3, 1, head)        # comb front
        put(26, 2, 2, 1, head)        # comb back
        put(28, 6, 2, 2, lo)          # beak at the lowered head's front edge
        put(24, 4, 2, 2, BLACK)       # eye

        # Legs folded/tucked: knees up, shanks shortened, feet lifted off the
        # planted y21 row to y18 so the leap reads as airborne.
        put(11, 11, 2, 3, DARK)       # near thigh
        put(10, 13, 4, 2, DARK)       # near knee
        put(11, 15, 2, 2, DARK)       # near shank
        put(11, 15, 1, 2, LIGHT)      # near shank highlight
        put(10, 18, 5, 1, DARK)       # near foot
        put(10, 17, 1, 1, DARK)       # near rear toe
        put(11, 18, 2, 1, LIGHT)      # near foot front highlight
        put(16, 11, 2, 3, DARK)       # far thigh
        put(15, 13, 4, 2, DARK)       # far knee
        put(16, 15, 2, 2, DARK)       # far shank
        put(16, 15, 1, 2, LIGHT)      # far shank highlight
        put(15, 18, 5, 1, DARK)       # far foot
        put(19, 17, 1, 1, DARK)       # far rear toe
        put(18, 18, 2, 1, LIGHT)      # far foot front highlight
        return

    # Peck: body leans 1 px forward, the neck angles down and the head lowers
    # onto the target (rows 4..9) instead of holding the idle head high while
    # the beak/wattle are driven below it -- the beak sits at the lowered head's
    # front edge, so no separate beak wedge is added.
    put(0, 3, 6, 3, body)             # tail plume, upper
    put(0, 6, 8, 4, body)             # tail plume, middle
    put(1, 10, 6, 3, body)            # tail plume, lower
    put(0, 3, 3, 1, hi)
    put(0, 6, 3, 1, hi)
    put(1, 10, 3, 1, hi)

    put(7, 4, 15, 10, body)           # body leaned 1 px
    put(16, 5, 5, 7, hi)              # chest highlight
    put(8, 12, 12, 2, lo)             # belly shadow
    put(8, 6, 10, 6, body)            # wing panel
    put(8, 6, 9, 1, hi)               # wing top
    put(9, 9, 9, 1, lo)               # wing feather row 1
    put(9, 11, 8, 1, lo)              # wing feather row 2

    put(18, 4, 6, 5, body)            # neck angled down to the lowered head
    put(21, 4, 9, 6, head)            # head lowered onto the target (rows 4..9)
    put(23, 3, 3, 1, head)            # comb front
    put(27, 3, 2, 1, head)            # comb back
    put(28, 7, 2, 2, lo)              # beak at the lowered head's front edge
    put(24, 5, 2, 2, BLACK)           # eye

    put(12, 13, 2, 4, DARK)           # near thigh
    put(11, 16, 4, 2, DARK)           # near knee
    put(12, 18, 2, 3, DARK)           # near shank
    put(12, 18, 1, 3, LIGHT)          # near shank highlight
    put(10, 21, 5, 1, DARK)           # near foot
    put(10, 20, 1, 1, DARK)           # near rear toe
    put(11, 21, 2, 1, LIGHT)          # near foot front highlight
    put(17, 13, 2, 4, DARK)           # far thigh
    put(16, 16, 4, 2, DARK)           # far knee
    put(17, 18, 2, 3, DARK)           # far shank
    put(17, 18, 1, 3, LIGHT)          # far shank highlight
    put(16, 21, 5, 1, DARK)           # far foot
    put(20, 20, 1, 1, DARK)           # far rear toe
    put(19, 21, 2, 1, LIGHT)          # far foot front highlight


def _chicken_attack_pose(mode):
    def draw(put, body, head):
        _chicken_attack_east(put, body, head, mode)
    return draw


def _attack_sheet(symbol, poses, dead_draw):
    """Interleaved attack sheet: shipped = [pose0 E, pose0 W, pose1 E, ...].

    Only the east poses are authored (ryh.2); the west frames are the mirrored
    east images, registered for the packer. Returns the shipped block frames.
    """
    src = [_beast_frame(draw, dead_draw, DARK, WHITE) for draw in poses]
    frames = []
    plan = []
    for i, img in enumerate(src):
        frames.append(_image_blocks(img))
        plan.append((i, False))
        frames.append(_image_blocks(flip(img)))
        plan.append((i, True))
    register_mirror(symbol, src, 32, 24, plan)
    return frames


def chickenatk_frames():
    """[peck E, peck W, leap E, leap W, wing E, wing W] as rect-block frame defs,
    so check_sheets/render_icon re-composite each authored pose exactly (the
    tailspin icon pattern). Only the three east poses are authored; the west
    frames are mirrored and packed by convert-sprite."""
    return _attack_sheet("fxchickenatk", [_chicken_attack_pose(m) for m in (0, 1, 2)],
                         _chicken_dead)


# ---- Bull attack sheet (bead monhun-ardu-nch.10; prg.12 windup frames). The
# whole bull is drawn from this 8-frame 32x24 sheet during the stomp/gore/
# rear_kick windup+attack instead of the generic BEAST_POSES coil/lunge frame,
# so every attack reads as bespoke art. The ordinal order is the prg.11 tell-slot
# order the selector indexes: 0 stomp release, 1 gore (line), 2 rear_kick (arc),
# 3 stomp WINDUP (ring; reared ground-slam read). The stomp release raises both
# front hooves tucked back under the chest, pitches the body forward onto the
# planted rear legs and holds the head/horns high; the gore lowers the head,
# drives the horns forward, leans the body 1 px and raises the tail; the
# rear_kick bucks with the hind legs kicked back off the ground; the stomp windup
# rears on the planted hind legs with both front hooves high and spread. Frames
# are authored east only (ryh.2); the packer mirrors the west twins.
def _bull_attack_east(put, body, head, mode):
    hi, lo = _beast_tone(body)

    if mode == 3:
        # Stomp windup (ring): reared on the planted hind legs, both front
        # hooves raised high and spread (the ground-slam area read), head and
        # horns held high. Distinct from the release stomp (hooves tucked back
        # low) and from the gore (head down, horns forward).
        put(1, 8, 2, 8, body)         # hanging tail
        put(0, 6, 3, 2, lo)           # tail tuft

        put(4, 9, 20, 10, body)       # reared body, front raised
        put(14, 4, 11, 5, body)       # raised front chest/shoulder
        put(6, 7, 12, 3, body)        # shoulder hump
        put(4, 9, 15, 2, hi)          # back highlight
        put(5, 12, 9, 3, hi)          # rib highlight
        put(5, 18, 16, 2, lo)         # belly shadow

        put(20, 0, 11, 8, head)       # head high
        put(21, 0, 2, 2, head)        # ear
        put(28, 5, 4, 3, lo)          # muzzle high
        put(28, 4, 4, 1, hi)          # muzzle bridge
        put(25, 3, 2, 2, BLACK)       # eye
        put(22, 0, 2, 2, head)        # near horn base
        put(23, 0, 2, 1, head)        # near horn tip
        put(29, 0, 2, 2, head)        # far horn base
        put(29, 0, 2, 1, head)        # far horn tip

        put(15, 8, 3, 5, body)        # near front thigh raised
        put(13, 5, 4, 3, body)        # near shank up
        put(12, 4, 4, 1, BLACK)       # near raised hoof, spread back
        put(23, 9, 3, 4, body)        # far front thigh
        put(24, 6, 4, 3, body)        # far shank up
        put(25, 5, 4, 1, BLACK)       # far raised hoof, spread forward

        for lx in (5, 10):            # planted rear legs
            put(lx, 18, 3, 4, body)
            put(lx, 20, 1, 2, hi)
            put(lx - 1, 22, 4, 1, BLACK)
        return

    if mode == 2:
        # rear_kick (arc behind): bucking counter-attack. The hindquarters kick
        # back off the ground while the front hooves stay planted forward; the
        # head stays low and forward and the tail whips up. The raised, extended
        # hind legs are the pose signature (stomp/gore keep all hooves planted
        # or tucked).
        put(1, 4, 2, 8, body)         # tail raised
        put(0, 2, 3, 2, lo)           # tail tuft

        put(6, 9, 19, 10, body)       # body pitched, rear high
        put(7, 7, 11, 3, body)        # shoulder hump
        put(7, 9, 14, 2, hi)          # back highlight
        put(8, 12, 8, 3, hi)          # rib highlight
        put(7, 18, 17, 2, lo)         # belly shadow

        put(21, 16, 10, 6, head)      # low forward head
        put(22, 15, 2, 2, head)       # ear
        put(28, 19, 4, 3, lo)         # muzzle
        put(28, 18, 4, 1, hi)         # muzzle bridge
        put(25, 18, 2, 2, BLACK)      # eye
        put(23, 14, 2, 2, head)       # near horn base
        put(26, 14, 3, 2, head)       # near horn forward
        put(28, 13, 3, 2, head)       # near horn tip
        put(27, 12, 2, 2, head)       # far horn base
        put(30, 12, 2, 2, head)       # far horn tip

        for lx in (20, 25):           # front hooves planted forward
            put(lx, 18, 3, 4, body)
            put(lx, 20, 1, 2, hi)
            put(lx - 1, 22, 4, 1, BLACK)

        put(4, 13, 4, 3, body)        # near hind thigh raised
        put(1, 15, 5, 2, body)        # near shank extended back
        put(0, 14, 3, 1, BLACK)       # near raised hoof
        put(9, 12, 4, 3, body)        # far hind thigh raised
        put(6, 14, 5, 2, body)        # far shank extended back
        put(5, 13, 3, 1, BLACK)       # far raised hoof
        return

    if mode == 1:
        # Tail raised (base rows 7..15 -> 2..10); body leaned 1 px forward;
        # head lowered and horns driven forward along the facing edge.
        put(1, 2, 2, 9, body)         # tail raised
        put(0, 0, 3, 2, lo)           # tail tuft raised

        put(4, 8, 22, 12, body)       # barrel body leaned 1 px forward
        put(7, 6, 12, 3, body)        # shoulder hump
        put(5, 8, 15, 2, hi)          # back highlight
        put(6, 11, 9, 3, hi)          # rib highlight
        put(5, 18, 19, 2, lo)         # belly shadow

        put(21, 13, 10, 7, head)      # lowered head
        put(22, 12, 2, 2, head)       # ear
        put(28, 17, 4, 3, lo)         # muzzle driven low
        put(28, 16, 4, 1, hi)         # muzzle bridge
        put(25, 15, 2, 2, BLACK)      # eye

        put(23, 12, 2, 2, head)       # near horn base
        put(26, 12, 3, 2, head)       # near horn mid, forward
        put(29, 12, 3, 2, head)       # near horn tip, forward
        put(27, 10, 2, 2, head)       # far horn base
        put(30, 10, 2, 2, head)       # far horn tip, forward

        for lx in (5, 10, 18, 23):
            put(lx, 18, 3, 4, body)   # planted leg
            put(lx, 20, 1, 2, hi)     # shank highlight
            put(lx - 1, 22, 4, 1, BLACK)   # hoof
        return

    # Stomp: rear legs planted, front hooves raised and tucked back, body pitched
    # forward (rear-low barrel + raised front shoulder), head/horns held high.
    put(1, 7, 2, 9, body)             # hanging tail
    put(0, 5, 3, 2, lo)               # tail tuft

    put(3, 9, 22, 11, body)           # barrel body pitched, rear low
    put(15, 6, 10, 4, body)           # raised front shoulder
    put(6, 7, 12, 3, body)            # shoulder hump
    put(4, 9, 15, 2, hi)              # back highlight
    put(5, 12, 9, 3, hi)              # rib highlight
    put(4, 19, 19, 2, lo)             # belly shadow

    put(21, 3, 10, 8, head)           # high head
    put(22, 2, 2, 2, head)            # ear
    put(28, 7, 4, 3, lo)              # muzzle high
    put(28, 6, 4, 1, hi)              # muzzle bridge
    put(25, 5, 2, 2, BLACK)           # eye

    put(22, 1, 2, 2, head)            # near horn base
    put(23, 0, 2, 1, head)            # near horn tip
    put(29, 1, 2, 2, head)            # far horn base
    put(29, 0, 2, 1, head)            # far horn tip

    for lx in (5, 10):
        put(lx, 18, 3, 4, body)       # planted rear leg
        put(lx, 20, 1, 2, hi)         # shank highlight
        put(lx - 1, 22, 4, 1, BLACK)  # rear hoof

    put(18, 16, 3, 4, body)           # near front thigh
    put(15, 15, 4, 3, body)           # near shank folded back
    put(14, 14, 4, 1, BLACK)          # near raised hoof
    put(23, 16, 3, 4, body)           # far front thigh
    put(20, 15, 4, 3, body)           # far shank folded back
    put(19, 14, 4, 1, BLACK)          # far raised hoof


def _bull_attack_pose(mode):
    def draw(put, body, head):
        _bull_attack_east(put, body, head, mode)
    return draw


def bullatk_frames():
    """[stomp E, stomp W, gore E, gore W, rear_kick E/W, stomp_windup E/W] as
    rect-block frame defs, so check_sheets/render_icon re-composite each authored
    pose exactly (the tailspin/chickenatk icon pattern). Only the east poses are
    authored; the west frames are mirrored and packed by convert-sprite."""
    return _attack_sheet("fxbullatk", [_bull_attack_pose(m) for m in (0, 1, 2, 3)],
                         _bull_dead)


# ---- Heavy attack sheet (bead monhun-ardu-prg.12). The longtail is drawn from
# this 8-frame 32x24 sheet for the non-locked attacks, indexed by the prg.11 tell
# slot: ordinal 0 bite release (from the attack order), 1 bite windup (line),
# 2 tail_spin windup (arc), 3 tail_slam windup (ring). The locked tail_spin/
# tail_slam branch wins in drawMonster, so only ordinals 0/1 draw; 2/3 author the
# remaining slots so the selector can never leave the sheet. Frames are authored
# east only (ryh.2); the packer mirrors the west twins.
def _heavy_attack_east(put, body, head, mode):
    hi, lo = _beast_tone(body)

    if mode == 3:
        # tail_slam windup: tail raised high overhead (the slam read), body
        # braced, head forward and level.
        put(0, 0, 8, 6, body)         # tail raised overhead
        put(0, 0, 4, 1, hi)           # tail top highlight
        put(6, 5, 3, 3, body)         # tail root shoulder
        put(8, 7, 15, 13, body)       # braced bulk
        put(16, 9, 5, 9, hi)          # chest highlight
        put(9, 18, 13, 2, lo)         # belly shadow
        put(19, 4, 11, 9, head)       # head forward/level
        put(28, 8, 4, 4, head)        # snout
        put(27, 12, 4, 2, lo)         # jaw
        put(24, 7, 2, 2, BLACK)       # eye
        for lx in (10, 15, 20, 25):
            put(lx, 19, 3, 3, body)
            put(lx - 1, 22, 4, 1, BLACK)
        return

    if mode == 2:
        # tail_spin windup: tail whipped up-left, body crouched, head turned up.
        put(0, 1, 7, 9, body)         # tail raised up-left
        put(1, 1, 1, 8, lo)           # tail segments
        put(5, 0, 2, 2, body)         # ridge spike up
        put(2, 0, 2, 2, body)         # ridge spike up
        put(9, 6, 14, 14, body)       # crouched bulk
        put(16, 8, 5, 10, hi)         # chest highlight
        put(10, 17, 12, 2, lo)        # belly shadow
        put(18, 2, 12, 9, head)       # head turned up
        put(28, 5, 4, 4, head)        # snout
        put(27, 9, 4, 2, lo)          # jaw
        put(24, 4, 2, 2, BLACK)       # eye
        for lx in (11, 16, 21, 26):
            put(lx, 19, 3, 3, body)
            put(lx - 1, 22, 4, 1, BLACK)
        return

    if mode == 1:
        # bite windup (line): head/neck drawn back and high, snout up, jaw open
        # below it; the body coils back onto the rear legs. The raised head is
        # the pose signature (release thrusts the head forward and low).
        put(0, 6, 8, 11, body)        # tail braced/raised
        put(3, 7, 1, 9, lo)           # tail segment
        put(1, 4, 2, 2, body)         # ridge spike
        put(3, 3, 2, 2, body)         # ridge spike
        put(0, 15, 6, 2, lo)          # tail underside
        put(7, 6, 15, 14, body)       # coiled bulk (weight back)
        put(15, 8, 5, 10, hi)         # chest highlight
        put(8, 17, 13, 2, lo)         # belly shadow
        put(18, 1, 12, 10, head)      # head drawn back and high
        put(28, 4, 4, 4, head)        # snout up
        put(27, 8, 4, 2, lo)          # jaw open
        put(30, 5, 1, 1, BLACK)       # nostril
        put(23, 3, 2, 2, BLACK)       # eye
        for lx in (9, 14, 19, 24):
            put(lx, 19, 3, 3, body)
            put(lx - 1, 22, 4, 1, BLACK)
        return

    # mode 0 bite release: head/neck thrust forward and low, jaw open, tail
    # braced counter to the lunge.
    put(0, 8, 8, 10, body)            # tail braced low
    put(3, 9, 1, 8, lo)               # tail segment
    put(6, 9, 2, 2, body)             # ridge spike
    put(0, 15, 6, 2, lo)              # tail underside
    put(7, 8, 14, 13, body)           # bulk leaning forward
    put(15, 10, 5, 9, hi)             # chest highlight
    put(8, 19, 12, 2, lo)             # belly shadow
    put(19, 7, 13, 9, head)           # head thrust forward
    put(28, 11, 4, 4, head)           # snout
    put(27, 14, 4, 2, lo)             # jaw open
    put(30, 12, 1, 1, BLACK)          # nostril
    put(24, 9, 2, 2, BLACK)           # eye
    for lx in (9, 14, 19, 24):
        put(lx, 19, 3, 3, body)
        put(lx - 1, 22, 4, 1, BLACK)


def _heavy_attack_pose(mode):
    def draw(put, body, head):
        _heavy_attack_east(put, body, head, mode)
    return draw


def heavyatk_frames():
    """[bite E, bite W, bite_windup E/W, spin_windup E/W, slam_windup E/W] as
    rect-block frame defs (the tailspin/chickenatk icon pattern). Only the east
    poses are authored; the west frames are mirrored and packed by
    convert-sprite."""
    return _attack_sheet("fxheavyatk", [_heavy_attack_pose(m) for m in (0, 1, 2, 3)],
                         _longtail_dead)


# ---- Breakable-zone part overlays (bead monhun-ardu-kt7.6). Same treatment as
# the heavy long-tail overlay (4t4): every breakable demo-roster zone draws its
# visible part from a 4-frame combatPartArtFrame() sheet at the face-relative
# zone box origin, so the art lands on the exact rect the hit test uses. Frame
# order is east intact / east broken / west intact / west broken; west frames are
# the exact horizontal mirror of east. Intact frames repaint the baked part;
# broken frames first erase the baked part with shade-0 (BLACK) pixels on all
# three planes and then draw the damaged variant, so the overlay replaces the
# baked art everywhere the frame covers. Art is authored in frame-local
# coordinates (part cell pixel - zone box origin), clipped to the frame: the
# muzzle / lower head outside a frame stays baked and visible. Every height is a
# multiple of 8 so SpritesU's plus-mask page stride is exact.
def _zone_part_defs(id_, east_intact, east_broken, w, h):
    """East intact/broken part frames; the packer mirrors the west twin.

    Returns the shipped frame block lists (combatPartArtFrame order: east intact,
    east broken, west intact, west broken). The west frames are produced by
    mirroring the east images (not drawn twice), and the east pair + plan are
    registered so the one-facing source PNG is written and packer-fed. Every
    height is a multiple of 8 so SpritesU's plus-mask page stride is exact.
    """
    src = [compose(east_intact, w, h), compose(east_broken, w, h)]
    register_mirror("fx" + id_, src, w, h,
                    [(0, False), (1, False), (0, True), (1, True)])
    return [_image_blocks(src[0]), _image_blocks(src[1]),
            _image_blocks(flip(src[0])), _image_blocks(flip(src[1]))]


def _crop_blocks(blocks, box):
    """Crop cell-absolute part blocks to a zone box (origin - box origin).

    Frame size is box.w x pad8(box.h); the extra rows stay transparent so the
    SpritesU plus-mask page stride is exact. Blocks outside the box are clipped.
    """
    w, h = box[2], ((box[3] + 7) // 8) * 8
    out = []
    for color, x, y, bw, bh in blocks:
        lx, ly = x - box[0], y - box[1]
        x0, y0 = max(0, lx), max(0, ly)
        x1, y1 = min(w, lx + bw), min(h, ly + bh)
        if x1 > x0 and y1 > y0:
            out.append((color, x0, y0, x1 - x0, y1 - y0))
    return out, w, h


def _zone_part_defs_crop(id_, east_intact, east_broken, box):
    """Like _zone_part_defs but the authored blocks are cell-absolute and the
    overlay is cropped to the mask-derived zone bbox (bead ryh.3)."""
    intact, w, h = _crop_blocks(east_intact, box)
    broken, _, _ = _crop_blocks(east_broken, box)
    return _zone_part_defs(id_, intact, broken, w, h)


def head_chicken_defs():
    # lunge.json head box (18, 0, 11, 7): authored cell-absolute and cropped to
    # the mask-derived bbox (bead ryh.3). The baked white head (rows 0..5) with
    # its black eye, beak and wattle. Broken = the head erased and replaced by a
    # torn dark neck stump at the body end.
    east_intact = [
        (WHITE, 18, 0, 11, 6),   # head
        (BLACK, 23, 2, 2, 2),    # eye
        (BLACK, 28, 3, 1, 3),    # beak (cell x28 edge)
        (BLACK, 28, 6, 1, 1),    # wattle
    ]
    east_broken = [
        (BLACK, 18, 0, 11, 6),   # erase the baked head
        (DARK, 18, 1, 4, 4),     # torn neck stump
        (LIGHT, 18, 1, 3, 1),    # stump highlight
        (BLACK, 21, 0, 2, 2),    # wound notch
    ]
    return _zone_part_defs_crop("head_chicken", east_intact, east_broken,
                                _part_box("lunge", "head", (18, 0, 11, 7)))


def legs_chicken_defs():
    # lunge.json appendage box, mask-derived (9, 13, 9, 9) after ryh.3. Authored
    # cell-absolute and cropped to the bbox: the baked DARK legs with LIGHT
    # shank/foot highlights; the far leg's outer toe is clipped (frame width 9).
    # Broken = the legs sheared at the thigh, leaving short dark stumps.
    box = _part_box("lunge", "appendage", (9, 13, 9, 9))
    east_intact = [
        (DARK, 11, 13, 2, 4),    # near thigh
        (DARK, 10, 16, 4, 2),    # near knee
        (DARK, 11, 18, 2, 3),    # near shank
        (LIGHT, 11, 18, 1, 3),   # near shank highlight
        (DARK, 9, 20, 1, 1),     # near rear toe
        (DARK, 9, 21, 5, 1),     # near foot
        (LIGHT, 10, 21, 2, 1),   # near foot front highlight
        (DARK, 16, 13, 2, 4),    # far thigh
        (DARK, 15, 16, 3, 2),    # far knee (outer column clipped)
        (DARK, 16, 18, 2, 3),    # far shank
        (LIGHT, 16, 18, 1, 3),   # far shank highlight
        (DARK, 15, 21, 3, 1),    # far foot (outer columns clipped)
    ]
    east_broken = [
        (BLACK, 11, 13, 2, 4),   # erase the legs (exact baked rects, so the
        (BLACK, 10, 16, 4, 2),   # body between them is untouched)
        (BLACK, 11, 18, 2, 3),
        (BLACK, 9, 20, 1, 1),
        (BLACK, 9, 21, 5, 1),
        (BLACK, 16, 13, 2, 4),
        (BLACK, 15, 16, 3, 2),
        (BLACK, 16, 18, 2, 3),
        (BLACK, 15, 21, 3, 1),
        (DARK, 11, 13, 2, 2),    # near thigh stump
        (LIGHT, 11, 13, 1, 1),   # stump highlight
        (DARK, 16, 13, 2, 2),    # far thigh stump
        (LIGHT, 16, 13, 1, 1),   # stump highlight
        (BLACK, 10, 15, 7, 1),   # torn lower edge
    ]
    return _zone_part_defs_crop("legs_chicken", east_intact, east_broken, box)


def head_bull_defs():
    # sweep.json head box (17, -4, 12, 10): frame-local (0,0) = cell (17, -4).
    # The baked white horns/ear and the head top land in frame rows 8..15; the
    # muzzle/eye sit below the 16-row frame and stay baked. Broken = the horns
    # erased and snapped back to short dark stumps with the head top repainted.
    east_intact = [
        (WHITE, 4, 14, 8, 2),    # head top (cell y10..11)
        (WHITE, 5, 13, 2, 2),    # ear
        (WHITE, 5, 12, 2, 3),    # near horn base
        (WHITE, 6, 9, 2, 3),     # near horn mid
        (WHITE, 7, 8, 3, 2),     # near horn tip
        (WHITE, 11, 8, 1, 2),    # far horn tip (outer columns clipped)
    ]
    east_broken = [
        (BLACK, 4, 8, 8, 6),     # erase the horn band
        (WHITE, 4, 14, 8, 2),    # head top stays
        (WHITE, 5, 13, 2, 2),    # ear stays
        (DARK, 5, 11, 2, 3),     # near horn stump
        (LIGHT, 5, 11, 1, 1),    # stump highlight
        (DARK, 8, 11, 2, 3),     # far horn stump
        (BLACK, 6, 9, 2, 2),     # snapped gap
    ]
    return _zone_part_defs("head_bull", east_intact, east_broken, 12, 16)


def hooves_bull_defs():
    # sweep.json appendage box (4, 12, 20, 10): frame-local (0,0) = cell (4, 12).
    # The baked DARK legs with LIGHT shank highlights and BLACK hooves land in
    # frame rows 6..10; the fourth leg's outer columns are clipped. Broken = the
    # legs erased and cut to short stumps with no hooves.
    east_intact = [
        (DARK, 1, 6, 3, 4),      # leg 1
        (LIGHT, 1, 8, 1, 2),     # shank highlight
        (BLACK, 0, 10, 4, 1),    # hoof
        (DARK, 6, 6, 3, 4),      # leg 2
        (LIGHT, 6, 8, 1, 2),
        (BLACK, 5, 10, 4, 1),
        (DARK, 14, 6, 3, 4),     # leg 3
        (LIGHT, 14, 8, 1, 2),
        (BLACK, 13, 10, 4, 1),
        (DARK, 19, 6, 1, 4),     # leg 4 (outer columns clipped)
        (LIGHT, 19, 8, 1, 2),
        (BLACK, 18, 10, 2, 1),
    ]
    east_broken = [
        (BLACK, 1, 6, 3, 4),     # erase the legs (exact baked rects)
        (BLACK, 6, 6, 3, 4),
        (BLACK, 14, 6, 3, 4),
        (BLACK, 19, 6, 1, 4),
        (BLACK, 0, 10, 4, 1),    # erase the hooves
        (BLACK, 5, 10, 4, 1),
        (BLACK, 13, 10, 4, 1),
        (BLACK, 18, 10, 2, 1),
        (DARK, 1, 6, 3, 2),      # leg stumps (no hooves)
        (DARK, 6, 6, 3, 2),
        (DARK, 14, 6, 3, 2),
        (DARK, 19, 6, 1, 2),
        (LIGHT, 1, 8, 1, 1),     # stump highlights
        (LIGHT, 6, 8, 1, 1),
        (LIGHT, 14, 8, 1, 1),
    ]
    return _zone_part_defs("hooves_bull", east_intact, east_broken, 20, 16)


# ---- HEAVY real spin sheet (bead monhun-ardu-nch.3). The whole longtail
# silhouette rotates a full revolution during the locked tail_spin attack, so
# the windup tell (fxtail_spin) is joined by this 8-frame 40x40 body sheet drawn
# in its place. Frame 0 is the east idle beast (_beast_east()[0], 32x24)
# centred in the 40x40 plus-mask cell at (4,8); the body cell centre (16,12)
# lands on the spin cell centre (20,20). Frame i is frame 0 rotated i*45 deg
# clockwise about that centre. The whole authored cell rotates, shadow
# included: the owner wants "the whole creature should rotate", and a rotating
# ground shadow reads as the creature turning rather than a planted foot. The
# rotation is exact 1/256 fixed point with nearest-neighbour sampling (no
# interpolation), so every output pixel is one of the four authored shades.
#
# 45-deg CW step (cos, sin) in 1/256 units.
_ROT45 = (
    (256, 0), (181, 181), (0, 256), (-181, 181),
    (-256, 0), (-181, -181), (0, -256), (181, -181),
)


def _rotate_cw(img, step):
    """Rotate a square RGBA image about its centre by step*45 deg clockwise.

    Inverse-maps each destination pixel through R(-theta) in doubled integer
    coordinates, rounds to the nearest source pixel and copies the authored
    RGBA value (so shades never blend). Source pixels outside the image stay
    transparent.
    """
    w, h = img.size
    if w != h:
        raise SystemExit("gen-art: _rotate_cw wants a square image, got %dx%d" % (w, h))
    src = img.load()
    out = new(w, h)
    dst = out.load()
    c, s = _ROT45[step & 7]
    last = w - 1
    for y in range(h):
        vy2 = 2 * y - last
        for x in range(w):
            vx2 = 2 * x - last
            # 512 * rotated source coordinate (centre at (last/2, last/2)).
            sx2 = c * vx2 + s * vy2
            sy2 = -s * vx2 + c * vy2
            ix = (last * 256 + sx2 + 256) >> 9
            iy = (last * 256 + sy2 + 256) >> 9
            if 0 <= ix < w and 0 <= iy < h:
                col = src[ix, iy]
                if col != CLEAR:
                    dst[x, y] = col
    return out


def _image_blocks(img):
    """Run-length encode each image row into (color, x, y, w, h) rect blocks.

    check_sheets/render_icon re-composite the declared blocks, so encoding the
    rotated frames exactly (one run per color) keeps the pixel check tight
    without any hand-authored rect list.
    """
    blocks = []
    px = img.load()
    w, h = img.size
    for y in range(h):
        x = 0
        while x < w:
            color = px[x, y]
            if color == CLEAR:
                x += 1
                continue
            x0 = x
            while x < w and px[x, y] == color:
                x += 1
            blocks.append((color, x0, y, x - x0, 1))
    return blocks


def tailspin_frames():
    base = new(40, 40)
    base.paste(_beast_east(_longtail_east, _longtail_dead)[0], (4, 8))
    return [_image_blocks(_rotate_cw(base, i)) for i in range(8)]


def pole_frame(flash):
    img = new(20, 40)   # 20x36 art, padded to a multiple of 8
    rect(img, 2, 12, 16, 24, DARK)
    for band in (20, 27, 34):
        rect(img, 2, band, 16, 1, BLACK)
    rect(img, 0, 0, 20, 16, WHITE if flash else LIGHT)
    rect(img, 8, 5, 4, 4, BLACK)
    rect(img, 0, 34, 20, 2, BLACK)
    return img


# ---- Breakable pole variants (bead monhun-ardu-6zb.5; staged 6zb.7; markers
# 6zb.8; whole-pole zones 6zb.9; part-locked zones + additive parts 6zb.10).
# Each variant is a 24x40 six-frame sheet: the pole itself is a 16-px DARK shaft
# centered at x4..20 and the breakable PART is a LIGHT additive shape spanning
# the full 24 px, so it sticks 4 px out of the post on each side and reads at
# 1x. Every part carries a WHITE highlight, a BLACK seam where it meets the
# post, and the neutral 7-px jagged fracture marker (BLACK on LIGHT) on its
# face. Frame layout is stage*2 + flash: intact, intact-flash, damaged,
# damaged-flash, broken, broken-flash. Damaged chips the marker + adds crack
# lines; broken leaves a stub on the post and puts the detached part on the
# ground (the key 1x read). PLAIN keeps the original two-frame 20x40 sheet
# (pole_frame) byte-identical for parity. Sub-8 tall ground pieces live in the
# 4 padding rows below the 36-tall art (plus-mask preserves them).
def pole_variant_frames(draw):
    return [draw(stage, flash) for stage in range(3) for flash in (False, True)]


# Neutral fracture stamp: a 7-px jagged crack burst in `color`. `chipped`
# drops the two far-end pixels (5 px) for the damaged stage.
_FRACTURE = ((2, 0), (1, 1), (1, 2), (2, 2), (2, 3), (3, 3), (3, 4))


def _fracture(img, x, y, color, chipped=False):
    for i, (dx, dy) in enumerate(_FRACTURE):
        if chipped and i in (0, 6):
            continue
        rect(img, x + dx, y + dy, 1, 1, color)


def _pole_post(img):
    # 16-px DARK shaft centered in the 24-px frame (aligned with the 20-px pole
    # rect once the sheet is drawn 2 px left of the anchor).
    rect(img, 4, 12, 16, 24, DARK)
    for band in (20, 27, 34):
        rect(img, 4, band, 16, 1, BLACK)


def _pole_ground(img):
    rect(img, 0, 34, 24, 2, BLACK)          # ground plate


def _pole_piece(img, x, y, w, h, head):
    rect(img, x, y, w, h, head)
    rect(img, x, y, w, 1, BLACK)            # cut edge
    rect(img, x + 4, y + h - 1, 2, 1, BLACK)  # fracture remnant
    rect(img, x + w - 2, y + 1, 1, 1, BLACK)


# SEVER's part is a wide cap/head block that shears off; broken leaves a jagged
# stump and puts the cap on the ground.
def pole_sever_frame(stage, flash):
    img = new(24, 40)
    head = WHITE if flash else LIGHT
    if stage == 2:
        # slanted-cut stump: four stepped DARK columns, each with a BLACK edge
        for k in range(4):
            x, top = 4 + k * 4, 13 + k * 2
            rect(img, x, top, 4, 36 - top, DARK)
            rect(img, x, top, 4, 1, BLACK)
        _pole_piece(img, 3, 36, 14, 4, head)
    else:
        _pole_post(img)
        rect(img, 0, 0, 24, 14, head)       # 24-px cap on the post
        rect(img, 2, 11, 20, 2, WHITE)      # bright underside highlight
        rect(img, 0, 14, 24, 1, BLACK)      # seam where the cap meets the post
        _fracture(img, 9, 3, BLACK, chipped=(stage == 1))
        if stage == 1:
            rect(img, 16, 2, 1, 4, BLACK)   # extra cracks on the cap
            rect(img, 17, 3, 1, 2, BLACK)
            rect(img, 5, 8, 1, 3, BLACK)
    _pole_ground(img)
    return img


# BREAK's part is a thick curved horn growing out of the upper post to the
# right; broken leaves a base stub and puts the horn on the ground. Runs are
# (x, y, w) LIGHT rows mirrored by mock/game.js POLE_HORN.
_BREAK_HORN = (
    (18, 1, 4), (18, 2, 5), (17, 3, 5), (17, 4, 6), (16, 5, 6), (16, 6, 6),
    (15, 7, 6), (15, 8, 6), (14, 9, 6), (13, 10, 6), (12, 11, 6), (11, 12, 6),
    (10, 13, 6),
)


def _break_horn(img, head):
    for x, y, w in _BREAK_HORN:
        rect(img, x, y, w, 1, head)
    for x, y, w in _BREAK_HORN:
        rect(img, x, y, 1, 1, WHITE)        # highlight spine up the inner edge


def pole_break_frame(stage, flash):
    img = new(24, 40)
    head = WHITE if flash else LIGHT
    _pole_post(img)
    if stage == 2:
        rect(img, 10, 11, 6, 3, head)       # horn-base stub left on the post
        rect(img, 10, 11, 6, 1, BLACK)
        rect(img, 12, 14, 2, 1, head)
        _pole_piece(img, 2, 36, 13, 4, head)   # horn on the ground
        rect(img, 15, 37, 6, 2, head)
        rect(img, 15, 37, 6, 1, BLACK)
    else:
        _break_horn(img, head)
        rect(img, 10, 13, 8, 1, BLACK)      # seam where the horn meets the post
        _fracture(img, 16, 5, BLACK, chipped=(stage == 1))
        if stage == 1:
            rect(img, 20, 3, 1, 3, BLACK)   # extra cracks down the horn
            rect(img, 13, 10, 1, 3, BLACK)
    _pole_ground(img)
    return img


# CRACK's part is a 24-px collar ringing the post; broken splits it, leaving a
# displaced chunk on the post and one on the ground.
def pole_crack_frame(stage, flash):
    img = new(24, 40)
    head = WHITE if flash else LIGHT
    _pole_post(img)
    if stage == 2:
        rect(img, 0, 16, 24, 4, head)       # upper collar chunk
        rect(img, 0, 16, 24, 1, BLACK)
        rect(img, 3, 26, 21, 4, head)       # displaced lower chunk
        rect(img, 3, 26, 21, 1, BLACK)
        for x, y in ((2, 21), (6, 22), (10, 23), (14, 22), (18, 21), (21, 23)):
            rect(img, x, y, 1, 1, BLACK)    # jagged crack teeth
        _pole_piece(img, 3, 36, 14, 4, head)
    else:
        rect(img, 0, 18, 24, 12, head)      # 24-px collar wrapping the post
        rect(img, 1, 19, 22, 2, WHITE)      # bright inner band
        rect(img, 0, 18, 24, 1, BLACK)      # collar rim seams
        rect(img, 0, 29, 24, 1, BLACK)
        _fracture(img, 10, 21, BLACK, chipped=(stage == 1))
        if stage == 1:
            rect(img, 3, 21, 1, 4, BLACK)   # extra cracks along the collar
            rect(img, 19, 23, 1, 3, BLACK)
    _pole_ground(img)
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
    "$": [0b010, 0b111, 0b100, 0b111, 0b010],
    "-": [0b000, 0b000, 0b111, 0b000, 0b000],
    ".": [0b000, 0b000, 0b000, 0b000, 0b010],
    "/": [0b001, 0b001, 0b010, 0b100, 0b100],
    ">": [0b100, 0b010, 0b001, 0b010, 0b100],
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


# ---------------------------------------------------------------- text blocks

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
    must be pixel-identical to the sheet tile for the same character. This is the
    pixel-oracle link between the HUD sheet and the font sheet."""
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


# ------------------------------------------------------- authored sheet table


def render_icon(defn):
    frames = []
    for blocks in defn["frames"]:
        img = new(defn["w"], defn["h"])
        for color, dx, dy, w, h in blocks:
            rect(img, dx, dy, w, h, color)
        frames.append(img)
    return strip(frames, defn["w"], defn["h"])


def map_tent_frame():
    """Mock-up 4-shade camp tent prop (bead monhun-ardu-fie.5), 32x24.

    A dark ridge tent with a light left slope, a white centre pole, a black
    door and a dark ground line. One frame: the room prop record selects frame 0.
    The background stays transparent so convert-sprite emits the plus-mask plane
    SpritesU::drawPlusMaskFX reads. Integer-only construction (no float) so the
    PNG is byte-stable across platforms.
    """
    img = new(32, 24)
    px = img.load()
    for y in range(2, 21):
        half = ((y - 2) * 13 + 9) // 18 + 1   # apex (16,2) -> half-width 14 at y=20
        for x in range(16 - half, 16 + half + 1):
            px[x, y] = DARK
        px[16 - half, y] = LIGHT
        px[16, y] = WHITE
    for y in range(13, 21):
        for x in range(13, 20):
            px[x, y] = BLACK
    for x in range(1, 31):
        px[x, 21] = DARK
    px[16, 1] = WHITE
    return img


def render_all(dims):
    icons = icon_defs(dims)
    sheets = {}
    # Beast/pole scene art.
    sheets["player"] = strip(player_frames(), 16, 16)
    sheets["monster"] = strip(monster_frames(), 32, 24)
    for body, draw, dead in (("monster_lunge", _chicken_east, _chicken_dead),
                             ("monster_sweep", _bull_east, _bull_dead),
                             ("monster_heavy", _longtail_east, _longtail_dead)):
        east, ship, plan = _beast_sheet(draw, dead)
        sheets[body] = strip(ship, 32, 24)
        register_mirror("fx" + body, east, 32, 24, plan)
    sheets["pole"] = strip([pole_frame(False), pole_frame(True)], 20, 40)
    sheets["pole_sever"] = strip(pole_variant_frames(pole_sever_frame), 24, 40)
    sheets["pole_break"] = strip(pole_variant_frames(pole_break_frame), 24, 40)
    sheets["pole_crack"] = strip(pole_variant_frames(pole_crack_frame), 24, 40)
    for d in icons:
        sheets[d["id"]] = render_icon(d)
    sheets["map_tent"] = strip([map_tent_frame()], 32, 24)
    sheets["ball"] = strip([ball_frame()], 7, 8)
    sheets["scatter"] = strip([scatter_frame()], 4, 8)
    sheets["spark"] = strip([spark_frame(LIGHT), spark_frame(WHITE)], 4, 4)
    sheets["fontw"] = font_sheet(WHITE)
    sheets["fontg"] = font_sheet(LIGHT)
    # One-facing sources (ryh.2): in-scope sheets are written as their east-only
    # source; every other sheet's source is the shipped image itself.
    sources = {}
    for body, img in sheets.items():
        entry = MIRROR_SOURCES.get("fx" + body)
        sources[body] = entry[0] if entry is not None else img
    return icons, sheets, sources


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
    by_id = {d["id"]: d for d in icons}
    d = by_id.get(body)
    if d is not None:
        return "fx%s_%dx%d.png" % (body, d["w"], d["h"])
    if body == "player":
        return "fxplayer_16x16.png"
    if body == "monster" or body.startswith("monster_"):
        return "fx%s_32x24.png" % body
    if body == "pole":
        return "fxpole_20x40.png"
    if body in ("pole_sever", "pole_break", "pole_crack"):
        return "fx%s_24x40.png" % body
    if body == "map_tent":
        # Map prop sheet lives under images/blocks like the other block sheets
        # (convert-sprite names it mh_map_tent, which gen-zones resolves).
        return "mh_map_tent_32x24.png"
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
    return "blocks"


def check_disk(sources, icons):
    """Re-read every written PNG and compare it pixel-for-pixel."""
    for body, img in sources.items():
        directory = os.path.join(ROOT, "images", sheet_kind(body))
        path = os.path.join(directory, sheet_filename(body, img, icons))
        disk = Image.open(path).convert("RGBA")
        if disk.size != img.size:
            raise SystemExit("gen-art: %s on disk %s, want %s" % (path, disk.size, img.size))
        if disk.tobytes() != img.tobytes():
            raise SystemExit("gen-art: %s on disk differs from authored pixels" % path)


def emit_pack_layout(path):
    """images/blocks/layout.json: the mirror pack plan the converter reads.

    Only sheets registered in MIRROR_SOURCES appear. Each entry lists the packed
    frames in shipped order as [source_frame_index, mirror]; a frame whose west
    twin is not the mirror of an east frame (ravager feet, the dead heap) is
    absent -- those sheets ship authored in full and are packed in order.
    """
    plan = {}
    for symbol, (_, frames) in sorted(MIRROR_SOURCES.items()):
        plan[symbol] = [[int(i), bool(m)] for i, m in frames]
    data = json.dumps({"version": 1, "sheets": plan}, indent=2, sort_keys=True) + "\n"
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(data)


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
        # 100 px cap so the font strips stay readable.
        limit = 100
        px = img.load()
        for yy in range(img.size[1]):
            row = "".join(chars[px[xx, yy]] for xx in range(img.size[0]))
            if len(row) > limit:
                row = row[:limit] + "..."
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
    # Demo beast animation layout (BEAST_POSES order, east base; west = +stride).
    L.append("")
    L.append("// Demo beast sheet layout (tools/gen-art.py BEAST_POSES). Authoring")
    L.append("// contract + host pin only: the firmware draws each creature from its")
    L.append("// per-creature art descriptor (data/creatures/*.json -> combat ART).")
    L.append("constexpr uint8_t beast_stride = %d;" % BEAST_STRIDE)
    L.append("constexpr uint8_t beast_idle_count = 2;")
    for i, pose in enumerate(BEAST_POSES):
        L.append("constexpr uint8_t beast_%s_frame = %d;" % (pose[0], i))
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
            "fonts": os.path.join(ROOT, "images", "fonts")}
    gen_dir = os.path.join(ROOT, "src", "generated")
    for directory in dirs.values():
        os.makedirs(directory, exist_ok=True)
    os.makedirs(gen_dir, exist_ok=True)

    load_part_boxes(os.path.join(ROOT, "build", "hitboxes.json"))
    icons, sheets, sources = render_all(dims)
    defs = icons
    check_sheets(defs, sheets)
    check_hud_identity(sheets)

    names = {kind: set() for kind in dirs}
    for body, img in sources.items():
        names[sheet_kind(body)].add(sheet_filename(body, img, defs))
    clean_stale(dirs["blocks"], names["blocks"], "fx")
    clean_stale(dirs["fonts"], names["fonts"], "fx")

    for body, img in sources.items():
        img.save(os.path.join(dirs[sheet_kind(body)], sheet_filename(body, img, defs)))
    check_disk(sources, defs)

    emit_pack_layout(os.path.join(dirs["blocks"], "layout.json"))
    emit_dims_header(dims, icons, os.path.join(gen_dir, "art_dims.hpp"))

    n_blocks = len(sheets) - 2
    print("gen-art: wrote %d block sheets (%d overlay/effect icons) + 2 font sheets" %
          (n_blocks, len(icons)))
    print("gen-art: pixel check OK (%d sheets, disk-exact)" % len(sheets))
    print("gen-art: %d mirror-packed sheets (one-facing sources)" % len(MIRROR_SOURCES))
    if args.dump:
        print(ascii_dump(sheets, defs))


if __name__ == "__main__":
    main()
