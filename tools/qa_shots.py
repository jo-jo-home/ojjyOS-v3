#!/usr/bin/env python3
"""qa_shots.py — text-friendly screenshot review for QA days.

Every QEMU/VZ tour leaves PNG evidence. This tool reads them without a
human eye: it prints, per image, the size, total ink, and the vertical
bands of dark pixels with their horizontal extents, and flags fully
blank captures. A text-only reviewer reads those profiles (and can
re-render any suspicious region as ASCII art) to spot empty frames,
missing windows or clipped content. Used on the QA 2026-10-27 pass day.

Usage: python3 tools/qa_shots.py <dir-or-png> [<dir-or-png> ...]

Created by Jonas Lee.
"""
import sys
from pathlib import Path
from PIL import Image


def profile(path):
    img = Image.open(path).convert('RGB')
    W, H = img.size
    k = 2 if W >= 2560 else 1
    px = img.load()
    rows = []
    for yy in range(0, H, 2 * k):
        dark = 0
        for xx in range(0, W, k * 2):
            r, g, b = px[xx, yy]
            if (r + g + b) // 3 < 120:
                dark += 1
        rows.append((yy, dark))
    total = sum(d for _, d in rows)
    print(f"{path.name}: {W}x{H} (k={k}) ink={total}")
    if total == 0:
        print("  !! COMPLETELY BLANK")
        return
    clusters = []
    cur = None
    for yy, d in rows:
        if d > 3:
            if cur is None:
                cur = [yy, yy, d]
            else:
                cur[1] = yy
                cur[2] += d
        else:
            if cur is not None:
                clusters.append(tuple(cur))
                cur = None
    if cur is not None:
        clusters.append(tuple(cur))
    for y0, y1, d in clusters[:12]:
        xs = [xx for xx in range(0, W, k * 2)
              if any(sum(px[xx, yy]) // 3 < 120 for yy in range(y0, min(y1 + 1, H), 4 * k))]
        span = (xs[0] // k, xs[-1] // k) if xs else (0, 0)
        print(f"  band y={y0 // k}..{y1 // k} ink={d} x={span[0]}..{span[1]}")
    if len(clusters) > 12:
        print(f"  …{len(clusters) - 12} more clusters")


def main(argv):
    for name in argv:
        p = Path(name)
        if p.is_dir():
            for f in sorted(p.glob('*.png')):
                profile(f)
        elif p.is_file():
            profile(p)
        else:
            print(f"qa_shots: no such file or directory: {name}", file=sys.stderr)
            return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
