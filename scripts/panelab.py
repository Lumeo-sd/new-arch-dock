#!/usr/bin/env python3
"""Locate the lingmo-panel surface by A/B differencing, not by guessing rows.

Takes one screenshot per state (panel running / stopped), then reports which
rows and columns differ. A pixel cluster that spans the full width means the
measured band simply is not where the panel is -- it was reading wallpaper.

Usage: panelab.py <label>
"""
import subprocess
import sys
from PIL import Image


def shot(path):
    subprocess.run(["spectacle", "-b", "-n", "-o", path], check=False)
    return Image.open(path).convert("RGB")


def row_profile(im, y0, y1):
    px = im.load()
    return [(y, tuple(sum(px[x, y][c] for x in range(im.size[0])) // im.size[0]
                      for c in range(3))) for y in range(y0, y1)]


def main():
    label = sys.argv[1]
    path = f"/tmp/ab-{label}.png"
    im = shot(path)
    w, h = im.size
    print(f"{label}: {w}x{h} -> {path}")
    for y, mean in row_profile(im, 0, 70):
        print(f"  y={y:3d}  mean={mean}")
    return 0


if __name__ == "__main__":
    sys.exit(main())