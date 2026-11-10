#!/usr/bin/env python3
"""Draw the Homebrew Menu icon (256x256 JPEG) for the Switch build.

    python3 tools/switch/make_icon.py assets/switch/icon.jpg
"""
import sys
from PIL import Image, ImageDraw, ImageFont

SIZE = 256
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def main(out):
    img = Image.new("RGB", (SIZE, SIZE))
    px = img.load()
    # dark vertical gradient with a faint red glow from the bottom
    for y in range(SIZE):
        t = y / (SIZE - 1)
        for x in range(SIZE):
            r = int(14 + 70 * t * t)
            g = int(14 + 14 * t)
            b = int(20 + 10 * (1 - t))
            px[x, y] = (r, g, b)
    d = ImageDraw.Draw(img)
    # frame
    d.rectangle([6, 6, SIZE - 7, SIZE - 7], outline=(230, 120, 30), width=4)
    d.rectangle([14, 14, SIZE - 15, SIZE - 15], outline=(120, 60, 20), width=1)
    # big Q4
    big = ImageFont.truetype(FONT, 120)
    text = "Q4"
    w = d.textlength(text, font=big)
    d.text(((SIZE - w) / 2 + 3, 40 + 3), text, font=big, fill=(60, 20, 8))
    d.text(((SIZE - w) / 2, 40), text, font=big, fill=(240, 130, 35))
    # caption
    small = ImageFont.truetype(FONT, 26)
    cap = "openQ4"
    w = d.textlength(cap, font=small)
    d.text(((SIZE - w) / 2, 172), cap, font=small, fill=(235, 235, 235))
    tiny = ImageFont.truetype(FONT, 16)
    cap2 = "for Switch"
    w = d.textlength(cap2, font=tiny)
    d.text(((SIZE - w) / 2, 208), cap2, font=tiny, fill=(200, 110, 40))
    img.save(out, "JPEG", quality=92)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "icon.jpg")
