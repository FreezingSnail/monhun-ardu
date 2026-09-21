#!/usr/bin/env python3
"""Authoring contact sheet: creature JSON -> PNG attack-window review (ljj.6).

The fight is authored in data/creatures/*.json and data/skeletons.json; this
tool renders those numbers back as a picture so windows, timings and the zone
geometry can be eyeballed without flashing the device:

  - a tick timeline per attack: one 3 px column per tick, shaded by phase
    (dark = windup, light = active, mid = recover) with the hit-window spans
    highlighted along the bottom,
  - one preview cell per hit window at 1 world px per pixel: the implicit body
    box (creature w/h) and the window box (face-relative centre offset) drawn
    to the same scale, so the arc of a multi-window sweep is visible,
  - a per-creature header with the stats and the zone list
    (dmgMul/hp/bodyShare/staggerOnHit).

Usage:
    python3 tools/contact_sheet.py                     # all creatures
    python3 tools/contact_sheet.py --creature ravager  # one creature
    python3 tools/contact_sheet.py --out build/review.png

The output is a build artifact: it is written under build/ by default and never
committed. Pure Pillow + stdlib; no game code is imported, so the sheet shows
exactly what the JSON declares (and tools/tests pins the JSON -> pixel layout).
"""
import argparse
import json
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# 3x5 bitmap font (subset: A-Z, 0-9, punctuation used by the sheet). Keeping a
# local table avoids Pillow font/version drift: the sheet is byte-deterministic.
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
    "(": [0b001, 0b010, 0b010, 0b010, 0b001],
    ")": [0b001, 0b001, 0b001, 0b001, 0b001],
    "+": [0b000, 0b010, 0b111, 0b010, 0b000],
    "=": [0b000, 0b111, 0b000, 0b111, 0b000],
    ",": [0b000, 0b000, 0b000, 0b010, 0b100],
    "!": [0b010, 0b010, 0b010, 0b000, 0b010],
}

BLACK = (0, 0, 0, 255)
DARK = (85, 85, 85, 255)
MID = (150, 150, 150, 255)
LIGHT = (205, 205, 205, 255)
WHITE = (255, 255, 255, 255)
CLEAR = (0, 0, 0, 0)

# timeline column width (px) and colours per phase
TICK_W = 3
PHASE_COLORS = {"windup": MID, "active": BLACK, "recover": LIGHT}
WINDOW_COLORS = [BLACK, DARK, MID]

# prg.11: `tell` is a windup animation-frame selector, not a procedural shape.
# The sheet names the window class each tell selects so a reviewer can confirm
# the windup pose matches the hit window; unauthored frames fall back to the 2x2
# core marker (prg.12 authors the per-attack frames on the FX cart).
TELL_CLASS = {
    "dot": "core",
    "line": "ray",
    "arc": "sweep",
    "ring": "aoe",
    "zone": "rect",
}


def tell_class(tell):
    """Window class a tell selects (prg.11); unknown/absent -> '?'."""
    return TELL_CLASS.get(tell, "?")

PREVIEW_W = 72
PREVIEW_H = 44
MARGIN = 8
TEXT_H = 6
TEXT_ADV = 4


def text(draw, x, y, s, color=BLACK):
    s = s.upper()
    for ch in s:
        glyph = GLYPHS.get(ch)
        if glyph:
            for row, bits in enumerate(glyph):
                for col in range(3):
                    if bits & (4 >> col):
                        draw.point((x + col, y + row), fill=color)
        x += TEXT_ADV
    return x


def text_width(s):
    return len(s) * TEXT_ADV


def load_data(root):
    with open(os.path.join(root, "data", "skeletons.json"), encoding="utf-8") as handle:
        skeletons = {s["id"]: s for s in json.load(handle)["skeletons"]}
    creatures = []
    creatures_dir = os.path.join(root, "data", "creatures")
    for name in sorted(os.listdir(creatures_dir)):
        if not name.endswith(".json"):
            continue
        with open(os.path.join(creatures_dir, name), encoding="utf-8") as handle:
            creatures.append(json.load(handle))
    return skeletons, creatures


def attack_ticks(attack):
    return int(attack["windup"]) + int(attack["active"]) + int(attack["recover"])


def band_height(creature):
    h = MARGIN + TEXT_H + 4
    for attack in creature["attacks"]:
        h += TEXT_H + 4
        h += 4 + 12 + 4                      # timeline band
        h += PREVIEW_H + 4
        windows = attack.get("windows", [])
        if not windows:
            continue
        h += PREVIEW_H + 4                   # preview row
    return h


def render(creatures, skeletons, only=None):
    """Render the contact sheet and return the PIL image (deterministic)."""
    selected = [c for c in creatures if only is None or c["id"] == only]
    if not selected:
        raise SystemExit("contact_sheet: no creature matches %r" % only)
    width = 480
    height = MARGIN
    for creature in selected:
        height += band_height(creature)
    img = Image.new("RGBA", (width, height), WHITE)
    draw = ImageDraw.Draw(img)
    y = MARGIN
    for creature in selected:
        y = draw_creature_band(draw, creature, skeletons[creature["skeleton"]], y, width)
    return img


def draw_creature_band(draw, creature, skeleton, y, width):
    stats = creature["stats"]
    zones = []
    for name, zone in sorted(creature.get("zones", {}).items()):
        zones.append("%s D%d HP%d S%d ST%d" % (
            name, zone["dmgMul"], zone["hp"], zone["bodyShare"], zone.get("staggerOnHit", 0)))
    text(draw, MARGIN, y, "CREATURE %s (%s) HP%d SPD%d %s" % (
        creature["id"], skeleton["id"], stats["hp"], stats["spd"], " ".join(zones)))
    y += TEXT_H + 4
    for attack in creature["attacks"]:
        y = draw_attack_band(draw, attack, int(creature["stats"]["w"]), int(creature["stats"]["h"]), y)
    return y + MARGIN


def draw_attack_band(draw, attack, body_w, body_h, y):
    windup = int(attack["windup"])
    active = int(attack["active"])
    recover = int(attack["recover"])
    total = windup + active + recover
    tell = attack.get("tell", "dot")
    text(draw, MARGIN, y, "ATTACK %s W%d A%d R%d DMG%d %s TELL %s/%s FRAME-PRG12" % (
        attack["id"], windup, active, recover, attack["dmg"], attack.get("phys", "-"),
        tell, tell_class(tell)))
    y += TEXT_H + 4

    # timeline: one column per tick, phase shaded, window spans highlighted
    for t in range(total):
        phase = "windup" if t < windup else ("active" if t < windup + active else "recover")
        x = MARGIN + t * TICK_W
        draw.rectangle([x, y, x + TICK_W - 2, y + 11], fill=PHASE_COLORS[phase])
        for wi, window in enumerate(attack.get("windows", [])):
            t0 = windup + int(window["t0"])
            t1 = windup + int(window["t1"])
            if t0 <= t <= t1:
                draw.rectangle([x, y + 8, x + TICK_W - 2, y + 11], fill=WINDOW_COLORS[wi % 3])
    # tick ruler every 10 ticks
    for t in range(0, total, 10):
        x = MARGIN + t * TICK_W
        draw.rectangle([x, y + 12, x, y + 14], fill=BLACK)
    draw.rectangle([MARGIN - 1, y - 1, MARGIN + total * TICK_W, y + 11], outline=DARK)
    y += 4 + 12 + 4

    # per-window previews at 1 world px per pixel
    bx = MARGIN
    for wi, window in enumerate(attack.get("windows", [])):
        box = window["box"]
        cx = bx + PREVIEW_W // 2
        cy = y + PREVIEW_H // 2
        # implicit body box (creature w/h) centred in the cell
        draw.rectangle([cx - body_w // 2, cy - body_h // 2,
                        cx + body_w // 2 - 1, cy + body_h // 2 - 1], outline=DARK)
        # window box: face-relative centre offset (docs section 5), drawn east
        draw.rectangle([cx + box["ox"] - box["w"] // 2, cy + box["oy"] - box["h"] // 2,
                        cx + box["ox"] + box["w"] // 2 - 1, cy + box["oy"] + box["h"] // 2 - 1],
                       outline=WINDOW_COLORS[wi % 3])
        text(draw, bx, y + PREVIEW_H + 1, "W%d T%d-%d X%d Y%d %dX%d" % (
            wi, window["t0"], window["t1"], box["ox"], box["oy"], box["w"], box["h"]))
        bx += PREVIEW_W + 4
    if attack.get("windows"):
        y += PREVIEW_H + 4 + TEXT_H
    return y


def ascii_dump(img, step=4):
    """Downsampled ASCII view of the sheet (evidence for output.md)."""
    px = img.load()
    lines = []
    for y in range(0, img.size[1], step):
        row = ""
        for x in range(0, img.size[0], step):
            r, g, b, a = px[x, y]
            if a == 0:
                row += " "
            elif r > 230:
                row += "."
            elif r > 170:
                row += ":"
            elif r > 100:
                row += "*"
            else:
                row += "#"
        lines.append(row.rstrip())
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=ROOT, help="repository root holding data/ (default: repo root)")
    parser.add_argument("--out", default=None, help="output PNG (default: build/contact_sheet.png)")
    parser.add_argument("--creature", default=None, help="only this creature id")
    parser.add_argument("--dump", action="store_true", help="print a downsampled ASCII view")
    args = parser.parse_args(argv)

    skeletons, creatures = load_data(os.path.abspath(args.root))
    img = render(creatures, skeletons, args.creature)
    out = args.out or os.path.join(args.root, "build", "contact_sheet.png")
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    img.save(out)
    print("contact_sheet: %s (%dx%d, %d creatures)" % (out, img.size[0], img.size[1], len(creatures)))
    if args.dump:
        print(ascii_dump(img))
    return 0


if __name__ == "__main__":
    sys.exit(main())
