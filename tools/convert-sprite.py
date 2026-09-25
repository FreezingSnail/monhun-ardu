from PIL import Image
import os
import argparse
import io
import json
import re


def get_shade(rgba, shades, shade):
    w = (254 + shades) // shades
    b = (shade + 1) * w
    return 1 if rgba[0] >= b else 0


def get_mask(rgba):
    return 1 if rgba[3] >= 128 else 0


def convert(fname, shades, sw=None, sh=None, num=None, plan=None):

    if not (shades >= 2 and shades <= 4):
        print('shades argument must be 2, 3, or 4')
        return None

    im = Image.open(fname).convert('RGBA')
    pixels = list(im.getdata())

    masked = False
    for i in pixels:
        if i[3] < 255:
            masked = True
            break

    w = im.width
    h = im.height
    if sw is None:
        sw = w
    if sh is None:
        sh = h
    nw = w // sw
    nh = h // sh
    if plan is not None:
        num = len(plan)
    elif num is None:
        num = nw * nh
    sp = (sh + 7) // 8

    if nw * nh <= 0:
        print('%s: Invalid sprite dimensions' % fname)
        return None

    # 2-byte width/height header (w, h each a single byte, both <= 255 for our
    # sheets). SpritesU::drawPlusMaskFX(x, y, image, frame) reads exactly these
    # two bytes and then draws from image + 2, so the blob must start with them.
    bytes = bytearray([sw, sh])

    # One-facing source + packer mirror (bead monhun-ardu-ryh.2): when a plan is
    # given (images/blocks/layout.json) it lists the packed frames as
    # (source_frame_index, mirror) in shipped order, so each west frame is read
    # as its east twin mirrored instead of being authored twice. Without a plan
    # the source frames are packed in order (fonts/equip/legacy sheets).
    for n in range(num):
        if plan is not None:
            src, mirror = plan[n]
        else:
            src, mirror = n, False
        if not 0 <= src < nw * nh:
            print('%s: frame %d source %d out of range' % (fname, n, src))
            return None
        bx = (src % nw) * sw
        by = (src // nw) * sh
        for shade in range(shades - 1):
            for p in range(sp):
                for ix in range(sw):
                    x = bx + (sw - 1 - ix if mirror else ix)
                    byte = 0
                    mask = 0
                    for iy in range(8):
                        y = p * 8 + iy
                        if y >= sh:
                            break
                        y += by
                        i = y * w + x
                        rgba = pixels[i]
                        byte |= (get_shade(rgba, shades, shade) << iy)
                        mask |= (get_mask(rgba) << iy)
                    bytes += bytearray([byte])
                    if masked:
                        bytes += bytearray([mask])

    return bytes


def convert_header(fname, sym, shades=2, sw=None, sh=None, num=None, plan=None):
    bytes = convert(fname, shades, sw, sh, num, plan)
    if bytes is None:
        return
    f = io.StringIO()
    f.write('uint8_t %s[] =\n{\n' % sym)
    for n in range(len(bytes)):
        if n % 16 == 0:
            f.write('    ')
        f.write('%3d,' % bytes[n])
        f.write(' ' if n % 16 != 15 else '\n')
    if len(bytes) % 16 != 0:
        f.write('\n')
    f.write('};\n')
    return f.getvalue()


def parse_filename(filename):
    name, rest = filename.rsplit('_', 1)
    dimensions, _ = rest.split('.')
    width, height = dimensions.split('x')
    return name, int(width), int(height)


def sort_strings(strings):
    def key_func(s):
        match = re.match(r'(\D+)(\d+)', s)
        prefix, num = match.groups()
        return (prefix, int(num))

    return sorted(strings, key=key_func)


def main():
    parser = argparse.ArgumentParser(
        description="This script converts images to a specific format.\n Place all images of the format <name>_<width>x<height>.png in a directory and run this script with the path to the dir as an argument.")
    parser.add_argument(
        "dirpath", help="the path of the directory to convert files from")
    parser.add_argument("-s", "--shades", type=int, help="an optional number")
    parser.add_argument("-o", "--out", help="path to output file")
    args = parser.parse_args()

    if not args.dirpath:
        parser.print_help()
        return

    if not args.out:
        args.out = "."
    if not args.shades:
        args.shades = 2

    all_buffers = ""

    pattern = re.compile(r'^.*_\d+x\d+\.png$')
    script_dir = os.path.dirname(os.path.abspath(__file__))
    dir_path = os.path.join(script_dir, args.dirpath)

    # One-facing source + packer mirror (bead monhun-ardu-ryh.2): layout.json in
    # the source directory (written by tools/gen-art.py) lists the packed frames
    # of each mirrored sheet as (source_frame, mirror). Sheets absent from it are
    # packed in source order.
    layout = {}
    layout_path = os.path.join(dir_path, "layout.json")
    if os.path.isfile(layout_path):
        with open(layout_path, encoding="utf-8") as handle:
            layout = json.load(handle).get("sheets", {})

    for filename in os.listdir(dir_path):
        filepath = os.path.join(script_dir, args.dirpath, filename)
        if os.path.isfile(filepath) and pattern.match(filename):
            file_details = parse_filename(filename)
            out = convert_header(filepath,  file_details[0], args.shades, file_details[1],
                                 file_details[2], plan=layout.get(file_details[0]))
            all_buffers += out
            # print(filename, len(out), len(all_buffers))

    out_dir = os.path.join(script_dir, args.out)
    with open(out_dir+"Sprites.txt", 'w') as f:
        f.write(all_buffers)


if __name__ == "__main__":
    main()
