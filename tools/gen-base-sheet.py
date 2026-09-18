#!/usr/bin/env python3
"""Base player sheet for the equipment framework (docs/equipment-framework.md).

One sheet, one row, all 8 facings of the naked player (shadow + body + head).
This is the draw-over template: copy the file, keep the cell grid and palette,
replace the art, and it is a new player/equipment set (the pipeline reads it as
8 frames of 16x16, cell = anchor center 8,8).

    docs/art/player_base_16x16.png       128x16, facing 0..7 left to right
    docs/art/guide_player_base_4x.png    4x guide: direction labels, cell grid,
                                         anchor crosses, palette swatches

Facing order is DIR8: 0=E, 1=SE, 2=S, 3=SW, 4=W, 5=NW, 6=N, 7=NE. All 8 cells
are prefilled with the current renderer's silhouette (flat 4-shade shapes) so
every angle has a starting point; redraw each cell in place. Guide-only
annotation colors (grid gray, anchor magenta, label cyan) never appear in the
1x sheet.

Usage: python3 tools/gen-base-sheet.py [outdir]   (default: docs/art)
"""
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

CLEAR = (0, 0, 0, 0)
BLACK = (0, 0, 0, 255)
DARK = (85, 85, 85, 255)
LIGHT = (170, 170, 170, 255)
WHITE = (255, 255, 255, 255)
SHADES = (BLACK, DARK, LIGHT, WHITE)
SHADE_NAMES = ("black (0)", "dark (1)", "light (2)", "white (3)")

DIR8_NAMES = ("E", "SE", "S", "SW", "W", "NW", "N", "NE")

GRID = (90, 90, 90, 255)
ANCHOR = (255, 0, 255, 255)
LABEL = (0, 200, 255, 255)
SCALE = 4

CELL = 16
FRAMES = 8


def rect(img, x, y, w, h, color):
    for j in range(y, y + h):
        for i in range(x, x + w):
            img.putpixel((i, j), color)


def new(w, h):
    return Image.new("RGBA", (w, h), CLEAR)


def player_cell():
    """Current fxplayer silhouette: shadow + head + torso + legs (mock rects)."""
    img = new(CELL, CELL)
    rect(img, 2, 15, 12, 1, DARK)   # shadow
    rect(img, 5, 1, 6, 6, WHITE)    # head
    rect(img, 4, 7, 8, 6, WHITE)    # torso
    rect(img, 5, 13, 2, 2, WHITE)   # legs
    rect(img, 9, 13, 2, 2, WHITE)
    return img


def guide(img):
    big = img.resize((FRAMES * CELL * SCALE, CELL * SCALE), Image.NEAREST)
    d = ImageDraw.Draw(big)
    for cx in range(FRAMES + 1):
        x = cx * CELL * SCALE
        d.line([(x, 0), (x, CELL * SCALE)], fill=GRID, width=1)
    d.line([(0, CELL * SCALE - 1), (FRAMES * CELL * SCALE, CELL * SCALE - 1)], fill=GRID, width=1)
    for cx in range(FRAMES):
        d.text((cx * CELL * SCALE + 4, 4), "%d %s" % (cx, DIR8_NAMES[cx]), fill=LABEL)
        ax = (cx * CELL + 8) * SCALE
        ay = 8 * SCALE
        d.line([(ax - 5, ay), (ax + 5, ay)], fill=ANCHOR, width=1)
        d.line([(ax, ay - 5), (ax, ay + 5)], fill=ANCHOR, width=1)
    return big


def legend(base, entries):
    h = 22 * len(entries) + 8
    out = Image.new("RGBA", (base.size[0], base.size[1] + h), (0, 0, 0, 255))
    out.paste(base, (0, 0))
    d = ImageDraw.Draw(out)
    y = base.size[1] + 4
    for text, color in entries:
        if color is not None:
            d.rectangle([6, y + 2, 6 + 16, y + 16], fill=color)
        d.text((30, y + 4), text, fill=LABEL)
        y += 22
    return out


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "docs", "art")
    os.makedirs(outdir, exist_ok=True)

    sheet = new(CELL * FRAMES, CELL)
    for i in range(FRAMES):
        sheet.paste(player_cell(), (i * CELL, 0))   # all 8 angles prefilled

    path = os.path.join(outdir, "player_base_16x16.png")
    sheet.save(path)
    print("gen-base-sheet: %s (%dx%d, %d frames)" % (path, sheet.size[0], sheet.size[1], FRAMES))

    entries = [("8 facing cells of 16x16; cell center (8,8) = player center", None)]
    entries.append(("palette (1:1 with L4 planes):", None))
    for name, color in zip(SHADE_NAMES, SHADES):
        entries.append((name, color))
    entries.append(("transparent = mask 0 (nothing drawn)", None))
    entries.append(("copy this file, draw, save as <symbol>_16x16.png", None))

    gpath = os.path.join(outdir, "guide_player_base_4x.png")
    legend(guide(sheet), entries).save(gpath)
    print("gen-base-sheet: %s" % gpath)


if __name__ == "__main__":
    main()
