#!/usr/bin/env python3
"""Two checks of a high frame rate, from a scripted run's pictures
(--frames DIR --frames-every 1, under --virtual-time).

    python engine/tools/frame_motion.py steps DIR
    python engine/tools/frame_motion.py match DIR60 DIR_FAST FROM [--rate 2]

steps: the mean difference between each picture and the one before, in
order. Above 60, nothing placed between the game's steps shows as pairs (a
big difference, then ~0: the in-between picture the same as the one
before) - placed, the differences are even. A pattern like "x 0 0 0" is
something stepping at 30 (a movie); "x 0" at 60.

match: whether the game runs at its own speed above 60. A run switched to
the faster rate at frame FROM (PS2HOST_HIGH_FPS_FROM, so its presses land
as at 60): its picture FROM + rate*k should show what the 60 run's frame
FROM + k shows - the best match among FROM+k-3 .. FROM+k+3, its offset
steady (the switch's own frame or two) and the difference near 0, for every
k. An offset that grows is the game running faster (or slower) above 60.
"""

from __future__ import annotations

import argparse
import glob
import os
import sys

import numpy as np
from PIL import Image


def picture(path: str) -> np.ndarray:
    return np.asarray(Image.open(path).convert("L"), dtype=np.float32)


def frame_path(directory: str, frame: int) -> str:
    return os.path.join(directory, "frame_%06d.bmp" % frame)


def steps(directory: str) -> int:
    files = sorted(glob.glob(os.path.join(directory, "frame_*.bmp")))
    previous, out = None, []
    for f in files:
        p = picture(f)
        if previous is not None and previous.shape == p.shape:
            out.append(float(np.abs(p - previous).mean()))
        previous = p
    print("%d pictures" % len(files))
    print(" ".join("%.1f" % d for d in out))
    return 0


def match(slow: str, fast: str, start: int, rate: int, every: int) -> int:
    k = every
    offsets = []
    while os.path.exists(frame_path(fast, start + rate * k)) and os.path.exists(frame_path(slow, start + k + 3)):
        b = picture(frame_path(fast, start + rate * k))
        best = min((float(np.abs(picture(frame_path(slow, start + k + d)) - b).mean()), d) for d in range(-3, 4))
        offsets.append(best[1])
        print("picture %d above 60: best at 60 is %d (%+d), difference %.2f"
              % (start + rate * k, start + k + best[1], best[1], best[0]))
        k += every
    if not offsets:
        print("no pictures to compare (the runs' frames missing)")
        return 1
    print("offsets %d .. %d: %s" % (min(offsets), max(offsets),
                                   "steady - the game's own speed" if max(offsets) - min(offsets) <= 1
                                   else "drifting - NOT the game's own speed"))
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="what", required=True)
    s = sub.add_parser("steps")
    s.add_argument("dir")
    m = sub.add_parser("match")
    m.add_argument("slow")
    m.add_argument("fast")
    m.add_argument("start", type=int)
    m.add_argument("--rate", type=int, default=2, help="pictures above 60 for each at 60 (2 at 120)")
    m.add_argument("--every", type=int, default=25)
    args = parser.parse_args(argv)
    if args.what == "steps":
        return steps(args.dir)
    return match(args.slow, args.fast, args.start, args.rate, args.every)


if __name__ == "__main__":
    sys.exit(main())
