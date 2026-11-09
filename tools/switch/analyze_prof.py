#!/usr/bin/env python3
"""
Summarize the sampling profile written by the Switch build (sdmc:/switch/openQ4/prof.bin).

    python3 -I analyze_prof.py prof.bin openQ4.elf [--phase 1] [--top 40]

Slot 0 is the main thread, slot 1 the render thread. Phase 1 is "a world is loaded" (gameplay),
phase 0 menus and loading. For each thread the report lists the functions with the most self
time (the sampled pc) and the most inclusive time (any function anywhere in the sampled call
chain), as a share of that thread's samples.
"""
import argparse
import bisect
import collections
import struct
import subprocess
import sys

MAGIC = 0x31464F5251344F51
NM = "/opt/devkitpro/devkitA64/bin/aarch64-none-elf-nm"


def load_symbols(elf):
    out = subprocess.run([NM, "-n", "-C", "--defined-only", elf], capture_output=True, text=True, check=True).stdout
    addrs, names, main_addr = [], [], None
    for line in out.splitlines():
        parts = line.split(" ", 2)
        if len(parts) < 3 or parts[1] not in "tTwWiI":
            continue
        a = int(parts[0], 16)
        addrs.append(a)
        names.append(parts[2])
        if parts[2] == "main" and main_addr is None:
            main_addr = a
    return addrs, names, main_addr


def read_samples(path):
    data = open(path, "rb").read()
    magic, main_runtime = struct.unpack_from("<QQ", data, 0)
    if magic != MAGIC:
        sys.exit("not a prof.bin file")
    pos, samples = 16, []
    while pos + 3 <= len(data):
        slot, phase, n = data[pos], data[pos + 1], data[pos + 2]
        pos += 3
        if pos + n * 8 > len(data):
            break
        pcs = struct.unpack_from("<%dQ" % n, data, pos)
        pos += n * 8
        samples.append((slot, phase, pcs))
    return main_runtime, samples


def short(name, width=96):
    return name if len(name) <= width else name[: width - 3] + "..."


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("prof")
    ap.add_argument("elf")
    ap.add_argument("--phase", type=int, default=1)
    ap.add_argument("--top", type=int, default=40)
    args = ap.parse_args()

    addrs, names, main_static = load_symbols(args.elf)
    main_runtime, samples = read_samples(args.prof)
    base = main_runtime - main_static

    def sym(pc):
        off = pc - base
        if off < 0 or off > addrs[-1] + 0x100000:
            return None
        i = bisect.bisect_right(addrs, off) - 1
        return names[i] if i >= 0 else None

    slot_names = {0: "main thread", 1: "render thread"}
    for slot in sorted({s[0] for s in samples}):
        mine = [s for s in samples if s[0] == slot and s[1] == args.phase]
        if not mine:
            continue
        total = len(mine)
        self_c, incl_c = collections.Counter(), collections.Counter()
        for _, _, pcs in mine:
            leaf = sym(pcs[0]) or "?"
            self_c[leaf] += 1
            seen = set()
            for pc in pcs:
                n = sym(pc)
                if n and n not in seen:
                    seen.add(n)
                    incl_c[n] += 1
        print("=" * 100)
        print("%s, phase %d: %d samples (%.1f s at 500 Hz)" % (slot_names.get(slot, "slot %d" % slot), args.phase, total, total / 500.0))
        print("-- self time (top %d)" % args.top)
        for n, c in self_c.most_common(args.top):
            print("  %5.1f%%  %s" % (100.0 * c / total, short(n)))
        print("-- inclusive time (top %d)" % args.top)
        for n, c in incl_c.most_common(args.top):
            print("  %5.1f%%  %s" % (100.0 * c / total, short(n)))


if __name__ == "__main__":
    main()
