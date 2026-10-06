#!/usr/bin/env python3
"""Detect panel *elements* in one frame: bright marks (icons/text) on the
translucent mid-grey background. Contrast-vs-local-median fails here because
the panel's own background (#666666 @ 0.5 over the wallpaper) is never flat,
so every pixel differs from its neighbour.

Method: within the panel band, any pixel much brighter than the band's local
luminance = an element (icon or text). The translucent background itself stays
mid-grey, so it never crosses the threshold.

Usage: panelbright.py [frame.png] [y0 y1]
"""
import sys
from PIL import Image

Y0, Y1 = 24, 49
DELTA = 90          # luminance above local median counts as an element


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/strip-with.png"
    y0 = int(sys.argv[2]) if len(sys.argv) > 2 else Y0
    y1 = int(sys.argv[3]) if len(sys.argv) > 3 else Y1
    im = Image.open(path).convert("L")
    w, h = im.size
    px = im.load()
    print(f"{path}  y={y0}..{y1}")

    colpeak = [0] * w
    for y in range(y0, min(y1, h)):
        for x in range(w):
            v = px[x, y]
            if v > colpeak[x]:
                colpeak[x] = v

    cols = [x for x in range(w) if colpeak[x] > DELTA]
    groups = []
    if cols:
        cur = [cols[0]]
        for x in cols[1:]:
            if x - cur[-1] <= 14:
                cur.append(x)
            else:
                groups.append((cur[0], cur[-1]))
                cur = [x]
        groups.append((cur[0], cur[-1]))

    print(f"bright-element clusters ({len(groups)}):")
    for lo, hi in groups:
        peak = max(colpeak[x] for x in range(lo, hi + 1))
        print(f"  x {lo:4d}..{hi:4d}  width {hi - lo + 1:3d}  peak luma {peak:3d}")
    return 0


if __name__ == "__main__":
    sys.exit(main())