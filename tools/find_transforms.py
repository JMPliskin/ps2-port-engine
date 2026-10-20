#!/usr/bin/env python3
"""Find a game's moving transforms in RAM snapshots a step apart - what a
high frame rate's in-between pictures must blend (games/burnout3/docs/
HIGH_FPS_PLAN.md: "For every game").

    burnout3.exe ... --until-frame N   --dump-ram a.bin
    burnout3.exe ... --until-frame N+1 --dump-ram b.bin
    burnout3.exe ... --until-frame N+2 --dump-ram c.bin
    python engine/tools/find_transforms.py a.bin b.bin c.bin

A transform here is RenderWare's RwMatrix, as most PS2 games of the time
keep them: 64 bytes, 16-aligned - right, flags, up, pad, at, pad, pos, pad
(four floats each row) - its first three rows orthonormal. One that moves
does so smoothly: its position changes by a little from one step to the
next, by about as much again the step after. Listed: each moving matrix's
address, its speed (position change a step) and how steady that is, grouped
into runs of matrices a fixed stride apart (the arrays a game keeps its
objects in: cars, traffic, debris).
"""

from __future__ import annotations

import argparse
import sys

import numpy as np


def matrices(ram: np.ndarray, tolerance: float) -> tuple:
    """(offsets, rows): every 16-aligned 64 bytes whose first three rows
    are orthonormal within `tolerance`; rows as (n, 4, 4) float32."""
    words = ram[: len(ram) // 64 * 64].view(np.float32)
    n = len(words) // 4 - 3
    # Every 16-byte position as the start of a 4x4 (rows overlap): 4 rows of 4.
    quads = np.lib.stride_tricks.as_strided(words, shape=(n, 4, 4), strides=(16, 16, 4))
    with np.errstate(all="ignore"):
        r = quads[:, 0, :3].astype(np.float64)
        u = quads[:, 1, :3].astype(np.float64)
        a = quads[:, 2, :3].astype(np.float64)
        p = quads[:, 3, :3].astype(np.float64)
        ok = np.isfinite(r).all(1) & np.isfinite(u).all(1) & np.isfinite(a).all(1) & np.isfinite(p).all(1)
        for v in (r, u, a):
            ok &= np.abs((v * v).sum(1) - 1.0) < tolerance
        ok &= np.abs((r * u).sum(1)) < tolerance
        ok &= np.abs((r * a).sum(1)) < tolerance
        ok &= np.abs((u * a).sum(1)) < tolerance
        ok &= np.abs(p).max(1) < 1e6
    index = np.nonzero(ok)[0]
    return index * 16, quads[index].astype(np.float64)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("dumps", nargs=3, help="RAM at three successive steps (--dump-ram)")
    parser.add_argument("--base", type=lambda x: int(x, 0), default=0, help="the dumps' first byte's address")
    parser.add_argument("--tolerance", type=float, default=0.01, help="orthonormality, for the rows' lengths and dots")
    parser.add_argument("--min-speed", type=float, default=1e-4, help="the least position change a step to call moving")
    parser.add_argument("--max-speed", type=float, default=100.0, help="more than this a step is a cut, not motion")
    args = parser.parse_args(argv)
    rams = [np.fromfile(path, dtype=np.uint8) for path in args.dumps]
    size = min(len(r) for r in rams)
    rams = [r[:size] for r in rams]
    offsets, first = matrices(rams[0], args.tolerance)
    print("%d matrix-shaped 64-byte runs in the first dump" % len(offsets), file=sys.stderr)
    rows = []
    later = []
    for ram in rams[1:]:
        quads = ram[: size // 64 * 64].view(np.float32)
        later.append(np.stack([quads[o // 4: o // 4 + 16].reshape(4, 4) for o in offsets]).astype(np.float64)
                     if len(offsets) else np.zeros((0, 4, 4)))
    second, third = later
    d1 = np.linalg.norm(second[:, 3, :3] - first[:, 3, :3], axis=1)
    d2 = np.linalg.norm(third[:, 3, :3] - second[:, 3, :3], axis=1)
    # Still matrices a step later, moving, and steadily: the second step's
    # travel within half of the first's.
    shaped = np.ones(len(offsets), bool)
    for m in (second, third):
        for k in range(3):
            shaped &= np.abs((m[:, k, :3] ** 2).sum(1) - 1.0) < args.tolerance
    moving = shaped & (d1 > args.min_speed) & (d1 < args.max_speed) & (d2 > args.min_speed) & (d2 < args.max_speed)
    steady = moving & (np.abs(d2 - d1) <= 0.5 * np.maximum(d1, d2))
    picked = np.nonzero(steady)[0]
    # Overlapping finds (a 16-aligned window inside another matrix's rows)
    # are not separate: kept only where the window starts a matrix of its
    # own, 64 bytes on from the last kept.
    kept = []
    for i in picked:
        if kept and offsets[i] - offsets[kept[-1]] < 64:
            continue
        kept.append(i)
    print("%d moving smoothly (%d moving at all)" % (len(kept), int(moving.sum())), file=sys.stderr)
    # Runs a fixed stride apart.
    groups = []
    for i in kept:
        address = args.base + int(offsets[i])
        if groups and len(groups[-1]) >= 1:
            g = groups[-1]
            stride = g[1] if len(g) > 1 else None
            last = g[-1][0]
            if (stride is None and address - last <= 0x4000) or (stride is not None and address - last == stride[0]):
                if stride is None:
                    g.insert(1, (address - last,))
                g.append((address, d1[i], d2[i], first[i, 3, :3]))
                continue
        groups.append([(address, d1[i], d2[i], first[i, 3, :3])])
    for g in groups:
        members = [m for m in g if len(m) == 4]
        stride = g[1][0] if len(g) > 1 and len(g[1]) == 1 else 0
        print("0x%08X: %d matrix(es)%s" % (members[0][0], len(members), ", stride 0x%X" % stride if stride else ""))
        for address, s1, s2, pos in members[:8]:
            print("    0x%08X  %.4f -> %.4f a step  at (%.1f, %.1f, %.1f)" % (address, s1, s2, *pos))
        if len(members) > 8:
            print("    ...")
    return 0


if __name__ == "__main__":
    sys.exit(main())
