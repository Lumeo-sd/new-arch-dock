#!/usr/bin/env python3
"""Pixel-verify the lingmo-panel strip.

Takes a screenshot with spectacle, then clusters columns in the panel band that
differ from the locally-computed background colour. The background must be
computed from the frame itself -- sampling it from a stored value makes the
panel disappear from the measurement whenever the desktop underneath changes.
"""
import subprocess
import sys
from collections import Counter
from PIL import Image

SHOT = "/tmp/v.png"
Y0, Y1 = 24, 50  # panel band measured earlier: y=24..48
THRESH = 45
GAP = 16


def main():
    subprocess.run(["spectacle", "-b", "-n", "-o", SHOT], check=False)
    im = Image.open(SHOT).convert("RGB")
    w, h = im.size
    px = im.load()
    rows = range(Y0, min(Y1, h))
    mid = Y0 + (min(Y1, h) - Y0) // 2
    base = Counter(px[x, mid] for x in range(w)).most_common(1)[0][0]

    cols = sorted({
        x
        for y in rows
        for x in range(w)
        if sum(abs(a - b) for a, b in zip(px[x, y], base)) > THRESH
    })

    groups = []
    if cols:
        cur = [cols[0]]
        for x in cols[1:]:
            if x - cur[-1] <= GAP:
                cur.append(x)
            else:
                groups.append((cur[0], cur[-1]))
                cur = [x]
        groups.append((cur[0], cur[-1]))

    print(f"size: {w}x{h}")
    print(f"background (computed locally): {base}")
    print(f"element clusters ({len(groups)}):")
    for a, b in groups:
        print(f"  x {a:4d}..{b:4d}  width {b - a + 1:3d}")
    return 0


if __name__ == "__main__":
    sys.exit(main())