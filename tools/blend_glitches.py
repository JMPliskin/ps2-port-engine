#!/usr/bin/env python3
"""Pictures of a high frame rate that are unlike both of their neighbours.

    python engine/tools/blend_glitches.py DIR [FROM TO] [--threshold N] [--top N]

DIR holds a scripted run's pictures, one a frame (--frames DIR --frames-every
1 --frames-limit 100000). A picture between two steps lies between its
neighbours: where they agree, it agrees. A pixel unlike both by more than
--threshold (40) where the two agree is a glitch's - a thing placed where
neither step had it: two things blended into each other (a game that drew
them in another order), a camera's cut blended, a vertex thrown across the
screen. Each picture's count of such pixels, the worst listed: a few hundred
is noise (a camera's cut shown clean, sparks); thousands are glitches - look
at the picture and its neighbours.
"""

import argparse
import glob
import os
import re
import sys

import numpy as np
from PIL import Image


def load(path):
    try:
        return np.asarray(Image.open(path).convert("L"), dtype=np.int16)
    except Exception:       # a dump cut short
        return None


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    p.add_argument("dir")
    p.add_argument("first", nargs="?", type=int)
    p.add_argument("last", nargs="?", type=int)
    p.add_argument("--threshold", type=int, default=40)
    p.add_argument("--top", type=int, default=15)
    a = p.parse_args()
    frames = {}
    for path in glob.glob(os.path.join(a.dir, "frame_*.bmp")):
        m = re.search(r"frame_(\d+)\.bmp$", path)
        if m:
            frames[int(m.group(1))] = path
    numbers = sorted(f for f in frames if (a.first is None or f >= a.first) and (a.last is None or f <= a.last))
    if len(numbers) < 3:
        sys.exit("fewer than 3 pictures in %s" % a.dir)
    rows = []
    prev, cur = load(frames[numbers[0]]), load(frames[numbers[1]])
    for i in range(1, len(numbers) - 1):
        nxt = load(frames[numbers[i + 1]])
        if prev is not None and cur is not None and nxt is not None and prev.shape == cur.shape == nxt.shape:
            agree = np.abs(prev - nxt) < 12
            odd = (np.abs(cur - prev) > a.threshold) & (np.abs(cur - nxt) > a.threshold) & agree
            rows.append((int(odd.sum()), numbers[i]))
        prev, cur = cur, nxt
    counts = [r[0] for r in rows]
    print("%d pictures: median %d, 99th percentile %d, worst %d" %
          (len(rows), int(np.median(counts)), int(np.percentile(counts, 99)), max(counts)))
    for n, f in sorted(rows, reverse=True)[:a.top]:
        print("  frame %d: %d pixels" % (f, n))


if __name__ == "__main__":
    main()
