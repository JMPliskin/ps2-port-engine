#!/usr/bin/env python3
"""Where a sampled run spent its time, to the instruction.

The host's sampler (profile.cpp) writes its samples' stacks as they are with
PS2HOST_SAMPLE_RAW=file - and samples as often as PS2HOST_SAMPLE_US says
(down to tens of microseconds, for the thousands of samples a compiled VU
block's lines need). This resolves them with llvm-symbolizer, each address to
its whole chain of inlined functions, and reports:

  - the functions the samples were in (self), and with their callers (total);
  - by step: the innermost inlined function, so a compiled VU block's time is
    split by VuCore's steps (Fmac, Finish, Load...) and the block's own lines;
  - --annotate N: the N hottest functions' instructions, each with its share
    of the samples and the source line (innermost) it is from.

    PS2HOST_SAMPLE_US=50 PS2HOST_SAMPLE_RAW=samples.txt burnout3.exe --vu-fast --sample --vu-bench vu_race.bin
    python engine/tools/hotspots.py --exe games/burnout3/build/port/cmake/burnout3.exe samples.txt --annotate 3
"""

from __future__ import annotations

import argparse
import collections
import os
import re
import shutil
import subprocess
import sys

LLVM_BIN = r"C:\Program Files\LLVM\bin"


def tool(name: str) -> str:
    found = shutil.which(name)
    if found:
        return found
    path = os.path.join(LLVM_BIN, name + (".exe" if os.name == "nt" else ""))
    if os.path.exists(path):
        return path
    sys.exit("hotspots: %s not found (LLVM's tools)" % name)


def image_base(exe: str) -> int:
    """The executable's preferred base (the file's header)."""
    out = subprocess.run([tool("llvm-objdump"), "-p", exe], capture_output=True, text=True).stdout
    m = re.search(r"ImageBase\s+([0-9a-fA-F]+)", out)
    return int(m.group(1), 16) if m else 0x140000000


def read_samples(path: str, base: int) -> list:
    """[[leaf, caller, ...], ...] as addresses at the executable's preferred
    base (the file has offsets from where it was loaded; 0: outside it)."""
    samples = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            parts = line.split()
            if parts:
                samples.append([int(p, 16) + base if int(p, 16) else 0 for p in parts])
    return samples


def symbolize(exe: str, addresses: list, inline: bool = True, demangle: bool = True) -> dict:
    """address -> [(function, file, line), ...] innermost first."""
    args = [tool("llvm-symbolizer"), "--obj=" + exe, "--functions=linkage"]
    args.append("--inlining" if inline else "--no-inlines")
    if not demangle:
        args.append("--no-demangle")
    text = "\n".join("0x%x" % a for a in addresses) + "\n"
    out = subprocess.run(args, input=text, capture_output=True, text=True, check=True).stdout
    chains = out.split("\n\n")
    result = {}
    for address, chain in zip(addresses, chains):
        lines = [l for l in chain.split("\n") if l.strip()]
        frames = []
        for i in range(0, len(lines) - 1, 2):
            location = lines[i + 1].strip()
            m = re.match(r"(.*):(\d+):\d+$", location)
            file, line = (m.group(1), int(m.group(2))) if m else (location, 0)
            frames.append((lines[i].strip(), file, line))
        result[address] = frames or [("??", "", 0)]
    return result


def short(name: str, width: int = 90) -> str:
    name = re.sub(r"\(.*\)$", "", name)              # the parameters
    return name if len(name) <= width else name[:width - 3] + "..."


def source_text(file: str, line: int, cache: dict) -> str:
    """The source line's text, where the file is found (paths as built, with
    the drive's root standing for where the repository was)."""
    if file not in cache:
        text = None
        for candidate in (file, file.replace("C:\\", "")):
            if os.path.exists(candidate):
                text = open(candidate, encoding="utf-8", errors="replace").read().split("\n")
                break
        cache[file] = text
    lines = cache[file]
    return lines[line - 1].strip() if lines and 0 < line <= len(lines) else ""


def step_of(frames: list, sources: dict) -> str:
    """What a sample was doing: the innermost inlined function, or - in a
    compiled VU block's own code - the operation on the block's line."""
    inner, file, line = frames[0]
    if len(frames) > 1 or not inner.startswith("vu_generated::"):
        return short(inner, 70)
    text = source_text(file, line, sources)
    m = re.match(r"(C::\w+|\+\+count|if \((stopping|delayed|count|s\.count)|u64 t|fm\d+|fl\d+|PS2HOST_VU_NEXT|s\.\w+)",
                 text)
    return "(block) " + re.sub(r"\d+", "N", m.group(1) if m else text[:40])


def report(counter: collections.Counter, total: int, title: str, top: int) -> None:
    print("%s:" % title)
    for key, n in counter.most_common(top):
        print("  %6.2f%%  %s" % (100.0 * n / total, key))


def annotate(exe: str, function: str, counts: collections.Counter, frames: dict, total: int,
             threshold: float) -> None:
    """The function's instructions from its first sampled address to its
    last (the executable has no symbol table for llvm-objdump; the PDB is
    llvm-symbolizer's)."""
    mine = [a for a in counts if frames.get(a) and frames[a][-1][0] == function]
    out = subprocess.run([tool("llvm-objdump"), "-d", "--no-show-raw-insn", "--start-address=0x%x" % min(mine),
                          "--stop-address=0x%x" % (max(mine) + 1), exe], capture_output=True, text=True).stdout
    here = sum(n for a, n in counts.items() if frames.get(a) and frames[a][-1][0] == function)
    print("\n== %s: %.2f%% of the samples" % (short(function, 120), 100.0 * here / total))
    for line in out.split("\n"):
        m = re.match(r"\s*([0-9a-f]+):\s+(.*)$", line)
        if not m:
            continue
        address = int(m.group(1), 16)
        n = counts.get(address, 0)
        if 100.0 * n / total < threshold:
            continue
        where = frames.get(address, [("", "", 0)])
        inner = short(where[0][0], 40).replace("ps2host::", "") if len(where) > 1 else ""
        print("  %6.2f%%  %x  %-44s %-26s %s:%d" % (100.0 * n / total, address, m.group(2)[:44], inner,
                                                    os.path.basename(where[0][1]), where[0][2]))


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("samples", help="PS2HOST_SAMPLE_RAW's file")
    parser.add_argument("--exe", required=True, help="the executable that was sampled (its PDB beside it)")
    parser.add_argument("--top", type=int, default=30)
    parser.add_argument("--annotate", type=int, default=0, help="the hottest N functions' instructions")
    parser.add_argument("--threshold", type=float, default=0.05, help="annotate instructions at this %% or more")
    parser.add_argument("--only", help="only samples whose function (outermost inlined) holds this text")
    args = parser.parse_args(argv)

    samples = read_samples(args.samples, image_base(args.exe))
    leaves = collections.Counter(s[0] for s in samples)
    callers = sorted(set(a for s in samples for a in s[1:] if a))
    frames = symbolize(args.exe, sorted(a for a in leaves if a))
    outer = symbolize(args.exe, callers, inline=False) if callers else {}
    function = lambda a: frames[a][-1][0] if a in frames else "(outside the executable)"
    if args.only:
        samples = [s for s in samples if args.only in function(s[0])]
        leaves = collections.Counter(s[0] for s in samples)
    total = len(samples)
    if not total:
        print("no samples")
        return 1
    print("%d samples" % total)

    self_count = collections.Counter()
    steps = collections.Counter()
    total_count = collections.Counter()
    sources = {}
    for address, n in leaves.items():
        self_count[short(function(address))] += n
        if address in frames:
            steps[step_of(frames[address], sources)] += n
    for s in samples:
        seen = {short(function(s[0]))}
        for a in s[1:]:
            if a and a in outer:
                seen.add(short(outer[a][0][0]))
        for name in seen:
            total_count[name] += 1
    report(self_count, total, "self, by function", args.top)
    report(steps, total, "self, by step (innermost inlined function; a VU block's own lines by operation)", args.top)
    report(total_count, total, "total (with callers)", args.top)

    if args.annotate:
        by_function = collections.Counter()
        for address, n in leaves.items():
            if address in frames:
                by_function[frames[address][-1][0]] += n
        for name, _ in by_function.most_common(args.annotate):
            annotate(args.exe, name, leaves, frames, total, args.threshold)
    return 0


if __name__ == "__main__":
    sys.exit(main())
