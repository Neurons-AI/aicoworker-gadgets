# Copyright (c) Neurons AI. Licensed under the Apache License, Version 2.0.
"""Turn the raw frames from tools/dou/dou_gif.c into avatar/dou_happy.gif.

The GIF uses the layout tools/gen_happy_anim.py expects: every art pixel is a
4x4 block plus a 1-pixel gap (5-pixel grid), the art sits at x=4..59, y=0..58.

  python3 tools/dou/make_dou_gif.py /tmp/dou.raw avatar/dou_happy.gif
"""
import struct
import sys

from PIL import Image

SRC = 64
W, H, OX = 56, 59, 4   # area gen_happy_anim.py crops (CROP_X, WIDTH, HEIGHT)


def rgb(c):
    return (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)


def main():
    raw = open(sys.argv[1], "rb").read()
    n = len(raw) // (SRC * SRC * 2)
    frames = []
    for f in range(n):
        px = struct.unpack_from(f"<{SRC * SRC}H", raw, f * SRC * SRC * 2)
        img = Image.new("RGB", ((OX + W) * 5, H * 5), (0, 0, 0))
        for y in range(H):
            for x in range(W):
                c = rgb(px[(y + 3) * SRC + (x + 4)])
                for dy in range(4):
                    for dx in range(4):
                        img.putpixel(((x + OX) * 5 + dx, y * 5 + dy), c)
        frames.append(img.quantize(colors=255, method=Image.Quantize.MEDIANCUT))
    frames[0].save(sys.argv[2], save_all=True, append_images=frames[1:], duration=40, loop=0, disposal=1)
    print(f"{n} frames -> {sys.argv[2]}")


if __name__ == "__main__":
    main()
