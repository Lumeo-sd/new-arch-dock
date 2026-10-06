#!/usr/bin/env python3
"""Find panel *elements* by local contrast inside a single frame.

A/B differencing only proves the strip exists: the translucent background
(#666666 @ 0.5) changes every pixel, so every column "differs". Elements are
bright marks on that background, so they are found by comparing each pixel to a
rolling median along x -- icons and text stick out locally, the wallpaper does
not.

Usage: panelels.py [frame.png]
"""
import sys
from PIL import Image

Y0, Y1 = 24, 49
ROLL = 31          # rolling window in px for the background estimate
DELTA = 45         # deviation from local background that counts as an element


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/strip-with.png"
    im = Image.open(path).convert("L")          # luminance is enough for contrast
    w, h = im.size
    px = im.load()
    print(f"{path}: {w}x{h}")

    colscore = [0] * w
    for y in range(Y0, min(Y1, h)):
        row = [px[x, y] for x in range(w)]
        half = ROLL // 2
        for x in range(w):
            lo = max(0, x - half)
            hi = min(w, x + half + 1)
            window = sorted(row[lo:hi])
            med = window[len(window) // 2]
            colscore[x] = max(colscore[x], abs(row[x] - med))

    cols = [x for x in range(w) if colscore[x] > DELTA]
    groups = []
    if cols:
        cur = [cols[0]]
        for x in cols[1:]:
            if x - cur[-1] <= 12:
                cur.append(x)
            else:
                groups.append((cur[0], cur[-1]))
                cur = [x]
        groups.append((cur[0], cur[-1]))

    print(f"element clusters ({len(groups)}):")
    for lo, hi in groups:
        peak = max(colscore[x] for x in range(lo, hi + 1))
        print(f"  x {lo:4d}..{hi:4d}  width {hi - lo + 1:3d}  peak dev {peak:3d}")
    return 0


if __name__ == "__main__":
    sys.exit(main())