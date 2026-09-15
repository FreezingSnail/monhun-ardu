#!/usr/bin/env python3
"""Author the 4-shade block-art and font sprite sheets for monhun-ardu.

Source of truth for every silhouette is mock/game.js (drawPlayer, drawMonster,
drawPole, drawProjectiles, drawEffects) and its FONT table. Each PNG is an RGBA
sprite sheet laid out left-to-right, one frame per tile; heights are multiples
of 8 to satisfy the converter. Palette maps 1:1 onto the L4 triplane levels:

    transparent -> mask 0 (nothing drawn)
    black       -> shade 0 (no plane)
    dark gray   -> shade 1 (plane 0)
    light gray  -> shade 2 (plane 1)
    white       -> shade 3 (plane 2)

tools/convert-sprite.py (shades=4) turns each sheet into the 2-byte-header
plus-mask triplane blob that SpritesU::drawPlusMaskFX reads.
"""
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

CLEAR = (0, 0, 0, 0)
BLACK = (0, 0, 0, 255)
DARK = (85, 85, 85, 255)
LIGHT = (170, 170, 170, 255)
WHITE = (255, 255, 255, 255)


def rect(img, x, y, w, h, color):
    if w <= 0 or h <= 0:
        return
    ImageDraw.Draw(img).rectangle([x, y, x + w - 1, y + h - 1], fill=color)


def new(w, h):
    return Image.new("RGBA", (w, h), CLEAR)


def strip(frames, w, h):
    """Lay frames left-to-right into one sheet."""
    sheet = new(w * len(frames), h)
    for i, frame in enumerate(frames):
        sheet.paste(frame, (i * w, 0))
    return sheet


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


# --------------------------------------------------------------- drawPlayer

def player_frames():
    """16x16 body+shadow only; weapon overlays stay procedural (reach and aim
    are dynamic and exceed the 16x16 frame)."""
    frames = []
    for body in (WHITE, LIGHT):  # normal shade3, dodge shade2
        img = new(16, 16)
        rect(img, 2, 15, 12, 1, DARK)   # shadow
        rect(img, 5, 1, 6, 6, body)     # head
        rect(img, 4, 7, 8, 6, body)     # torso
        rect(img, 5, 13, 2, 2, body)    # legs
        rect(img, 9, 13, 2, 2, body)
        frames.append(img)
    return frames


# -------------------------------------------------------------- drawMonster

def monster_frame(body, head, east, dead=False):
    img = new(32, 24)
    if dead:
        rect(img, 0, 16, 32, 8, DARK)
        rect(img, 12, 14, 8, 4, LIGHT)
        return img
    rect(img, 2, 23, 28, 1, BLACK)          # ground shadow
    for i in range(4):
        rect(img, 3 + i * 8, 21, 3, 3, BLACK)  # feet
    rect(img, 2, 4, 28, 14, body)
    rect(img, 6, 1, 20, 6, body)
    head_x = 22 if east else 0
    rect(img, head_x, 6, 10, 12, head)
    rect(img, head_x + (7 if east else 1), 13, 2, 2, BLACK)  # face-side eye
    rect(img, head_x + 4, 9, 2, 2, BLACK)
    return img


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


# ---------------------------------------------------------------- drawPole

def pole_frame(flash):
    img = new(20, 40)  # 20x36 art, padded to a multiple of 8
    rect(img, 2, 12, 16, 24, DARK)
    for band in (20, 27, 34):
        rect(img, 2, band, 16, 1, BLACK)
    rect(img, 0, 0, 20, 16, WHITE if flash else LIGHT)
    rect(img, 8, 5, 4, 4, BLACK)
    rect(img, 0, 34, 20, 2, BLACK)
    return img


# -------------------------------------------------------- projectiles / fx

def ball_frame():
    img = new(7, 8)  # 7x6 art, padded
    rect(img, 0, 0, 7, 6, LIGHT)
    rect(img, 1, 1, 5, 4, WHITE)
    rect(img, 0, 4, 7, 1, BLACK)
    return img


def scatter_frame():
    img = new(4, 8)  # 4x4 art, padded so the tile has transparency (plus-mask)
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


# -------------------------------------------------------------------- main

def main():
    blocks = os.path.join(ROOT, "images", "blocks")
    fonts = os.path.join(ROOT, "images", "fonts")
    os.makedirs(blocks, exist_ok=True)
    os.makedirs(fonts, exist_ok=True)

    strip(player_frames(), 16, 16).save(os.path.join(blocks, "fxplayer_16x16.png"))
    strip(monster_frames(), 32, 24).save(os.path.join(blocks, "fxmonster_32x24.png"))
    strip([pole_frame(False), pole_frame(True)], 20, 40).save(
        os.path.join(blocks, "fxpole_20x40.png"))
    strip([ball_frame()], 7, 8).save(os.path.join(blocks, "fxball_7x8.png"))
    strip([scatter_frame()], 4, 8).save(
        os.path.join(blocks, "fxscatter_4x8.png"))
    strip([spark_frame(LIGHT), spark_frame(WHITE)], 4, 4).save(
        os.path.join(blocks, "fxspark_4x4.png"))

    font_sheet(WHITE).save(os.path.join(fonts, "fxfontw_4x8.png"))
    font_sheet(LIGHT).save(os.path.join(fonts, "fxfontg_4x8.png"))

    print("gen-art: wrote images/blocks (6 sheets) and images/fonts (2 sheets)")


if __name__ == "__main__":
    main()
