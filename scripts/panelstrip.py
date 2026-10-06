#!/usr/bin/env python3
"""A/B diff of the panel strip: panel running vs stopped.

Why A/B and not "columns differing from one background colour": the panel is
50% translucent, so its background is the wallpaper blended with #666666. There
is no single background value to compare against -- a one-sample threshold
reports every column as an element, which is what produced a bogus
"0..1919" cluster.

The stop/start pair is the calibration: whatever changes between the two frames
is exactly what the panel draws, including translucency.

Usage:
    panelstrip.py shoot <with|without>   # one frame, saved
    panelstrip.py diff                   # compare the two frames
"""
import subprocess
import sys
from PIL import Image

Y0, Y1 = 24, 49  # measured panel band: y=24..48 inclusive
THRESH = 18


def shot(path):
    subprocess.run(["spectacle", "-b", "-n", "-o", path], check=False)
    return Image.open(path).convert("RGB")


def shoot(label):
    path = f"/tmp/strip-{label}.png"
    shot(path)
    print(f"saved {path}")


def diff():
    a = Image.open("/tmp/strip-with.png").convert("RGB")
    b = Image.open("/tmp/strip-without.png").convert("RGB")
    if a.size != b.size:
        print(f"size mismatch: {a.size} vs {b.size}")
        return 1
    w, _ = a.size
    pa, pb = a.load(), b.load()

    rows = []
    for y in range(Y0, Y1):
        n = sum(1 for x in range(w)
                if sum(abs(pa[x, y][c] - pb[x, y][c]) for c in range(3)) > THRESH)
        rows.append((y, n))
    print("changed pixels per row:")
    print("  " + "  ".join(f"{y}:{n}" for y, n in rows if n))
    live = [y for y, n in rows if n > w * 0.5]
    print(f"rows changed across >50% of width: {live}")

    mid = (Y0 + Y1) // 2
    cols = sorted({x for y in range(Y0, Y1) for x in range(w)
                   if sum(abs(pa[x, y][c] - pb[x, y][c]) for c in range(3)) > THRESH})
    groups = []
    if cols:
        cur = [cols[0]]
        for x in cols[1:]:
            if x - cur[-1] <= 16:
                cur.append(x)
            else:
                groups.append((cur[0], cur[-1]))
                cur = [x]
        groups.append((cur[0], cur[-1]))
    print(f"\nchanged columns at y={mid}, {len(groups)} clusters:")
    for lo, hi in groups:
        print(f"  x {lo:4d}..{hi:4d}  width {hi - lo + 1:3d}")
    return 0


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    if sys.argv[1] == "shoot":
        shoot(sys.argv[2])
    elif sys.argv[1] == "diff":
        return diff()
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())