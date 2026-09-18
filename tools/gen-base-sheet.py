#!/usr/bin/env python3
"""Base sprite sheets for the equipment framework (docs/equipment-framework.md).

Emits the canonical draw-over template into docs/art/: one 1x sheet per slot
(shadow/body/head/weapon) plus a 4x guide with per-cell grid, frame labels,
anchor crosses and the palette swatches. The 1x sheets are the files an artist
opens and draws inside; cell size, frame order and anchors are fixed by the doc.

Slot layout (cell -> frames):
    shadow  16x16   1        current shadow bar
    body    16x16   8        facing 0..7; facing 0 filled with the current art
    head    16x16   8        facing 0..7; facing 0 filled with the current art
    weapon  32x32   24       facing-major, 8 x 3 (startup/active/recover)

Anchors: body/head/shadow cell center (8,8) = player center; weapon cell center
(16,16) = grip/pivot. Guide-only annotation colors (grid gray, anchor magenta,
label cyan) never appear in the 1x sheets.

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

GRID = (90, 90, 90, 255)
ANCHOR = (255, 0, 255, 255)
LABEL = (0, 200, 255, 255)
SCALE = 4


def rect(img, x, y, w, h, color):
    for j in range(y, y + h):
        for i in range(x, x + w):
            img.putpixel((i, j), color)


def new(w, h):
    return Image.new("RGBA", (w, h), CLEAR)


def body_cell(shade):
    """Current fxplayer body minus head and shadow (mock drawPlayer rects)."""
    img = new(16, 16)
    rect(img, 4, 7, 8, 6, shade)    # torso
    rect(img, 5, 13, 2, 2, shade)   # legs
    rect(img, 9, 13, 2, 2, shade)
    return img


def head_cell(shade):
    img = new(16, 16)
    rect(img, 5, 1, 6, 6, shade)   # mock head rect
    return img


def shadow_cell():
    img = new(16, 16)
    rect(img, 2, 15, 12, 1, DARK)
    return img


def sheet(cells, w, h):
    out = new(w * len(cells), h)
    for i, cell in enumerate(cells):
        out.paste(cell, (i * w, 0))
    return out


def guide(name, img, cells_x, cells_y, cell_w, cell_h, anchors, labels=None):
    """4x annotated guide: grid, per-cell labels, anchor crosses."""
    big = img.resize((cells_x * cell_w * SCALE, cells_y * cell_h * SCALE), Image.NEAREST)
    d = ImageDraw.Draw(big)
    for cx in range(cells_x + 1):
        x = cx * cell_w * SCALE
        d.line([(x, 0), (x, cells_y * cell_h * SCALE)], fill=GRID, width=1)
    for cy in range(cells_y + 1):
        y = cy * cell_h * SCALE
        d.line([(0, y), (cells_x * cell_w * SCALE, y)], fill=GRID, width=1)
    for cy in range(cells_y):
        for cx in range(cells_x):
            idx = cy * cells_x + cx
            text = labels[idx] if labels else str(idx)
            d.text((cx * cell_w * SCALE + 4, cy * cell_h * SCALE + 4), text, fill=LABEL)
            for ax, ay in anchors:
                x = (cx * cell_w + ax) * SCALE
                y = (cy * cell_h + ay) * SCALE
                d.line([(x - 5, y), (x + 5, y)], fill=ANCHOR, width=1)
                d.line([(x, y - 5), (x, y + 5)], fill=ANCHOR, width=1)
    return big, d, name


def label_strip(base, entries):
    """Palette/legend strip appended under the guide."""
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

    # 1x sheets: facing 0 filled with the current art, the rest blank cells.
    body_empty = new(16, 16)
    head_empty = new(16, 16)
    weapon_empty = new(32, 32)

    body = sheet([body_cell(WHITE)] + [body_empty] * 7, 16, 16)
    head = sheet([head_cell(WHITE)] + [head_empty] * 7, 16, 16)
    shadow = sheet([shadow_cell()], 16, 16)
    # Weapon: 8 facing columns x 3 phase rows (row-major frame index = facing*3
    # + phase only for single-row sheets; the doc's cell order is row-major).
    weapon = new(32 * 8, 32 * 3)
    for i in range(24):
        weapon.paste(weapon_empty, ((i % 8) * 32, (i // 8) * 32))

    files = [
        ("base_shadow_16x16.png", shadow),
        ("base_body_16x16.png", body),
        ("base_head_16x16.png", head),
        ("base_weapon_32x32.png", weapon),
    ]
    for fname, img in files:
        path = os.path.join(outdir, fname)
        img.save(path)
        print("gen-base-sheet: %s (%dx%d)" % (path, img.size[0], img.size[1]))

    # Guide: one big sheet showing cell order + anchors + palette.
    anchors = [(8, 8)]
    g_body, _, _ = guide("body", body, 8, 1, 16, 16, anchors)
    g_head, _, _ = guide("head", head, 8, 1, 16, 16, anchors)
    g_shadow, _, _ = guide("shadow", shadow, 1, 1, 16, 16, anchors)
    wlabels = ["f%d/p%d" % (f, p) for p in range(3) for f in range(8)]
    g_weapon, _, _ = guide("weapon", weapon, 8, 3, 32, 32, [(16, 16)], labels=wlabels)

    legend = [("palette (1:1 with L4 planes):", None)]
    for name, color in zip(SHADE_NAMES, SHADES):
        legend.append((name, color))
    legend.append(("transparent = mask 0 (nothing drawn)", CLEAR))
    legend.append(("magenta cross = anchor (body/head 8,8; weapon 16,16)", None))
    legend.append(("label = frame index (weapon: facing*3 + phase)", None))

    strip = label_strip(g_weapon, legend)
    strip2 = label_strip(g_shadow, [("shadow", None)])
    combo = Image.new("RGBA", (max(strip.size[0], g_body.size[0] + g_head.size[0] + 16), strip.size[1] + strip2.size[1] + g_body.size[1] + 16), (0, 0, 0, 255))
    combo.paste(strip, (0, 0))
    y = strip.size[1] + 8
    combo.paste(g_body, (0, y))
    combo.paste(g_head, (g_body.size[0] + 16, y))
    combo.paste(strip2, (0, y + g_body.size[1] + 8))
    path = os.path.join(outdir, "guide_player_base_4x.png")
    combo.save(path)
    print("gen-base-sheet: %s (%dx%d)" % (path, combo.size[0], combo.size[1]))


if __name__ == "__main__":
    main()
