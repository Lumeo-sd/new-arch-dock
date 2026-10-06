#!/usr/bin/env python3
"""Per-column bright-pixel profile of the panel band -- a coarse structural
map of what the panel actually paints (icons = tall narrow peaks, text = short
broad peaks). The translucent background never goes bright, so this only shows
the panel's own elements.

Usage: panellayout.py [frame.png]
"""
import sys
from PIL import Image

Y0, Y1 = 24, 48


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/strip-with.png"
    im = Image.open(path).convert("L")
    w, h = im.size
    px = im.load()
    bright = [0] * w
    for y in range(Y0, min(Y1, h)):
        for x in range(w):
            if px[x, y] > 150:
                bright[x] += 1

    # coarse map in 24px windows
    print(f"{path}: window(24px) -> bright-pixels")
    for x0 in range(0, w, 24):
        tot = sum(bright[x0:x0 + 24])
        if tot:
            print(f"  x {x0:4d}..{x0 + 23:4d}: {tot}")
    return 0


if __name__ == "__main__":
    sys.exit(main())